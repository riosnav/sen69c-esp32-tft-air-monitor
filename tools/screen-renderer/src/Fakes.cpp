// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-ins for the firmware functions the UI calls, outside the UI
/// itself: each shows the simulated device (`world`) instead of talking to
/// hardware.
///
/// Members the UI never calls are left undefined, so the link fails, naming
/// them, when the UI starts using one.

#include <cstring>

#include "Device.h"
#include "World.h"
#include "net/Ota.h"
#include "net/Web.h"
#include "ui/StatusLed.h"

namespace {

/// @brief Copies text into a char array, cut short to fit.
/// @tparam N The array's size.
/// @param[out] dst The array.
/// @param src The text.
template <size_t N>
void copy(char (&dst)[N], const std::string& src) {
  strlcpy(dst, src.c_str(), N);
}

}  // namespace

namespace settings {

/// Stand-in for settings::load(): World::settings.
Settings load() { return world.settings; }
/// Stand-in for settings::save(): nothing to save, as the UI changes
/// World::settings in place.
void save(const Settings&) {}
/// Stand-in for settings::erase(): back to the defaults, with a new web
/// password, as after the restart that follows.
void erase() {
  world.settings = Settings();
  strlcpy(world.settings.webPassword, "p3nx7rka", sizeof world.settings.webPassword);
}

}  // namespace settings

namespace device {

/// Stand-in for device::shortId(): what follows the last dash in
/// World::hostname.
const char* shortId() {
  const size_t dash = world.hostname.rfind('-');
  return world.hostname.c_str() + (dash == std::string::npos ? 0 : dash + 1);
}
/// Stand-in for device::hostname(): World::hostname.
const char* hostname() { return world.hostname.c_str(); }
/// Stand-in for device::macId(): a made-up MAC address.
const char* macId() { return "dc5475c8a1b2"; }
/// @brief Stand-in for device::randomPassword(): always the same characters,
/// so the screens come out the same on every run.
/// @param[out] out Where to write them: `length` + 1 bytes.
/// @param length How many characters.
void randomPassword(char* out, size_t length) {
  static const char kSample[] = "k4fz8wqemt7c";
  for (size_t i = 0; i < length; ++i) out[i] = kSample[i % (sizeof kSample - 1)];
  out[length] = '\0';
}

}  // namespace device

// Doxygen skips the stand-ins for class members: the classes are documented
// with the firmware, outside this documentation, so it can't place them.
/// @cond

/// Stand-in: the settings make no difference to the simulated sensor.
void SensorService::applyConfig(const SensorConfig&) {}

/// Stand-in: the sensor is busy cleaning the fan for 12 s.
void SensorService::cleanFan() { world.startOperation("Cleaning fan", 12000, "Fan cleaned"); }

/// Stand-in: the sensor is busy heating for 23 s.
void SensorService::heatHumiditySensor() {
  world.startOperation("Heating sensor", 23000, "Humidity sensor heated");
}

/// Stand-in: the sensor is busy for 3 s.
void SensorService::resetCo2Calibration() {
  world.startOperation("Resetting CO2", 3000, "CO2 calibration reset");
}

/// Stand-in: the sensor is busy restarting for 30 s.
void SensorService::restartVocLearning() {
  world.startOperation("Restarting VOC", 30000, "VOC learning restarted");
}

/// Stand-in: clears the device status register in World::sensor.
void SensorService::clearErrors() {
  world.sensor.deviceStatus = 0;
  world.showResult("Sensor errors cleared");
}

/// @brief Stand-in: refuses, as the firmware does, until the sensor has
/// measured for kRecalibrationWarmupMs; then the sensor is busy for 1.5 s and
/// reports a made-up correction.
void SensorService::recalibrateCo2(uint16_t) {
  if (world.nowMs - world.sensor.measuringSinceMs < kRecalibrationWarmupMs) {
    world.showResult("Measure 3 min before calibrating");
  } else {
    world.startOperation("Calibrating CO2", 1500, "CO2 calibrated (-14 ppm)");
  }
}

/// Stand-in: forgets every network, in memory only.
void WifiStore::clear() {
  networks_ = {};
  count_ = 0;
}

/// @brief Stand-in: adds the network at the end, in memory only, while there's
/// room. The order is World::wifi's.
void WifiStore::add(WifiCredentials network) {
  if (count_ < kCapacity) networks_[count_++] = network;
}

/// Stand-in: takes the settings and shows the simulated Wi-Fi (see loop()).
void WifiService::begin(Settings& settings) {
  settings_ = &settings;
  loop(0);
}

/// @brief Stand-in: copies the simulated Wi-Fi (World::wifi) into the members
/// the UI reads.
///
/// The Wi-Fi is off while it's switched off in the settings or no network is
/// saved, as on the device.
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

/// Stand-in: World::wifi's signal strength.
int8_t WifiService::rssi() const { return world.wifi.rssi; }

/// Stand-in: World::wifi's address while connected; 0.0.0.0 otherwise.
IPAddress WifiService::ip() const {
  return state_ == WifiState::Connected ? world.wifi.ip : IPAddress();
}

/// Stand-in: whether World::wifi has a fixed address, while connected.
bool WifiService::fixedAddress() const {
  return state_ == WifiState::Connected && world.wifi.fixedAddress;
}

/// Stand-in: the setup hotspot is up, waiting for a phone.
void WifiService::startSetup() {
  world.wifi.setup = SetupState::Waiting;
  loop(world.nowMs);
}

/// Stand-in: the setup ends at once.
void WifiService::stopSetup() {
  world.wifi.setup = SetupState::Off;
  loop(world.nowMs);
}

/// Stand-in: forgets the saved networks, and the one that couldn't be joined.
void WifiService::forgetAll() {
  world.wifi.saved.clear();
  world.wifi.failedSsid.clear();
  loop(world.nowMs);
}

/// Stand-in: there's nothing to set up.
void Ota::begin() {}

/// Stand-in: shows World::ota, whose time to confirm runs out as on the
/// device.
void Ota::loop(bool, bool, uint32_t nowMs) {
  auto& ota = world.ota;
  if (ota.state == OtaState::Confirming && static_cast<int32_t>(nowMs - ota.deadlineMs) >= 0) {
    ota.state = OtaState::TimedOut;
  }
  state_ = ota.state;
  deadlineMs_ = ota.deadlineMs;
}

/// Stand-in: accepts the update waiting for confirmation.
void Ota::confirm() {
  if (world.ota.state == OtaState::Confirming) state_ = world.ota.state = OtaState::Accepted;
}

/// Stand-in: refuses the update waiting for confirmation.
void Ota::refuse() {
  if (world.ota.state == OtaState::Confirming) state_ = world.ota.state = OtaState::Refused;
}

/// Stand-in: World::ota's progress.
uint8_t Ota::percent() const { return world.ota.percent; }

/// Stand-in: the seconds left to confirm, rounded up.
uint32_t Ota::secondsLeft(uint32_t nowMs) const {
  if (state_ != OtaState::Confirming) return 0;
  const int32_t left = static_cast<int32_t>(deadlineMs_ - nowMs);
  return left > 0 ? (static_cast<uint32_t>(left) + 999) / 1000 : 0;
}

/// @endcond

namespace web {

/// Stand-in for web::failedLogins(): World::failedLogins.
uint32_t failedLogins() { return world.failedLogins; }

}  // namespace web

// Doxygen skips these too; see above.
/// @cond

/// Stand-in: World::mqtt's state.
MqttState MqttService::state() const { return world.mqtt.state; }

/// Stand-in: World::mqtt's error while it has failed; "" otherwise.
const char* MqttService::error() const {
  return world.mqtt.state == MqttState::Failed ? world.mqtt.error.c_str() : "";
}

/// Stand-in: the renderer shows no status LED.
void StatusLed::update(aq::Level, bool, bool, LedMode, uint32_t) {}

/// @endcond
