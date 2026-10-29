// SPDX-License-Identifier: GPL-3.0-or-later

#include "Settings.h"

#include <Preferences.h>

namespace settings {

namespace {
constexpr const char* kNamespace = "settings";
}

Settings load() {
  Settings s;
  Preferences p;
  if (!p.begin(kNamespace, true)) return s;  // nothing saved yet
  s.brightness = p.getUChar("bright", s.brightness);
  s.dimAfterS = p.getUShort("dim", s.dimAfterS);
  s.fahrenheit = p.getBool("degF", s.fahrenheit);
  s.led = static_cast<LedMode>(p.getUChar("led", static_cast<uint8_t>(s.led)));
  s.tempOffsetC = p.getFloat("tOffset", s.tempOffsetC);
  s.altitudeM = p.getUShort("alt", s.altitudeM);
  s.co2SelfCalibration = p.getBool("co2Asc", s.co2SelfCalibration);
  p.end();
  return s;
}

void save(const Settings& s) {
  Preferences p;
  if (!p.begin(kNamespace, false)) return;
  p.putUChar("bright", s.brightness);
  p.putUShort("dim", s.dimAfterS);
  p.putBool("degF", s.fahrenheit);
  p.putUChar("led", static_cast<uint8_t>(s.led));
  p.putFloat("tOffset", s.tempOffsetC);
  p.putUShort("alt", s.altitudeM);
  p.putBool("co2Asc", s.co2SelfCalibration);
  p.end();
}

void erase() {
  Preferences p;
  if (!p.begin(kNamespace, false)) return;
  p.clear();
  p.end();
}

}  // namespace settings
