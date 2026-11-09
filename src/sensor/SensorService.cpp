// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The sensor task: running the SEN69C and its maintenance operations,
/// and reading the battery monitor (see SensorService.h).

#include "SensorService.h"

#include <Wire.h>
#include <esp_attr.h>

#include <algorithm>
#include <cstdarg>

namespace {

/// @brief The settings the sensor was last given (the altitude it confirmed,
/// and CO2 self-calibration), as pack() makes them.
///
/// Kept in memory that survives restarts of the ESP32 but not power cuts, like
/// the sensor's own settings. They stay valid when the sensor keeps measuring
/// across a restart.
RTC_NOINIT_ATTR uint32_t sensorSettings;
RTC_NOINIT_ATTR uint32_t sensorSettingsCheck;  ///< Tells saved settings from leftover garbage.
constexpr uint32_t kSettingsKey = 0x5A17C0DE;  ///< Mixed into sensorSettingsCheck.

/// @brief Packs the settings the sensor keeps into one word.
/// @param altitudeM The altitude, in m.
/// @param co2SelfCalibration Whether CO2 self-calibration is on.
/// @return The altitude in the low 16 bits, self-calibration in bit 16.
uint32_t pack(uint16_t altitudeM, bool co2SelfCalibration) {
  return altitudeM | (co2SelfCalibration ? 1u << 16 : 0);
}
/// @brief Saves the settings the sensor was given and confirmed.
/// @param settings The settings, as pack() makes them.
void remember(uint32_t settings) {
  sensorSettings = settings;
  sensorSettingsCheck = settings ^ kSettingsKey;
}
/// Marks the saved settings invalid, before the sensor is given new ones.
void forgetSettings() { sensorSettingsCheck = ~(sensorSettings ^ kSettingsKey); }
/// @brief Whether these are the settings saved, and still valid.
/// @param settings The settings, as pack() makes them.
bool remembered(uint32_t settings) {
  return sensorSettings == settings && sensorSettingsCheck == (settings ^ kSettingsKey);
}

constexpr uint8_t kAddrMax17048 = 0x36;  ///< The battery monitor's I2C address.
/// The MAX17048's power-on HIBRT: it hibernates (about 3 uA instead of 23 uA)
/// while the battery is hardly used.
constexpr uint16_t kGaugeHibernateDefault = 0x8030;
/// Raw unsigned value the SEN69C returns while a measurement isn't available yet.
constexpr uint16_t kUnknownU16 = 0xFFFF;
constexpr int16_t kUnknownI16 = 0x7FFF;  ///< The same for a signed value.
/// @brief The shortest a measurement runs before it's stopped, in ms: stopping
/// too soon after a start upsets the sensor.
///
/// In the first 24 s it causes a CO2 error. And the VOC and NOx algorithms
/// only take their starting point after about 45 s of measuring (they show
/// `--` until then): stopped earlier, they take it from the unsettled readings
/// after the restart and report nonsense (NOx near 500, VOC 1) until they have
/// relearned.
constexpr uint32_t kMinRunMs = 60000;
constexpr uint32_t kFanCleaningMs = 10000;  ///< How long the fan cleaning runs, in ms.
/// @brief The first firmware version (major part) that reports when the
/// heater is done.
///
/// The humidity sensor's heater runs for 1 s. Firmware older than 9.0 is busy
/// for 1.3 s after the command; newer firmware reports when it's done. The
/// temperature reading needs 20 s to settle afterwards.
constexpr uint8_t kHeaterReportsFromFirmware = 9;
constexpr uint32_t kHeaterBusyMs = 1300;  ///< How long older firmware is busy, in ms.
constexpr uint32_t kHeaterPollMs = 50;  ///< How often to ask newer firmware, in ms.
constexpr uint32_t kHeaterMaxMs = 3000;  ///< The longest to wait for its answer, in ms.
constexpr uint32_t kHeaterSettleMs = 20000;  ///< For the temperature reading to settle, in ms.
/// PM readings are only stable 30 s after a measurement starts (datasheet);
/// earlier ones often overshoot and would raise false alerts.
constexpr uint32_t kPmStartupMs = 30000;
constexpr uint32_t kPowerUpMs = 150;  ///< For the just-powered STEMMA QT port to come up, in ms.
/// How often to look for a new measurement, in ms; commands are handled at once.
constexpr uint32_t kPollMs = 200;
constexpr uint32_t kStatusIntervalMs = 10000;  ///< How often to read the device status, in ms.
/// @brief How often to read the battery monitor, in ms.
///
/// Often enough that the voltage jumps from one reading to the next when USB
/// power is plugged in or unplugged (see power::BatteryWatch).
constexpr uint32_t kBatteryIntervalMs = 5000;
/// How long to wait before looking for the sensor or starting it again, in ms.
constexpr uint32_t kRetryMs = 5000;
/// After this many failed reads in a row, look for the sensor again.
constexpr uint32_t kMaxConsecutiveErrors = 10;
/// @brief Without a new measurement for this long, in ms, look for the sensor
/// again.
///
/// It measures every second; a sensor that answers but stops measuring
/// would otherwise go unnoticed.
constexpr uint32_t kNoDataMs = 10000;

/// @brief A raw unsigned value in its units.
/// @param raw The raw value.
/// @param divisor What it's divided by.
/// @return The value; NaN if not available yet (kUnknownU16).
float scaled(uint16_t raw, float divisor) { return raw == kUnknownU16 ? NAN : raw / divisor; }
/// @brief A raw signed value in its units.
/// @param raw The raw value.
/// @param divisor What it's divided by.
/// @return The value; NaN if not available yet (kUnknownI16).
float scaled(int16_t raw, float divisor) { return raw == kUnknownI16 ? NAN : raw / divisor; }
/// @brief A raw VOC or NOx index as the index; NaN while not available.
///
/// The VOC and NOx indices run from 1 to 500. For a while after a measurement
/// starts, the sensor reports 0 rather than unknown: its gas algorithm is
/// still starting up.
/// @param raw The raw value: the index times 10.
/// @return The index, or NaN.
float gasIndex(int16_t raw) { return raw < 10 ? NAN : scaled(raw, 10); }

/// Holds a FreeRTOS mutex for as long as it exists.
class Lock {
 public:
  /// @brief Takes the mutex, waiting as long as that takes.
  /// @param mutex The mutex.
  explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
  /// Gives the mutex back.
  ~Lock() { xSemaphoreGive(mutex_); }

 private:
  SemaphoreHandle_t mutex_;
};

/// @brief Reads a 16-bit register, high byte first.
/// @param address The device's I2C address.
/// @param reg The register.
/// @param[out] value What it holds.
/// @return Whether that worked.
bool readRegister16(uint8_t address, uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(2)) != 2) return false;
  value = static_cast<uint16_t>(Wire.read() << 8);
  value |= static_cast<uint16_t>(Wire.read());
  return true;
}

/// @brief Writes a 16-bit register, high byte first.
/// @param address The device's I2C address.
/// @param reg The register.
/// @param value What to write.
/// @return Whether that worked.
bool writeRegister16(uint8_t address, uint8_t reg, uint16_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(value >> 8));
  Wire.write(static_cast<uint8_t>(value & 0xFF));
  return Wire.endTransmission() == 0;
}

}  // namespace

void SensorService::begin(const SensorConfig& config) {
  config_ = config;
  mutex_ = xSemaphoreCreateMutex();
  queue_ = xQueueCreate(4, sizeof(Command));
  xTaskCreatePinnedToCore(taskEntry, "sensor", 6144, this, 2, nullptr, 1);
}

void SensorService::snapshot(SensorSnapshot& out) const {
  Lock lock(mutex_);
  out = shared_;
}

void SensorService::applyConfig(const SensorConfig& config) {
  Lock lock(mutex_);
  pendingConfig_ = config;
  configPending_ = true;
}

void SensorService::cleanFan() { send({CommandType::CleanFan, 0}); }

void SensorService::heatHumiditySensor() { send({CommandType::HeatHumiditySensor, 0}); }

void SensorService::recalibrateCo2(uint16_t referencePpm) {
  send({CommandType::RecalibrateCo2, referencePpm});
}

void SensorService::resetCo2Calibration() { send({CommandType::ResetCo2Calibration, 0}); }

void SensorService::restartVocLearning() { send({CommandType::RestartVocLearning, 0}); }

void SensorService::clearErrors() { send({CommandType::ClearErrors, 0}); }

void SensorService::restartBatteryGauge() { gaugeRestartRequested_ = true; }

void SensorService::powerDown(uint32_t timeoutMs) {
  powerDownRequested_ = true;
  for (uint32_t start = millis(); !poweredDown_ && millis() - start < timeoutMs;) delay(10);
}

void SensorService::send(const Command& cmd) {
  if (xQueueSend(queue_, &cmd, 0) != pdTRUE) setResult("Sensor busy, try again");
}

void SensorService::taskEntry(void* self) { static_cast<SensorService*>(self)->run(); }

void SensorService::run() {
  vTaskDelay(pdMS_TO_TICKS(kPowerUpMs));  // the STEMMA QT port was only just powered
  Wire.begin(SDA, SCL, 100000);           // the SEN6x supports 100 kHz at most
  sen_.begin(Wire, SEN69C_I2C_ADDR_6B);
  lastBatteryMs_ = millis() - kBatteryIntervalMs;  // read it at once

  for (;;) {
    while (!identify()) {
      setState(SensorState::NotFound);
      idle(kRetryMs);
    }
    if (!startMeasurement(true)) {
      setState(SensorState::NotFound);
      idle(kRetryMs);
      continue;
    }
    consecutiveErrors_ = 0;
    uint32_t lastStatus = millis();
    while (consecutiveErrors_ < kMaxConsecutiveErrors) {
      Command cmd;
      if (xQueueReceive(queue_, &cmd, pdMS_TO_TICKS(kPollMs)) == pdTRUE) handle(cmd);
      if (configPending_) applyPendingConfig();
      pollMeasurement();
      serveGauge();
      uint32_t now = millis();
      if (now - lastStatus >= kStatusIntervalMs) {
        lastStatus = now;
        readDeviceStatus();
      }
      if (state_ == SensorState::Measuring && now - lastDataMs_ >= kNoDataMs) {
        Serial.println("No new measurement from the sensor");
        consecutiveErrors_ = kMaxConsecutiveErrors;
      }
    }
    Serial.println("Sensor stopped answering; looking for it again");
    setState(SensorState::Starting);
  }
}

void SensorService::wait(uint32_t ms) {
  for (uint32_t start = millis(), waited = 0; waited < ms; waited = millis() - start) {
    serveGauge();
    vTaskDelay(pdMS_TO_TICKS(std::min(ms - waited, kPollMs)));
  }
}

void SensorService::idle(uint32_t ms) {
  for (uint32_t start = millis(), waited = 0; waited < ms; waited = millis() - start) {
    serveGauge();
    if (configPending_) takePendingConfig();  // for the next start
    Command cmd;
    if (xQueueReceive(queue_, &cmd, pdMS_TO_TICKS(std::min(ms - waited, kPollMs))) == pdTRUE) {
      setResult("Sensor not found");
    }
  }
}

void SensorService::takePendingConfig() {
  Lock lock(mutex_);
  config_ = pendingConfig_;
  configPending_ = false;
}

void SensorService::applyPendingConfig() {
  const SensorConfig old = config_;
  takePendingConfig();
  if (config_.tempOffsetC != old.tempOffsetC) applyTemperatureOffset();
  if (config_.altitudeM == old.altitudeM &&
      config_.co2SelfCalibration == old.co2SelfCalibration) {
    return;
  }
  setState(SensorState::Busy, "Applying settings");
  stopMeasurement();
  // If the sensor didn't come back, make run() look for it again.
  if (!startMeasurement(false)) consecutiveErrors_ = kMaxConsecutiveErrors;
}

void SensorService::serveGauge() {
  if (powerDownRequested_) {
    writeRegister16(kAddrMax17048, 0x0A, kGaugeHibernateDefault);
    poweredDown_ = true;
    vTaskSuspend(nullptr);  // until the power goes
  }
  if (gaugeRestartRequested_.exchange(false)) {
    writeRegister16(kAddrMax17048, 0x06, 0x4000);  // MODE: QuickStart
  }
  const uint32_t now = millis();
  if (now - lastBatteryMs_ >= kBatteryIntervalMs) {
    lastBatteryMs_ = now;
    readBattery();
  }
}

bool SensorService::identify() {
  int8_t product[32] = {};
  int8_t serial[32] = {};
  uint8_t major = 0;
  uint8_t minor = 0;
  int16_t error = sen_.getProductName(product, sizeof product);
  if (error == 0) error = sen_.getSerialNumber(serial, sizeof serial);
  if (error == 0) error = sen_.getVersion(major, minor);

  Lock lock(mutex_);
  shared_.lastError = error;
  if (error != 0) return false;
  strlcpy(shared_.product, reinterpret_cast<const char*>(product), sizeof shared_.product);
  strlcpy(shared_.serial, reinterpret_cast<const char*>(serial), sizeof shared_.serial);
  shared_.firmwareMajor = major;
  shared_.firmwareMinor = minor;
  return true;
}

bool SensorService::startMeasurement(bool resumeIfRunning) {
  if (resumeIfRunning) {
    // If only the ESP32 restarted, the sensor may still be measuring. Restarting
    // it would restart CO2 conditioning, so keep it running if it reports
    // values: an idle sensor reports every value as unknown.
    uint16_t pm1, pm25, pm4, pm10, hcho;
    int16_t rh, t, voc, nox, co2;
    if (sen_.readMeasuredValuesAsIntegers(pm1, pm25, pm4, pm10, rh, t, voc, nox, hcho, co2) == 0 &&
        t != kUnknownI16) {
      // Unless it has other settings than ours, as after a factory reset.
      if (remembered(pack(config_.altitudeM, config_.co2SelfCalibration))) {
        Serial.println("Sensor was already measuring; continuing");
        applyTemperatureOffset();
        startedMs_ = millis();
        {
          Lock lock(mutex_);
          shared_.measuringSinceMs = startedMs_;
          shared_.confirmedAltitudeM = config_.altitudeM;
        }
        setState(SensorState::Measuring);
        return true;
      }
      Serial.println("Sensor was measuring with other settings; restarting it");
      sen_.stopMeasurement();
    }
  }

  // Altitude and CO2 self-calibration can only be set while idle. The sensor
  // forgets both when it loses power, so they're set at every start.
  forgetSettings();
  int16_t error = sen_.setSensorAltitude(config_.altitudeM);
  bool confirmed = false;
  int32_t confirmedAltitude = -1;
  if (error == 0) {
    vTaskDelay(pdMS_TO_TICKS(20));
    uint16_t altitude = 0;
    if (sen_.getSensorAltitude(altitude) == 0) {
      Serial.printf("Sensor altitude set to %u m\n", altitude);
      confirmed = altitude == config_.altitudeM;
      confirmedAltitude = altitude;
    }
    error = sen_.setCo2SensorAutomaticSelfCalibration(config_.co2SelfCalibration ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(20));  // execution time; the driver doesn't wait here
  }
  {
    Lock lock(mutex_);
    shared_.confirmedAltitudeM = confirmedAltitude;
  }
  if (error == 0 && confirmed) remember(pack(config_.altitudeM, config_.co2SelfCalibration));
  if (error == 0) error = sen_.startContinuousMeasurement();
  if (error != 0) {
    Lock lock(mutex_);
    shared_.lastError = error;
    return false;
  }
  applyTemperatureOffset();
  startedMs_ = millis();
  {
    Lock lock(mutex_);
    shared_.measuringSinceMs = startedMs_;
    shared_.lastError = 0;
  }
  setState(SensorState::Measuring);
  Serial.println("Measurement started");
  return true;
}

void SensorService::stopMeasurement() {
  uint32_t running = millis() - startedMs_;
  if (running < kMinRunMs) wait(kMinRunMs - running);
  sen_.stopMeasurement();  // waits the required 1.4 s itself
}

void SensorService::applyTemperatureOffset() {
  auto offset = static_cast<int16_t>(lroundf(config_.tempOffsetC * 200));  // degC x200
  sen_.setTemperatureOffsetParameters(offset, 0, 0, 0);
}

bool SensorService::runHeater() {
  uint8_t firmwareMajor;
  {
    Lock lock(mutex_);
    firmwareMajor = shared_.firmwareMajor;
  }
  if (sen_.activateShtHeater() != 0) return false;
  if (firmwareMajor < kHeaterReportsFromFirmware) {
    wait(kHeaterBusyMs);
  } else {
    int16_t rh = kUnknownI16;
    int16_t t = kUnknownI16;
    for (uint32_t waited = 0; rh == kUnknownI16 && waited < kHeaterMaxMs; waited += kHeaterPollMs) {
      vTaskDelay(pdMS_TO_TICKS(kHeaterPollMs));
      if (sen_.getShtHeaterMeasurements(rh, t) != 0) rh = kUnknownI16;
    }
    if (rh != kUnknownI16) {
      Serial.printf("Heater done: %.1f %%RH, %.1f C while hot\n", rh / 100.0f, t / 200.0f);
    }
  }
  wait(kHeaterSettleMs);
  return true;
}

void SensorService::handle(const Command& cmd) {
  bool restarted = true;
  switch (cmd.type) {
    case CommandType::CleanFan:
      setState(SensorState::Busy, "Cleaning fan");
      stopMeasurement();
      if (sen_.startFanCleaning() == 0) {
        wait(kFanCleaningMs);
        setResult("Fan cleaned");
      } else {
        setResult("Fan cleaning failed");
      }
      restarted = startMeasurement(false);
      break;
    case CommandType::HeatHumiditySensor:
      setState(SensorState::Busy, "Heating sensor");
      stopMeasurement();
      setResult(runHeater() ? "Humidity sensor heated" : "Heating failed");
      restarted = startMeasurement(false);
      break;
    case CommandType::RecalibrateCo2: {
      if (millis() - startedMs_ < kRecalibrationWarmupMs) {
        setResult("Measure 3 min before calibrating");
        return;
      }
      setState(SensorState::Busy, "Calibrating CO2");
      stopMeasurement();
      uint16_t correction = 0xFFFF;
      int16_t error = sen_.performForcedCo2Recalibration(cmd.referencePpm, correction);
      if (error != 0 || correction == 0xFFFF) {
        setResult("CO2 calibration failed");
      } else {
        setResult("CO2 calibrated (%+d ppm)", static_cast<int>(correction) - 0x8000);
      }
      restarted = startMeasurement(false);
      break;
    }
    case CommandType::ResetCo2Calibration:
      setState(SensorState::Busy, "Resetting CO2");
      stopMeasurement();
      setResult(sen_.performCo2SensorFactoryReset() == 0 ? "CO2 calibration reset"
                                                         : "CO2 reset failed");
      restarted = startMeasurement(false);  // sets self-calibration again
      break;
    case CommandType::RestartVocLearning:
      // Only a reset or a power cut restarts the VOC algorithm (datasheet). The
      // sensor also forgets its settings, which the start sets again.
      setState(SensorState::Busy, "Restarting VOC");
      stopMeasurement();
      setResult(sen_.deviceReset() == 0 ? "VOC learning restarted" : "VOC restart failed");
      restarted = startMeasurement(false);
      break;
    case CommandType::ClearErrors: {
      SEN69CDeviceStatus status{};
      const bool cleared = sen_.readAndClearDeviceStatus(status) == 0;
      readDeviceStatus();  // shows what's still wrong now
      setResult(cleared ? "Sensor errors cleared" : "Could not clear the errors");
      return;
    }
  }
  // If the sensor didn't come back, make run() look for it again.
  if (!restarted) consecutiveErrors_ = kMaxConsecutiveErrors;
}

void SensorService::pollMeasurement() {
  if (state_ != SensorState::Measuring) return;
  uint8_t padding = 0;
  bool ready = false;
  int16_t error = sen_.getDataReady(padding, ready);
  if (error == 0 && !ready) return;

  uint16_t pm1 = 0, pm25 = 0, pm4 = 0, pm10 = 0, hcho = 0;
  int16_t rh = 0, t = 0, voc = 0, nox = 0, co2 = 0;
  if (error == 0) {
    error = sen_.readMeasuredValuesAsIntegers(pm1, pm25, pm4, pm10, rh, t, voc, nox, hcho, co2);
  }
  if (error != 0) {
    ++consecutiveErrors_;
    Lock lock(mutex_);
    shared_.lastError = error;
    return;
  }
  consecutiveErrors_ = 0;

  using aq::Metric;
  aq::Measurement m;
  if (millis() - startedMs_ >= kPmStartupMs) {
    m[Metric::Pm1] = scaled(pm1, 10);
    m[Metric::Pm25] = scaled(pm25, 10);
    m[Metric::Pm4] = scaled(pm4, 10);
    m[Metric::Pm10] = scaled(pm10, 10);
  }
  m[Metric::Humidity] = scaled(rh, 100);
  m[Metric::Temperature] = scaled(t, 200);
  m[Metric::Voc] = gasIndex(voc);
  m[Metric::Nox] = gasIndex(nox);
  m[Metric::Hcho] = scaled(hcho, 10);
  m[Metric::Co2] = scaled(co2, 1);

  lastDataMs_ = millis();
  Lock lock(mutex_);
  shared_.measurement = m;
  ++shared_.sequence;
  shared_.lastError = 0;
}

void SensorService::readDeviceStatus() {
  SEN69CDeviceStatus status{};
  if (sen_.readDeviceStatus(status) != 0) return;
  Lock lock(mutex_);
  if (status.value != 0 && status.value != shared_.deviceStatus) {
    Serial.printf("Sensor status 0x%08lX\n", static_cast<unsigned long>(status.value));
  }
  shared_.deviceStatus = status.value;
}

void SensorService::readBattery() {
  // MAX17048: VCELL in 78.125 uV steps, SOC in 1/256 %, CRATE (signed) in
  // 0.208 % per hour. It hibernates when the battery is hardly used, and then
  // updates the rate rarely; HIBRT = 0 keeps it awake (about 20 uA more).
  if (!gaugeAwake_) gaugeAwake_ = writeRegister16(kAddrMax17048, 0x0A, 0x0000);
  uint16_t vcell = 0;
  uint16_t soc = 0;
  uint16_t crate = 0;
  const bool ok = readRegister16(kAddrMax17048, 0x02, vcell) &&
                  readRegister16(kAddrMax17048, 0x04, soc) &&
                  readRegister16(kAddrMax17048, 0x16, crate);
  Lock lock(mutex_);
  shared_.batteryVolts = ok ? vcell * 78.125e-6f : NAN;
  shared_.batteryPercent = ok ? soc / 256.0f : NAN;
  shared_.batteryRate = ok ? static_cast<int16_t>(crate) * 0.208f : NAN;
  ++shared_.batterySequence;
}

void SensorService::setState(SensorState state, const char* activity) {
  const bool wasMeasuring = state_ == SensorState::Measuring;
  state_ = state;
  if (state == SensorState::Measuring) lastDataMs_ = millis();
  Lock lock(mutex_);
  shared_.state = state;
  shared_.activity = activity;
  if (wasMeasuring && state != SensorState::Measuring) {
    // Readings from before mustn't pass for current ones.
    shared_.measurement = aq::Measurement();
    ++shared_.sequence;
  }
}

void SensorService::setResult(const char* format, ...) {
  char text[sizeof shared_.result];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  Serial.println(text);
  Lock lock(mutex_);
  strlcpy(shared_.result, text, sizeof shared_.result);
  ++shared_.resultSequence;
}
