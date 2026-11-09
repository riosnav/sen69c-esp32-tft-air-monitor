// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Slows down guessing of the web password.
///
/// After LoginGuard::kFreeTries wrong passwords in a row, every password is
/// refused for a while: a minute, doubling with each further miss, up to
/// LoginGuard::kMaxWaitMs, so an attack can't lock the owner out for longer
/// than that. The right password resets it.
#pragma once

#include <cstdint>

namespace net {

/// @brief Counts wrong web passwords, and says how long to refuse passwords
/// without checking them.
///
/// Tell it each password's outcome with failed() or succeeded().
class LoginGuard {
 public:
  static constexpr uint32_t kFreeTries = 3;  ///< Wrong passwords in a row that start a wait.
  static constexpr uint32_t kFirstWaitMs = 60000;     ///< The first wait, in ms.
  static constexpr uint32_t kMaxWaitMs = 15 * 60000;  ///< The longest wait, in ms.

  /// @brief How much longer passwords are refused without being checked, in
  /// ms; 0 = they're checked.
  /// @param nowMs The time, as millis().
  uint32_t waitMs(uint32_t nowMs) const;
  /// @brief Takes a wrong password: from the kFreeTries-th in a row on, each
  /// one starts a wait, kFirstWaitMs and then twice the last, up to kMaxWaitMs.
  /// @param nowMs The time, as millis().
  void failed(uint32_t nowMs);
  /// Takes the right password: no wait, and the next miss is the first in a row.
  void succeeded();
  /// Wrong passwords since start.
  uint32_t failures() const { return failures_; }

 private:
  uint32_t streak_ = 0;  ///< Wrong passwords in a row.
  uint32_t failures_ = 0;
  uint32_t waitFromMs_ = 0;  ///< When the current wait started.
  uint32_t waitForMs_ = 0;   ///< How long it lasts, in ms; 0 = none.
};

}  // namespace net
