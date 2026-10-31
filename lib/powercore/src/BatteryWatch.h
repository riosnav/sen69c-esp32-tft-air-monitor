// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the battery monitor's readings mean: whether the battery is charging,
// and when to warn that it's running low.
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

  // With the latest reading (NaN where there is none), as often as convenient.
  void update(float volts, float percent, float ratePerHour, uint32_t nowMs);
  // 0: fine, charging, or no battery; 1: low; 2: very low.
  uint8_t warning() const { return warning_; }

 private:
  uint8_t warning_ = 0;
};

}  // namespace power
