// SPDX-License-Identifier: GPL-3.0-or-later
//
// Web server (ESP-IDF esp_http_server): the dashboard, the Wi-Fi setup page
// and a small JSON API. Handlers run in the server's task and take AppLock to
// reach the main loop's state.
//
//   GET  /              dashboard; the setup page when reached through the setup hotspot
//   GET  /setup         setup page
//   GET  /api/state     readings, levels and status
//   GET  /api/history   ?metric=co2&range=hour|day|week (10 s, 1 min, 5 min means)
//   GET  /api/history.csv  every value's 1 min means for the 7 days; the first
//                       column says how many seconds ago each minute started
//   GET  /api/settings  POST changes them *
//   POST /api/action    clean fan, heat the humidity sensor, calibrate CO2, reset
//                       the CO2 calibration, clear sensor errors, restart *
//   GET  /api/wifi      POST: scan, connect, save, forget, address, finish *
//   GET  /api/mqtt      Home Assistant broker settings and state; POST changes them *
//   GET  /api/auth      204 if the request carries the web password
//   GET  /api/ota       state of an update: confirming on the monitor, accepted, ...
//   POST /api/ota       firmware image as the body; installed once confirmed on the monitor *
//
// * needs the web password (HTTP basic auth, any user name), except through
//   the setup hotspot: joining it already took the password on the screen.
//   After 3 wrong passwords in a row, passwords are refused (429) for a minute,
//   doubling with each further miss, up to 15 minutes.
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

namespace web {

struct Context {
  const SensorSnapshot* snapshot;  // the main loop's latest
  const aq::AirQuality* airQuality;
  const aq::MetricHistory* history;
  Settings* settings;
  SensorService* sensor;
  WifiService* wifi;
  Ota* ota;
  const MqttService* mqtt;
  const power::BatteryWatch* battery;
  Ui* ui;  // told when the settings change
};

// Starts the server; call once the network stack is up.
bool begin(const Context& context);
bool started();
// Wrong web passwords since start.
uint32_t failedLogins();

}  // namespace web
