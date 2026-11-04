// SPDX-License-Identifier: GPL-3.0-or-later

#include "BatteryWatch.h"

#include <algorithm>
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

float BatteryWatch::percent() const {
  if (std::isnan(gaugePercent_)) return NAN;
  const float left = (gaugePercent_ - emptyPercent_) / (100 - emptyPercent_) * 100;
  return std::clamp(left, 0.0f, 100.0f);
}

void BatteryWatch::setEmptyPercent(float gaugePercent) {
  const bool plausible = gaugePercent >= 0 && gaugePercent <= kMaxEmptyPercent;  // not NaN
  emptyPercent_ = plausible ? gaugePercent : 0;
}

bool BatteryWatch::learnEmpty() {
  const float before = emptyPercent_;
  if (gaugePercent_ <= kMaxEmptyPercent) setEmptyPercent(gaugePercent_);
  return emptyPercent_ != before;
}

void BatteryWatch::update(float volts, float gaugePercent, float ratePerHour, uint32_t nowMs) {
  gaugePercent_ = gaugePercent;
  noteJump(volts, nowMs);
  state_ = stateFrom(volts, ratePerHour, nowMs);
  const bool discharging = state_ == BatteryState::Discharging;
  const float left = percent();

  uint8_t warning = 0;
  if (discharging && !std::isnan(left)) {
    warning = left <= kVeryLowPercent ? 2 : left <= kLowPercent ? 1 : 0;
    // A warning lasts until the charge is clearly above its limit again, so
    // a reading that wavers around the limit doesn't warn again and again.
    const float limit = warning_ == 2 ? kVeryLowPercent : kLowPercent;
    if (warning < warning_ && left <= limit + kWarningHysteresis) warning = warning_;
  }

  // NaN compares false, so a missing reading counts as not low.
  const bool veryLow = veryLow_.update(discharging && volts <= kVeryLowVolts, nowMs);
  const bool low = low_.update(discharging && volts <= kLowVolts, nowMs);
  if (!discharging) {
    voltsWarning_ = 0;
  } else if (veryLow) {
    voltsWarning_ = 2;
  } else if (low && voltsWarning_ == 0) {
    voltsWarning_ = 1;
  }
  warning_ = std::max(warning, voltsWarning_);

  isEmpty_ = empty_.update(discharging && volts <= kEmptyVolts, nowMs);
}

bool BatteryWatch::Below::update(bool below, uint32_t nowMs) {
  if (!below) {
    below_ = false;
    return false;
  }
  if (!below_) {
    below_ = true;
    sinceMs_ = nowMs;
  }
  return nowMs - sinceMs_ >= kLowForMs;
}

bool powerIsBack(float volts, float ratePerHour) {
  return stateOf(ratePerHour) == BatteryState::Charging ||
         (!std::isnan(volts) && volts >= BatteryWatch::kBackVolts);
}

}  // namespace power
