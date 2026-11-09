// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Home Assistant over MQTT: announces the monitor through MQTT
/// discovery and publishes the readings every 30 s.
///
/// The announcement makes one device with a sensor per value, a Good/Fair/Poor
/// sensor per value with limits, the overall air quality, the battery if one
/// is installed, and a Silence select. Topics, with `<id>` the host name, e.g.
/// `airmonitor-a1b2`:
///
/// ```unparsed
///  homeassistant/device/<id>/config   discovery, retained
///  <id>/status                        online / offline (last will), retained
///  <id>/state                         {"co2": 612, "pm25": 3.1, ...,
///                                      "levels": {"co2": "Good", ...}, "air": "Good",
///                                      "rssi": -60, "battery": 78, "silence": "Off"}
///  <id>/silence/set                   Off / Warnings / All, from Home Assistant
/// ```
///
/// ESP-IDF's MQTT client runs in its own task and reconnects by itself; the
/// rest runs in the main loop, and the web server reaches it under AppLock.
#pragma once

#include <AirQuality.h>
#include <BatteryWatch.h>
#include <mqtt_client.h>

#include <atomic>

#include "Settings.h"
#include "net/WifiService.h"
#include "sensor/SensorService.h"

/// Where the connection to the broker is.
enum class MqttState : uint8_t {
  Off,         ///< Turned off, or no broker set.
  Waiting,     ///< For Wi-Fi, or for the client to start.
  Connecting,  ///< Connecting to the broker.
  Connected,   ///< Connected to the broker.
  Failed,      ///< See MqttService::error(); keeps trying.
};

/// @brief Home Assistant over MQTT: runs the client, announces the monitor
/// and publishes its readings.
class MqttService {
 public:
  /// @brief Sets up the topics; the client starts in loop().
  /// @param settings The settings, kept: the broker, the monitor's name and
  ///   Silence.
  /// @param wifi The Wi-Fi, kept: the client only starts while it's connected.
  /// @param battery The battery, kept: announced and published while present.
  void begin(const Settings& settings, const WifiService& wifi,
             const power::BatteryWatch& battery);
  /// @brief Starts, restarts (when the settings change) or stops the client,
  /// and publishes.
  ///
  /// Announces the monitor again once connected, and when its name or the
  /// battery changes. Publishes the readings every 30 s, and a new Silence at
  /// once, so Home Assistant's select follows.
  /// @param snapshot The sensor's latest readings.
  /// @param air The air-quality levels.
  /// @param nowMs The time, as millis().
  void loop(const SensorSnapshot& snapshot, const aq::AirQuality& air, uint32_t nowMs);

  /// Where the connection to the broker is.
  MqttState state() const;
  /// Why connecting failed, for people; "" otherwise.
  const char* error() const;
  /// Says "offline" and disconnects, before the monitor switches off.
  void shutDown();
  /// @brief A Silence chosen in Home Assistant since the last call, if any.
  /// @param[out] silence The choice; left as it was if there's none.
  /// @return Whether there was one.
  bool takeSilence(Silence& silence);

 private:
  /// @brief Why connecting failed: none, the broker can't be reached, it
  /// refused the connection, or it refused the user name or password.
  enum class Error : uint8_t { None, Unreachable, Refused, BadLogin };
  /// The broker settings a client was started with.
  struct Broker {
    char host[sizeof Settings::mqttHost] = "";
    uint16_t port = 0;
    char user[sizeof Settings::mqttUser] = "";
    char password[sizeof Settings::mqttPassword] = "";
  };

  /// @brief The client's events: connected, a message on the Silence topic,
  /// disconnected, and errors. Runs in the client's task.
  /// @param self The MqttService.
  /// @param base The event base; not used.
  /// @param id The event, an `esp_mqtt_event_id_t`.
  /// @param data The event's data, an `esp_mqtt_event_t`.
  static void onEvent(void* self, esp_event_base_t base, int32_t id, void* data);
  /// Whether MQTT is on in the settings and a broker is set.
  bool wanted() const;
  /// Whether the broker settings differ from those the client started with.
  bool brokerChanged() const;
  /// Starts the client with the broker settings; it connects by itself.
  void start();
  /// Stops the client, saying "offline" first if connected.
  void stop();
  /// @brief Sends the discovery message and "online", queued for the
  /// client's task.
  void announce();
  /// @brief Publishes the readings, levels, Wi-Fi signal, battery and Silence
  /// on the state topic.
  /// @param snapshot The sensor's latest readings.
  /// @param air The air-quality levels.
  void publishState(const SensorSnapshot& snapshot, const aq::AirQuality& air);
  /// Logs when the client connects or fails, once each time.
  void logChanges();

  const Settings* settings_ = nullptr;
  const WifiService* wifi_ = nullptr;
  const power::BatteryWatch* battery_ = nullptr;
  char stateTopic_[48] = "";    ///< `<id>/state`.
  char statusTopic_[48] = "";   ///< `<id>/status`.
  char configTopic_[80] = "";   ///< `homeassistant/device/<id>/config`.
  char silenceTopic_[48] = "";  ///< `<id>/silence/set`.

  esp_mqtt_client_handle_t client_ = nullptr;
  Broker broker_;  ///< What client_ was started with.
  char announcedName_[sizeof Settings::name] = "";  ///< Empty: announce again.
  bool announcedBattery_ = false;  ///< Whether the announcement had the battery.
  Silence publishedSilence_ = Silence::Off;
  uint32_t nextStartMs_ = 0;  ///< When the client may start again, as millis().
  uint32_t lastStateMs_ = 0;  ///< When the readings were last published, as millis().
  MqttState loggedState_ = MqttState::Off;  ///< What logChanges() last saw.

  // Set by the client's task.
  std::atomic<bool> connected_{false};
  std::atomic<bool> justConnected_{false};  ///< Connected since loop() last looked.
  std::atomic<Error> error_{Error::None};
  std::atomic<int> requestedSilence_{-1};  ///< A Silence, or -1.
};
