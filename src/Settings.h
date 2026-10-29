// SPDX-License-Identifier: GPL-3.0-or-later
//
// User settings, persisted in NVS.
#pragma once

#include <cstdint>

enum class LedMode : uint8_t { Off, Dim, Bright };

struct Settings {
  uint8_t brightness = 60;  // backlight, %
  uint16_t dimAfterS = 120;  // dim the screen after this long without input; 0 = never
  bool fahrenheit = false;
  LedMode led = LedMode::Dim;
  float tempOffsetC = 0;  // added to the measured temperature (corrects self-heating)
  uint16_t altitudeM = 0;  // for the CO2 sensor's pressure compensation
  bool co2SelfCalibration = true;
};

namespace settings {

Settings load();
void save(const Settings& s);
void erase();

}  // namespace settings
