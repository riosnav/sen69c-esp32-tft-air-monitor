// SPDX-License-Identifier: GPL-3.0-or-later
//
// Home Assistant over MQTT: announces the monitor through MQTT discovery (one
// device with a sensor per value, a Good/Fair/Poor sensor per value with
// limits, the overall air quality, the battery if one is installed, and a
// Silence select) and publishes the readings every 30 s. Topics, with <id> the
// host name, e.g. airmonitor-a1b2:
//
//   homeassistant/device/<id>/config   discovery, retained
//   <id>/status                        online / offline (last will), retained
//   <id>/state                         {"co2": 612, "pm25": 3.1, ..., "levels": {"co2": "Good", ...},
//                                       "air": "Good", "rssi": -60, "battery": 78, "silence": "Off"}
//   <id>/silence/set                   Off / Warnings / All, from Home Assistant
//
// ESP-IDF's MQTT client runs in its own task and reconnects by itself; the
// rest runs in the main loop, and the web server reaches it under AppLock.
#pragma once

#include <AirQuality.h>
#include <BatteryWatch.h>
#include <mqtt_client.h>

#include <atomic>

#include "Settings.h"
#include "net/WifiService.h"
#include "sensor/SensorService.h"

enum class MqttState : uint8_t {
  Off,         // turned off, or no broker set
  Waiting,     // for Wi-Fi
  Connecting,
  Connected,
  Failed,      // see MqttService::error(); keeps trying
};

class MqttService {
 public:
  void begin(const Settings& settings, const WifiService& wifi,
             const power::BatteryWatch& battery);
  // Starts, restarts (when the settings change) or stops the client, and
  // publishes.
  void loop(const SensorSnapshot& snapshot, const aq::AirQuality& air, uint32_t nowMs);

  MqttState state() const;
  // Why connecting failed; "" otherwise.
  const char* error() const;
  // Says "offline" and disconnects, before the monitor switches off.
  void shutDown();
  // A Silence chosen in Home Assistant since the last call, if any.
  bool takeSilence(Silence& silence);

 private:
  enum class Error : uint8_t { None, Unreachable, Refused, BadLogin };
  struct Broker {
    char host[sizeof Settings::mqttHost] = "";
    uint16_t port = 0;
    char user[sizeof Settings::mqttUser] = "";
    char password[sizeof Settings::mqttPassword] = "";
  };

  static void onEvent(void* self, esp_event_base_t base, int32_t id, void* data);
  bool wanted() const;
  bool brokerChanged() const;
  void start();
  void stop();
  void announce();
  void publishState(const SensorSnapshot& snapshot, const aq::AirQuality& air);
  void logChanges();

  const Settings* settings_ = nullptr;
  const WifiService* wifi_ = nullptr;
  const power::BatteryWatch* battery_ = nullptr;
  char stateTopic_[48] = "";
  char statusTopic_[48] = "";
  char configTopic_[80] = "";
  char silenceTopic_[48] = "";

  esp_mqtt_client_handle_t client_ = nullptr;
  Broker broker_;  // what client_ was started with
  char announcedName_[sizeof Settings::name] = "";
  bool announcedBattery_ = false;
  Silence publishedSilence_ = Silence::Off;
  uint32_t nextStartMs_ = 0;
  uint32_t lastStateMs_ = 0;
  MqttState loggedState_ = MqttState::Off;

  // Set by the client's task.
  std::atomic<bool> connected_{false};
  std::atomic<bool> justConnected_{false};
  std::atomic<Error> error_{Error::None};
  std::atomic<int> requestedSilence_{-1};  // a Silence, or -1
};
