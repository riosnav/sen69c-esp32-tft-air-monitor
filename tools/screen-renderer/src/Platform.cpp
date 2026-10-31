// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Arduino functions the UI uses, and the platform functions LovyanGFX's
// desktop back end would have provided (see shim/SDL.h). Time is the
// simulated clock, and nothing waits.

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include "World.h"

HardwareSerial Serial;
EspClass ESP;

void EspClass::restart() { world.restartRequested = true; }

namespace {

// Pins set up as INPUT_PULLUP read LOW while their button is pressed (D0);
// the others read HIGH (D1, D2).
bool activeLow[3] = {};

}  // namespace

uint32_t millis() { return world.nowMs; }
uint32_t micros() { return world.nowMs * 1000; }
void delay(uint32_t) {}

void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < 3) activeLow[pin] = mode == INPUT_PULLUP;
}

void digitalWrite(uint8_t, uint8_t) {}

// The buttons are on GPIO 0, 1 and 2.
int digitalRead(uint8_t pin) {
  if (pin >= 3) return LOW;
  return world.buttonDown[pin] != activeLow[pin] ? HIGH : LOW;
}

namespace lgfx {
inline namespace v1 {

unsigned long millis() { return world.nowMs; }
unsigned long micros() { return world.nowMs * 1000UL; }
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}

void gpio_hi(uint32_t) {}
void gpio_lo(uint32_t) {}
bool gpio_in(uint32_t) { return false; }
void pinMode(int_fast16_t, pin_mode_t) {}
void lgfxPinMode(int_fast16_t, pin_mode_t) {}

namespace spi {

cpp::result<void, error_t> init(int, int, int, int, int) { return {}; }
void beginTransaction(int) {}

}  // namespace spi

}  // namespace v1
}  // namespace lgfx
