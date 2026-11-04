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

void BatteryWatch::pluggedIn(uint32_t nowMs) {
  jump_ = Jump::Up;
  jumpMs_ = nowMs;
}

void BatteryWatch::noteJump(float volts, uint32_t nowMs) {
  // Against the highest and lowest of the last readings, so a reading after a
  // single dip doesn't count as a rise.
  float high = NAN;
  float low = NAN;
  for (float v : recent_) {
    high = std::fmax(high, v);
    low = std::fmin(low, v);
  }
  if (volts >= high + kJumpVolts) {
    jump_ = Jump::Up;
    jumpMs_ = nowMs;
  } else if (volts <= low - kJumpVolts) {
    jump_ = Jump::Down;
    jumpMs_ = nowMs;
  }
  recent_[2] = recent_[1];
  recent_[1] = recent_[0];
  recent_[0] = volts;
}

BatteryState BatteryWatch::stateFrom(float volts, float ratePerHour, uint32_t nowMs) {
  BatteryState state = stateOf(ratePerHour);
  if (state == BatteryState::Unknown) return state;
  if (state == BatteryState::Discharging && volts >= kFullVolts) state = BatteryState::Idle;
  if (jump_ != Jump::None) {
    const BatteryState jumped =
        jump_ == Jump::Up ? BatteryState::Charging : BatteryState::Discharging;
    if (state != jumped && nowMs - jumpMs_ < kSettleMs) return jumped;
    jump_ = Jump::None;  // the rate has followed, or never will
  }
  return state;
}

void BatteryWatch::update(float volts, float percent, float ratePerHour, uint32_t nowMs) {
  noteJump(volts, nowMs);
  state_ = stateFrom(volts, ratePerHour, nowMs);
  const bool discharging = state_ == BatteryState::Discharging;

  uint8_t warning = 0;
  if (discharging && !std::isnan(percent)) {
    warning = percent <= kVeryLowPercent ? 2 : percent <= kLowPercent ? 1 : 0;
    // A warning lasts until the charge is clearly above its limit again, so
    // a reading that wavers around the limit doesn't warn again and again.
    const float limit = warning_ == 2 ? kVeryLowPercent : kLowPercent;
    if (warning < warning_ && percent <= limit + kWarningHysteresis) warning = warning_;
  }
  warning_ = warning;

  if (!discharging || std::isnan(volts) || volts > kEmptyVolts) {
    low_ = false;
    empty_ = false;
    return;
  }
  if (!low_) {
    low_ = true;
    lowSinceMs_ = nowMs;
  }
  empty_ = nowMs - lowSinceMs_ >= kEmptyForMs;
}

bool powerIsBack(float volts, float ratePerHour) {
  return stateOf(ratePerHour) == BatteryState::Charging ||
         (!std::isnan(volts) && volts >= BatteryWatch::kBackVolts);
}

}  // namespace power
