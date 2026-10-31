// SPDX-License-Identifier: GPL-3.0-or-later

#include "Ipv4.h"

#include <cstdio>

namespace net {

namespace {

// Not 0.x.x.x, loopback (127.x.x.x), multicast or reserved (224.0.0.0 and up).
bool unicast(uint32_t address) {
  const uint32_t first = address >> 24;
  return first != 0 && first != 127 && first < 224;
}

}  // namespace

bool parseIpv4(const char* text, uint32_t& out) {
  uint32_t address = 0;
  const char* c = text;
  for (int part = 0; part < 4; ++part) {
    if (part > 0 && *c++ != '.') return false;
    if (*c < '0' || *c > '9') return false;
    uint32_t value = 0;
    for (int digits = 0; *c >= '0' && *c <= '9'; ++digits) {
      if (digits == 3) return false;
      value = value * 10 + static_cast<uint32_t>(*c++ - '0');
    }
    if (value > 255) return false;
    address = address << 8 | value;
  }
  if (*c != '\0') return false;
  out = address;
  return true;
}

void formatIpv4(uint32_t address, char* out, size_t size) {
  snprintf(out, size, "%u.%u.%u.%u", static_cast<unsigned>(address >> 24),
           static_cast<unsigned>(address >> 16 & 0xFF), static_cast<unsigned>(address >> 8 & 0xFF),
           static_cast<unsigned>(address & 0xFF));
}

const char* checkFixedAddress(uint32_t ip, uint32_t subnet, uint32_t gateway, uint32_t dns) {
  // The mask's ones must be contiguous, leaving room for at least two devices.
  const uint32_t hosts = ~subnet;
  if ((hosts & (hosts + 1)) != 0 || subnet >> 24 != 0xFF || hosts < 3) {
    return "Subnet mask: e.g. 255.255.255.0";
  }
  // Within the subnet, the first and last addresses are reserved.
  auto usable = [&](uint32_t address) {
    const uint32_t host = address & hosts;
    return unicast(address) && host != 0 && host != hosts;
  };
  if (!usable(ip)) return "IP address: not a usable address on this subnet";
  if (!usable(gateway) || (gateway & subnet) != (ip & subnet)) {
    return "Gateway: an address on the same subnet as the IP address";
  }
  if (gateway == ip) return "Gateway: not the monitor's own address";
  if (dns != 0 && !unicast(dns)) return "DNS server: not a valid address";
  return nullptr;
}

}  // namespace net
