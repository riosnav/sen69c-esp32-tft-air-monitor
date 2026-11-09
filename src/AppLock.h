// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The lock on the main loop's state.
///
/// The main loop owns the readings, the air-quality levels, the history, the
/// settings, and the Wi-Fi, MQTT, battery and UI state. It holds this lock
/// while it updates them; the web server, which runs in its own task, takes it
/// to read or change them.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/// @brief Holds the lock on the main loop's state for as long as it exists.
///
/// The lock is a recursive mutex, so a task that holds it may take it again.
class AppLock {
 public:
  /// Takes the lock, waiting as long as it takes.
  AppLock() { xSemaphoreTakeRecursive(mutex(), portMAX_DELAY); }
  /// Gives the lock back.
  ~AppLock() { xSemaphoreGiveRecursive(mutex()); }
  /// Not copyable: each AppLock gives back only what it took.
  AppLock(const AppLock&) = delete;
  /// Not copyable: each AppLock gives back only what it took.
  AppLock& operator=(const AppLock&) = delete;

 private:
  /// The recursive mutex, created on first use.
  static SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    return m;
  }
};
