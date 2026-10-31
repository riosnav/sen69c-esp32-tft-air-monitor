// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulated device: everything the firmware's UI reads from the rest of
// the firmware (sensor, settings, Wi-Fi, Home Assistant, updates), plus the
// clock and the buttons. Scripts change it; Fakes.cpp shows it to the UI
// through the firmware's own classes.
#pragma once

#include <string>
#include <vector>

#include "Settings.h"
#include "net/MqttService.h"
#include "net/Ota.h"
#include "net/WifiService.h"
#include "sensor/SensorService.h"

struct World {
  // A device that has been running for a day and a bit, in good air, on a
  // home network.
  World();

  // Sets every reading: "good", "fair", "poor" or "warm-up" (just started:
  // only some values yet). Returns false for an unknown name.
  bool setAir(const std::string& preset);
  // A maintenance operation: the sensor is busy with `activity` for
  // `durationMs`, then `result` shows as a message.
  void startOperation(const char* activity, uint32_t durationMs, const char* result);
  // A message from the sensor task, e.g. "Fan cleaned".
  void showResult(const char* text);

  uint32_t nowMs;           // millis()
  bool buttonDown[3] = {};  // D0, D1, D2
  bool restartRequested = false;

  Settings settings;
  SensorSnapshot sensor;
  std::string activity;  // sensor.activity points here
  uint32_t busyUntilMs = 0;
  std::string busyResult;

  struct {
    WifiState state = WifiState::Connected;
    std::string ssid = "HomeNetwork";
    int8_t rssi = -58;
    IPAddress ip{192, 168, 1, 42};
    bool fixedAddress = false;  // set on the web page rather than by DHCP
    std::vector<std::string> saved{"HomeNetwork"};
    // A saved network that was in range but couldn't be joined, and why.
    std::string failedSsid;
    std::string failedReason;
    SetupState setup = SetupState::Off;
    std::string hotspotSsid = "AirMonitor-A1B2";
    std::string hotspotPassword = "mq7ve2kx";
    IPAddress hotspotIp{192, 168, 4, 1};
    std::string trialSsid = "HomeNetwork";  // the network the phone chose
    std::string setupMessage = "Wrong password?";
  } wifi;

  struct {
    MqttState state = MqttState::Off;
    std::string error = "Can't reach the broker";
  } mqtt;

  struct {
    OtaState state = OtaState::Idle;
    uint8_t percent = 0;     // while receiving
    uint32_t deadlineMs = 0;  // while confirming
  } ota;

  uint32_t failedLogins = 0;  // wrong web passwords

  std::string hostname = "airmonitor-a1b2";
  uint32_t historyMinutes = 24 * 60;  // how far back the trend pages reach
};

extern World world;
