// SPDX-License-Identifier: GPL-3.0-or-later

#include "AirQuality.h"

namespace aq {

const std::array<Metric, kMetricCount> kPriority = {
    Metric::Co2,  Metric::Pm25,     Metric::Hcho,        Metric::Pm10, Metric::Voc,
    Metric::Nox,  Metric::Humidity, Metric::Temperature, Metric::Pm1,  Metric::Pm4,
};

bool Thresholds::any() const {
  return !std::isnan(warnHigh) || !std::isnan(alarmHigh) || !std::isnan(warnLow) ||
         !std::isnan(alarmLow);
}

Level evaluate(float value, const Thresholds& t, Level previous) {
  if (std::isnan(value)) return Level::Unknown;

  // With limits on both sides, hysteresis only applies on the side the value
  // is on; otherwise leaving "too dry" could briefly count as "too humid".
  bool twoSided = !std::isnan(t.warnLow) && !std::isnan(t.warnHigh);
  float middle = twoSided ? (t.warnLow + t.warnHigh) / 2 : NAN;
  bool upperSide = !twoSided || value >= middle;
  bool lowerSide = !twoSided || value < middle;

  auto high = [&](float limit, Level level) {
    return !std::isnan(limit) &&
           (value >= limit || (upperSide && previous >= level && value > limit - t.hysteresis));
  };
  auto low = [&](float limit, Level level) {
    return !std::isnan(limit) &&
           (value <= limit || (lowerSide && previous >= level && value < limit + t.hysteresis));
  };

  if (high(t.alarmHigh, Level::Alarm) || low(t.alarmLow, Level::Alarm)) return Level::Alarm;
  if (high(t.warnHigh, Level::Warning) || low(t.warnLow, Level::Warning)) return Level::Warning;
  return Level::Good;
}

const Thresholds& defaultThresholds(Metric m) {
  static const std::array<Thresholds, kMetricCount> kDefaults = [] {
    std::array<Thresholds, kMetricCount> d{};
    auto high = [&](Metric metric, float warn, float alarm, float hysteresis) {
      Thresholds& t = d[idx(metric)];
      t.warnHigh = warn;
      t.alarmHigh = alarm;
      t.hysteresis = hysteresis;
    };
    // UNE 171330:2024 is Spain's indoor air standard: its "acceptable" value
    // for IDA 2 (offices, classrooms) gives the warning, its maximum the alarm.
    high(Metric::Pm25, 15, 50, 2);     // ug/m3: UNE IDA 2 / maximum
    high(Metric::Pm10, 45, 150, 5);    // ug/m3: EU 24 h limit (= WHO) / WHO interim target
    high(Metric::Co2, 950, 2500, 50);  // ppm: UNE IDA 2 (~450 outdoors + 500) / maximum
    high(Metric::Hcho, 40, 80, 5);     // ppb: UNE (50 ug/m3) / WHO 30 min guideline
    high(Metric::Voc, 150, 250, 10);   // Sensirion VOC index, 100 = typical
    high(Metric::Nox, 20, 150, 5);     // Sensirion NOx index, 1 = typical
    d[idx(Metric::Temperature)] = {26, 30, 18, 17, 0.5f};  // degC: EN 16798-1 homes; UNE 17
    d[idx(Metric::Humidity)] = {60, 70, 30, 20, 2};        // %RH: UNE winter range; UNE 70
    return d;
  }();
  return kDefaults[idx(m)];
}

AirQuality::AirQuality() {
  for (size_t i = 0; i < kMetricCount; ++i) thresholds_[i] = defaultThresholds(Metric(i));
}

void AirQuality::update(const Measurement& m) {
  overall_ = Level::Unknown;
  for (Metric metric : kPriority) {
    size_t i = idx(metric);
    levels_[i] = evaluate(m.values[i], thresholds_[i], levels_[i]);
    if (thresholds_[i].any() && levels_[i] > overall_) {
      overall_ = levels_[i];
      worst_ = metric;
    }
  }
}

}  // namespace aq
