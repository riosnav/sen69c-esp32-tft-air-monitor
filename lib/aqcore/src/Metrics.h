// SPDX-License-Identifier: GPL-3.0-or-later
//
// The quantities the SEN69C measures, and one reading of all of them.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aq {

enum class Metric : uint8_t {
  Pm1,
  Pm25,
  Pm4,
  Pm10,
  Co2,
  Hcho,
  Voc,
  Nox,
  Temperature,
  Humidity,
  Count
};

constexpr size_t kMetricCount = static_cast<size_t>(Metric::Count);

constexpr size_t idx(Metric m) { return static_cast<size_t>(m); }

// One reading of every metric. NaN marks a value the sensor doesn't provide
// yet (warm-up) or couldn't measure.
struct Measurement {
  std::array<float, kMetricCount> values;

  Measurement() { values.fill(NAN); }
  float operator[](Metric m) const { return values[idx(m)]; }
  float& operator[](Metric m) { return values[idx(m)]; }
};

}  // namespace aq
