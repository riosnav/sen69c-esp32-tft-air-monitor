// SPDX-License-Identifier: GPL-3.0-or-later
//
// Arduino's IPAddress, IPv4 only.
#pragma once

#include <cstdint>

#include "WString.h"

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a, b, c, d} {}

  uint8_t operator[](int i) const { return bytes_[i]; }
  bool operator==(const IPAddress& other) const {
    for (int i = 0; i < 4; ++i) {
      if (bytes_[i] != other.bytes_[i]) return false;
    }
    return true;
  }
  String toString() const {
    String s(bytes_[0]);
    for (int i = 1; i < 4; ++i) s += String(".") + bytes_[i];
    return s;
  }

 private:
  uint8_t bytes_[4] = {};
};
