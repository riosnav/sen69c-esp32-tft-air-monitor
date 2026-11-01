// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recent history of every metric for the trend graphs.
#pragma once

#include <algorithm>

#include "Metrics.h"

namespace aq {

// Fixed-size ring buffer of samples; index 0 is the oldest.
template <size_t N>
class Ring {
 public:
  void push(float v) {
    data_[head_] = v;
    head_ = (head_ + 1) % N;
    if (size_ < N) ++size_;
  }
  size_t size() const { return size_; }
  static constexpr size_t capacity() { return N; }
  float operator[](size_t i) const { return data_[(head_ + N - size_ + i) % N]; }

 private:
  std::array<float, N> data_{};
  size_t head_ = 0;
  size_t size_ = 0;
};

// Mean of the non-NaN values added since the last take().
class Averager {
 public:
  void add(float v) {
    if (std::isnan(v)) return;
    sum_ += v;
    ++count_;
  }
  float take() {
    float mean = count_ ? static_cast<float>(sum_ / count_) : NAN;
    sum_ = 0;
    count_ = 0;
    return mean;
  }

 private:
  double sum_ = 0;
  uint32_t count_ = 0;
};

// Means of `group` consecutive samples over the newest `span` samples of
// `ring` (all of them if it has fewer), oldest first. The groups line up with
// the newest sample, so only the oldest one can be short. Writes up to
// span / group values (rounded up) to `out` and returns how many.
template <size_t N>
size_t groupMeans(const Ring<N>& ring, size_t span, size_t group, float* out) {
  const size_t count = std::min(span, ring.size());
  const size_t first = ring.size() - count;
  size_t length = count % group == 0 ? group : count % group;
  size_t written = 0;
  size_t taken = 0;
  Averager mean;
  for (size_t i = 0; i < count; ++i) {
    mean.add(ring[first + i]);
    if (++taken == length) {
      out[written++] = mean.take();
      taken = 0;
      length = group;
    }
  }
  return written;
}

// Every metric at two resolutions: 1 h of 10 s means and 7 days of 1 min
// means. Periods without data are stored as NaN, which the graphs draw as
// gaps. About 420 KB, so the firmware places it in PSRAM.
class MetricHistory {
 public:
  static constexpr uint32_t kFinePeriodMs = 10000;
  static constexpr uint32_t kCoarsePeriodMs = 60000;
  using Fine = Ring<360>;
  using Coarse = Ring<7 * 24 * 60>;

  // Call with each new measurement, or with nullptr just to let time pass.
  void update(const Measurement* m, uint32_t nowMs);

  const Fine& fine(Metric m) const { return fine_[idx(m)]; }
  const Coarse& coarse(Metric m) const { return coarse_[idx(m)]; }
  // When the 1 min mean still being collected started (as `nowMs`); the
  // newest one in coarse() is of the minute before.
  uint32_t coarseStartMs() const { return coarseStartMs_; }

 private:
  std::array<Fine, kMetricCount> fine_;
  std::array<Coarse, kMetricCount> coarse_;
  std::array<Averager, kMetricCount> fineMean_;
  std::array<Averager, kMetricCount> coarseMean_;
  bool started_ = false;
  uint32_t fineStartMs_ = 0;
  uint32_t coarseStartMs_ = 0;
};

}  // namespace aq
