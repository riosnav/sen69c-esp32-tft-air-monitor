// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief cJSON helpers shared by the web API and MQTT.
#pragma once

#include <cJSON.h>

#include <cmath>
#include <memory>

/// Frees a cJSON tree, for Json.
struct JsonDeleter {
  /// @brief Frees the tree.
  /// @param json Its root.
  void operator()(cJSON* json) const { cJSON_Delete(json); }
};
/// A cJSON tree that frees itself.
using Json = std::unique_ptr<cJSON, JsonDeleter>;

/// Frees text that cJSON printed, for JsonText.
struct JsonTextDeleter {
  /// @brief Frees the text.
  /// @param text The text.
  void operator()(char* text) const { cJSON_free(text); }
};
/// Text that cJSON printed, e.g. with cJSON_PrintUnformatted(), that frees itself.
using JsonText = std::unique_ptr<char, JsonTextDeleter>;

/// @brief Rounds a value, so the JSON doesn't carry float noise such as
/// 23.100000381.
/// @param value The value.
/// @param decimals How many decimals to keep.
/// @return The value, rounded.
inline double rounded(float value, int decimals) {
  const double scale = std::pow(10.0, decimals);
  return std::round(value * scale) / scale;
}

/// @brief Adds a number to a JSON object, or null for NaN (a value not
/// measured yet).
/// @param object The object.
/// @param key The number's key.
/// @param value The number; NaN if not measured yet.
/// @param decimals How many decimals to keep (see rounded()).
inline void addNumber(cJSON* object, const char* key, float value, int decimals) {
  if (std::isnan(value)) {
    cJSON_AddNullToObject(object, key);
  } else {
    cJSON_AddNumberToObject(object, key, rounded(value, decimals));
  }
}
