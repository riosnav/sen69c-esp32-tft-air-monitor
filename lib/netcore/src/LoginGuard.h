// SPDX-License-Identifier: GPL-3.0-or-later
//
// Slows down guessing of the web password. After kFreeTries wrong passwords in
// a row, every password is refused for a while: a minute, doubling with each
// further miss, up to kMaxWaitMs, so an attack can't lock the owner out for
// longer than that. The right password resets it.
#pragma once

#include <cstdint>

namespace net {

class LoginGuard {
 public:
  static constexpr uint32_t kFreeTries = 3;
  static constexpr uint32_t kFirstWaitMs = 60000;
  static constexpr uint32_t kMaxWaitMs = 15 * 60000;

  // How much longer passwords are refused without being checked; 0 = they're checked.
  uint32_t waitMs(uint32_t nowMs) const;
  void failed(uint32_t nowMs);
  void succeeded();
  // Wrong passwords since start.
  uint32_t failures() const { return failures_; }

 private:
  uint32_t streak_ = 0;  // wrong passwords in a row
  uint32_t failures_ = 0;
  uint32_t waitFromMs_ = 0;
  uint32_t waitForMs_ = 0;
};

}  // namespace net
