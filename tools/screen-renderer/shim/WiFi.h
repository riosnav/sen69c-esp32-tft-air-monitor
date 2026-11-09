// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for Arduino-ESP32's WiFi.h: the names WifiService.h
/// mentions.
#pragma once

#include <Arduino.h>

/// ESP-IDF's Wi-Fi modes, without WIFI_MODE_NAN, which nothing here uses.
typedef enum {
  WIFI_MODE_NULL = 0,  ///< Wi-Fi off.
  WIFI_MODE_STA,       ///< Station: joins a network.
  WIFI_MODE_AP,        ///< Access point: its own hotspot.
  WIFI_MODE_APSTA,     ///< Both.
  WIFI_MODE_MAX        ///< One past the last mode; 1 less than ESP-IDF's.
} wifi_mode_t;
