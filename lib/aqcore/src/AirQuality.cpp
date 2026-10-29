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
    high(Metric::Pm25, 15, 35, 2);     // ug/m3: WHO 24 h guideline / US EPA 24 h standard
    high(Metric::Pm10, 45, 150, 5);    // ug/m3: WHO 24 h guideline / US EPA 24 h standard
    high(Metric::Co2, 1000, 1500, 50);  // ppm: ventilation guidance
    high(Metric::Hcho, 40, 80, 5);     // ppb: ~WELL limit / WHO 30 min guideline
    high(Metric::Voc, 150, 250, 10);   // Sensirion VOC index, 100 = typical
    high(Metric::Nox, 20, 150, 5);     // Sensirion NOx index, 1 = typical
    d[idx(Metric::Temperature)] = {26, 30, 18, 16, 0.5f};  // degC comfort range
    d[idx(Metric::Humidity)] = {60, 70, 30, 20, 2};        // %RH; above 70 mould risk
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
