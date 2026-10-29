// SPDX-License-Identifier: GPL-3.0-or-later
//
// SEN69C air-quality monitor: wires the sensor task, the air-quality logic,
// the history and the UI together. Works fully offline.

#include <AirQuality.h>
#include <Arduino.h>
#include <History.h>
#include <esp_system.h>

#include <new>

#include "Settings.h"
#include "sensor/SensorService.h"
#include "ui/Ui.h"

namespace {

constexpr uint32_t kLogIntervalMs = 10000;

Settings userSettings;
SensorService sensor;
aq::AirQuality airQuality;
aq::MetricHistory* history = nullptr;
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

void logMeasurement(const SensorSnapshot& s) {
  using aq::Metric;
  static const char* const kLevels[] = {"unknown", "good", "warning", "alarm"};
  const aq::Measurement& m = s.measurement;
  Serial.printf(
      "PM1 %.1f PM2.5 %.1f PM4 %.1f PM10 %.1f | CO2 %.0f HCHO %.1f VOC %.0f NOx %.0f | "
      "T %.2f RH %.1f | air %s | free RAM %u KB, PSRAM %u KB\n",
      m[Metric::Pm1], m[Metric::Pm25], m[Metric::Pm4], m[Metric::Pm10], m[Metric::Co2],
      m[Metric::Hcho], m[Metric::Voc], m[Metric::Nox], m[Metric::Temperature],
      m[Metric::Humidity], kLevels[static_cast<int>(airQuality.overall())],
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
  sensor.begin(
      {userSettings.tempOffsetC, userSettings.altitudeM, userSettings.co2SelfCalibration});
  ui.begin(sensor, userSettings, airQuality, *history);
}

void loop() {
  static SensorSnapshot snapshot;
  static uint32_t lastSequence = 0;
  static uint32_t lastLogMs = 0;
  const uint32_t now = millis();

  sensor.snapshot(snapshot);
  if (snapshot.sequence != lastSequence) {
    lastSequence = snapshot.sequence;
    airQuality.update(snapshot.measurement);
    history->update(&snapshot.measurement, now);
  } else {
    history->update(nullptr, now);
  }
  ui.update(snapshot, now);

  if (now - lastLogMs >= kLogIntervalMs) {
    lastLogMs = now;
    logMeasurement(snapshot);
  }
  delay(5);
}
