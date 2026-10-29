// SPDX-License-Identifier: GPL-3.0-or-later

#include "Ota.h"

#include <Arduino.h>
#include <esp_app_format.h>

#include <cstring>

namespace {

// How long a new image has to rejoin Wi-Fi before it's rolled back.
constexpr uint32_t kProbationMs = 5 * 60000;

// An app image starts with the image header, the first segment's header and
// the app description.
constexpr size_t kAppDescOffset = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);

}  // namespace

// Tells the Arduino core not to confirm a new image at boot; Ota::loop does.
extern "C" bool verifyRollbackLater() { return true; }

void Ota::begin() {
  esp_ota_img_states_t state;
  probation_ = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
               state == ESP_OTA_IMG_PENDING_VERIFY;
  if (probation_) Serial.println("OTA: new firmware, confirms itself once it rejoins Wi-Fi");
}

void Ota::loop(bool connected, bool offline, uint32_t nowMs) {
  if (!probation_) return;
  if (connected || offline) {
    probation_ = false;
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("OTA: new firmware confirmed");
  } else if (nowMs >= kProbationMs) {
    Serial.println("OTA: new firmware can't rejoin Wi-Fi; rolling back");
    Serial.flush();
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

const char* Ota::start(size_t size) {
  bool expected = false;
  if (!active_.compare_exchange_strong(expected, true)) return "Another update is running";
  succeeded_ = false;
  written_ = 0;
  size_ = size;
  partition_ = esp_ota_get_next_update_partition(nullptr);
  if (partition_ == nullptr) return fail("No OTA partition");
  if (size < kAppDescOffset + sizeof(esp_app_desc_t) || size > partition_->size) {
    return fail("Wrong file size for a firmware image");
  }
  if (esp_ota_begin(partition_, OTA_WITH_SEQUENTIAL_WRITES, &handle_) != ESP_OK) {
    return fail("Could not start the update");
  }
  Serial.printf("OTA: receiving %u bytes\n", static_cast<unsigned>(size));
  return nullptr;
}

const char* Ota::write(const uint8_t* data, size_t length) {
  if (written_ == 0) {
    // Refuse anything that isn't an app image for this chip, such as the
    // merged "factory" image that also contains the bootloader.
    esp_image_header_t header;
    esp_app_desc_t desc;
    if (length < kAppDescOffset + sizeof desc) return fail("Not a firmware image");
    memcpy(&header, data, sizeof header);
    memcpy(&desc, data + kAppDescOffset, sizeof desc);
    if (header.magic != ESP_IMAGE_HEADER_MAGIC || desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
      return fail("Not a firmware image (use firmware.bin)");
    }
    if (header.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) return fail("Firmware is for another chip");
  }
  if (esp_ota_write(handle_, data, length) != ESP_OK) return fail("Could not write to flash");
  written_ += length;
  return nullptr;
}

const char* Ota::finish() {
  if (written_ != size_) return fail("Upload incomplete");
  const esp_err_t err = esp_ota_end(handle_);
  handle_ = 0;
  if (err != ESP_OK) return fail("The image is damaged");
  if (esp_ota_set_boot_partition(partition_) != ESP_OK) return fail("Could not select the image");
  Serial.println("OTA: update stored");
  succeeded_ = true;
  active_ = false;
  return nullptr;
}

void Ota::abort() { fail(nullptr); }

const char* Ota::fail(const char* reason) {
  if (handle_ != 0) esp_ota_abort(handle_);
  handle_ = 0;
  if (reason != nullptr) Serial.printf("OTA: %s\n", reason);
  active_ = false;
  return reason;
}

uint8_t Ota::percent() const {
  return size_ == 0 ? 0 : static_cast<uint8_t>(static_cast<uint64_t>(written_.load()) * 100 / size_);
}
