// SPDX-License-Identifier: GPL-3.0-or-later

#include "World.h"

#include <cmath>

using aq::Metric;

World world;

World::World() : nowMs(30 * 3600 * 1000UL) {
  strlcpy(settings.webPassword, "k4fz8wqe", sizeof settings.webPassword);
  sensor.state = SensorState::Measuring;
  sensor.measuringSinceMs = 5000;
  strlcpy(sensor.product, "SEN69C", sizeof sensor.product);
  strlcpy(sensor.serial, "5E1A90C47B2D83F6", sizeof sensor.serial);
  sensor.firmwareMajor = 9;
  sensor.firmwareMinor = 0;
  sensor.confirmedAltitudeM = 0;
  setAir("good");
}

bool World::setAir(const std::string& preset) {
  // pm1, pm2.5, pm4, pm10, co2, hcho, voc, nox, temperature, humidity
  static const struct {
    const char* name;
    float values[aq::kMetricCount];
  } kPresets[] = {
      {"good", {2.1f, 3.4f, 4.1f, 4.6f, 612, 11.2f, 98, 1, 22.4f, 46}},
      {"fair", {9.8f, 17.6f, 19.9f, 21.3f, 1180, 23.5f, 162, 2, 23.6f, 62}},
      {"poor", {33.8f, 56.4f, 64.1f, 71.6f, 2680, 46.1f, 268, 6, 26.8f, 72}},
      {"warm-up", {2.3f, 3.6f, 4.2f, 4.7f, NAN, NAN, NAN, NAN, 22.1f, 47}},
  };
  for (const auto& p : kPresets) {
    if (preset != p.name) continue;
    for (size_t i = 0; i < aq::kMetricCount; ++i) sensor.measurement.values[i] = p.values[i];
    sensor.state = SensorState::Measuring;
    if (preset == "warm-up") sensor.measuringSinceMs = nowMs - 8000;
    return true;
  }
  return false;
}

void World::startOperation(const char* what, uint32_t durationMs, const char* result) {
  sensor.state = SensorState::Busy;
  activity = what;
  busyUntilMs = nowMs + durationMs;
  busyResult = result;
}

void World::showResult(const char* text) {
  strlcpy(sensor.result, text, sizeof sensor.result);
  ++sensor.resultSequence;
}
