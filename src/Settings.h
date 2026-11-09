// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief User settings, persisted in NVS.
#pragma once

#include <cstdint>

/// How bright the LED (the NeoPixel) shows the air quality.
enum class LedMode : uint8_t { Off, Dim, Bright };
/// @brief Quiet, e.g. for the night: which alerts no longer turn the screen on.
///
/// Off: every alert does. Warnings: only alarms do (battery alerts count as
/// warnings). All: none does. While it's not Off, the screen also switches off
/// when idle instead of dimming, and the LED stays off but for an alarm that
/// may still turn the screen on.
enum class Silence : uint8_t { Off, Warnings, All };

/// Choices for Settings::dimAfterS, in s.
constexpr uint16_t kDimOptionsS[] = {0, 30, 60, 120, 300, 900};

/// The user's settings; settings::load() and settings::save() keep them in NVS.
struct Settings {
  uint8_t brightness = 60;  ///< Backlight, in %.
  /// @brief Dim the screen after this long without input, in s; 0 = never.
  ///
  /// While silenced (see Silence), the screen switches off instead, after
  /// 2 min if this is 0.
  uint16_t dimAfterS = 120;
  bool screenOffWhenIdle = false;  ///< Switch the screen off then, instead of dimming it.
  bool fahrenheit = false;  ///< Show temperatures in degF.
  LedMode led = LedMode::Dim;  ///< How bright the LED shows the air quality.
  Silence silence = Silence::Off;  ///< Which alerts no longer turn the screen on.
  /// Added to the measured temperature, in degC (corrects self-heating).
  float tempOffsetC = 0;
  uint16_t altitudeM = 0;  ///< In m, for the CO2 sensor's pressure compensation.
  bool co2SelfCalibration = true;  ///< The CO2 sensor's automatic self-calibration.
  bool wifiEnabled = true;  ///< Whether Wi-Fi is on.
  char name[32] = "Air Monitor";  ///< Shown on the web page and in Home Assistant.
  /// @brief Protects changes made from the web page; empty = no password.
  ///
  /// A random one is generated on first start and shown on the device's Wi-Fi
  /// page.
  char webPassword[33] = "";
  /// Home Assistant, through an MQTT broker such as its Mosquitto add-on.
  bool mqttEnabled = false;
  char mqttHost[65] = "";  ///< The broker's name or IP address.
  uint16_t mqttPort = 1883;  ///< The broker's port.
  char mqttUser[65] = "";  ///< The user name for the broker; empty = none.
  char mqttPassword[65] = "";  ///< The password for the broker; empty = none.
};

/// Keeping the Settings, and what the monitor found out about its battery, in NVS.
namespace settings {

/// @brief Loads the settings; defaults for any never saved.
///
/// On first start, and after erase(), makes up a random web password and
/// saves the settings with it.
/// @return The settings.
Settings load();
/// @brief Saves the settings.
/// @param s The settings.
void save(const Settings& s);
/// Erases the saved settings, the battery's included (part of a factory reset).
void erase();
/// @brief Whether a battery is connected, as the monitor last found out (see
/// power::BatteryWatch::present()), to start from after a restart.
///
/// Erased with the settings; false if never saved.
bool loadBatteryPresent();
/// @brief Saves whether a battery is connected, for loadBatteryPresent().
/// @param present Whether one is.
void saveBatteryPresent(bool present);
/// @brief Where "empty" is for the battery, as the monitor last learned it
/// (power::BatteryWatch::emptyPercent()), to start from after a restart.
///
/// Erased with the settings.
/// @return The battery monitor's reading at "empty", in %; 0 if not learned,
///   NaN if never saved.
float loadBatteryEmpty();
/// @brief Saves where "empty" is for the battery, for loadBatteryEmpty().
/// @param percent The battery monitor's reading at "empty", in %.
void saveBatteryEmpty(float percent);

}  // namespace settings
