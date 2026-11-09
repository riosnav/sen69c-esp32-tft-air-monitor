// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The Arduino functions the UI uses (see Arduino.h), and the platform
/// functions LovyanGFX's desktop back end would have provided (see SDL.h), on
/// the simulated device.
///
/// Time is the simulated clock, and nothing waits.

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "World.h"

HardwareSerial Serial;
EspClass ESP;

void EspClass::restart() { world.restartRequested = true; }

namespace {

/// @brief Whether each button's pin, GPIO 0 to 2, reads LOW while pressed.
///
/// Pins set up as INPUT_PULLUP read LOW while their button is pressed (D0);
/// the others read HIGH (D1, D2).
bool activeLow[3] = {};

}  // namespace

uint32_t millis() { return world.nowMs; }
uint32_t micros() { return world.nowMs * 1000; }
void delay(uint32_t) {}

void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < 3) activeLow[pin] = mode == INPUT_PULLUP;
}

void digitalWrite(uint8_t, uint8_t) {}

int digitalRead(uint8_t pin) {
  if (pin >= 3) return LOW;
  return world.buttonDown[pin] != activeLow[pin] ? HIGH : LOW;
}

namespace lgfx {
inline namespace v1 {

/// LovyanGFX's clock: the simulated one (World::nowMs), in ms.
unsigned long millis() { return world.nowMs; }
/// LovyanGFX's clock in microseconds: millis() times 1000, so whole ms only.
unsigned long micros() { return world.nowMs * 1000UL; }
/// Returns at once, as ::delay() does.
void delay(unsigned long) {}
/// Returns at once, as ::delay() does.
void delayMicroseconds(unsigned int) {}

/// Does nothing: the renderer has no pins for LovyanGFX to drive.
void gpio_hi(uint32_t) {}
/// Does nothing (see gpio_hi()).
void gpio_lo(uint32_t) {}
/// Reads any pin as low: false.
bool gpio_in(uint32_t) { return false; }
/// Does nothing (see gpio_hi()).
void pinMode(int_fast16_t, pin_mode_t) {}
/// Does nothing (see gpio_hi()).
void lgfxPinMode(int_fast16_t, pin_mode_t) {}

/// LovyanGFX's SPI functions, which do nothing: the renderer has no SPI bus
/// (see Bus_SPI).
namespace spi {

/// @brief Sets up nothing.
/// @return Success.
cpp::result<void, error_t> init(int, int, int, int, int) { return {}; }
/// Does nothing.
void beginTransaction(int) {}

}  // namespace spi

}  // namespace v1
}  // namespace lgfx
