// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Which saved Wi-Fi networks to try, and in which order, after a scan.
#pragma once

#include <cstddef>
#include <cstdint>

/// @brief Network logic without the hardware: which Wi-Fi networks to try
/// (net::chooseNetworks()), fixed IPv4 addresses, and the web password's guard
/// (net::LoginGuard).
namespace net {

/// A saved network, as chooseNetworks() needs it.
struct SavedNetwork {
  const char* ssid;  ///< Its name.
  bool hidden;  ///< Doesn't broadcast its name, so scans can't show it.
};

/// One access point a scan found.
struct ScanEntry {
  const char* ssid;  ///< Empty for hidden networks.
  int32_t rssi;  ///< Its signal strength, in dBm.
};

/// A saved network to try, from chooseNetworks().
struct Candidate {
  size_t saved;   ///< Index into the saved networks.
  int scanIndex;  ///< Its strongest access point in the scan, or -1 if not seen.
};

/// @brief Which saved networks to try after a scan, and in which order.
///
/// Saved networks seen in the scan come first, strongest signal first (ties
/// keep the saved order); saved hidden networks the scan couldn't show follow.
/// Other saved networks aren't in range, and aren't tried.
/// @param saved The saved networks.
/// @param savedCount How many there are.
/// @param scan The access points the scan found; a network may have several.
/// @param scanCount How many there are.
/// @param[out] out Where to write the candidates, in that order: at most
///   `savedCount`.
/// @return How many it wrote.
size_t chooseNetworks(const SavedNetwork* saved, size_t savedCount, const ScanEntry* scan,
                      size_t scanCount, Candidate* out);

}  // namespace net
