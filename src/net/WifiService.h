// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wi-Fi: joins the strongest saved network in range and keeps looking while
// none is, and on request runs a setup hotspot with a captive portal, where a
// phone picks the network to join. The monitor works the same without Wi-Fi,
// so all of this is best effort.
//
// Runs in the main loop; the web server reaches it under AppLock.
#pragma once

#include <DNSServer.h>
#include <WiFi.h>

#include <array>
#include <atomic>

#include "Settings.h"
#include "net/WifiStore.h"

enum class WifiState : uint8_t {
  Off,        // turned off in the settings, or no network saved
  Searching,  // no saved network in range; scans again now and then
  Connecting,
  Connected,
};

enum class SetupState : uint8_t {
  Off,
  Waiting,  // hotspot up, waiting for a phone to join
  Joined,   // a phone joined; the setup page should open on it
  Testing,  // trying the network the phone chose
  Succeeded,
  Failed,  // see setupMessage()
};

struct ScannedNetwork {
  char ssid[33];
  int8_t rssi;
  bool secure;
};

class WifiService {
 public:
  static constexpr size_t kMaxScanned = 20;

  void begin(Settings& settings);
  void loop(uint32_t nowMs);

  WifiState state() const { return state_; }
  // Whether the network stack has been started; the web server needs it.
  bool networkStarted() const { return networkStarted_; }
  // Network joined or being tried; empty otherwise.
  const char* ssid() const { return ssid_; }
  int8_t rssi() const;
  IPAddress ip() const;
  // The last address the router gave (0.0.0.0 if none since start): most give
  // it again, which helps find the monitor after going back from a fixed one.
  IPAddress routerIp() const { return routerIp_; }

  // A saved network that was in range but couldn't be joined (e.g. its password
  // changed), and why; empty when none was in range or one was joined.
  const char* failedSsid() const { return failedSsid_; }
  const char* failedReason() const { return failedReason_; }

  const WifiStore& saved() const { return store_; }
  // Saves a network without trying it first (web page, while connected).
  void save(const WifiCredentials& network);
  bool forget(const char* ssid);
  void forgetAll();
  // A fixed address for a saved network (FixedAddress() for DHCP). Rejoins it
  // if it's the network in use, so the new address applies at once.
  bool setAddress(const char* ssid, const FixedAddress& address);
  // Whether the network in use has a fixed address.
  bool fixedAddress() const;

  // Setup hotspot.
  void startSetup();
  void stopSetup();
  // Stops it in a moment, so the request asking for that can still be answered.
  void stopSetupSoon();
  bool setupActive() const { return setup_ != SetupState::Off; }
  SetupState setupState() const { return setup_; }
  const char* hotspotSsid() const { return hotspotSsid_; }
  const char* hotspotPassword() const { return hotspotPassword_; }
  IPAddress hotspotIp() const { return hotspotIp_; }
  // Network the phone chose.
  const char* trialSsid() const { return trial_.ssid; }
  // Why the last try failed.
  const char* setupMessage() const { return setupMessage_; }
  // Tries `network` and saves it if it works.
  void tryNetwork(const WifiCredentials& network);

  // Networks the last scan found, strongest first, one entry per name.
  void requestScan() { scanRequested_ = true; }
  bool scanning() const { return scanning_ || scanRequested_; }
  size_t scannedCount() const { return scannedCount_; }
  const ScannedNetwork& scanned(size_t i) const { return scanned_[i]; }

 private:
  struct Attempt {
    WifiCredentials network;
    int32_t channel = 0;  // 0 = any
    uint8_t bssid[6] = {};
    bool hasBssid = false;
  };

  void updateMode(uint32_t nowMs);
  void pollScan(uint32_t nowMs);
  void collectScan(int16_t count);
  void planAttempts(int16_t count);
  void connectNext(uint32_t nowMs);
  void pollConnecting(uint32_t nowMs);
  void onConnected(uint32_t nowMs);
  void onAttemptFailed(uint32_t nowMs);
  void searchLater(uint32_t nowMs);
  void rejoin(const char* ssid, uint32_t nowMs);
  void loopSetup(uint32_t nowMs);
  const char* failureReason() const;

  Settings* settings_ = nullptr;
  WifiStore store_;
  wifi_mode_t mode_ = WIFI_MODE_NULL;
  bool networkStarted_ = false;
  bool mdnsStarted_ = false;
  WifiState state_ = WifiState::Off;
  char ssid_[33] = "";
  bool staticIp_ = false;  // a fixed address is configured in the network stack
  IPAddress routerIp_;

  // Joining saved networks.
  std::array<Attempt, WifiStore::kCapacity> attempts_{};
  size_t attemptCount_ = 0;
  size_t nextAttempt_ = 0;
  uint32_t attemptStartMs_ = 0;
  uint32_t nextScanMs_ = 0;
  uint32_t retryDelayMs_ = 0;
  // Set by the Wi-Fi event task when a connection attempt fails.
  std::atomic<uint8_t> lastReason_{0};
  std::atomic<uint8_t> failures_{0};
  char failedSsid_[33] = "";
  const char* failedReason_ = "";

  // Scanning.
  bool scanning_ = false;
  bool scanRequested_ = false;
  bool scanToConnect_ = false;  // the scan was started to pick a network to join
  std::array<ScannedNetwork, kMaxScanned> scanned_{};
  size_t scannedCount_ = 0;

  // Setup hotspot.
  SetupState setup_ = SetupState::Off;
  char hotspotSsid_[24] = "";
  char hotspotPassword_[12] = "";
  IPAddress hotspotIp_;
  DNSServer dns_;
  WifiCredentials trial_;
  bool trialPending_ = false;
  bool testing_ = false;
  const char* setupMessage_ = "";
  uint32_t setupStartMs_ = 0;
  uint32_t setupActivityMs_ = 0;
  uint32_t setupDoneMs_ = 0;
  bool setupStopPending_ = false;
  uint32_t setupStopAtMs_ = 0;
  bool rejoinPending_ = false;  // with a new address, see setAddress()
  uint32_t rejoinAtMs_ = 0;
};
