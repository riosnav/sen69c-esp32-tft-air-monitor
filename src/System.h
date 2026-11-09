// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Deferred restart, so a web request can still be answered first.
#pragma once

#include <cstdint>

/// Restarting the device a little later, from any task.
namespace sys {

/// @brief Restarts the device after `delayMs`.
///
/// Safe to call from any task: the restart itself happens in loop().
/// @param delayMs How long to wait, in ms.
void restartLater(uint32_t delayMs = 1000);
/// @brief Restarts the device once restartLater() says it's time. Call from
/// the main loop.
/// @param nowMs The time, as millis().
void loop(uint32_t nowMs);

}  // namespace sys
