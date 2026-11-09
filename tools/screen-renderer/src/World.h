// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The simulated device: everything the firmware's UI reads from the
/// rest of the firmware (sensor, settings, Wi-Fi, Home Assistant, updates),
/// plus the clock and the buttons.
///
/// Scripts change it; Fakes.cpp shows it to the UI through the firmware's own
/// classes, and Platform.cpp through the Arduino functions.
#pragma once

#include <string>
#include <vector>

#include "Settings.h"
#include "net/MqttService.h"
#include "net/Ota.h"
#include "net/WifiService.h"
#include "sensor/SensorService.h"

/// @brief The simulated device's state: what the rest of the firmware would
/// tell the UI, the clock and the buttons.
///
/// Scripts change it; Fakes.cpp and Platform.cpp show it to the UI.
struct World {
  /// A device that has been running for 30 hours, in good air, on a home
  /// network, with Home Assistant off.
  World();

  /// @brief Sets every reading to a preset, and the sensor to measuring.
  /// @param preset "good", "fair", "poor" or "warm-up" (measuring for 8 s:
  ///   no gas readings yet).
  /// @return Whether there's a preset by that name.
  bool setAir(const std::string& preset);
  /// @brief Starts a maintenance operation: the sensor is busy with
  /// `activity` for `durationMs`, then `result` shows as a message.
  /// @param activity What the sensor is doing, e.g. "Cleaning fan".
  /// @param durationMs How long it takes, in ms.
  /// @param result The message afterwards, e.g. "Fan cleaned".
  void startOperation(const char* activity, uint32_t durationMs, const char* result);
  /// @brief Shows a message from the sensor task.
  /// @param text The message, e.g. "Fan cleaned".
  void showResult(const char* text);

  uint32_t nowMs;                 ///< The clock, as millis().
  bool buttonDown[3] = {};        ///< Whether D0, D1 and D2 are pressed.
  bool restartRequested = false;  ///< ESP.restart() was called (see EspClass::restart()).

  Settings settings;              ///< The settings, which the UI changes in place.
  SensorSnapshot sensor;          ///< The sensor task's latest snapshot.
  bool batteryConnected = false;  ///< Whether the battery readings come from one.
  std::string activity;           ///< What the sensor is busy with; sensor.activity points here.
  uint32_t busyUntilMs = 0;       ///< When the sensor's work ends, as millis(); 0 for never.
  std::string busyResult;         ///< The message it ends with.

  struct {
    WifiState state = WifiState::Connected;  ///< The connection.
    std::string ssid = "HomeNetwork";        ///< The network it joins or has joined.
    int8_t rssi = -58;                       ///< Signal strength, in dBm.
    IPAddress ip{192, 168, 1, 42};           ///< The monitor's address on the network.
    bool fixedAddress = false;               ///< Set on the web page rather than by DHCP.
    std::vector<std::string> saved{"HomeNetwork"};  ///< The saved networks.
    /// A saved network that was in range but couldn't be joined; "" if none.
    std::string failedSsid;
    std::string failedReason;                      ///< Why, e.g. "Wrong password?".
    SetupState setup = SetupState::Off;            ///< The Wi-Fi setup's stage; Off if not running.
    std::string hotspotSsid = "AirMonitor-A1B2";   ///< The setup hotspot's name.
    std::string hotspotPassword = "mq7ve2kx";      ///< Its password.
    IPAddress hotspotIp{192, 168, 4, 1};           ///< The monitor's address on it.
    std::string trialSsid = "HomeNetwork";         ///< The network the phone chose.
    std::string setupMessage = "Wrong password?";  ///< Why the setup failed (SetupState::Failed).
  } wifi;  ///< The Wi-Fi, as WifiService would report it.

  struct {
    MqttState state = MqttState::Off;              ///< The connection to the MQTT broker.
    std::string error = "Can't reach the broker";  ///< Why it failed (MqttState::Failed).
  } mqtt;  ///< Home Assistant, as MqttService would report it.

  struct {
    OtaState state = OtaState::Idle;  ///< Where an update is.
    uint8_t percent = 0;              ///< How much has arrived, in %, while receiving.
    uint32_t deadlineMs = 0;          ///< While confirming: when time's up, as millis().
  } ota;  ///< Firmware updates, as Ota would report them.

  uint32_t failedLogins = 0;  ///< Wrong web passwords so far.

  /// The host name; the part after the dash is the short id.
  std::string hostname = "airmonitor-a1b2";
  uint32_t historyMinutes = 7 * 24 * 60;  ///< How far back the trend pages reach, in minutes.
};

/// The simulated device.
extern World world;
