// SPDX-License-Identifier: GPL-3.0-or-later

#include "SensorService.h"

#include <Wire.h>
#include <esp_attr.h>

#include <cstdarg>

namespace {

// The settings the sensor was last given (the altitude it confirmed, and CO2
// self-calibration), in memory that survives restarts of the ESP32 but not
// power cuts, like the sensor's own settings. They stay valid when the sensor
// keeps measuring across a restart.
RTC_NOINIT_ATTR uint32_t sensorSettings;
RTC_NOINIT_ATTR uint32_t sensorSettingsCheck;  // tells saved settings from leftover garbage
constexpr uint32_t kSettingsKey = 0x5A17C0DE;

uint32_t pack(uint16_t altitudeM, bool co2SelfCalibration) {
  return altitudeM | (co2SelfCalibration ? 1u << 16 : 0);
}
void remember(uint32_t settings) {
  sensorSettings = settings;
  sensorSettingsCheck = settings ^ kSettingsKey;
}
void forgetSettings() { sensorSettingsCheck = ~(sensorSettings ^ kSettingsKey); }
bool remembered(uint32_t settings) {
  return sensorSettings == settings && sensorSettingsCheck == (settings ^ kSettingsKey);
}

constexpr uint8_t kAddrMax17048 = 0x36;
// Raw values the SEN69C returns while a measurement isn't available yet.
constexpr uint16_t kUnknownU16 = 0xFFFF;
constexpr int16_t kUnknownI16 = 0x7FFF;
// Stopping during the first 24 s of CO2 conditioning causes a CO2 error.
constexpr uint32_t kCo2ConditioningMs = 24000;
constexpr uint32_t kFanCleaningMs = 10000;
// The humidity sensor's heater runs for 1 s. Firmware older than 9.0 is busy
// for 1.3 s after the command; newer firmware reports when it's done. The
// temperature reading needs 20 s to settle afterwards.
constexpr uint8_t kHeaterReportsFromFirmware = 9;
constexpr uint32_t kHeaterBusyMs = 1300;
constexpr uint32_t kHeaterPollMs = 50;
constexpr uint32_t kHeaterMaxMs = 3000;
constexpr uint32_t kHeaterSettleMs = 20000;
// PM readings are only stable 30 s after a measurement starts (datasheet);
// earlier ones often overshoot and would raise false alerts.
constexpr uint32_t kPmStartupMs = 30000;
constexpr uint32_t kPowerUpMs = 150;
constexpr uint32_t kPollMs = 200;
constexpr uint32_t kStatusIntervalMs = 10000;
constexpr uint32_t kBatteryIntervalMs = 30000;
constexpr uint32_t kRetryMs = 5000;
// After this many failed reads in a row, look for the sensor again.
constexpr uint32_t kMaxConsecutiveErrors = 10;

float scaled(uint16_t raw, float divisor) { return raw == kUnknownU16 ? NAN : raw / divisor; }
float scaled(int16_t raw, float divisor) { return raw == kUnknownI16 ? NAN : raw / divisor; }
// The VOC and NOx indices run from 1 to 500. For a while after a measurement
// starts, the sensor reports 0 rather than unknown: its gas algorithm is
// still starting up.
float gasIndex(int16_t raw) { return raw < 10 ? NAN : scaled(raw, 10); }

class Lock {
 public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex_); }

 private:
  SemaphoreHandle_t mutex_;
};

bool readRegister16(uint8_t address, uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(2)) != 2) return false;
  value = static_cast<uint16_t>(Wire.read() << 8);
  value |= static_cast<uint16_t>(Wire.read());
  return true;
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
  send({CommandType::ApplyConfig, config, 0});
}

void SensorService::cleanFan() { send({CommandType::CleanFan, {}, 0}); }

void SensorService::heatHumiditySensor() { send({CommandType::HeatHumiditySensor, {}, 0}); }

void SensorService::recalibrateCo2(uint16_t referencePpm) {
  send({CommandType::RecalibrateCo2, {}, referencePpm});
}

void SensorService::resetCo2Calibration() { send({CommandType::ResetCo2Calibration, {}, 0}); }

void SensorService::clearErrors() { send({CommandType::ClearErrors, {}, 0}); }

void SensorService::send(const Command& cmd) {
  if (xQueueSend(queue_, &cmd, 0) != pdTRUE) setResult("Sensor busy, try again");
}

void SensorService::taskEntry(void* self) { static_cast<SensorService*>(self)->run(); }

void SensorService::run() {
  vTaskDelay(pdMS_TO_TICKS(kPowerUpMs));  // the STEMMA QT port was only just powered
  Wire.begin(SDA, SCL, 100000);           // the SEN6x supports 100 kHz at most
  sen_.begin(Wire, SEN69C_I2C_ADDR_6B);

  for (;;) {
    while (!identify()) {
      setState(SensorState::NotFound);
      vTaskDelay(pdMS_TO_TICKS(kRetryMs));
    }
    if (!startMeasurement(true)) {
      vTaskDelay(pdMS_TO_TICKS(kRetryMs));
      continue;
    }
    consecutiveErrors_ = 0;
    uint32_t lastStatus = millis();
    uint32_t lastBattery = millis() - kBatteryIntervalMs;
    while (consecutiveErrors_ < kMaxConsecutiveErrors) {
      Command cmd;
      if (xQueueReceive(queue_, &cmd, pdMS_TO_TICKS(kPollMs)) == pdTRUE) handle(cmd);
      pollMeasurement();
      uint32_t now = millis();
      if (now - lastStatus >= kStatusIntervalMs) {
        lastStatus = now;
        readDeviceStatus();
      }
      if (now - lastBattery >= kBatteryIntervalMs) {
        lastBattery = now;
        readBattery();
      }
    }
    Serial.println("Sensor stopped answering; looking for it again");
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
  if (error == 0) {
    vTaskDelay(pdMS_TO_TICKS(20));
    uint16_t altitude = 0;
    if (sen_.getSensorAltitude(altitude) == 0) {
      Serial.printf("Sensor altitude set to %u m\n", altitude);
      confirmed = altitude == config_.altitudeM;
      Lock lock(mutex_);
      shared_.confirmedAltitudeM = altitude;
    }
    error = sen_.setCo2SensorAutomaticSelfCalibration(config_.co2SelfCalibration ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(20));  // execution time; the driver doesn't wait here
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
  if (running < kCo2ConditioningMs) vTaskDelay(pdMS_TO_TICKS(kCo2ConditioningMs - running));
  sen_.stopMeasurement();  // waits the required 1.4 s itself
}

void SensorService::applyTemperatureOffset() {
  auto offset = static_cast<int16_t>(lroundf(config_.tempOffsetC * 200));  // degC x200
  sen_.setTemperatureOffsetParameters(offset, 0, 0, 0);
}

// Only while the sensor is idle.
bool SensorService::runHeater() {
  uint8_t firmwareMajor;
  {
    Lock lock(mutex_);
    firmwareMajor = shared_.firmwareMajor;
  }
  if (sen_.activateShtHeater() != 0) return false;
  if (firmwareMajor < kHeaterReportsFromFirmware) {
    vTaskDelay(pdMS_TO_TICKS(kHeaterBusyMs));
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
  vTaskDelay(pdMS_TO_TICKS(kHeaterSettleMs));
  return true;
}

void SensorService::handle(const Command& cmd) {
  bool restarted = true;
  switch (cmd.type) {
    case CommandType::ApplyConfig: {
      SensorConfig old = config_;
      config_ = cmd.config;
      if (config_.tempOffsetC != old.tempOffsetC) applyTemperatureOffset();
      if (config_.altitudeM == old.altitudeM &&
          config_.co2SelfCalibration == old.co2SelfCalibration) {
        return;
      }
      setState(SensorState::Busy, "Applying settings");
      stopMeasurement();
      restarted = startMeasurement(false);
      break;
    }
    case CommandType::CleanFan:
      setState(SensorState::Busy, "Cleaning fan");
      stopMeasurement();
      if (sen_.startFanCleaning() == 0) {
        vTaskDelay(pdMS_TO_TICKS(kFanCleaningMs));
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
  // MAX17048: VCELL in 78.125 uV steps, SOC in 1/256 %.
  uint16_t vcell = 0;
  uint16_t soc = 0;
  bool ok = readRegister16(kAddrMax17048, 0x02, vcell) && readRegister16(kAddrMax17048, 0x04, soc);
  Lock lock(mutex_);
  shared_.batteryVolts = ok ? vcell * 78.125e-6f : NAN;
  shared_.batteryPercent = ok ? soc / 256.0f : NAN;
}

void SensorService::setState(SensorState state, const char* activity) {
  state_ = state;
  Lock lock(mutex_);
  shared_.state = state;
  shared_.activity = activity;
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
