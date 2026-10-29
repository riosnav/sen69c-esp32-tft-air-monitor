// SPDX-License-Identifier: GPL-3.0-or-later

#include "StatusLed.h"

#include <Arduino.h>

void StatusLed::update(aq::Level overall, bool warmingUp, bool alarm, LedMode mode,
                       uint32_t nowMs) {
  uint8_t r = 0, g = 0, b = 0;
  if (warmingUp || overall == aq::Level::Unknown) {
    r = 0, g = 2, b = 4;
  } else if (overall == aq::Level::Good) {
    r = 0, g = 4, b = 0;
  } else if (overall == aq::Level::Warning) {
    r = 4, g = 2, b = 0;
  } else {
    r = 4;
  }
  if (alarm && (nowMs / 500) % 2 == 1) r = g = b = 0;

  const uint8_t scale = mode == LedMode::Bright ? 16 : mode == LedMode::Dim ? 3 : 0;
  r *= scale, g *= scale, b *= scale;
  const uint32_t color = (uint32_t{r} << 16) | (uint32_t{g} << 8) | b;
  if (color == shown_) return;
  shown_ = color;
  rgbLedWrite(PIN_NEOPIXEL, r, g, b);
}
