// SPDX-License-Identifier: GPL-3.0-or-later
//
// Firmware updates over Wi-Fi. The image is written to the inactive OTA slot
// as it arrives. It's only installed once someone confirms it on the monitor
// itself, by holding a button, so a stolen web password isn't enough to
// replace the firmware; then the device boots it after a restart.
//
// A new image runs on probation: it confirms itself once it has rejoined
// Wi-Fi, so an update that crashes, or can't reconnect within a few minutes,
// is rolled back to the previous firmware.
#pragma once

#include <esp_ota_ops.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

enum class OtaState : uint8_t {
  Idle,
  Receiving,
  Confirming,  // stored; waits for a long press on the monitor
  Accepted,    // restarting into it
  Refused,     // a button said no
  TimedOut,    // nobody answered
  Failed,      // confirmed, but couldn't be installed
};

class Ota {
 public:
  // How long the monitor waits for an answer, and how long a button must be
  // held to say yes.
  static constexpr uint32_t kConfirmWindowMs = 2 * 60000;
  static constexpr uint32_t kConfirmHoldMs = 5000;

  void begin();
  // Main loop. `connected`: joined a network; `offline`: Wi-Fi is off or
  // nothing is saved, so there is nothing to wait for.
  void loop(bool connected, bool offline, uint32_t nowMs);

  // Upload, from the web server's task. Each returns nullptr on success or
  // the reason it failed; after a failure the upload is over.
  const char* start(size_t size);
  const char* write(const uint8_t* data, size_t length);
  const char* finish();  // stores the image and asks for confirmation
  void abort();

  // The answer, from the UI.
  void confirm();
  void refuse();

  OtaState state() const { return state_; }
  uint8_t percent() const;
  // While confirming: how long is left to answer.
  uint32_t secondsLeft(uint32_t nowMs) const;

 private:
  const char* fail(const char* reason);

  const esp_partition_t* partition_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  size_t size_ = 0;
  std::atomic<OtaState> state_{OtaState::Idle};
  std::atomic<uint32_t> written_{0};
  std::atomic<uint32_t> deadlineMs_{0};
  bool probation_ = false;
};
