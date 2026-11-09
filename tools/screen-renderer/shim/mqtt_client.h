// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for ESP-IDF's mqtt_client.h: the types MqttService.h
/// mentions.
#pragma once

#include <cstdint>

/// An MQTT client: a pointer to a type that's never defined, as in ESP-IDF.
typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;
/// The family an event belongs to, as ESP-IDF's event loop names it.
typedef const char* esp_event_base_t;
