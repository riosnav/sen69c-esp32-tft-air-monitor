// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stand-in for ESP-IDF's mqtt_client.h: the types MqttService.h mentions.
#pragma once

#include <cstdint>

typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;
typedef const char* esp_event_base_t;
