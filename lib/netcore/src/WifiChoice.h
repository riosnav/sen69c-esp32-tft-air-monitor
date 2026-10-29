// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which saved Wi-Fi networks to try, and in which order, after a scan.
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

struct SavedNetwork {
  const char* ssid;
  bool hidden;  // doesn't broadcast its name, so scans can't show it
};

struct ScanEntry {
  const char* ssid;  // empty for hidden networks
  int32_t rssi;
};

struct Candidate {
  size_t saved;   // index into the saved networks
  int scanIndex;  // its strongest access point in the scan, or -1 if not seen
};

// Saved networks seen in the scan come first, strongest signal first (ties
// keep the saved order); saved hidden networks the scan couldn't show follow.
// Writes at most `savedCount` candidates to `out` and returns how many.
size_t chooseNetworks(const SavedNetwork* saved, size_t savedCount, const ScanEntry* scan,
                      size_t scanCount, Candidate* out);

}  // namespace net
