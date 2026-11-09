// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The deferred restart (see System.h).

#include "System.h"

#include <Arduino.h>

#include <atomic>

namespace sys {

namespace {
std::atomic<bool> pending{false};      ///< Whether a restart is due.
std::atomic<uint32_t> restartAtMs{0};  ///< When, as millis().
}  // namespace

void restartLater(uint32_t delayMs) {
  restartAtMs = millis() + delayMs;
  pending = true;
}

void loop(uint32_t nowMs) {
  if (pending && static_cast<int32_t>(nowMs - restartAtMs) >= 0) {
    Serial.println("Restarting");
    Serial.flush();
    ESP.restart();
  }
}

}  // namespace sys
