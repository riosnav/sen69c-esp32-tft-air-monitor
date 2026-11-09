// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The status LED: the NeoPixel mirrors the overall air quality.
#pragma once

#include <AirQuality.h>

#include <cstdint>

#include "Settings.h"

/// @brief The NeoPixel, in the overall air quality's colour.
///
/// Green for good, orange for fair, red for poor, and blue while warming up or
/// unknown; blinking once a second for an alarm.
class StatusLed {
 public:
  /// @brief Shows the overall air quality in the LED mode set; call it often.
  ///
  /// Writes to the LED only when its colour changes.
  /// @param overall The overall air quality.
  /// @param warmingUp Whether some values aren't available yet.
  /// @param alarm Whether an unacknowledged alarm is showing, which makes the
  ///   LED blink.
  /// @param mode How bright, or off.
  /// @param nowMs The time, as millis().
  void update(aq::Level overall, bool warmingUp, bool alarm, LedMode mode, uint32_t nowMs);

 private:
  uint32_t shown_ = 0xFFFFFFFF;  ///< The colour last written, as 0xRRGGBB; none at first.
};
