// SPDX-License-Identifier: GPL-3.0-or-later
//
// cJSON helpers shared by the web API and MQTT.
#pragma once

#include <cJSON.h>

#include <cmath>
#include <memory>

struct JsonDeleter {
  void operator()(cJSON* json) const { cJSON_Delete(json); }
};
using Json = std::unique_ptr<cJSON, JsonDeleter>;

struct JsonTextDeleter {
  void operator()(char* text) const { cJSON_free(text); }
};
using JsonText = std::unique_ptr<char, JsonTextDeleter>;

// Rounded, so the JSON doesn't carry float noise such as 23.100000381.
inline double rounded(float value, int decimals) {
  const double scale = std::pow(10.0, decimals);
  return std::round(value * scale) / scale;
}

// A number, or null for NaN (a value not measured yet).
inline void addNumber(cJSON* object, const char* key, float value, int decimals) {
  if (std::isnan(value)) {
    cJSON_AddNullToObject(object, key);
  } else {
    cJSON_AddNumberToObject(object, key, rounded(value, decimals));
  }
}
