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
  bool screenOffWhenIdle = false;  // switch the screen off then, instead of dimming it
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
  // Home Assistant, through an MQTT broker such as its Mosquitto add-on.
  bool mqttEnabled = false;
  char mqttHost[65] = "";  // name or IP address
  uint16_t mqttPort = 1883;
  char mqttUser[65] = "";
  char mqttPassword[65] = "";
};

namespace settings {

Settings load();
void save(const Settings& s);
void erase();
// What the monitor last found out about its battery (see
// power::BatteryWatch), to start from after a restart. Erased with the
// settings. Whether one is connected:
bool loadBatteryPresent();
void saveBatteryPresent(bool present);
// Where "empty" is for it (power::BatteryWatch::emptyPercent()); NaN if not
// learned yet.
float loadBatteryEmpty();
void saveBatteryEmpty(float percent);

}  // namespace settings
