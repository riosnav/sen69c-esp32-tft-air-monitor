// SPDX-License-Identifier: GPL-3.0-or-later
//
// IPv4 addresses as typed on the web page, for a fixed address instead of
// DHCP. An address is a 32-bit number with the first part in the top byte:
// 192.168.1.20 is 0xC0A80114.
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

// Reads "a.b.c.d"; false if it isn't exactly that.
bool parseIpv4(const char* text, uint32_t& out);
// Writes "a.b.c.d" (needs 16 bytes).
void formatIpv4(uint32_t address, char* out, size_t size);

// What's wrong with a fixed address on a network, or nullptr if it can work.
// `dns` may be 0 (none given).
const char* checkFixedAddress(uint32_t ip, uint32_t subnet, uint32_t gateway, uint32_t dns);

}  // namespace net
