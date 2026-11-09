// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief IPv4 addresses as typed on the web page, for a fixed address instead
/// of DHCP.
///
/// An address is a 32-bit number with the first part in the top byte:
/// 192.168.1.20 is 0xC0A80114.
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

/// @brief Reads `a.b.c.d`; false if it isn't exactly that.
///
/// Each part is 0 to 255, in at most three digits, with nothing before or
/// after.
/// @param text The text.
/// @param[out] out The address; left as it was if the text isn't one.
/// @return Whether the text is an address.
bool parseIpv4(const char* text, uint32_t& out);
/// @brief Writes `a.b.c.d` (needs 16 bytes).
/// @param address The address.
/// @param[out] out Where to write it, with a terminator.
/// @param size The room at `out`, in bytes.
void formatIpv4(uint32_t address, char* out, size_t size);

/// @brief What's wrong with a fixed address on a network, or nullptr if it can
/// work.
///
/// The subnet mask must be 255.0.0.0 or narrower, with its ones contiguous,
/// and leave room for at least two devices. The address and the gateway must
/// be different, usable addresses on that subnet, and a DNS server an ordinary
/// (unicast) address.
/// @param ip The monitor's address.
/// @param subnet The subnet mask.
/// @param gateway The gateway's (router's) address.
/// @param dns The DNS server's address; may be 0 (none given).
/// @return A message for the web page, naming the field and what it needs;
///   nullptr if it can work.
const char* checkFixedAddress(uint32_t ip, uint32_t subnet, uint32_t gateway, uint32_t dns);

}  // namespace net
