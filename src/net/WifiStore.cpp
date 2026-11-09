// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Saved Wi-Fi networks, persisted in NVS (see WifiStore.h).

#include "WifiStore.h"

#include <Preferences.h>

#include <algorithm>
#include <cstring>

namespace {

constexpr const char* kNamespace = "wifi";  ///< The NVS namespace.

/// @brief The NVS key of one of a network's fields, e.g. `ssid0`.
/// @param prefix The field's prefix, e.g. `ssid`.
/// @param i The network's index.
/// @return The prefix followed by the index.
String key(const char* prefix, size_t i) { return String(prefix) + i; }

}  // namespace

void WifiStore::load() {
  count_ = 0;
  Preferences p;
  if (!p.begin(kNamespace, true)) return;  // nothing saved yet
  const size_t count = std::min<size_t>(p.getUChar("count", 0), kCapacity);
  for (size_t i = 0; i < count; ++i) {
    WifiCredentials& n = networks_[count_];
    n = WifiCredentials();
    p.getString(key("ssid", i).c_str(), n.ssid, sizeof n.ssid);
    p.getString(key("pass", i).c_str(), n.password, sizeof n.password);
    n.hidden = p.getBool(key("hid", i).c_str(), false);
    n.address.ip = p.getUInt(key("ip", i).c_str(), 0);  // absent: DHCP
    n.address.subnet = p.getUInt(key("mask", i).c_str(), 0);
    n.address.gateway = p.getUInt(key("gw", i).c_str(), 0);
    n.address.dns = p.getUInt(key("dns", i).c_str(), 0);
    if (n.ssid[0] != '\0') ++count_;
  }
  p.end();
}

const WifiCredentials* WifiStore::find(const char* ssid) const {
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(networks_[i].ssid, ssid) == 0) return &networks_[i];
  }
  return nullptr;
}

void WifiStore::add(WifiCredentials network) {
  const WifiCredentials* old = find(network.ssid);
  if (old != nullptr && !network.address.set()) network.address = old->address;
  remove(network.ssid);
  if (count_ == kCapacity) --count_;
  std::move_backward(networks_.begin(), networks_.begin() + count_,
                     networks_.begin() + count_ + 1);
  networks_[0] = network;
  ++count_;
  save();
}

bool WifiStore::setAddress(const char* ssid, const FixedAddress& address) {
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(networks_[i].ssid, ssid) != 0) continue;
    networks_[i].address = address;
    save();
    return true;
  }
  return false;
}

bool WifiStore::forget(const char* ssid) {
  if (!remove(ssid)) return false;
  save();
  return true;
}

bool WifiStore::remove(const char* ssid) {
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(networks_[i].ssid, ssid) != 0) continue;
    std::move(networks_.begin() + i + 1, networks_.begin() + count_, networks_.begin() + i);
    --count_;
    return true;
  }
  return false;
}

void WifiStore::clear() {
  count_ = 0;
  save();
}

void WifiStore::save() const {
  Preferences p;
  if (!p.begin(kNamespace, false)) return;
  p.clear();
  p.putUChar("count", static_cast<uint8_t>(count_));
  for (size_t i = 0; i < count_; ++i) {
    p.putString(key("ssid", i).c_str(), networks_[i].ssid);
    p.putString(key("pass", i).c_str(), networks_[i].password);
    p.putBool(key("hid", i).c_str(), networks_[i].hidden);
    const FixedAddress& a = networks_[i].address;
    if (!a.set()) continue;
    p.putUInt(key("ip", i).c_str(), a.ip);
    p.putUInt(key("mask", i).c_str(), a.subnet);
    p.putUInt(key("gw", i).c_str(), a.gateway);
    p.putUInt(key("dns", i).c_str(), a.dns);
  }
  p.end();
}
