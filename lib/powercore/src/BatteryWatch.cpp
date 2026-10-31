// SPDX-License-Identifier: GPL-3.0-or-later

#include "BatteryWatch.h"

#include <cmath>

namespace power {

BatteryState stateOf(float ratePerHour) {
  if (std::isnan(ratePerHour)) return BatteryState::Unknown;
  if (ratePerHour >= BatteryWatch::kRateThreshold) return BatteryState::Charging;
  if (ratePerHour <= -BatteryWatch::kRateThreshold) return BatteryState::Discharging;
  return BatteryState::Idle;
}

void BatteryWatch::update(float volts, float percent, float ratePerHour, uint32_t nowMs) {
  const bool discharging = stateOf(ratePerHour) == BatteryState::Discharging;

  uint8_t warning = 0;
  if (discharging && !std::isnan(percent)) {
    warning = percent <= kVeryLowPercent ? 2 : percent <= kLowPercent ? 1 : 0;
    // A warning lasts until the charge is clearly above its limit again, so
    // a reading that wavers around the limit doesn't warn again and again.
    const float limit = warning_ == 2 ? kVeryLowPercent : kLowPercent;
    if (warning < warning_ && percent <= limit + kWarningHysteresis) warning = warning_;
  }
  warning_ = warning;
}

}  // namespace power
