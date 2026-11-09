// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Colours and level styling shared by all screens.
#pragma once

#include <AirQuality.h>

#include <cstdint>

/// @brief The screens' colours, and how each air-quality level is shown: its
/// colour and its word.
namespace theme {

/// @brief A colour as the display takes it: RGB565, 16 bits.
/// @param r Red, 0 to 255.
/// @param g Green, 0 to 255.
/// @param b Blue, 0 to 255.
/// @return The colour, from the top 5, 6 and 5 bits of red, green and blue.
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBackground = rgb(0, 0, 0);  ///< Black, behind everything.
constexpr uint16_t kText = rgb(240, 240, 240);  ///< Main text and values.
constexpr uint16_t kMuted = rgb(150, 150, 150);  ///< Labels, units, hints and other secondary text.
constexpr uint16_t kFaint = rgb(55, 55, 55);  ///< Lines, frames, empty bars; what's inactive.
constexpr uint16_t kAccent = rgb(80, 200, 255);  ///< Titles, the selection, trend lines.
constexpr uint16_t kGood = rgb(60, 205, 110);  ///< aq::Level::Good; also what worked.
constexpr uint16_t kWarning = rgb(255, 176, 0);  ///< aq::Level::Warning; also busy, editing.
constexpr uint16_t kAlarm = rgb(245, 66, 54);  ///< aq::Level::Alarm; also errors.
constexpr uint16_t kUnknown = rgb(110, 110, 110);  ///< aq::Level::Unknown: no level yet.

/// @brief The colour a level is shown in.
/// @param level The level.
inline uint16_t levelColor(aq::Level level) {
  switch (level) {
    case aq::Level::Good:
      return kGood;
    case aq::Level::Warning:
      return kWarning;
    case aq::Level::Alarm:
      return kAlarm;
    default:
      return kUnknown;
  }
}

/// @brief The word a level is shown as: `GOOD`, `FAIR` or `POOR`, and `...`
/// while unknown.
/// @param level The level.
inline const char* levelName(aq::Level level) {
  switch (level) {
    case aq::Level::Good:
      return "GOOD";
    case aq::Level::Warning:
      return "FAIR";
    case aq::Level::Alarm:
      return "POOR";
    default:
      return "...";
  }
}

}  // namespace theme
