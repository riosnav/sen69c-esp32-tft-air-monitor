// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the battery monitor's readings mean: whether the battery is charging,
// when to warn that it's running low, when to switch off before it's empty,
// and whether USB power is back after that.
//
// There's no way to see USB power directly on this board; the battery
// monitor's charge rate tells instead. While the monitor runs on battery it
// draws far more than kRateThreshold, and while USB power charges an empty
// battery the rate is clearly positive.
#pragma once

#include <cstdint>

namespace power {

enum class BatteryState : uint8_t {
  Unknown,
  Charging,
  Discharging,  // running the monitor
  Idle,         // full on USB power, or hardly used
};

// The charge rate in % per hour, negative while discharging; NaN if unknown.
BatteryState stateOf(float ratePerHour);

class BatteryWatch {
 public:
  static constexpr float kRateThreshold = 0.5f;  // % per hour
  static constexpr float kLowPercent = 20;
  static constexpr float kVeryLowPercent = 10;
  static constexpr float kWarningHysteresis = 2;  // % above a limit before its warning ends
  // The sensor needs 3.15 V, which the board's regulator can't hold with the
  // battery much below this.
  static constexpr float kEmptyVolts = 3.5f;
  // How long it must stay there, so a brief dip under load doesn't count.
  static constexpr uint32_t kEmptyForMs = 60000;
  // After switching off: a battery this full can only be charging.
  static constexpr float kBackVolts = 3.8f;

  // With the latest reading (NaN where there is none), as often as convenient.
  void update(float volts, float percent, float ratePerHour, uint32_t nowMs);
  // 0: fine, charging, or no battery; 1: low; 2: very low.
  uint8_t warning() const { return warning_; }
  // Low for long enough while discharging: time to switch off.
  bool empty() const { return empty_; }

 private:
  uint8_t warning_ = 0;
  bool empty_ = false;
  bool low_ = false;
  uint32_t lowSinceMs_ = 0;
};

// After switching off for an empty battery: whether USB power is back, so
// the monitor can start again.
bool powerIsBack(float volts, float ratePerHour);

}  // namespace power
