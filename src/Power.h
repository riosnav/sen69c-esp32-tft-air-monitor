// SPDX-License-Identifier: GPL-3.0-or-later
//
// Switching off by itself before the battery is empty (see
// power::BatteryWatch). The board can't disconnect its battery, so "off" is
// the chip's deep sleep, with the screen, the sensor's power and the radio
// off; it draws about 0.1 mA.
//
// It wakes every few minutes, and when D1 or D2 is pressed, and starts again
// once USB power is back; pressed without it, D1 or D2 shows why nothing
// happens.
#pragma once

#include <cstdint>

namespace power {

// How often a monitor switched off for an empty battery checks for USB power.
constexpr uint32_t kCheckEveryMinutes = 5;

// First thing in setup(): after switching off, goes straight back to sleep
// unless it's time to start.
void begin();
// Switches off until USB power is back; doesn't return.
[[noreturn]] void shutDownForBattery();

}  // namespace power
