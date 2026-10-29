// SPDX-License-Identifier: GPL-3.0-or-later
//
// Text with a small markup for glyphs the ASCII-only fonts lack:
//   _x  subscript x    (CO_2)
//   ^x  superscript x  (m^3)
//   ~u  micro sign     (~ug/m^3)
//   ~d  degree sign    (~dC)
#pragma once

#include "Lgfx.h"

// Draws `text` in the sprite's current font with its left edge at `x` and its
// baseline at `baseline`. Returns the width in pixels.
int drawRich(LGFX_Sprite& s, const char* text, int x, int baseline, uint16_t color);

// Width `text` would take in the sprite's current font.
int richWidth(LGFX_Sprite& s, const char* text);

// Same, right-aligned at `right`.
inline int drawRichRight(LGFX_Sprite& s, const char* text, int right, int baseline,
                         uint16_t color) {
  return drawRich(s, text, right - richWidth(s, text), baseline, color);
}
