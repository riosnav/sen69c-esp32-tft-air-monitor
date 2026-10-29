// SPDX-License-Identifier: GPL-3.0-or-later
//
// The NeoPixel mirrors the overall air quality.
#pragma once

#include <AirQuality.h>

#include <cstdint>

#include "Settings.h"

class StatusLed {
 public:
  // `warmingUp`: some values aren't available yet. `alarm`: an unacknowledged
  // alarm is showing, which makes the LED blink.
  void update(aq::Level overall, bool warmingUp, bool alarm, LedMode mode, uint32_t nowMs);

 private:
  uint32_t shown_ = 0xFFFFFFFF;
};
