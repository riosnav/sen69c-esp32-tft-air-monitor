// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Firmware updates over Wi-Fi, installed only once confirmed on the
/// monitor itself.
///
/// The image is written to the inactive OTA slot as it arrives. It's only
/// installed once someone confirms it on the monitor itself, by holding a
/// button, so a stolen web password isn't enough to replace the firmware; then
/// the device boots it after a restart.
///
/// A new image runs on probation: it confirms itself once it has rejoined
/// Wi-Fi, so an update that crashes, or can't reconnect within a few minutes,
/// is rolled back to the previous firmware.
#pragma once

#include <esp_ota_ops.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

/// Where a firmware update is.
enum class OtaState : uint8_t {
  Idle,        ///< None yet, or the last upload failed.
  Receiving,   ///< Being uploaded and written to flash.
  Confirming,  ///< Stored; waits for a long press on the monitor.
  Accepted,    ///< Restarting into it.
  Refused,     ///< A button said no.
  TimedOut,    ///< Nobody answered.
  Failed,      ///< Confirmed, but couldn't be installed.
};

/// @brief A firmware update over Wi-Fi: receives the image, waits for a yes
/// on the monitor, and confirms new firmware or rolls it back.
///
/// The upload comes from the web server's task, the rest from the main loop;
/// the state is atomic, so either may read it.
class Ota {
 public:
  /// How long the monitor waits for an answer, in ms.
  static constexpr uint32_t kConfirmWindowMs = 2 * 60000;
  /// How long a button must be held to say yes, in ms.
  static constexpr uint32_t kConfirmHoldMs = 5000;

  /// At startup: notes whether the running firmware is new, on probation.
  void begin();
  /// @brief Main loop: ends a confirmation nobody answered in time, and
  /// confirms new firmware on probation, or rolls it back.
  ///
  /// New firmware confirms itself once it has joined a network, or at once
  /// when there's nothing to wait for; it's rolled back if that hasn't
  /// happened 5 minutes after the start.
  /// @param connected Whether Wi-Fi has joined a network.
  /// @param offline Whether Wi-Fi is off or nothing is saved, so there is
  ///   nothing to wait for.
  /// @param nowMs The time, as millis().
  void loop(bool connected, bool offline, uint32_t nowMs);

  /// @brief Starts an upload, from the web server's task.
  ///
  /// This, write() and finish() return nullptr on success or the reason it
  /// failed; after a failure the upload is over.
  /// @param size The image's size, in bytes.
  /// @return nullptr, or why it can't start, e.g. another update is running.
  const char* start(size_t size);
  /// @brief Writes the next part of the image to flash, from the web server's
  /// task.
  ///
  /// The first part must hold the image's headers and app description:
  /// anything but an app image for this chip is refused, such as the merged
  /// "factory" image that also contains the bootloader.
  /// @param data The part.
  /// @param length Its length, in bytes.
  /// @return nullptr, or why it failed.
  const char* write(const uint8_t* data, size_t length);
  /// @brief Stores the image and asks for confirmation on the monitor, from
  /// the web server's task.
  /// @return nullptr, or why it failed: the upload is incomplete, or the image
  ///   is damaged.
  const char* finish();
  /// Ends the upload, e.g. when the connection is lost.
  void abort();

  /// From the UI: yes, install the stored image and restart into it.
  void confirm();
  /// From the UI: no; the stored image is discarded.
  void refuse();

  /// Where the update is.
  OtaState state() const { return state_; }
  /// How much of the image has been received, in %.
  uint8_t percent() const;
  /// @brief While confirming: how long is left to answer, in s.
  /// @param nowMs The time, as millis().
  /// @return The seconds left, rounded up; 0 when not confirming.
  uint32_t secondsLeft(uint32_t nowMs) const;

 private:
  /// @brief Ends the upload: abandons what was written and goes back to
  /// Idle.
  /// @param reason Why, which is logged; nullptr for none.
  /// @return `reason`.
  const char* fail(const char* reason);

  const esp_partition_t* partition_ = nullptr;  ///< The slot the image goes to.
  esp_ota_handle_t handle_ = 0;  ///< The update being written; 0 if none.
  std::atomic<uint32_t> size_{0};  ///< The image's size, in bytes.
  std::atomic<OtaState> state_{OtaState::Idle};
  std::atomic<uint32_t> written_{0};     ///< How much has been written, in bytes.
  std::atomic<uint32_t> deadlineMs_{0};  ///< When confirming ends, as millis().
  bool probation_ = false;  ///< New firmware, not confirmed yet (see begin()).
};
