// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Text with a small markup for the glyphs the ASCII-only fonts lack:
/// subscripts, superscripts, and the micro and degree signs.
///
/// Every string the screens draw with drawRich(), drawRichRight() or measure
/// with richWidth() uses this markup. A mark works on the one character after
/// it:
///
/// | Markup | Draws                | Example   | For                      |
/// |--------|----------------------|-----------|--------------------------|
/// | `_x`   | `x` as a subscript   | `CO_2`    | CO2, with the 2 lowered  |
/// | `^x`   | `x` as a superscript | `m^3`     | cubic metres             |
/// | `~u`   | the micro sign       | `~ug/m^3` | micrograms per cubic metre |
/// | `~d`   | the degree sign      | `~dC`     | degrees Celsius          |
///
/// Subscripts and superscripts are a single character, drawn in a smaller
/// font. A `~` followed by any other character draws neither. A mark at the
/// very end of the text is drawn as it is. There's no way to escape a mark, so
/// text people type, such as Wi-Fi network names and passwords, goes through
/// drawPlain() instead, which draws it as it is. At most 63 characters are
/// drawn between two marks; any more are left out.
#pragma once

#include "Lgfx.h"

/// @brief Draws `text`, with its markup, in the sprite's current font.
/// @param[in,out] s The sprite to draw on, with the font to use.
/// @param text The text, with the markup.
/// @param x Its left edge, in px.
/// @param baseline Its baseline, in px.
/// @param color Its colour, as RGB565 (see theme::rgb()).
/// @return The width it took, in px.
int drawRich(LGFX_Sprite& s, const char* text, int x, int baseline, uint16_t color);

/// @brief The width `text`, with its markup, would take in the sprite's
/// current font, in px.
/// @param s The sprite, with the font to use.
/// @param text The text, with the markup.
int richWidth(LGFX_Sprite& s, const char* text);

/// @brief Draws `text` like drawRich(), but right-aligned at `right`.
/// @param[in,out] s The sprite to draw on, with the font to use.
/// @param text The text, with the markup.
/// @param right Its right edge, in px.
/// @param baseline Its baseline, in px.
/// @param color Its colour, as RGB565 (see theme::rgb()).
/// @return The width it took, in px.
inline int drawRichRight(LGFX_Sprite& s, const char* text, int right, int baseline,
                         uint16_t color) {
  return drawRich(s, text, right - richWidth(s, text), baseline, color);
}

/// @brief Draws `text` as it is, without the markup: for names people type,
/// such as Wi-Fi networks.
///
/// Longer than `maxWidth`, it's cut short with "...".
/// @param[in,out] s The sprite to draw on, with the font to use.
/// @param text The text, drawn as it is.
/// @param x Its left edge, in px.
/// @param baseline Its baseline, in px.
/// @param color Its colour, as RGB565 (see theme::rgb()).
/// @param maxWidth The most it may take, in px, "..." included.
/// @return The width it took, in px.
int drawPlain(LGFX_Sprite& s, const char* text, int x, int baseline, uint16_t color,
              int maxWidth = 240);
