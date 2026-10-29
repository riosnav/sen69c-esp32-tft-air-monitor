// SPDX-License-Identifier: GPL-3.0-or-later
//
// The main loop owns the air-quality levels, the history, the settings and
// the Wi-Fi state. It holds this lock while it updates them; the web server,
// which runs in its own task, takes it to read or change them.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class AppLock {
 public:
  AppLock() { xSemaphoreTakeRecursive(mutex(), portMAX_DELAY); }
  ~AppLock() { xSemaphoreGiveRecursive(mutex()); }
  AppLock(const AppLock&) = delete;
  AppLock& operator=(const AppLock&) = delete;

 private:
  static SemaphoreHandle_t mutex() {
    static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
    return m;
  }
};
