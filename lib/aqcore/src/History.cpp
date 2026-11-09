// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Collecting the 10 s and 1 min means of every metric (see History.h).

#include "History.h"

namespace aq {

void MetricHistory::update(const Measurement* m, uint32_t nowMs) {
  if (!started_) {
    started_ = true;
    fineStartMs_ = coarseStartMs_ = nowMs;
  }
  // Close finished periods first, so this sample counts towards the new one.
  while (nowMs - fineStartMs_ >= kFinePeriodMs) {
    for (size_t i = 0; i < kMetricCount; ++i) fine_[i].push(fineMean_[i].take());
    fineStartMs_ += kFinePeriodMs;
  }
  while (nowMs - coarseStartMs_ >= kCoarsePeriodMs) {
    for (size_t i = 0; i < kMetricCount; ++i) coarse_[i].push(coarseMean_[i].take());
    coarseStartMs_ += kCoarsePeriodMs;
  }
  if (m == nullptr) return;
  for (size_t i = 0; i < kMetricCount; ++i) {
    fineMean_[i].add(m->values[i]);
    coarseMean_[i].add(m->values[i]);
  }
}

}  // namespace aq
