// SPDX-License-Identifier: GPL-3.0-or-later
//
// Colours and level styling shared by all screens.
#pragma once

#include <AirQuality.h>

#include <cstdint>

namespace theme {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBackground = rgb(0, 0, 0);
constexpr uint16_t kText = rgb(240, 240, 240);
constexpr uint16_t kMuted = rgb(150, 150, 150);
constexpr uint16_t kFaint = rgb(55, 55, 55);
constexpr uint16_t kAccent = rgb(80, 200, 255);
constexpr uint16_t kGood = rgb(60, 205, 110);
constexpr uint16_t kWarning = rgb(255, 176, 0);
constexpr uint16_t kAlarm = rgb(245, 66, 54);
constexpr uint16_t kUnknown = rgb(110, 110, 110);

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
