// SPDX-License-Identifier: GPL-3.0-or-later

#include "Settings.h"

#include <Preferences.h>

#include "Device.h"

namespace settings {

namespace {
constexpr const char* kNamespace = "settings";
constexpr size_t kWebPasswordLength = 8;
}  // namespace

Settings load() {
  Settings s;
  Preferences p;
  bool hasPassword = false;
  if (p.begin(kNamespace, true)) {
    s.brightness = p.getUChar("bright", s.brightness);
    s.dimAfterS = p.getUShort("dim", s.dimAfterS);
    s.screenOffWhenIdle = p.getBool("idleOff", s.screenOffWhenIdle);
    s.fahrenheit = p.getBool("degF", s.fahrenheit);
    s.led = static_cast<LedMode>(p.getUChar("led", static_cast<uint8_t>(s.led)));
    s.tempOffsetC = p.getFloat("tOffset", s.tempOffsetC);
    s.altitudeM = p.getUShort("alt", s.altitudeM);
    s.co2SelfCalibration = p.getBool("co2Asc", s.co2SelfCalibration);
    s.batteryInstalled = p.getBool("battery", s.batteryInstalled);
    s.wifiEnabled = p.getBool("wifi", s.wifiEnabled);
    if (p.isKey("name")) p.getString("name", s.name, sizeof s.name);
    hasPassword = p.isKey("webPass");
    if (hasPassword) p.getString("webPass", s.webPassword, sizeof s.webPassword);
    s.mqttEnabled = p.getBool("mqtt", s.mqttEnabled);
    if (p.isKey("mqttHost")) p.getString("mqttHost", s.mqttHost, sizeof s.mqttHost);
    s.mqttPort = p.getUShort("mqttPort", s.mqttPort);
    if (p.isKey("mqttUser")) p.getString("mqttUser", s.mqttUser, sizeof s.mqttUser);
    if (p.isKey("mqttPass")) p.getString("mqttPass", s.mqttPassword, sizeof s.mqttPassword);
    p.end();
  }
  if (!hasPassword) {
    // First start, or after a factory reset.
    device::randomPassword(s.webPassword, kWebPasswordLength);
    save(s);
  }
  return s;
}

void save(const Settings& s) {
  Preferences p;
  if (!p.begin(kNamespace, false)) return;
  p.putUChar("bright", s.brightness);
  p.putUShort("dim", s.dimAfterS);
  p.putBool("idleOff", s.screenOffWhenIdle);
  p.putBool("degF", s.fahrenheit);
  p.putUChar("led", static_cast<uint8_t>(s.led));
  p.putFloat("tOffset", s.tempOffsetC);
  p.putUShort("alt", s.altitudeM);
  p.putBool("co2Asc", s.co2SelfCalibration);
  p.putBool("battery", s.batteryInstalled);
  p.putBool("wifi", s.wifiEnabled);
  p.putString("name", s.name);
  p.putString("webPass", s.webPassword);
  p.putBool("mqtt", s.mqttEnabled);
  p.putString("mqttHost", s.mqttHost);
  p.putUShort("mqttPort", s.mqttPort);
  p.putString("mqttUser", s.mqttUser);
  p.putString("mqttPass", s.mqttPassword);
  p.end();
}

void erase() {
  Preferences p;
  if (!p.begin(kNamespace, false)) return;
  p.clear();
  p.end();
}

}  // namespace settings
