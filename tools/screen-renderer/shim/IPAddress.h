// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for Arduino's IPAddress.h, IPv4 only.
#pragma once

#include <cstdint>

#include "WString.h"

/// An IPv4 address, as Arduino's IPAddress (without its IPv6 side).
class IPAddress {
 public:
  /// The address 0.0.0.0.
  IPAddress() = default;
  /// @brief The address `a.b.c.d`.
  /// @param a The first byte.
  /// @param b The second.
  /// @param c The third.
  /// @param d The fourth.
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a, b, c, d} {}

  /// @brief One of the address's bytes.
  /// @param i Which: 0 to 3, as in `a.b.c.d`.
  uint8_t operator[](int i) const { return bytes_[i]; }
  /// @brief Whether two addresses are the same.
  /// @param other The other address.
  bool operator==(const IPAddress& other) const {
    for (int i = 0; i < 4; ++i) {
      if (bytes_[i] != other.bytes_[i]) return false;
    }
    return true;
  }
  /// The address as text, e.g. "192.168.1.42".
  String toString() const {
    String s(bytes_[0]);
    for (int i = 1; i < 4; ++i) s += String(".") + bytes_[i];
    return s;
  }

 private:
  uint8_t bytes_[4] = {};  ///< In order: a, b, c, d.
};
