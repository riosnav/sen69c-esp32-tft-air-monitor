// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for ESP-IDF's esp_ota_ops.h: the types Ota.h mentions.
#pragma once

#include <cstdint>

/// An update being written to flash, as ESP-IDF numbers them.
typedef uint32_t esp_ota_handle_t;
/// A flash partition: only declared, as Ota.h only points to one.
typedef struct esp_partition_t esp_partition_t;
