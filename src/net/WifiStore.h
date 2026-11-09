// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Saved Wi-Fi networks, persisted in NVS.
///
/// The device joins whichever one is in range, so it works at home, at work,
/// and so on.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// @brief A fixed IPv4 address on a network, instead of one from DHCP.
///
/// Addresses have their first part in the top byte (see Ipv4.h); an `ip` of 0
/// means DHCP.
struct FixedAddress {
  uint32_t ip = 0;       ///< The monitor's address; 0 for DHCP.
  uint32_t subnet = 0;   ///< The subnet mask.
  uint32_t gateway = 0;  ///< The router's address.
  uint32_t dns = 0;      ///< The DNS server; 0: the gateway.
  /// Whether there is one, rather than DHCP.
  bool set() const { return ip != 0; }
};

/// A saved network: what it takes to join it.
struct WifiCredentials {
  char ssid[33] = "";      ///< Its name, up to 32 characters.
  char password[65] = "";  ///< Up to 64 characters; empty for an open network.
  bool hidden = false;     ///< Whether it hides its name, so scans can't show it.
  FixedAddress address;    ///< Its fixed address, if it has one.
};

/// The saved Wi-Fi networks, up to kCapacity, persisted in NVS.
class WifiStore {
 public:
  static constexpr size_t kCapacity = 5;  ///< How many networks it keeps.

  /// Reads the saved networks from NVS.
  void load();
  /// How many networks are saved.
  size_t size() const { return count_; }
  /// @brief A saved network, most recently added first.
  /// @param i Its index, below size().
  const WifiCredentials& operator[](size_t i) const { return networks_[i]; }
  /// @brief The saved network called `ssid`.
  /// @param ssid Its name.
  /// @return It, or nullptr if none is saved.
  const WifiCredentials* find(const char* ssid) const;

  /// @brief Adds a network, or updates it if it's already saved, and moves it
  /// to the front.
  ///
  /// When full, the oldest one is dropped. A saved network keeps its fixed
  /// address unless `network` brings one.
  /// @param network The network.
  void add(WifiCredentials network);
  /// @brief Sets a saved network's fixed address.
  /// @param ssid The network's name.
  /// @param address The address; FixedAddress() for DHCP.
  /// @return Whether such a network is saved.
  bool setAddress(const char* ssid, const FixedAddress& address);
  /// @brief Forgets a saved network.
  /// @param ssid Its name.
  /// @return Whether it was saved.
  bool forget(const char* ssid);
  /// Forgets every network.
  void clear();

 private:
  /// @brief Removes a network, without saving.
  /// @param ssid Its name.
  /// @return Whether it was there.
  bool remove(const char* ssid);
  /// Writes the networks to NVS, replacing what was there.
  void save() const;

  std::array<WifiCredentials, kCapacity> networks_{};  ///< Most recently added first.
  size_t count_ = 0;
};
