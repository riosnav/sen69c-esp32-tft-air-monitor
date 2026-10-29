// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saved Wi-Fi networks, persisted in NVS. The device joins whichever one is in
// range, so it works at home, at work, and so on.
#pragma once

#include <array>
#include <cstddef>

struct WifiCredentials {
  char ssid[33] = "";
  char password[65] = "";
  bool hidden = false;
};

class WifiStore {
 public:
  static constexpr size_t kCapacity = 5;

  void load();
  size_t size() const { return count_; }
  // Most recently added first.
  const WifiCredentials& operator[](size_t i) const { return networks_[i]; }
  const WifiCredentials* find(const char* ssid) const;

  // Adds a network, or updates it if it's already saved, and moves it to the
  // front. When full, the oldest one is dropped.
  void add(WifiCredentials network);
  bool forget(const char* ssid);
  void clear();

 private:
  bool remove(const char* ssid);  // without saving
  void save() const;

  std::array<WifiCredentials, kCapacity> networks_{};
  size_t count_ = 0;
};
