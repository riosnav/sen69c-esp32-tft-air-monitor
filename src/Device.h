// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Names that tell this device apart from others, derived from its MAC
/// address.
#pragma once

#include <cstddef>

/// @brief This device: names derived from its MAC address, and random
/// passwords.
namespace device {

/// Last four hex digits of the Wi-Fi MAC address, e.g. "a1b2".
const char* shortId();
/// DHCP and mDNS host name, e.g. "airmonitor-a1b2".
const char* hostname();
/// The whole MAC address as 12 hex digits, for ids that must be unique.
const char* macId();

/// @brief Writes `length` random characters plus a terminator to `out`.
///
/// The alphabet leaves out characters that are easy to confuse (0/o, 1/l).
/// @param[out] out Room for `length` + 1 characters.
/// @param length How many random characters.
void randomPassword(char* out, size_t length);

}  // namespace device
