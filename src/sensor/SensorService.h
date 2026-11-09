// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Owns the I2C bus: runs the SEN69C (and reads the battery monitor) in
/// its own FreeRTOS task, so slow sensor operations never block the UI.
#pragma once

#include <Arduino.h>
#include <Metrics.h>
#include <SensirionI2cSen69c.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <atomic>

/// What the sensor is doing.
enum class SensorState : uint8_t {
  Starting,   ///< Looking for the sensor and starting it.
  Measuring,  ///< Measuring continuously.
  Busy,       ///< Stopped for a maintenance operation; see SensorSnapshot::activity.
  NotFound,   ///< Not answering, or not starting; it's tried again every few seconds.
};

/// @brief Sensor settings that the firmware must re-apply after every sensor
/// restart (the SEN69C forgets them on power-up).
struct SensorConfig {
  float tempOffsetC = 0;  ///< Added to the measured temperature, in degC.
  uint16_t altitudeM = 0;  ///< For the CO2 sensor's pressure compensation, in m.
  bool co2SelfCalibration = true;  ///< The CO2 sensor's automatic self-calibration.
};

/// @brief The sensor's latest state, as the sensor task shares it with the
/// rest (see SensorService::snapshot()).
struct SensorSnapshot {
  /// The latest values; NaN where not available yet, and all NaN while the
  /// sensor isn't measuring.
  aq::Measurement measurement;
  uint32_t sequence = 0;  ///< Increments with every new measurement, and when they are cleared.
  SensorState state = SensorState::Starting;  ///< What the sensor is doing.
  const char* activity = "";      ///< What the sensor is doing while Busy.
  uint32_t measuringSinceMs = 0;  ///< millis() when the current measurement started.
  uint32_t deviceStatus = 0;      ///< SEN69C device status register (0 = no errors).
  int16_t lastError = 0;          ///< Last I2C/driver error, 0 if the last command worked.
  char product[32] = "";  ///< The sensor's product name; `""` until it's found.
  char serial[32] = "";  ///< Its serial number.
  uint8_t firmwareMajor = 0;  ///< Its firmware version, major part.
  uint8_t firmwareMinor = 0;  ///< Its firmware version, minor part.
  float batteryVolts = NAN;  ///< Battery voltage, in V; NaN when no battery monitor answers.
  float batteryPercent = NAN;  ///< The battery monitor's charge, in %; NaN the same.
  float batteryRate = NAN;  ///< Charge rate, in % per hour, negative while discharging.
  uint32_t batterySequence = 0;  ///< Increments with every battery reading.
  /// @brief Altitude the sensor reported back after it was set, in m; -1 when
  /// it couldn't be read (it can only be read while the sensor is idle, i.e.
  /// at start).
  int32_t confirmedAltitudeM = -1;
  char result[48] = "";       ///< Outcome of the last maintenance operation.
  uint32_t resultSequence = 0;  ///< Increments with every new `result`.
};

/// @brief Runs the SEN69C, and reads the battery monitor, in its own FreeRTOS
/// task: the only one that uses the I2C bus.
///
/// Others read the latest state with snapshot() and send commands, which are
/// queued and return at once, so a slow sensor operation never blocks them.
/// When a command's outcome is worth telling, it's in SensorSnapshot::result.
class SensorService {
 public:
  /// Time the sensor must have been measuring before a CO2 recalibration, in ms.
  static constexpr uint32_t kRecalibrationWarmupMs = 3 * 60 * 1000;

  /// @brief Starts the sensor task, which finds the sensor and starts it
  /// measuring.
  /// @param config The sensor settings to start with.
  void begin(const SensorConfig& config);
  /// @brief Copies the sensor's latest state.
  /// @param[out] out Where to.
  void snapshot(SensorSnapshot& out) const;

  // All commands are queued and run in the sensor task.
  /// @brief Gives the sensor new settings.
  ///
  /// A new temperature offset applies at once. The sensor takes a new altitude
  /// or CO2 self-calibration setting only while idle, so for those it stops
  /// measuring and starts again.
  /// @param config The settings.
  void applyConfig(const SensorConfig& config);
  /// @brief Cleans the fan: runs it at full speed for 10 s, with the
  /// measurement stopped.
  void cleanFan();
  /// @brief Briefly heats the humidity sensor, which undoes the too-high
  /// readings it gives after a long time in very humid air.
  ///
  /// The measurement stops meanwhile, and until the temperature reading has
  /// settled again.
  void heatHumiditySensor();
  /// @brief Forced CO2 recalibration: tells the sensor how much CO2 there is.
  ///
  /// Only after it has been measuring for kRecalibrationWarmupMs; sooner, the
  /// result says to wait.
  /// @param referencePpm The CO2 around it, in ppm, e.g. about 420 outdoors.
  void recalibrateCo2(uint16_t referencePpm);
  /// Erases every CO2 calibration, forced or automatic.
  void resetCo2Calibration();
  /// @brief Resets the sensor as unplugging it would, so the VOC and NOx
  /// indices learn the room's usual air afresh.
  ///
  /// CO2 calibrations are kept.
  void restartVocLearning();
  /// @brief Clears the sensor's error flags.
  ///
  /// Most error flags stay set until cleared, even once the error is gone.
  void clearErrors();
  /// @brief Makes the battery monitor estimate the charge afresh from the
  /// voltage, as when it's first powered.
  ///
  /// Not queued: done within a fraction of a second, whatever the sensor is
  /// doing.
  void restartBatteryGauge();
  /// @brief Before powering off: lets the battery monitor hibernate again and
  /// stops using the bus.
  ///
  /// Not queued: the sensor task gets there within a fraction of a second,
  /// whatever the sensor is doing (a maintenance operation is left unfinished;
  /// the power is about to go anyway), and then stops for good.
  /// @param timeoutMs How long to wait for the sensor task to get there, in
  ///   ms.
  void powerDown(uint32_t timeoutMs);

 private:
  /// What a Command asks for: one for each public command.
  enum class CommandType : uint8_t {
    ApplyConfig,          ///< applyConfig()
    CleanFan,             ///< cleanFan()
    HeatHumiditySensor,   ///< heatHumiditySensor()
    RecalibrateCo2,       ///< recalibrateCo2()
    ResetCo2Calibration,  ///< resetCo2Calibration()
    RestartVocLearning,   ///< restartVocLearning()
    ClearErrors,          ///< clearErrors()
  };
  /// A command for the sensor task.
  struct Command {
    CommandType type;  ///< What it asks for.
    SensorConfig config;  ///< For ApplyConfig.
    uint16_t referencePpm;  ///< For RecalibrateCo2, in ppm.
  };

  /// @brief The sensor task's entry point.
  /// @param self The SensorService to run.
  static void taskEntry(void* self);
  /// @brief The sensor task: finds the sensor and starts it measuring, then
  /// runs the commands and reads the sensor and the battery monitor; never
  /// returns.
  ///
  /// If the sensor stops answering, it looks for it again. The battery monitor
  /// is read all the while, whatever the sensor is doing: the warnings and
  /// switching off before the battery is empty depend on it.
  void run();
  /// @brief Waits, while still reading the battery monitor and serving
  /// restartBatteryGauge() and powerDown().
  ///
  /// For every wait longer than a moment, e.g. during a maintenance operation.
  /// @param ms How long, in ms.
  void wait(uint32_t ms);
  /// @brief Waits while the sensor isn't measuring, like wait(), and answers
  /// queued commands with "Sensor not found".
  /// @param ms How long, in ms.
  void idle(uint32_t ms);
  /// @brief Serves restartBatteryGauge() and powerDown(), and reads the
  /// battery monitor every kBatteryIntervalMs.
  void serveGauge();
  /// @brief Reads the sensor's product name, serial number and firmware
  /// version into the snapshot.
  /// @return Whether the sensor answered.
  bool identify();
  /// @brief Gives the sensor its settings and starts it measuring.
  /// @param resumeIfRunning Whether to keep a measurement that's still running
  ///   with our settings, as after a restart of only the ESP32: restarting it
  ///   would restart the CO2 sensor's conditioning.
  /// @return Whether it's measuring.
  bool startMeasurement(bool resumeIfRunning);
  /// @brief Stops the measurement, after waiting until it has run for
  /// kMinRunMs.
  void stopMeasurement();
  /// Gives the sensor the temperature offset in config_.
  void applyTemperatureOffset();
  /// @brief Heats the humidity sensor, and waits until it's done and the
  /// temperature reading has settled.
  ///
  /// Only while the sensor is idle.
  /// @return Whether the sensor took the command.
  bool runHeater();
  /// @brief Runs a command from the queue.
  ///
  /// If the sensor doesn't start measuring again after it, run() looks for it
  /// again.
  /// @param cmd The command.
  void handle(const Command& cmd);
  /// Reads a new measurement into the snapshot, if there's one.
  void pollMeasurement();
  /// Reads the sensor's device status register into the snapshot.
  void readDeviceStatus();
  /// @brief Reads the battery monitor's voltage, charge and charge rate into
  /// the snapshot; NaN if it doesn't answer.
  void readBattery();
  /// @brief Sets what the sensor is doing, here and in the snapshot.
  ///
  /// Leaving Measuring clears the snapshot's values, so old readings don't
  /// pass for current ones.
  /// @param state The state.
  /// @param activity What it's doing while Busy, shown on the screen; it must
  ///   last, like a string literal.
  void setState(SensorState state, const char* activity = "");
  /// @brief Sets the outcome of a maintenance operation, which the screen
  /// shows, and logs it.
  /// @param format A printf() format; the text is cut at 47 characters.
  /// @param ... The values for it.
  void setResult(const char* format, ...);
  /// @brief Queues a command for the sensor task, without waiting.
  ///
  /// If the queue is full, the result says `Sensor busy, try again`.
  /// @param cmd The command.
  void send(const Command& cmd);

  // Owned by the sensor task after begin().
  SensirionI2cSen69c sen_;  ///< The SEN69C's driver.
  SensorConfig config_;  ///< The sensor settings in use.
  SensorState state_ = SensorState::Starting;
  uint32_t startedMs_ = 0;  ///< millis() when the measurement last started.
  uint32_t consecutiveErrors_ = 0;  ///< Failed reads in a row.
  bool gaugeAwake_ = false;  ///< The battery monitor's hibernation is off.
  uint32_t lastBatteryMs_ = 0;  ///< millis() when the battery monitor was last read.
  uint32_t lastDataMs_ = 0;  ///< millis() of the last measurement, or of the start.
  std::atomic<bool> gaugeRestartRequested_{false};  ///< restartBatteryGauge() was called.
  std::atomic<bool> powerDownRequested_{false};  ///< powerDown() was called.
  std::atomic<bool> poweredDown_{false};  ///< The sensor task has done powerDown().

  mutable SemaphoreHandle_t mutex_ = nullptr;  ///< Guards shared_.
  QueueHandle_t queue_ = nullptr;  ///< Commands for the sensor task.
  SensorSnapshot shared_;  ///< The latest state, guarded by mutex_.
};
