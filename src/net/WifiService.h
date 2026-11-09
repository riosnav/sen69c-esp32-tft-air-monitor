// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Wi-Fi: joins the strongest saved network in range, and on request
/// runs a setup hotspot with a captive portal.
///
/// It keeps looking while no saved network is in range. On the setup hotspot,
/// a phone picks the network to join. The monitor works the same without
/// Wi-Fi, so all of this is best effort.
///
/// Runs in the main loop; the web server reaches it under AppLock.
#pragma once

#include <DNSServer.h>
#include <WiFi.h>

#include <array>
#include <atomic>

#include "Settings.h"
#include "net/WifiStore.h"

/// Where joining a saved network is.
enum class WifiState : uint8_t {
  Off,         ///< Turned off in the settings, or no network saved.
  Searching,   ///< No saved network in range; scans again now and then.
  Connecting,  ///< Trying a network.
  Connected,   ///< Joined one.
};

/// Where the setup hotspot is.
enum class SetupState : uint8_t {
  Off,        ///< No hotspot.
  Waiting,    ///< Hotspot up, waiting for a phone to join.
  Joined,     ///< A phone joined; the setup page should open on it.
  Testing,    ///< Trying the network the phone chose.
  Succeeded,  ///< Joined and saved it; the hotspot closes 2 minutes later.
  Failed,     ///< See setupMessage().
};

/// A network a scan found.
struct ScannedNetwork {
  char ssid[33];  ///< Its name.
  int8_t rssi;    ///< The strongest signal heard from it, in dBm.
  bool secure;    ///< Whether it needs a password.
};

/// @brief Wi-Fi: joins the strongest saved network in range, and runs the
/// setup hotspot on request.
class WifiService {
 public:
  static constexpr size_t kMaxScanned = 20;  ///< The most networks a scan lists.

  /// @brief Loads the saved networks and sets up the Wi-Fi driver; Wi-Fi
  /// starts in loop().
  /// @param settings The settings, kept: Wi-Fi follows their `wifiEnabled`,
  ///   which startSetup() turns on.
  void begin(Settings& settings);
  /// @brief Main loop: turns Wi-Fi on or off as needed, scans, joins and
  /// rejoins networks, and runs the setup hotspot.
  /// @param nowMs The time, as millis().
  void loop(uint32_t nowMs);

  /// Where joining a saved network is.
  WifiState state() const { return state_; }
  /// Whether the network stack has been started; the web server needs it.
  bool networkStarted() const { return networkStarted_; }
  /// The network joined or being tried; empty otherwise.
  const char* ssid() const { return ssid_; }
  /// The signal of the network joined, in dBm; 0 when not connected.
  int8_t rssi() const;
  /// The monitor's address on the network joined; 0.0.0.0 when not connected.
  IPAddress ip() const;
  /// @brief The last address the router gave (0.0.0.0 if none since start).
  ///
  /// Most give it again, which helps find the monitor after going back from a
  /// fixed one.
  IPAddress routerIp() const { return routerIp_; }

  /// @brief A saved network that was tried but couldn't be joined (e.g. its
  /// password changed); empty when none was tried or one was joined.
  const char* failedSsid() const { return failedSsid_; }
  /// Why failedSsid() couldn't be joined.
  const char* failedReason() const { return failedReason_; }

  /// The saved networks.
  const WifiStore& saved() const { return store_; }
  /// @brief Saves a network without trying it first (web page, while
  /// connected).
  ///
  /// While searching, it's looked for at once.
  /// @param network The network.
  void save(const WifiCredentials& network);
  /// @brief Forgets a saved network, and leaves it if it's the one in use.
  /// @param ssid Its name.
  /// @return Whether it was saved.
  bool forget(const char* ssid);
  /// Forgets every saved network, and leaves the one in use.
  void forgetAll();
  /// @brief Sets a fixed address for a saved network.
  ///
  /// Rejoins it if it's the network in use, so the new address applies at
  /// once.
  /// @param ssid The network's name.
  /// @param address The address; FixedAddress() for DHCP.
  /// @return Whether such a network is saved.
  bool setAddress(const char* ssid, const FixedAddress& address);
  /// Whether the network joined has a fixed address.
  bool fixedAddress() const;

  // Setup hotspot.
  /// @brief Starts the setup hotspot, named `AirMonitor-` and the device's
  /// short ID, with a new random password.
  ///
  /// Turns Wi-Fi on in the settings if it was off. While the hotspot is up,
  /// only the phone decides which network to try, so an attempt in progress is
  /// dropped; a network already joined stays.
  void startSetup();
  /// @brief Closes the setup hotspot, and drops a network still being tried
  /// from it; the search for saved networks starts again.
  void stopSetup();
  /// @brief Closes the setup hotspot in a moment, so the request asking for
  /// that can still be answered.
  void stopSetupSoon();
  /// Whether the setup hotspot is up.
  bool setupActive() const { return setup_ != SetupState::Off; }
  /// Where the setup hotspot is.
  SetupState setupState() const { return setup_; }
  /// The setup hotspot's name.
  const char* hotspotSsid() const { return hotspotSsid_; }
  /// The setup hotspot's password.
  const char* hotspotPassword() const { return hotspotPassword_; }
  /// The monitor's address on the setup hotspot.
  IPAddress hotspotIp() const { return hotspotIp_; }
  /// The network the phone chose.
  const char* trialSsid() const { return trial_.ssid; }
  /// Why the last try from the setup page failed; empty otherwise.
  const char* setupMessage() const { return setupMessage_; }
  /// @brief Tries `network` and saves it if it works; only while the setup
  /// hotspot is up.
  ///
  /// Joins with the fixed address the network already has, if it's saved
  /// with one.
  /// @param network The network the phone chose.
  void tryNetwork(const WifiCredentials& network);

  /// Asks for a scan; it starts once no network is being tried.
  void requestScan() { scanRequested_ = true; }
  /// Whether a scan is running or asked for.
  bool scanning() const { return scanning_ || scanRequested_; }
  /// How many networks the last scan found (see scanned()).
  size_t scannedCount() const { return scannedCount_; }
  /// @brief A network the last scan found: strongest first, one entry per
  /// name; hidden networks aren't listed.
  /// @param i Its index, below scannedCount().
  const ScannedNetwork& scanned(size_t i) const { return scanned_[i]; }

 private:
  /// A saved network to try, and where.
  struct Attempt {
    WifiCredentials network;  ///< The network.
    int32_t channel = 0;      ///< Its channel; 0 = any.
    uint8_t bssid[6] = {};    ///< The access point to join, if hasBssid.
    bool hasBssid = false;    ///< Whether to join that one rather than any.
  };

  /// @brief Turns the station and the hotspot on or off, as the settings and
  /// the setup ask.
  /// @param nowMs The time, as millis().
  void updateMode(uint32_t nowMs);
  /// @brief Starts a scan that was asked for, or collects one that has
  /// finished; after one started to pick a network, tries the saved ones.
  /// @param nowMs The time, as millis().
  void pollScan(uint32_t nowMs);
  /// @brief Lists the networks a finished scan found, for scanned().
  /// @param count How many it found; negative if it failed.
  void collectScan(int16_t count);
  /// @brief Plans which saved networks to try after a scan, and in which
  /// order (see net::chooseNetworks()).
  /// @param count How many networks the scan found.
  void planAttempts(int16_t count);
  /// @brief Tries the next planned network; with none left, searches again
  /// later.
  /// @param nowMs The time, as millis().
  void connectNext(uint32_t nowMs);
  /// @brief Checks on the network being tried: joined, or failed.
  ///
  /// A wrong password or a missing network fails at once; anything else gets
  /// a second chance, within 20 s.
  /// @param nowMs The time, as millis().
  void pollConnecting(uint32_t nowMs);
  /// @brief Joined: starts mDNS the first time, and saves a network tried
  /// from the setup page.
  /// @param nowMs The time, as millis().
  void onConnected(uint32_t nowMs);
  /// @brief A try failed: during setup, says why; otherwise notes it for
  /// failedSsid() and tries the next network.
  /// @param nowMs The time, as millis().
  void onAttemptFailed(uint32_t nowMs);
  /// @brief No network to join: scans again later, waiting twice as long each
  /// time, from 15 s up to 5 minutes.
  /// @param nowMs The time, as millis().
  void searchLater(uint32_t nowMs);
  /// @brief Tries the saved network `ssid` alone, then searches again later
  /// if that fails.
  /// @param ssid The network's name.
  /// @param nowMs The time, as millis().
  void rejoin(const char* ssid, uint32_t nowMs);
  /// @brief Runs the setup hotspot: notices a phone, tries the network it
  /// chose, and closes the hotspot when it's no longer needed.
  ///
  /// It closes after 10 minutes without a phone on it, 30 minutes in all, or
  /// 2 minutes after success.
  /// @param nowMs The time, as millis().
  void loopSetup(uint32_t nowMs);
  /// @brief Why the last try failed, for people: "Wrong password?",
  /// "Network not found" or "No answer".
  const char* failureReason() const;

  Settings* settings_ = nullptr;
  WifiStore store_;
  wifi_mode_t mode_ = WIFI_MODE_NULL;  ///< The mode the driver was set to.
  bool networkStarted_ = false;
  bool mdnsStarted_ = false;
  WifiState state_ = WifiState::Off;
  char ssid_[33] = "";     ///< See ssid().
  bool staticIp_ = false;  ///< A fixed address is configured in the network stack.
  IPAddress routerIp_;     ///< See routerIp().

  // Joining saved networks.
  std::array<Attempt, WifiStore::kCapacity> attempts_{};  ///< The networks to try, in order.
  size_t attemptCount_ = 0;
  size_t nextAttempt_ = 0;     ///< The next one to try, in attempts_.
  uint32_t attemptStartMs_ = 0;  ///< When the current try started, as millis().
  uint32_t nextScanMs_ = 0;    ///< When to scan again while searching, as millis().
  uint32_t retryDelayMs_ = 0;  ///< The wait before the next search after this one, in ms.
  // Set by the Wi-Fi event task when a connection attempt fails.
  std::atomic<uint8_t> lastReason_{0};  ///< Why, a `WIFI_REASON_` code.
  std::atomic<uint8_t> failures_{0};    ///< How often the current try has failed.
  char failedSsid_[33] = "";            ///< See failedSsid().
  const char* failedReason_ = "";       ///< See failedReason().

  // Scanning.
  bool scanning_ = false;       ///< A scan is running.
  bool scanRequested_ = false;  ///< A scan was asked for; see requestScan().
  bool scanToConnect_ = false;  ///< The scan was started to pick a network to join.
  std::array<ScannedNetwork, kMaxScanned> scanned_{};  ///< See scanned().
  size_t scannedCount_ = 0;

  // Setup hotspot.
  SetupState setup_ = SetupState::Off;
  char hotspotSsid_[24] = "";
  char hotspotPassword_[12] = "";
  IPAddress hotspotIp_;
  DNSServer dns_;  ///< Answers every name with the hotspot's address (captive portal).
  WifiCredentials trial_;        ///< The network the phone chose.
  bool trialPending_ = false;    ///< trial_ waits to be tried, after any scan.
  bool testing_ = false;         ///< trial_ is being tried.
  const char* setupMessage_ = "";  ///< See setupMessage().
  uint32_t setupStartMs_ = 0;      ///< When the hotspot started, as millis().
  uint32_t setupActivityMs_ = 0;   ///< When a phone was last on it, as millis().
  uint32_t setupDoneMs_ = 0;       ///< When setup succeeded, as millis().
  bool setupStopPending_ = false;  ///< See stopSetupSoon().
  uint32_t setupStopAtMs_ = 0;     ///< When to stop it, as millis().
  bool rejoinPending_ = false;  ///< To rejoin with a new address; see setAddress().
  uint32_t rejoinAtMs_ = 0;     ///< When to rejoin, as millis().
};
