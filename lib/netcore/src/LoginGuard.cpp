// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The waits after wrong web passwords (see LoginGuard.h).

#include "LoginGuard.h"

namespace net {

uint32_t LoginGuard::waitMs(uint64_t nowMs) const {
  const uint64_t elapsed = nowMs - waitFromMs_;
  return elapsed >= waitForMs_ ? 0 : static_cast<uint32_t>(waitForMs_ - elapsed);
}

void LoginGuard::failed(uint64_t nowMs) {
  ++failures_;
  if (++streak_ < kFreeTries) return;
  uint32_t wait = kFirstWaitMs;
  for (uint32_t i = kFreeTries; i < streak_ && wait < kMaxWaitMs; ++i) wait *= 2;
  waitFromMs_ = nowMs;
  waitForMs_ = wait < kMaxWaitMs ? wait : kMaxWaitMs;
}

void LoginGuard::succeeded() {
  streak_ = 0;
  waitForMs_ = 0;
}

}  // namespace net
