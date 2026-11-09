// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Time since the monitor started, for spans that can outlast
/// millis().
///
/// millis() wraps after 49.7 days, which a monitor on USB power easily runs.
/// That's fine for timeouts of seconds or minutes, measured as differences,
/// but not for how long it has been running or measuring.
#pragma once

#include <esp_timer.h>

#include <cstdint>

/// @brief Time since the monitor started, in ms; it doesn't wrap.
/// @return The time, from ESP-IDF's 64-bit microsecond timer.
inline uint64_t uptimeMs() { return static_cast<uint64_t>(esp_timer_get_time()) / 1000; }
