// SPDX-License-Identifier: GPL-3.0-or-later
//
// Derived comfort quantities.
#pragma once

namespace aq {

inline float toFahrenheit(float celsius) { return celsius * 9 / 5 + 32; }

// Dew point in degC (Magnus formula). NaN if an input is NaN or rh <= 0.
float dewPoint(float celsius, float rh);

// Water vapour content of the air in g/m3. NaN if an input is NaN.
float absoluteHumidity(float celsius, float rh);

}  // namespace aq
