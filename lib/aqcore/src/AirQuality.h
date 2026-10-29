// SPDX-License-Identifier: GPL-3.0-or-later
//
// Health levels per metric (good / warning / alarm) with hysteresis, and the
// overall air quality.
#pragma once

#include "Metrics.h"

namespace aq {

// Ordered by severity, so levels compare with < and >.
enum class Level : uint8_t { Unknown, Good, Warning, Alarm };

// Warning and alarm limits for one metric. A NaN limit disables that check.
struct Thresholds {
  float warnHigh = NAN;
  float alarmHigh = NAN;
  float warnLow = NAN;
  float alarmLow = NAN;
  // A level is only left once the value is this far back inside the limit,
  // so readings hovering around a limit don't flicker between levels.
  float hysteresis = 0;

  bool any() const;
};

// Level of `value`, given the level it had before (for hysteresis).
Level evaluate(float value, const Thresholds& t, Level previous);

// Starting defaults; see the table in the README for their sources.
const Thresholds& defaultThresholds(Metric m);

// Metrics in the order they win ties: when two metrics are equally bad, the
// earlier one is reported.
extern const std::array<Metric, kMetricCount> kPriority;

// Tracks the level of every metric and the overall air quality.
class AirQuality {
 public:
  AirQuality();

  void update(const Measurement& m);
  void setThresholds(Metric m, const Thresholds& t) { thresholds_[idx(m)] = t; }
  const Thresholds& thresholds(Metric m) const { return thresholds_[idx(m)]; }

  Level level(Metric m) const { return levels_[idx(m)]; }
  // Worst level among metrics that have limits; Unknown until one is valid.
  Level overall() const { return overall_; }
  // Metric responsible for overall(); meaningful when overall() >= Warning.
  Metric worst() const { return worst_; }

 private:
  std::array<Thresholds, kMetricCount> thresholds_;
  std::array<Level, kMetricCount> levels_{};
  Level overall_ = Level::Unknown;
  Metric worst_ = Metric::Co2;
};

}  // namespace aq
