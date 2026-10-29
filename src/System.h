// SPDX-License-Identifier: GPL-3.0-or-later
//
// Deferred restart, so a web request can still be answered first.
#pragma once

#include <cstdint>

namespace sys {

// Restarts the device after `delayMs`. Safe to call from any task.
void restartLater(uint32_t delayMs = 1000);
// Call from loop().
void loop(uint32_t nowMs);

}  // namespace sys
