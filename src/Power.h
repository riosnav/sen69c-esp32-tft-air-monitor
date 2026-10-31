// SPDX-License-Identifier: GPL-3.0-or-later
//
// Switching off: from the settings menu, e.g. to carry the monitor around,
// and by itself before the battery is empty (see power::BatteryWatch). The
// board can't disconnect its battery, so "off" is the chip's deep sleep, with
// the screen, the sensor's power and the radio off; it draws about 0.1 mA.
//
// Switched off from the menu, holding D1 or D2 for 2 s turns it on again.
// Switched off for an empty battery, it wakes every few minutes, and when D1
// or D2 is pressed, and starts again once USB power is back; pressed without
// it, D1 or D2 shows why nothing happens.
#pragma once

#include <cstdint>

namespace power {

// How often a monitor switched off for an empty battery checks for USB power.
constexpr uint32_t kCheckEveryMinutes = 5;
// How long D1 or D2 must be held to turn the monitor on.
constexpr uint32_t kHoldToStartMs = 2000;

// First thing in setup(): after switching off, goes straight back to sleep
// unless it's time to start.
void begin();
// Switches off until D1 or D2 is held; doesn't return.
[[noreturn]] void powerOff();
// Switches off until USB power is back; doesn't return.
[[noreturn]] void shutDownForBattery();

}  // namespace power
