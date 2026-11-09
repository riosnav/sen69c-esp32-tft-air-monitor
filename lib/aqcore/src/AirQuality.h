// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Health levels per metric (good / warning / alarm) with hysteresis,
/// and the overall air quality.
#pragma once

#include "Metrics.h"

namespace aq {

/// @brief A metric's health level; Unknown while it has no value.
///
/// Ordered by severity, so levels compare with `<` and `>`.
enum class Level : uint8_t { Unknown, Good, Warning, Alarm };

/// @brief Warning and alarm limits for one metric, in its unit.
///
/// A NaN limit disables that check.
struct Thresholds {
  float warnHigh = NAN;   ///< Warning at or above this.
  float alarmHigh = NAN;  ///< Alarm at or above this.
  float warnLow = NAN;    ///< Warning at or below this.
  float alarmLow = NAN;   ///< Alarm at or below this.
  /// @brief A level is only left once the value is this far back inside the
  /// limit, so readings hovering around a limit don't flicker between levels.
  float hysteresis = 0;

  /// Whether any limit is set.
  bool any() const;
};

/// @brief Level of `value`, given the level it had before (for hysteresis).
///
/// The hysteresis only applies on the way back from a limit. With limits on
/// both sides, it only applies on the side the value is on.
/// @param value The metric's value; NaN if there's none.
/// @param t The metric's limits.
/// @param previous The level it had before.
/// @return Unknown for NaN; otherwise Good, Warning or Alarm.
Level evaluate(float value, const Thresholds& t, Level previous);

/// @brief A metric's starting limits; see the table in the README for their
/// sources.
/// @param m The metric.
const Thresholds& defaultThresholds(Metric m);

/// @brief Metrics in the order they win ties: when two metrics are equally
/// bad, the earlier one is reported.
extern const std::array<Metric, kMetricCount> kPriority;

/// Tracks the level of every metric and the overall air quality.
class AirQuality {
 public:
  /// Starts with every metric's defaultThresholds().
  AirQuality();

  /// @brief Takes a new measurement: every metric's level, and the overall one.
  /// @param m The measurement.
  void update(const Measurement& m);
  /// @brief Sets a metric's limits.
  /// @param m The metric.
  /// @param t Its limits.
  void setThresholds(Metric m, const Thresholds& t) { thresholds_[idx(m)] = t; }
  /// @brief A metric's limits.
  /// @param m The metric.
  const Thresholds& thresholds(Metric m) const { return thresholds_[idx(m)]; }

  /// @brief A metric's level, as of the last update().
  /// @param m The metric.
  Level level(Metric m) const { return levels_[idx(m)]; }
  /// Worst level among metrics that have limits; Unknown until one is valid.
  Level overall() const { return overall_; }
  /// Metric responsible for overall(); meaningful when that's Warning or Alarm.
  Metric worst() const { return worst_; }

 private:
  std::array<Thresholds, kMetricCount> thresholds_;
  std::array<Level, kMetricCount> levels_{};
  Level overall_ = Level::Unknown;
  Metric worst_ = Metric::Co2;
};

}  // namespace aq
