// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Recent history of every metric for the trend graphs.
#pragma once

#include <algorithm>

#include "Metrics.h"

namespace aq {

/// @brief Fixed-size ring buffer of samples; index 0 is the oldest.
/// @tparam N How many samples it holds.
template <size_t N>
class Ring {
 public:
  /// @brief Adds a sample as the newest; once full, it replaces the oldest.
  /// @param v The sample.
  void push(float v) {
    data_[head_] = v;
    head_ = (head_ + 1) % N;
    if (size_ < N) ++size_;
  }
  /// How many samples it holds now, up to capacity().
  size_t size() const { return size_; }
  /// How many samples it can hold: N.
  static constexpr size_t capacity() { return N; }
  /// @brief A sample, by age.
  /// @param i 0 for the oldest, size() - 1 for the newest.
  float operator[](size_t i) const { return data_[(head_ + N - size_ + i) % N]; }

 private:
  std::array<float, N> data_{};
  size_t head_ = 0;  ///< Where the next sample goes.
  size_t size_ = 0;
};

/// Mean of the non-NaN values added since the last take().
class Averager {
 public:
  /// @brief Adds a value.
  /// @param v The value; NaN doesn't count.
  void add(float v) {
    if (std::isnan(v)) return;
    sum_ += v;
    ++count_;
  }
  /// @brief Takes the mean, and starts over.
  /// @return The mean of the values added since the last take(); NaN if none.
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

/// @brief Means of `group` consecutive samples over the newest `span` samples
/// of `ring` (all of them if it has fewer), oldest first.
///
/// The groups line up with the newest sample, so only the oldest one can be
/// short. Gaps (NaN) don't count; a group of nothing but gaps gives NaN.
/// @tparam N The ring's capacity.
/// @param ring The samples.
/// @param span How many of the newest samples to cover.
/// @param group How many samples each mean is of; at least 1.
/// @param[out] out Where to write the means: up to span / group values
///   (rounded up).
/// @return How many it wrote.
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

/// @brief Every metric at two resolutions: 1 h of 10 s means and 7 days of
/// 1 min means.
///
/// Periods without data are stored as NaN, which the graphs draw as gaps.
/// About 420 KB, so the firmware places it in PSRAM.
class MetricHistory {
 public:
  static constexpr uint32_t kFinePeriodMs = 10000;    ///< Each fine() mean's period, in ms.
  static constexpr uint32_t kCoarsePeriodMs = 60000;  ///< Each coarse() mean's period, in ms.
  using Fine = Ring<360>;                             ///< 1 h of 10 s means.
  using Coarse = Ring<7 * 24 * 60>;                   ///< 7 days of 1 min means.

  /// @brief Call with each new measurement, or with nullptr just to let time
  /// pass.
  /// @param m The new measurement, or nullptr.
  /// @param nowMs The time, as millis().
  void update(const Measurement* m, uint32_t nowMs);

  /// @brief A metric's 10 s means over the last hour, oldest first.
  /// @param m The metric.
  const Fine& fine(Metric m) const { return fine_[idx(m)]; }
  /// @brief A metric's 1 min means over the last 7 days, oldest first.
  /// @param m The metric.
  const Coarse& coarse(Metric m) const { return coarse_[idx(m)]; }
  /// @brief When the 1 min mean still being collected started (as `nowMs`);
  /// the newest one in coarse() is of the minute before.
  uint32_t coarseStartMs() const { return coarseStartMs_; }

 private:
  std::array<Fine, kMetricCount> fine_;
  std::array<Coarse, kMetricCount> coarse_;
  std::array<Averager, kMetricCount> fineMean_;    ///< The 10 s means being collected.
  std::array<Averager, kMetricCount> coarseMean_;  ///< The 1 min means being collected.
  bool started_ = false;                           ///< Whether the first periods have started.
  uint32_t fineStartMs_ = 0;  ///< When the 10 s mean being collected started.
  uint32_t coarseStartMs_ = 0;
};

}  // namespace aq
