// SPDX-License-Identifier: GPL-3.0-or-later

#include "LoginGuard.h"

namespace net {

uint32_t LoginGuard::waitMs(uint32_t nowMs) const {
  const uint32_t elapsed = nowMs - waitFromMs_;
  return elapsed >= waitForMs_ ? 0 : waitForMs_ - elapsed;
}

void LoginGuard::failed(uint32_t nowMs) {
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
