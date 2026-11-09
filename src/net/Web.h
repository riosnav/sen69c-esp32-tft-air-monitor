// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Web server (ESP-IDF `esp_http_server`): the dashboard, the Wi-Fi
/// setup page and a small JSON API.
///
/// Handlers run in the server's task and take AppLock to reach the main loop's
/// state.
///
/// ```unparsed
///  GET  /              dashboard; the setup page when reached through the setup hotspot
///  GET  /setup         setup page
///  GET  /api/state     readings, levels and status
///  GET  /api/history   ?metric=co2&range=hour|day|week (10 s, 1 min, 5 min means)
///  GET  /api/history.csv  every value's 1 min means for the 7 days; the first
///                      column says how many seconds ago each minute started
///  GET  /api/settings  POST changes them *
///  POST /api/action    clean fan, heat the humidity sensor, calibrate CO2, reset
///                      the CO2 calibration, restart VOC learning, clear sensor
///                      errors, restart *
///  GET  /api/wifi      POST: scan, connect, save, forget, address, finish *
///  GET  /api/mqtt      Home Assistant broker settings and state; POST changes them *
///  GET  /api/auth      204 if the request carries the web password
///  GET  /api/ota       state of an update: confirming on the monitor, accepted, ...
///  POST /api/ota       firmware image as the body; installed once confirmed on the monitor *
/// ```
///
/// Those marked `*` need the web password (HTTP basic auth, any user name),
/// except through the setup hotspot: joining it already took the password on
/// the screen. After 3 wrong passwords in a row, passwords are refused (429)
/// for a minute, doubling with each further miss, up to 15 minutes.
#pragma once

#include <AirQuality.h>
#include <BatteryWatch.h>
#include <History.h>

#include "Settings.h"
#include "net/MqttService.h"
#include "net/Ota.h"
#include "net/WifiService.h"
#include "sensor/SensorService.h"

class Ui;

/// The web server: the dashboard, the Wi-Fi setup page and the JSON API.
namespace web {

/// @brief What the handlers reach.
///
/// The main loop owns all of it; handlers take AppLock to use it, except for
/// the sensor's commands and the firmware update, which are safe from any task.
struct Context {
  const SensorSnapshot* snapshot;      ///< The main loop's latest.
  const aq::AirQuality* airQuality;    ///< The air-quality levels.
  const aq::MetricHistory* history;    ///< The readings' history.
  Settings* settings;                  ///< The settings, which the API changes.
  SensorService* sensor;               ///< For the sensor's commands.
  WifiService* wifi;                   ///< Wi-Fi and its setup hotspot.
  Ota* ota;                            ///< Firmware updates.
  const MqttService* mqtt;             ///< For the Home Assistant connection's state.
  const power::BatteryWatch* battery;  ///< The battery's state.
  Ui* ui;                              ///< Told when the settings change.
};

/// @brief Starts the server; call once the network stack is up.
/// @param context What the handlers reach; copied.
/// @return Whether it's running, or already was.
bool begin(const Context& context);
/// Whether the server is running.
bool started();
/// Wrong web passwords since start.
uint32_t failedLogins();

}  // namespace web
