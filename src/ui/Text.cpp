// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Drawing and measuring text with the markup for the glyphs the fonts
/// lack, and drawing it without (see Text.h).

#include "Text.h"

#include <algorithm>

namespace {

const lgfx::IFont* const kSmallFont = &fonts::DejaVu9;  ///< For subscripts and superscripts.

/// @brief Draws or measures `text` with its markup.
///
/// Shared by drawing and measuring, so both always agree.
/// @param[in,out] s The sprite, with the font to use.
/// @param text The text, with the markup (see Text.h).
/// @param x Its left edge, in px.
/// @param baseline Its baseline, in px.
/// @param color Its colour, as RGB565.
/// @param draw Whether to draw it; if not, it's only measured.
/// @return Its width, in px.
int render(LGFX_Sprite& s, const char* text, int x, int baseline, uint16_t color, bool draw) {
  const lgfx::IFont* font = s.getFont();
  const int height = s.fontHeight();
  const int capHeight = height * 5 / 8;  // DejaVu cap height is ~0.63 of the line height
  const int start = x;

  s.setTextDatum(baseline_left);
  s.setTextColor(color);

  char run[64];
  size_t length = 0;
  auto flush = [&] {
    if (length == 0) return;
    run[length] = '\0';
    if (draw) s.drawString(run, x, baseline);
    x += s.textWidth(run);
    length = 0;
  };

  for (const char* p = text; *p != '\0'; ++p) {
    const char c = *p;
    if ((c == '_' || c == '^') && p[1] != '\0') {
      flush();
      const char small[2] = {*++p, '\0'};
      s.setFont(kSmallFont);
      int shift = c == '_' ? std::max(2, height / 7) : -std::max(3, capHeight - 6);
      if (draw) s.drawString(small, x, baseline + shift);
      x += s.textWidth(small) + 1;
      s.setFont(font);
    } else if (c == '~' && p[1] != '\0') {
      flush();
      const char symbol = *++p;
      if (symbol == 'd') {
        const int r = std::max(1, height / 14);
        if (draw) s.drawCircle(x + r + 1, baseline - capHeight + r, r, color);
        x += 2 * r + 3;
      } else if (symbol == 'u') {
        // A "u" with a descender on its left stem.
        const int width = s.textWidth("u");
        if (draw) {
          s.drawString("u", x, baseline);
          const int stem = x + std::max(1, width / 7);
          const int thickness = height >= 24 ? 2 : 1;
          s.fillRect(stem, baseline - 2, thickness, height / 5 + 2, color);
        }
        x += width;
      }
    } else if (length < sizeof run - 1) {
      run[length++] = c;
    }
  }
  flush();
  return x - start;
}

}  // namespace

int drawRich(LGFX_Sprite& s, const char* text, int x, int baseline, uint16_t color) {
  return render(s, text, x, baseline, color, true);
}

int richWidth(LGFX_Sprite& s, const char* text) { return render(s, text, 0, 0, 0, false); }

int drawPlain(LGFX_Sprite& s, const char* text, int x, int baseline, uint16_t color,
              int maxWidth) {
  s.setTextDatum(baseline_left);
  s.setTextColor(color);
  String shown = text;
  if (s.textWidth(shown.c_str()) > maxWidth) {
    while (shown.length() > 0 && s.textWidth((shown + "...").c_str()) > maxWidth) {
      shown.remove(shown.length() - 1);
    }
    shown += "...";
  }
  s.drawString(shown.c_str(), x, baseline);
  return s.textWidth(shown.c_str());
}
