// SPDX-License-Identifier: GPL-3.0-or-later

#include "WifiChoice.h"

#include <algorithm>
#include <cstring>

namespace net {

size_t chooseNetworks(const SavedNetwork* saved, size_t savedCount, const ScanEntry* scan,
                      size_t scanCount, Candidate* out) {
  size_t n = 0;
  for (size_t i = 0; i < savedCount; ++i) {
    int best = -1;
    for (size_t j = 0; j < scanCount; ++j) {
      if (strcmp(scan[j].ssid, saved[i].ssid) != 0) continue;
      if (best < 0 || scan[j].rssi > scan[best].rssi) best = static_cast<int>(j);
    }
    if (best >= 0) out[n++] = {i, best};
  }
  std::stable_sort(out, out + n, [scan](const Candidate& a, const Candidate& b) {
    return scan[a.scanIndex].rssi > scan[b.scanIndex].rssi;
  });

  const size_t seen = n;
  for (size_t i = 0; i < savedCount; ++i) {
    if (!saved[i].hidden) continue;
    const bool found = std::any_of(out, out + seen, [i](const Candidate& c) { return c.saved == i; });
    if (!found) out[n++] = {i, -1};
  }
  return n;
}

}  // namespace net
