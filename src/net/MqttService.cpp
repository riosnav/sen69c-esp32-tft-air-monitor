// SPDX-License-Identifier: GPL-3.0-or-later

#include "MqttService.h"

#include <Arduino.h>
#include <esp_log.h>

#include <cstring>

#include "Device.h"
#include "net/Json.h"

using aq::Level;
using aq::Metric;

namespace {

constexpr uint32_t kStateIntervalMs = 30000;
// After announcing, Home Assistant needs a moment to create the sensors before
// it listens to their readings.
constexpr uint32_t kFirstStateDelayMs = 2000;
constexpr uint32_t kStartRetryMs = 60000;  // if the client couldn't even be created
// The broker announces "offline" about 1.5 keepalives after losing us.
constexpr int kKeepaliveS = 60;
// Also bounds how long stopping the client can take while it's connecting.
constexpr int kNetworkTimeoutMs = 5000;
constexpr int kOutBufferBytes = 8192;  // holds the ~4.2 KB discovery message
constexpr const char* kDiscoveryPrefix = "homeassistant";

// How Home Assistant shows each metric, in the order of aq::Metric. Units are
// spelled the way Home Assistant spells them (with a Greek mu), or it keeps no
// long-term statistics for them.
struct SensorInfo {
  const char* name;
  const char* deviceClass;  // nullptr where none fits
  const char* unit;         // nullptr for the indices
  const char* icon;         // for those without a device class
  int displayDecimals;
  // Its Good/Fair/Poor sensor; nullptr for the values without limits.
  const char* statusName;
};
constexpr const char* kMicrograms = "\xCE\xBCg/m\xC2\xB3";
constexpr SensorInfo kSensors[aq::kMetricCount] = {
    {"PM1", "pm1", kMicrograms, nullptr, 1, nullptr},
    {"PM2.5", "pm25", kMicrograms, nullptr, 1, "PM2.5 status"},
    // The icon of the other PM sensors.
    {"PM4", nullptr, kMicrograms, "mdi:molecule", 1, nullptr},
    {"PM10", "pm10", kMicrograms, nullptr, 1, "PM10 status"},
    {"CO\xE2\x82\x82", "carbon_dioxide", "ppm", nullptr, 0, "CO\xE2\x82\x82 status"},
    {"Formaldehyde", nullptr, "ppb", "mdi:flask-outline", 1, "Formaldehyde status"},
    {"VOC index", nullptr, nullptr, "mdi:spray", 0, "VOC status"},
    {"NO\xE2\x82\x93 index", nullptr, nullptr, "mdi:smoke", 0, "NO\xE2\x82\x93 status"},
    {"Temperature", "temperature", "\xC2\xB0" "C", nullptr, 1, "Temperature status"},
    {"Humidity", "humidity", "%", nullptr, 0, "Humidity status"},
};

// Air quality levels, named as on the web page.
const char* const kAirLevels[] = {"Good", "Fair", "Poor"};

const char* airText(Level level) {
  switch (level) {
    case Level::Good:
      return kAirLevels[0];
    case Level::Warning:
      return kAirLevels[1];
    case Level::Alarm:
      return kAirLevels[2];
    default:
      return nullptr;
  }
}

// A sensor of the discovery message, reading `path` of the state JSON. Keys
// are abbreviated where Home Assistant allows it, to keep the message small.
cJSON* addSensor(cJSON* components, const char* key, const char* name, const char* path) {
  cJSON* c = cJSON_AddObjectToObject(components, key);
  cJSON_AddStringToObject(c, "p", "sensor");
  cJSON_AddStringToObject(c, "name", name);
  char text[64];
  snprintf(text, sizeof text, "{{ value_json.%s }}", path);
  cJSON_AddStringToObject(c, "val_tpl", text);
  snprintf(text, sizeof text, "%s_%s", device::hostname(), key);
  cJSON_AddStringToObject(c, "uniq_id", text);
  return c;
}

// A Good/Fair/Poor sensor.
cJSON* addLevelSensor(cJSON* components, const char* key, const char* name, const char* path,
                      const char* icon) {
  cJSON* c = addSensor(components, key, name, path);
  cJSON_AddStringToObject(c, "dev_cla", "enum");
  cJSON_AddItemToObject(c, "options", cJSON_CreateStringArray(kAirLevels, 3));
  cJSON_AddStringToObject(c, "icon", icon);
  return c;
}

bool due(uint32_t nowMs, uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }

}  // namespace

void MqttService::begin(const Settings& settings, const WifiService& wifi) {
  settings_ = &settings;
  wifi_ = &wifi;
  snprintf(stateTopic_, sizeof stateTopic_, "%s/state", device::hostname());
  snprintf(statusTopic_, sizeof statusTopic_, "%s/status", device::hostname());
  snprintf(configTopic_, sizeof configTopic_, "%s/device/%s/config", kDiscoveryPrefix,
           device::hostname());
  // The client logs every failed attempt; state() and logChanges() say it once.
  esp_log_level_set("mqtt_client", ESP_LOG_NONE);
  esp_log_level_set("transport_base", ESP_LOG_NONE);
}

void MqttService::loop(const SensorSnapshot& snapshot, const aq::AirQuality& air,
                       uint32_t nowMs) {
  if (client_ != nullptr && (!wanted() || brokerChanged())) stop();
  if (client_ == nullptr && wanted() && wifi_->state() == WifiState::Connected &&
      due(nowMs, nextStartMs_)) {
    nextStartMs_ = nowMs + kStartRetryMs;
    start();
  }
  logChanges();
  if (!connected_) return;

  if (justConnected_.exchange(false) || strcmp(announcedName_, settings_->name) != 0 ||
      announcedBattery_ != settings_->batteryInstalled) {
    announce();
    lastStateMs_ = nowMs - kStateIntervalMs + kFirstStateDelayMs;
  }
  if (nowMs - lastStateMs_ >= kStateIntervalMs) {
    lastStateMs_ = nowMs;
    publishState(snapshot, air);
  }
}

MqttState MqttService::state() const {
  if (!wanted()) return MqttState::Off;
  if (connected_) return MqttState::Connected;
  if (client_ == nullptr || wifi_->state() != WifiState::Connected) return MqttState::Waiting;
  return error_ == Error::None ? MqttState::Connecting : MqttState::Failed;
}

const char* MqttService::error() const {
  if (state() != MqttState::Failed) return "";
  switch (error_.load()) {
    case Error::BadLogin:
      return "Wrong user name or password";
    case Error::Refused:
      return "The broker refused the connection";
    default:
      return "Can't reach the broker";
  }
}

bool MqttService::wanted() const {
  return settings_->mqttEnabled && settings_->mqttHost[0] != '\0';
}

bool MqttService::brokerChanged() const {
  const Settings& s = *settings_;
  return strcmp(s.mqttHost, broker_.host) != 0 || s.mqttPort != broker_.port ||
         strcmp(s.mqttUser, broker_.user) != 0 || strcmp(s.mqttPassword, broker_.password) != 0;
}

void MqttService::start() {
  const Settings& s = *settings_;
  strlcpy(broker_.host, s.mqttHost, sizeof broker_.host);
  broker_.port = s.mqttPort;
  strlcpy(broker_.user, s.mqttUser, sizeof broker_.user);
  strlcpy(broker_.password, s.mqttPassword, sizeof broker_.password);

  // The client copies these strings.
  esp_mqtt_client_config_t config = {};
  config.broker.address.hostname = broker_.host;
  config.broker.address.port = broker_.port;
  config.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
  config.credentials.client_id = device::hostname();
  if (broker_.user[0] != '\0') config.credentials.username = broker_.user;
  if (broker_.password[0] != '\0') config.credentials.authentication.password = broker_.password;
  config.session.keepalive = kKeepaliveS;
  config.session.last_will.topic = statusTopic_;
  config.session.last_will.msg = "offline";
  config.session.last_will.qos = 1;
  config.session.last_will.retain = 1;
  config.network.timeout_ms = kNetworkTimeoutMs;
  config.buffer.out_size = kOutBufferBytes;

  connected_ = false;
  justConnected_ = false;
  error_ = Error::None;
  client_ = esp_mqtt_client_init(&config);
  if (client_ == nullptr) {
    Serial.println("MQTT: could not create the client");
    return;
  }
  esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, onEvent, this);
  if (esp_mqtt_client_start(client_) != ESP_OK) {
    Serial.println("MQTT: could not start the client");
    esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    return;
  }
  Serial.printf("MQTT: connecting to %s:%u\n", broker_.host, broker_.port);
}

void MqttService::stop() {
  // A clean disconnect doesn't trigger the last will, so say it ourselves:
  // Home Assistant then shows the sensors as unavailable rather than stale.
  if (connected_) esp_mqtt_client_publish(client_, statusTopic_, "offline", 0, 1, 1);
  esp_mqtt_client_destroy(client_);  // stops the client's task first
  client_ = nullptr;
  connected_ = false;
  justConnected_ = false;
  error_ = Error::None;
  announcedName_[0] = '\0';
  nextStartMs_ = millis();  // with new settings, start again right away
  Serial.println("MQTT: stopped");
}

void MqttService::announce() {
  Json root(cJSON_CreateObject());
  cJSON* r = root.get();
  cJSON* dev = cJSON_AddObjectToObject(r, "dev");
  cJSON_AddStringToObject(dev, "ids", device::hostname());
  cJSON_AddStringToObject(dev, "name", settings_->name);
  cJSON_AddStringToObject(dev, "mdl", "SEN69C Air Monitor");
  cJSON_AddStringToObject(dev, "sw", FW_VERSION);
  const String url = "http://" + wifi_->ip().toString() + "/";
  cJSON_AddStringToObject(dev, "cu", url.c_str());
  cJSON* origin = cJSON_AddObjectToObject(r, "o");
  cJSON_AddStringToObject(origin, "name", "sen69c-esp32-tft-air-monitor");
  cJSON_AddStringToObject(origin, "sw", FW_VERSION);
  cJSON_AddStringToObject(origin, "url", "https://github.com/riosnav/sen69c-esp32-tft-air-monitor");
  // Shared by all the sensors.
  cJSON_AddStringToObject(r, "state_topic", stateTopic_);
  cJSON_AddStringToObject(r, "availability_topic", statusTopic_);

  cJSON* components = cJSON_AddObjectToObject(r, "cmps");
  for (size_t i = 0; i < aq::kMetricCount; ++i) {
    const SensorInfo& info = kSensors[i];
    const char* id = aq::kMetricIds[i];
    cJSON* c = addSensor(components, id, info.name, id);
    if (info.deviceClass != nullptr) cJSON_AddStringToObject(c, "dev_cla", info.deviceClass);
    if (info.unit != nullptr) cJSON_AddStringToObject(c, "unit_of_meas", info.unit);
    if (info.icon != nullptr) cJSON_AddStringToObject(c, "icon", info.icon);
    cJSON_AddStringToObject(c, "stat_cla", "measurement");
    cJSON_AddNumberToObject(c, "suggested_display_precision", info.displayDecimals);
    if (info.statusName != nullptr) {
      char key[16];
      char path[24];
      snprintf(key, sizeof key, "%s_status", id);
      snprintf(path, sizeof path, "levels.%s", id);
      addLevelSensor(components, key, info.statusName, path, "mdi:traffic-light");
    }
  }
  addLevelSensor(components, "air", "Air quality", "air", "mdi:air-filter");
  cJSON* rssi = addSensor(components, "rssi", "Wi-Fi signal", "rssi");
  cJSON_AddStringToObject(rssi, "dev_cla", "signal_strength");
  cJSON_AddStringToObject(rssi, "unit_of_meas", "dBm");
  cJSON_AddStringToObject(rssi, "stat_cla", "measurement");
  cJSON_AddStringToObject(rssi, "entity_category", "diagnostic");
  if (settings_->batteryInstalled) {
    cJSON* battery = addSensor(components, "battery", "Battery", "battery");
    cJSON_AddStringToObject(battery, "dev_cla", "battery");
    cJSON_AddStringToObject(battery, "unit_of_meas", "%");
    cJSON_AddStringToObject(battery, "stat_cla", "measurement");
    cJSON_AddStringToObject(battery, "entity_category", "diagnostic");
  } else {
    // Removes it, if an earlier announcement had it.
    cJSON_AddStringToObject(cJSON_AddObjectToObject(components, "battery"), "p", "sensor");
  }

  const JsonText text(cJSON_PrintUnformatted(r));
  if (!text) return;
  // Queued for the client's task, so a slow broker can't stall the main loop.
  const int config = esp_mqtt_client_enqueue(client_, configTopic_, text.get(), 0, 1, 1, true);
  const int online = esp_mqtt_client_enqueue(client_, statusTopic_, "online", 0, 1, 1, true);
  Serial.printf("MQTT: announced to Home Assistant (%u bytes)%s\n",
                static_cast<unsigned>(strlen(text.get())),
                config < 0 || online < 0 ? ", but it could not be queued" : "");
  strlcpy(announcedName_, settings_->name, sizeof announcedName_);
  announcedBattery_ = settings_->batteryInstalled;
}

void MqttService::publishState(const SensorSnapshot& snapshot, const aq::AirQuality& air) {
  Json root(cJSON_CreateObject());
  cJSON* r = root.get();
  auto addLevel = [](cJSON* object, const char* key, Level level) {
    if (const char* text = airText(level)) {
      cJSON_AddStringToObject(object, key, text);
    } else {
      cJSON_AddNullToObject(object, key);
    }
  };
  cJSON* levels = cJSON_AddObjectToObject(r, "levels");
  for (size_t i = 0; i < aq::kMetricCount; ++i) {
    const Metric m = static_cast<Metric>(i);
    addNumber(r, aq::kMetricIds[i], snapshot.measurement[m], aq::kMetricDecimals[i]);
    if (kSensors[i].statusName != nullptr) addLevel(levels, aq::kMetricIds[i], air.level(m));
  }
  addLevel(r, "air", air.overall());
  cJSON_AddNumberToObject(r, "rssi", wifi_->rssi());
  if (settings_->batteryInstalled) addNumber(r, "battery", snapshot.batteryPercent, 0);
  const JsonText text(cJSON_PrintUnformatted(r));
  if (text) esp_mqtt_client_enqueue(client_, stateTopic_, text.get(), 0, 0, 0, true);
}

void MqttService::logChanges() {
  const MqttState now = state();
  if (now == loggedState_) return;
  loggedState_ = now;
  if (now == MqttState::Connected) Serial.printf("MQTT: connected to %s\n", broker_.host);
  if (now == MqttState::Failed) Serial.printf("MQTT: %s\n", error());
}

// Runs in the client's task.
void MqttService::onEvent(void* self, esp_event_base_t, int32_t id, void* data) {
  MqttService& mqtt = *static_cast<MqttService*>(self);
  const auto* event = static_cast<const esp_mqtt_event_t*>(data);
  switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED:
      mqtt.error_ = Error::None;
      mqtt.connected_ = true;
      mqtt.justConnected_ = true;
      break;
    case MQTT_EVENT_DISCONNECTED:
      mqtt.connected_ = false;
      break;
    case MQTT_EVENT_ERROR: {
      const esp_mqtt_error_codes_t& e = *event->error_handle;
      if (e.error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        const bool login = e.connect_return_code == MQTT_CONNECTION_REFUSE_BAD_USERNAME ||
                           e.connect_return_code == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED;
        mqtt.error_ = login ? Error::BadLogin : Error::Refused;
      } else if (e.error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        mqtt.error_ = Error::Unreachable;
      }
      break;
    }
    default:
      break;
  }
}
