// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The device's names from its MAC address, and random passwords (see
/// Device.h).

#include "Device.h"

#include <bootloader_random.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <esp_wifi.h>

#include <cstdio>

namespace device {

namespace {

/// The names, worked out once from the Wi-Fi MAC address.
struct Ids {
  char shortId[5];    ///< See device::shortId().
  char hostname[24];  ///< See device::hostname().
  char macId[13];     ///< See device::macId().

  /// Reads the MAC address and writes the names.
  Ids() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(shortId, sizeof shortId, "%02x%02x", mac[4], mac[5]);
    snprintf(hostname, sizeof hostname, "airmonitor-%s", shortId);
    snprintf(macId, sizeof macId, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
  }
};

/// The names, worked out on first use.
const Ids& ids() {
  static const Ids instance;
  return instance;
}

}  // namespace

const char* shortId() { return ids().shortId; }
const char* hostname() { return ids().hostname; }
const char* macId() { return ids().macId; }

void randomPassword(char* out, size_t length) {
  static constexpr char kAlphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";  // 32 characters
  // esp_random() is only truly random while the radio runs or the
  // bootloader's entropy source is on, so turn that on when the radio is off.
  wifi_mode_t mode = WIFI_MODE_NULL;
  const bool radioOff = esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL;
  if (radioOff) bootloader_random_enable();
  for (size_t i = 0; i < length; ++i) out[i] = kAlphabet[esp_random() % (sizeof kAlphabet - 1)];
  out[length] = '\0';
  if (radioOff) bootloader_random_disable();
}

}  // namespace device
