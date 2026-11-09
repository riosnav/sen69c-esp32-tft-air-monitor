// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Dew point and absolute humidity (see Comfort.h).

#include "Comfort.h"

#include <cmath>

namespace aq {

float dewPoint(float celsius, float rh) {
  if (std::isnan(celsius) || std::isnan(rh) || rh <= 0) return NAN;
  constexpr float a = 17.62f;
  constexpr float b = 243.12f;
  float gamma = std::log(rh / 100) + a * celsius / (b + celsius);
  return b * gamma / (a - gamma);
}

float absoluteHumidity(float celsius, float rh) {
  if (std::isnan(celsius) || std::isnan(rh)) return NAN;
  float saturation = 6.112f * std::exp(17.67f * celsius / (celsius + 243.5f));  // hPa
  return saturation * rh * 2.1674f / (273.15f + celsius);
}

}  // namespace aq
