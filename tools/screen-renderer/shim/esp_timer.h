// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for ESP-IDF's esp_timer.h: the time since start, from the
/// simulated clock.
#pragma once

#include <cstdint>

/// @brief The time since start, in microseconds: millis() times 1000, so
/// whole ms only (defined in Platform.cpp).
int64_t esp_timer_get_time();
