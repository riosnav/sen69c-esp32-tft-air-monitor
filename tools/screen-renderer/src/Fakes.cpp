// SPDX-License-Identifier: GPL-3.0-or-later
//
// The firmware functions the UI calls, outside the UI itself: each shows the
// simulated device (`world`) instead of talking to hardware. Members the
// UI never calls are left undefined.

#include <cstring>

#include "Device.h"
#include "World.h"
#include "net/Ota.h"
#include "net/Web.h"
#include "ui/StatusLed.h"

namespace {

template <size_t N>
void copy(char (&dst)[N], const std::string& src) {
  strlcpy(dst, src.c_str(), N);
}

}  // namespace

namespace settings {

Settings load() { return world.settings; }
void save(const Settings&) {}
// Back to the defaults, with a new web password, as after the restart that
// follows.
void erase() {
  world.settings = Settings();
  strlcpy(world.settings.webPassword, "p3nx7rka", sizeof world.settings.webPassword);
}

}  // namespace settings

namespace device {

const char* shortId() {
  const size_t dash = world.hostname.rfind('-');
  return world.hostname.c_str() + (dash == std::string::npos ? 0 : dash + 1);
}
const char* hostname() { return world.hostname.c_str(); }
const char* macId() { return "dc5475c8a1b2"; }
void randomPassword(char* out, size_t length) {
  static const char kSample[] = "k4fz8wqemt7c";
  for (size_t i = 0; i < length; ++i) out[i] = kSample[i % (sizeof kSample - 1)];
  out[length] = '\0';
}

}  // namespace device

void SensorService::applyConfig(const SensorConfig&) {}

void SensorService::cleanFan() { world.startOperation("Cleaning fan", 12000, "Fan cleaned"); }

void SensorService::heatHumiditySensor() {
  world.startOperation("Heating sensor", 23000, "Humidity sensor heated");
}

void SensorService::resetCo2Calibration() {
  world.startOperation("Resetting CO2", 3000, "CO2 calibration reset");
}

void SensorService::restartVocLearning() {
  world.startOperation("Restarting VOC", 30000, "VOC learning restarted");
}

void SensorService::clearErrors() {
  world.sensor.deviceStatus = 0;
  world.showResult("Sensor errors cleared");
}

void SensorService::recalibrateCo2(uint16_t) {
  if (world.nowMs - world.sensor.measuringSinceMs < kRecalibrationWarmupMs) {
    world.showResult("Measure 3 min before calibrating");
  } else {
    world.startOperation("Calibrating CO2", 1500, "CO2 calibrated (-14 ppm)");
  }
}

void WifiStore::clear() {
  networks_ = {};
  count_ = 0;
}

void WifiStore::add(WifiCredentials network) {
  if (count_ < kCapacity) networks_[count_++] = network;
}

void WifiService::begin(Settings& settings) {
  settings_ = &settings;
  loop(0);
}

// Copies the simulated Wi-Fi into the members the UI reads.
void WifiService::loop(uint32_t) {
  const auto& w = world.wifi;
  store_.clear();
  for (const std::string& ssid : w.saved) {
    WifiCredentials network;
    copy(network.ssid, ssid);
    store_.add(network);
  }
  state_ = settings_->wifiEnabled && store_.size() > 0 ? w.state : WifiState::Off;
  copy(ssid_, state_ == WifiState::Connecting || state_ == WifiState::Connected ? w.ssid : "");
  copy(failedSsid_, w.failedSsid);
  failedReason_ = w.failedReason.c_str();
  setup_ = w.setup;
  copy(hotspotSsid_, w.hotspotSsid);
  copy(hotspotPassword_, w.hotspotPassword);
  hotspotIp_ = w.hotspotIp;
  copy(trial_.ssid, w.trialSsid);
  setupMessage_ = w.setupMessage.c_str();
}

int8_t WifiService::rssi() const { return world.wifi.rssi; }

IPAddress WifiService::ip() const {
  return state_ == WifiState::Connected ? world.wifi.ip : IPAddress();
}

bool WifiService::fixedAddress() const {
  return state_ == WifiState::Connected && world.wifi.fixedAddress;
}

void WifiService::startSetup() {
  world.wifi.setup = SetupState::Waiting;
  loop(world.nowMs);
}

void WifiService::stopSetup() {
  world.wifi.setup = SetupState::Off;
  loop(world.nowMs);
}

void WifiService::forgetAll() {
  world.wifi.saved.clear();
  world.wifi.failedSsid.clear();
  loop(world.nowMs);
}

void Ota::begin() {}

void Ota::loop(bool, bool, uint32_t nowMs) {
  auto& ota = world.ota;
  if (ota.state == OtaState::Confirming && static_cast<int32_t>(nowMs - ota.deadlineMs) >= 0) {
    ota.state = OtaState::TimedOut;
  }
  state_ = ota.state;
  deadlineMs_ = ota.deadlineMs;
}

void Ota::confirm() {
  if (world.ota.state == OtaState::Confirming) state_ = world.ota.state = OtaState::Accepted;
}

void Ota::refuse() {
  if (world.ota.state == OtaState::Confirming) state_ = world.ota.state = OtaState::Refused;
}

uint8_t Ota::percent() const { return world.ota.percent; }

uint32_t Ota::secondsLeft(uint32_t nowMs) const {
  if (state_ != OtaState::Confirming) return 0;
  const int32_t left = static_cast<int32_t>(deadlineMs_ - nowMs);
  return left > 0 ? (static_cast<uint32_t>(left) + 999) / 1000 : 0;
}

namespace web {

uint32_t failedLogins() { return world.failedLogins; }

}  // namespace web

MqttState MqttService::state() const { return world.mqtt.state; }

const char* MqttService::error() const {
  return world.mqtt.state == MqttState::Failed ? world.mqtt.error.c_str() : "";
}

void StatusLed::update(aq::Level, bool, bool, LedMode, uint32_t) {}
