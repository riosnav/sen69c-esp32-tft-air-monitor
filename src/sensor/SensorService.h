// SPDX-License-Identifier: GPL-3.0-or-later
//
// Owns the I2C bus: runs the SEN69C (and reads the battery monitor) in its own
// FreeRTOS task, so slow sensor operations never block the UI.
#pragma once

#include <Arduino.h>
#include <Metrics.h>
#include <SensirionI2cSen69c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <atomic>

enum class SensorState : uint8_t {
  Starting,   // looking for the sensor
  Measuring,
  Busy,       // stopped for a maintenance operation; see SensorSnapshot::activity
  NotFound,
};

// Sensor settings that the firmware must re-apply after every sensor restart
// (the SEN69C forgets them on power-up).
struct SensorConfig {
  float tempOffsetC = 0;
  uint16_t altitudeM = 0;
  bool co2SelfCalibration = true;
};

struct SensorSnapshot {
  aq::Measurement measurement;
  uint32_t sequence = 0;  // increments with every new measurement
  SensorState state = SensorState::Starting;
  const char* activity = "";      // what the sensor is doing while Busy
  uint32_t measuringSinceMs = 0;  // millis() when the current measurement started
  uint32_t deviceStatus = 0;      // SEN69C device status register (0 = no errors)
  int16_t lastError = 0;          // last I2C/driver error, 0 if the last command worked
  char product[32] = "";
  char serial[32] = "";
  uint8_t firmwareMajor = 0;
  uint8_t firmwareMinor = 0;
  float batteryVolts = NAN;  // NaN when no battery monitor answers
  float batteryPercent = NAN;
  float batteryRate = NAN;  // % per hour, negative while discharging
  uint32_t batterySequence = 0;  // increments with every battery reading
  // Altitude the sensor reported back after it was set; -1 when it couldn't be
  // read (it can only be read while the sensor is idle, i.e. at start).
  int32_t confirmedAltitudeM = -1;
  char result[48] = "";       // outcome of the last maintenance operation
  uint32_t resultSequence = 0;  // increments when `result` changes
};

class SensorService {
 public:
  // Time the sensor must have been measuring before a CO2 recalibration.
  static constexpr uint32_t kRecalibrationWarmupMs = 3 * 60 * 1000;

  void begin(const SensorConfig& config);
  void snapshot(SensorSnapshot& out) const;

  // All commands are queued and run in the sensor task.
  void applyConfig(const SensorConfig& config);
  void cleanFan();
  // Briefly heats the humidity sensor, which undoes the too-high readings it
  // gives after a long time in very humid air.
  void heatHumiditySensor();
  void recalibrateCo2(uint16_t referencePpm);
  // Erases every CO2 calibration, forced or automatic.
  void resetCo2Calibration();
  // Resets the sensor as unplugging it would, so the VOC and NOx indices learn
  // the room's usual air afresh. CO2 calibrations are kept.
  void restartVocLearning();
  // Most error flags stay set until cleared, even once the error is gone.
  void clearErrors();
  // Before powering off: lets the battery monitor hibernate again and stops
  // using the bus. Waits up to `timeoutMs` for the sensor task to get there.
  void powerDown(uint32_t timeoutMs);

 private:
  enum class CommandType : uint8_t {
    ApplyConfig,
    CleanFan,
    HeatHumiditySensor,
    RecalibrateCo2,
    ResetCo2Calibration,
    RestartVocLearning,
    ClearErrors,
    PowerDown,
  };
  struct Command {
    CommandType type;
    SensorConfig config;
    uint16_t referencePpm;
  };

  static void taskEntry(void* self);
  void run();
  bool identify();
  bool startMeasurement(bool resumeIfRunning);
  void stopMeasurement();
  void applyTemperatureOffset();
  bool runHeater();
  void handle(const Command& cmd);
  void pollMeasurement();
  void readDeviceStatus();
  void readBattery();
  void setState(SensorState state, const char* activity = "");
  void setResult(const char* format, ...);
  void send(const Command& cmd);

  // Owned by the sensor task after begin().
  SensirionI2cSen69c sen_;
  SensorConfig config_;
  SensorState state_ = SensorState::Starting;
  uint32_t startedMs_ = 0;
  uint32_t consecutiveErrors_ = 0;
  bool gaugeAwake_ = false;  // the battery monitor's hibernation is off
  std::atomic<bool> poweredDown_{false};

  mutable SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  SensorSnapshot shared_;  // guarded by mutex_
};
