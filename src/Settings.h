// SPDX-License-Identifier: GPL-3.0-or-later
//
// User settings, persisted in NVS.
#pragma once

#include <cstdint>

enum class LedMode : uint8_t { Off, Dim, Bright };

// Choices for Settings::dimAfterS.
constexpr uint16_t kDimOptionsS[] = {0, 30, 60, 120, 300, 900};

struct Settings {
  uint8_t brightness = 60;  // backlight, %
  uint16_t dimAfterS = 120;  // dim the screen after this long without input; 0 = never
  bool fahrenheit = false;
  LedMode led = LedMode::Dim;
  float tempOffsetC = 0;  // added to the measured temperature (corrects self-heating)
  uint16_t altitudeM = 0;  // for the CO2 sensor's pressure compensation
  bool co2SelfCalibration = true;
  bool wifiEnabled = true;
  char name[32] = "Air Monitor";  // shown on the web page
  // Protects changes made from the web page; empty = no password. A random
  // one is generated on first start and shown on the device's Wi-Fi page.
  char webPassword[33] = "";
};

namespace settings {

Settings load();
void save(const Settings& s);
void erase();

}  // namespace settings
