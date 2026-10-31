// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stand-in for Arduino-ESP32's WiFi.h: the names WifiService.h mentions.
#pragma once

#include <Arduino.h>

typedef enum {
  WIFI_MODE_NULL = 0,
  WIFI_MODE_STA,
  WIFI_MODE_AP,
  WIFI_MODE_APSTA,
  WIFI_MODE_MAX
} wifi_mode_t;
