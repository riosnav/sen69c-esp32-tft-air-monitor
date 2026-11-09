// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The quantities the SEN69C measures, and one reading of all of them.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

/// @brief Air quality without the hardware: the metrics (aq::Measurement),
/// their levels (aq::AirQuality), their history (aq::MetricHistory), comfort
/// values, and the buttons' logic (aq::ButtonLogic).
namespace aq {

/// The quantities the SEN69C measures, in the order of every per-metric array.
enum class Metric : uint8_t {
  Pm1,          ///< PM1.0, in ug/m3.
  Pm25,         ///< PM2.5, in ug/m3.
  Pm4,          ///< PM4.0, in ug/m3.
  Pm10,         ///< PM10, in ug/m3.
  Co2,          ///< CO2, in ppm.
  Hcho,         ///< Formaldehyde, in ppb.
  Voc,          ///< Sensirion VOC index, 100 = typical.
  Nox,          ///< Sensirion NOx index, 1 = typical.
  Temperature,  ///< In degC.
  Humidity,     ///< Relative humidity, in %RH.
  Count         ///< Not a metric: how many there are.
};

/// How many metrics there are.
constexpr size_t kMetricCount = static_cast<size_t>(Metric::Count);

/// @brief A metric's index into the per-metric arrays.
/// @param m The metric.
constexpr size_t idx(Metric m) { return static_cast<size_t>(m); }

/// Stable machine-readable names (web API, MQTT), in the order of Metric.
constexpr const char* kMetricIds[kMetricCount] = {"pm1", "pm25", "pm4", "pm10", "co2",
                                                  "hcho", "voc", "nox", "temp", "rh"};

/// @brief A metric's machine-readable name, from kMetricIds.
/// @param m The metric.
constexpr const char* metricId(Metric m) { return kMetricIds[idx(m)]; }

/// Decimal places worth reporting (web API, MQTT), in the order of Metric.
constexpr int kMetricDecimals[kMetricCount] = {1, 1, 1, 1, 0, 1, 0, 0, 2, 1};

/// @brief One reading of every metric.
///
/// NaN marks a value the sensor doesn't provide yet (warm-up) or couldn't
/// measure.
struct Measurement {
  std::array<float, kMetricCount> values;  ///< In the order of Metric; see idx().

  /// Starts with every value NaN.
  Measurement() { values.fill(NAN); }
  /// @brief One metric's value.
  /// @param m The metric.
  float operator[](Metric m) const { return values[idx(m)]; }
  /// @brief One metric's value, to set it.
  /// @param m The metric.
  float& operator[](Metric m) { return values[idx(m)]; }
};

}  // namespace aq
