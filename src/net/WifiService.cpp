// SPDX-License-Identifier: GPL-3.0-or-later

#include "WifiService.h"

#include <ESPmDNS.h>
#include <WifiChoice.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "Device.h"

namespace {

constexpr uint32_t kAttemptTimeoutMs = 20000;
constexpr uint32_t kFirstRetryMs = 15000;  // rescan delay when no saved network is in range...
constexpr uint32_t kMaxRetryMs = 5 * 60000;  // ...doubling up to this
constexpr uint32_t kSetupIdleMs = 10 * 60000;  // the hotspot closes when nobody uses it
constexpr uint32_t kSetupMaxMs = 30 * 60000;
constexpr uint32_t kSetupDoneMs = 2 * 60000;  // after success, so the phone can show the result
constexpr uint32_t kSetupStopDelayMs = 1500;
constexpr size_t kHotspotPasswordLength = 8;

bool due(uint32_t nowMs, uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }

// From FixedAddress's form, first part in the top byte.
IPAddress ipAddress(uint32_t a) { return IPAddress(a >> 24, a >> 16 & 0xFF, a >> 8 & 0xFF, a & 0xFF); }

// AUTH_EXPIRE is also what a router sends when it rejects the key: a wrong
// password during setup (hotspot up) came back as that rather than as a
// handshake timeout.
bool isPasswordFailure(uint8_t reason) {
  return reason == WIFI_REASON_MIC_FAILURE || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_AUTH_EXPIRE;
}

bool isNotFound(uint8_t reason) {
  return reason == WIFI_REASON_NO_AP_FOUND ||
         reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
         reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD ||
         reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
}

const wifi_ap_record_t* scanRecord(int i) {
  return static_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
}

}  // namespace

void WifiService::begin(Settings& settings) {
  settings_ = &settings;
  store_.load();
  WiFi.persistent(false);  // networks live in WifiStore, not in the driver's flash
  WiFi.setHostname(device::hostname());
  WiFi.setAutoReconnect(false);  // reconnection is handled here
  WiFi.onEvent(
      [this](arduino_event_id_t, arduino_event_info_t info) {
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        if (reason == WIFI_REASON_ASSOC_LEAVE) return;  // we disconnected on purpose
        lastReason_ = reason;
        ++failures_;
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t) {
        Serial.println("Wi-Fi: a device joined the setup hotspot");
      },
      ARDUINO_EVENT_WIFI_AP_STACONNECTED);
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t) {
        Serial.println("Wi-Fi: a device left the setup hotspot");
      },
      ARDUINO_EVENT_WIFI_AP_STADISCONNECTED);
}

void WifiService::loop(uint32_t nowMs) {
  if (setupStopPending_ && due(nowMs, setupStopAtMs_)) stopSetup();
  updateMode(nowMs);
  if (mode_ == WIFI_MODE_NULL) return;
  pollScan(nowMs);
  if (setupActive()) loopSetup(nowMs);

  switch (state_) {
    case WifiState::Searching:
      // While the hotspot is up, only the phone decides what to try.
      if (!setupActive() && !scanning() && due(nowMs, nextScanMs_)) {
        scanRequested_ = true;
        scanToConnect_ = true;
      }
      break;
    case WifiState::Connecting:
      pollConnecting(nowMs);
      break;
    case WifiState::Connected:
      if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("Wi-Fi: lost %s\n", ssid_);
        rejoin(ssid_, nowMs);  // the same network first, then scan
      }
      break;
    case WifiState::Off:
      break;
  }
}

void WifiService::updateMode(uint32_t nowMs) {
  wifi_mode_t want = WIFI_MODE_NULL;
  if (setupActive()) {
    want = WIFI_MODE_APSTA;
  } else if (settings_->wifiEnabled && store_.size() > 0) {
    want = WIFI_MODE_STA;
  }
  if (want == mode_) return;
  mode_ = want;
  if (want == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_OFF);
    state_ = WifiState::Off;
    ssid_[0] = '\0';
    scanning_ = scanRequested_ = scanToConnect_ = false;
    Serial.println("Wi-Fi: off");
    return;
  }
  WiFi.mode(want);
  networkStarted_ = true;
  if (state_ == WifiState::Off) {
    state_ = WifiState::Searching;
    nextScanMs_ = nowMs;
    retryDelayMs_ = kFirstRetryMs;
  }
}

void WifiService::pollScan(uint32_t nowMs) {
  if (!scanning_) {
    // A scan would disturb a connection attempt, so it waits for the result.
    if (!scanRequested_ || state_ == WifiState::Connecting) return;
    scanRequested_ = false;
    scanning_ = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;
    if (!scanning_ && scanToConnect_) {
      scanToConnect_ = false;
      searchLater(nowMs);
    }
    return;
  }
  const int16_t count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return;
  scanning_ = false;
  collectScan(count);
  if (scanToConnect_) {
    scanToConnect_ = false;
    planAttempts(count);
    connectNext(nowMs);
  }
  WiFi.scanDelete();
}

void WifiService::collectScan(int16_t count) {
  scannedCount_ = 0;
  for (int i = 0; i < count; ++i) {
    const wifi_ap_record_t* ap = scanRecord(i);
    if (ap == nullptr || ap->ssid[0] == 0) continue;  // hidden network
    const char* name = reinterpret_cast<const char*>(ap->ssid);
    const auto end = scanned_.begin() + scannedCount_;
    const auto same = std::find_if(scanned_.begin(), end, [name](const ScannedNetwork& s) {
      return strcmp(s.ssid, name) == 0;
    });
    if (same != end) {
      same->rssi = std::max(same->rssi, ap->rssi);
      continue;
    }
    if (scannedCount_ == kMaxScanned) continue;
    ScannedNetwork& s = scanned_[scannedCount_++];
    strlcpy(s.ssid, name, sizeof s.ssid);
    s.rssi = ap->rssi;
    s.secure = ap->authmode != WIFI_AUTH_OPEN;
  }
  std::sort(scanned_.begin(), scanned_.begin() + scannedCount_,
            [](const ScannedNetwork& a, const ScannedNetwork& b) { return a.rssi > b.rssi; });
}

void WifiService::planAttempts(int16_t count) {
  failedSsid_[0] = '\0';  // a new round
  net::SavedNetwork saved[WifiStore::kCapacity];
  for (size_t i = 0; i < store_.size(); ++i) saved[i] = {store_[i].ssid, store_[i].hidden};
  std::vector<net::ScanEntry> entries;
  for (int i = 0; i < count; ++i) {
    const wifi_ap_record_t* ap = scanRecord(i);
    entries.push_back({ap ? reinterpret_cast<const char*>(ap->ssid) : "", ap ? ap->rssi : -128});
  }
  net::Candidate candidates[WifiStore::kCapacity];
  attemptCount_ = net::chooseNetworks(saved, store_.size(), entries.data(), entries.size(),
                                      candidates);
  nextAttempt_ = 0;
  for (size_t i = 0; i < attemptCount_; ++i) {
    Attempt& a = attempts_[i];
    a = Attempt();
    a.network = store_[candidates[i].saved];
    if (candidates[i].scanIndex >= 0) {
      // Join the access point that was heard best (meshes have several).
      const wifi_ap_record_t* ap = scanRecord(candidates[i].scanIndex);
      a.channel = ap->primary;
      memcpy(a.bssid, ap->bssid, sizeof a.bssid);
      a.hasBssid = true;
    }
  }
}

void WifiService::connectNext(uint32_t nowMs) {
  if (nextAttempt_ >= attemptCount_) {
    searchLater(nowMs);
    return;
  }
  const Attempt& a = attempts_[nextAttempt_++];
  strlcpy(ssid_, a.network.ssid, sizeof ssid_);
  Serial.printf("Wi-Fi: connecting to %s\n", ssid_);
  WiFi.disconnect();
  failures_ = 0;
  lastReason_ = 0;
  const FixedAddress& f = a.network.address;
  if (f.set()) {
    WiFi.config(ipAddress(f.ip), ipAddress(f.gateway), ipAddress(f.subnet),
                ipAddress(f.dns != 0 ? f.dns : f.gateway));
    staticIp_ = true;
  } else if (staticIp_) {
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // back to DHCP
    staticIp_ = false;
  }
  WiFi.begin(a.network.ssid, a.network.password, a.channel, a.hasBssid ? a.bssid : nullptr);
  attemptStartMs_ = nowMs;
  state_ = WifiState::Connecting;
}

void WifiService::pollConnecting(uint32_t nowMs) {
  if (WiFi.status() == WL_CONNECTED) {
    onConnected(nowMs);
    return;
  }
  // A wrong password or a missing network won't fix itself; anything else
  // gets a second chance.
  const uint8_t failures = failures_;
  const uint8_t reason = lastReason_;
  const bool definite = isPasswordFailure(reason) || isNotFound(reason);
  if (failures >= 2 || (failures >= 1 && definite) ||
      nowMs - attemptStartMs_ >= kAttemptTimeoutMs) {
    onAttemptFailed(nowMs);
  }
}

void WifiService::onConnected(uint32_t nowMs) {
  state_ = WifiState::Connected;
  retryDelayMs_ = kFirstRetryMs;
  failedSsid_[0] = '\0';
  Serial.printf("Wi-Fi: connected to %s, %s\n", ssid_, WiFi.localIP().toString().c_str());
  if (!mdnsStarted_ && MDNS.begin(device::hostname())) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted_ = true;
  }
  if (testing_) {
    testing_ = false;
    store_.add(trial_);
    setup_ = SetupState::Succeeded;
    setupDoneMs_ = nowMs;
  }
}

void WifiService::onAttemptFailed(uint32_t nowMs) {
  Serial.printf("Wi-Fi: could not join %s (reason %u)\n", ssid_,
                static_cast<unsigned>(lastReason_.load()));
  WiFi.disconnect();
  if (testing_) {
    testing_ = false;
    setup_ = SetupState::Failed;
    setupMessage_ = failureReason();
    state_ = WifiState::Searching;  // resumes when the hotspot closes
    ssid_[0] = '\0';
    return;
  }
  strlcpy(failedSsid_, ssid_, sizeof failedSsid_);
  failedReason_ = failureReason();
  connectNext(nowMs);
}

// Tries the saved network `ssid` alone, then scans if that fails.
void WifiService::rejoin(const char* ssid, uint32_t nowMs) {
  const WifiCredentials* network = store_.find(ssid);
  if (network == nullptr) {
    searchLater(nowMs);
    return;
  }
  attempts_[0] = Attempt();
  attempts_[0].network = *network;
  attemptCount_ = 1;
  nextAttempt_ = 0;
  connectNext(nowMs);
}

void WifiService::searchLater(uint32_t nowMs) {
  state_ = WifiState::Searching;
  ssid_[0] = '\0';
  nextScanMs_ = nowMs + retryDelayMs_;
  retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryMs);
}

const char* WifiService::failureReason() const {
  const uint8_t reason = lastReason_;
  if (isPasswordFailure(reason)) return "Wrong password?";
  if (isNotFound(reason)) return "Network not found";
  return "No answer";
}

int8_t WifiService::rssi() const { return state_ == WifiState::Connected ? WiFi.RSSI() : 0; }

IPAddress WifiService::ip() const {
  return state_ == WifiState::Connected ? WiFi.localIP() : IPAddress();
}

void WifiService::save(const WifiCredentials& network) {
  store_.add(network);
  if (state_ == WifiState::Searching && !setupActive()) nextScanMs_ = millis();  // try it now
}

bool WifiService::forget(const char* ssid) {
  if (!store_.forget(ssid)) return false;
  if (strcmp(ssid, failedSsid_) == 0) failedSsid_[0] = '\0';
  if (state_ != WifiState::Off && strcmp(ssid, ssid_) == 0) {
    WiFi.disconnect();
    searchLater(millis());
  }
  return true;
}

bool WifiService::setAddress(const char* ssid, const FixedAddress& address) {
  if (!store_.setAddress(ssid, address)) return false;
  const bool inUse = state_ == WifiState::Connected || state_ == WifiState::Connecting;
  if (inUse && !setupActive() && strcmp(ssid, ssid_) == 0) {
    Serial.printf("Wi-Fi: new address for %s; rejoining\n", ssid_);
    rejoin(ssid_, millis());
  }
  return true;
}

bool WifiService::fixedAddress() const {
  if (state_ != WifiState::Connected) return false;
  const WifiCredentials* network = store_.find(ssid_);
  return network != nullptr && network->address.set();
}

void WifiService::forgetAll() {
  store_.clear();
  failedSsid_[0] = '\0';
  if (state_ == WifiState::Connected || state_ == WifiState::Connecting) {
    WiFi.disconnect();
    searchLater(millis());
  }
}

void WifiService::startSetup() {
  if (setupActive()) return;
  if (!settings_->wifiEnabled) {
    settings_->wifiEnabled = true;
    settings::save(*settings_);
  }
  if (state_ == WifiState::Connecting) {
    // From now on the phone decides what to try.
    WiFi.disconnect();
    state_ = WifiState::Searching;
    ssid_[0] = '\0';
  }
  const uint32_t now = millis();
  setup_ = SetupState::Waiting;
  setupMessage_ = "";
  setupStartMs_ = setupActivityMs_ = now;
  updateMode(now);  // hotspot and station

  snprintf(hotspotSsid_, sizeof hotspotSsid_, "AirMonitor-%s", device::shortId());
  for (char* c = hotspotSsid_ + strlen("AirMonitor-"); *c != '\0'; ++c) *c = toupper(*c);
  device::randomPassword(hotspotPassword_, kHotspotPasswordLength);
  WiFi.softAP(hotspotSsid_, hotspotPassword_);
  // Every DNS name resolves to us, so the phone's connectivity check reaches
  // the web server, which redirects it to the setup page. (No DHCP option
  // 114: phones expect it to name a JSON API, and an iPhone given the page
  // instead decided the hotspot had no internet and left it.)
  hotspotIp_ = WiFi.softAPIP();
  dns_.start(53, "*", hotspotIp_);
  scanRequested_ = true;  // for the list of networks on the setup page
  Serial.printf("Wi-Fi: setup hotspot %s started\n", hotspotSsid_);
}

void WifiService::stopSetup() {
  setupStopPending_ = false;
  if (!setupActive()) return;
  dns_.stop();
  if (testing_) {
    testing_ = false;
    WiFi.disconnect();
    state_ = WifiState::Searching;
    ssid_[0] = '\0';
  }
  trialPending_ = false;
  setup_ = SetupState::Off;
  const uint32_t now = millis();
  updateMode(now);  // station only, or off
  if (state_ == WifiState::Searching) {
    nextScanMs_ = now;
    retryDelayMs_ = kFirstRetryMs;
  }
  Serial.println("Wi-Fi: setup hotspot closed");
}

void WifiService::stopSetupSoon() {
  setupStopPending_ = true;
  setupStopAtMs_ = millis() + kSetupStopDelayMs;
}

void WifiService::tryNetwork(const WifiCredentials& network) {
  if (!setupActive()) return;
  trial_ = network;
  // Joins with the fixed address the network already has, if any.
  if (const WifiCredentials* saved = store_.find(network.ssid)) trial_.address = saved->address;
  trialPending_ = true;
  setup_ = SetupState::Testing;
  setupMessage_ = "";
}

void WifiService::loopSetup(uint32_t nowMs) {
  if (WiFi.softAPgetStationNum() > 0) {
    setupActivityMs_ = nowMs;
    if (setup_ == SetupState::Waiting) setup_ = SetupState::Joined;
  }
  if (trialPending_ && !scanning_) {
    trialPending_ = false;
    testing_ = true;
    attempts_[0] = Attempt();
    attempts_[0].network = trial_;
    attemptCount_ = 1;
    nextAttempt_ = 0;
    connectNext(nowMs);
  }
  const bool idle = nowMs - setupActivityMs_ >= kSetupIdleMs;
  const bool tooLong = nowMs - setupStartMs_ >= kSetupMaxMs;
  const bool done = setup_ == SetupState::Succeeded && nowMs - setupDoneMs_ >= kSetupDoneMs;
  if (idle || tooLong || done) stopSetup();
}
