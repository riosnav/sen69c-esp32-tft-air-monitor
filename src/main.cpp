// SPDX-License-Identifier: GPL-3.0-or-later
//
// SEN69C air-quality monitor: wires the sensor task, the air-quality logic,
// the history, the UI, Wi-Fi, the web server and Home Assistant (MQTT)
// together. Everything but the network parts works offline.

#include <AirQuality.h>
#include <Arduino.h>
#include <History.h>
#include <esp_system.h>

#include <new>

#include "AppLock.h"
#include "Settings.h"
#include "System.h"
#include "net/MqttService.h"
#include "net/Ota.h"
#include "net/Web.h"
#include "net/WifiService.h"
#include "sensor/SensorService.h"
#include "ui/Ui.h"

namespace {

constexpr uint32_t kLogIntervalMs = 10000;

// Owned by the main loop; the web server reaches them under AppLock.
Settings userSettings;
SensorService sensor;
SensorSnapshot latest;
aq::AirQuality airQuality;
aq::MetricHistory* history = nullptr;
WifiService wifi;
Ota ota;
MqttService mqtt;
Ui ui;

// The history is ~72 KB: keep it in PSRAM and leave internal RAM for Wi-Fi.
aq::MetricHistory* createHistory() {
  void* memory = ps_malloc(sizeof(aq::MetricHistory));
  if (memory == nullptr) {
    Serial.println("No PSRAM; history goes to internal RAM");
    memory = malloc(sizeof(aq::MetricHistory));
  }
  return new (memory) aq::MetricHistory();
}

const char* mqttText(MqttState state) {
  switch (state) {
    case MqttState::Waiting:
      return "waiting";
    case MqttState::Connecting:
      return "connecting";
    case MqttState::Connected:
      return "connected";
    case MqttState::Failed:
      return "failing";
    default:
      return "off";
  }
}

const char* wifiText(WifiState state) {
  switch (state) {
    case WifiState::Searching:
      return "searching";
    case WifiState::Connecting:
      return "connecting";
    case WifiState::Connected:
      return "connected";
    default:
      return "off";
  }
}

void logMeasurement(const SensorSnapshot& s) {
  using aq::Metric;
  static const char* const kLevels[] = {"unknown", "good", "warning", "alarm"};
  const aq::Measurement& m = s.measurement;
  Serial.printf(
      "PM1 %.1f PM2.5 %.1f PM4 %.1f PM10 %.1f | CO2 %.0f HCHO %.1f VOC %.0f NOx %.0f | "
      "T %.2f RH %.1f | air %s | wifi %s %d dBm | mqtt %s | free RAM %u KB, PSRAM %u KB\n",
      m[Metric::Pm1], m[Metric::Pm25], m[Metric::Pm4], m[Metric::Pm10], m[Metric::Co2],
      m[Metric::Hcho], m[Metric::Voc], m[Metric::Nox], m[Metric::Temperature],
      m[Metric::Humidity], kLevels[static_cast<int>(airQuality.overall())],
      wifiText(wifi.state()), wifi.rssi(), mqttText(mqtt.state()),
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, ESP.getFreePsram() / 1024);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // Give a serial monitor a moment to attach, but never block standalone use.
  for (uint32_t start = millis(); !Serial && millis() - start < 2000;) delay(10);
  Serial.printf("\nSEN69C Air Monitor %s\n", FW_VERSION);
  Serial.printf("%s rev %u, flash %u KB, PSRAM %u KB, reset reason %d\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getFlashChipSize() / 1024, ESP.getPsramSize() / 1024,
                static_cast<int>(esp_reset_reason()));

  userSettings = settings::load();
  history = createHistory();
  ota.begin();
  sensor.begin(
      {userSettings.tempOffsetC, userSettings.altitudeM, userSettings.co2SelfCalibration});
  wifi.begin(userSettings);
  mqtt.begin(userSettings, wifi);
  ui.begin(sensor, userSettings, airQuality, *history, wifi, ota, mqtt);
}

void loop() {
  static uint32_t lastSequence = 0;
  static uint32_t lastLogMs = 0;
  const uint32_t now = millis();
  {
    AppLock lock;
    sensor.snapshot(latest);
    if (latest.sequence != lastSequence) {
      lastSequence = latest.sequence;
      airQuality.update(latest.measurement);
      history->update(&latest.measurement, now);
    } else {
      history->update(nullptr, now);
    }
    wifi.loop(now);
    if (!web::started() && wifi.networkStarted()) {
      web::begin(
          {&latest, &airQuality, history, &userSettings, &sensor, &wifi, &ota, &mqtt, &ui});
    }
    const bool offline = !userSettings.wifiEnabled || wifi.saved().size() == 0;
    ota.loop(wifi.state() == WifiState::Connected, offline, now);
    mqtt.loop(latest, airQuality, now);
    ui.update(latest, now);

    if (now - lastLogMs >= kLogIntervalMs) {
      lastLogMs = now;
      logMeasurement(latest);
    }
  }
  sys::loop(now);
  delay(5);
}
