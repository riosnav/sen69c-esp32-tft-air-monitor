// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Switching off: from the settings menu, e.g. to carry the monitor
/// around, and by itself before the battery is empty (see
/// power::BatteryWatch).
///
/// The board can't disconnect its battery, so "off" is the chip's deep sleep,
/// with the screen, the sensor's power and the radio off. It still draws a
/// little current, so a battery left connected slowly runs down.
///
/// Switched off from the menu, holding D1 or D2 for 2 s turns it on again.
/// Switched off for an empty battery, it wakes every few minutes, and when D1
/// or D2 is pressed, and starts again once USB power is back; pressed without
/// it, D1 or D2 shows why nothing happens.
#pragma once

#include <cstdint>

namespace power {

/// How often a monitor switched off for an empty battery checks for USB
/// power, in minutes.
constexpr uint32_t kCheckEveryMinutes = 5;
/// How long D1 or D2 must be held to turn the monitor on, in ms.
constexpr uint32_t kHoldToStartMs = 2000;
/// How long the Power off screen counts down, in s, so there's time to read it.
constexpr uint32_t kOffCountdownS = 10;

/// @brief First thing in setup(): after switching off, goes straight back to
/// sleep unless it's time to start.
///
/// Doesn't return then. It's time to start after a power-on or reset, after
/// D1 or D2 was held for kHoldToStartMs (switched off from the menu), or once
/// USB power is back (switched off for the battery).
void begin();
/// @brief Whether begin() let it start because USB power came back after an
/// empty battery.
///
/// The battery monitor takes a minute or more to notice; see
/// BatteryWatch::pluggedIn().
bool startedOnUsbPower();
/// Switches off until D1 or D2 is held; doesn't return.
[[noreturn]] void powerOff();
/// Switches off until USB power is back; doesn't return.
[[noreturn]] void shutDownForBattery();

}  // namespace power
