// SPDX-License-Identifier: GPL-3.0-or-later
//
// Firmware updates over Wi-Fi. The image is written to the inactive OTA slot
// as it arrives, and the device boots it after a restart.
//
// A new image runs on probation: it confirms itself once it has rejoined
// Wi-Fi, so an update that crashes, or can't reconnect within a few minutes,
// is rolled back to the previous firmware.
#pragma once

#include <esp_ota_ops.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

class Ota {
 public:
  void begin();
  // Main loop. `connected`: joined a network; `offline`: Wi-Fi is off or
  // nothing is saved, so there is nothing to wait for.
  void loop(bool connected, bool offline, uint32_t nowMs);

  // Upload, from the web server's task. Each returns nullptr on success or
  // the reason it failed; after a failure the upload is over.
  const char* start(size_t size);
  const char* write(const uint8_t* data, size_t length);
  const char* finish();
  void abort();

  // For the UI.
  bool active() const { return active_; }
  bool succeeded() const { return succeeded_; }
  uint8_t percent() const;

 private:
  const char* fail(const char* reason);

  const esp_partition_t* partition_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  size_t size_ = 0;
  std::atomic<bool> active_{false};
  std::atomic<bool> succeeded_{false};
  std::atomic<uint32_t> written_{0};
  bool probation_ = false;
};
