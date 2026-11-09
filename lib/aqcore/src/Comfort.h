// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Derived comfort quantities.
#pragma once

namespace aq {

/// @brief Converts a temperature to degF.
/// @param celsius The temperature in degC.
/// @return The temperature in degF.
inline float toFahrenheit(float celsius) { return celsius * 9 / 5 + 32; }

/// @brief Dew point in degC (Magnus formula).
/// @param celsius The air temperature in degC.
/// @param rh The relative humidity in %.
/// @return The dew point in degC; NaN if an input is NaN or `rh <= 0`.
float dewPoint(float celsius, float rh);

/// @brief Water vapour content of the air in g/m3.
/// @param celsius The air temperature in degC.
/// @param rh The relative humidity in %.
/// @return The content in g/m3; NaN if an input is NaN.
float absoluteHumidity(float celsius, float rh);

}  // namespace aq
