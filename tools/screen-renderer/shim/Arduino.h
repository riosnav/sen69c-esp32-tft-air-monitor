// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for the Arduino core, with what the firmware's UI uses:
/// String, the clock, the button pins and the board's pin names.
///
/// The clock and the pins belong to the simulated device (see Platform.cpp):
/// time is the simulated clock, which only moves between the main loop's
/// passes, and the button pins read the simulated buttons.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "IPAddress.h"
#include "WString.h"

// Arduino-ESP32's values (esp32-hal-gpio.h).
#define LOW 0x0              ///< A pin's low level.
#define HIGH 0x1             ///< A pin's high level.
#define INPUT 0x01           ///< pinMode(): an input.
#define OUTPUT 0x03          ///< pinMode(): an output.
#define INPUT_PULLUP 0x05    ///< pinMode(): an input with its pull-up on.
#define INPUT_PULLDOWN 0x09  ///< pinMode(): an input with its pull-down on.

// Adafruit ESP32-S3 Reverse TFT Feather (the board variant's pins_arduino.h).
static const uint8_t MOSI = 35;  ///< SPI data out, to the screen.
static const uint8_t SCK = 36;   ///< SPI clock.
#define TFT_CS 42                ///< The screen's chip select.
#define TFT_RST 41               ///< The screen's reset.
#define TFT_DC 40                ///< The screen's data/command select.
#define TFT_BACKLITE 45          ///< The screen's backlight.

/// The simulated clock (World::nowMs), in ms.
uint32_t millis();
/// The simulated clock in microseconds: millis() times 1000, so whole ms only.
uint32_t micros();
/// @brief Returns at once: the simulated clock only moves between the main
/// loop's passes (see Simulator::run()).
/// @param ms How long the caller wanted to wait, in ms.
void delay(uint32_t ms);
/// @brief Sets a pin up. Only notes which button pins are active low: those
/// set up as INPUT_PULLUP (D0).
/// @param pin The GPIO number.
/// @param mode INPUT, OUTPUT, INPUT_PULLUP or INPUT_PULLDOWN.
void pinMode(uint8_t pin, uint8_t mode);
/// @brief Does nothing: the renderer shows no output pin.
/// @param pin The GPIO number.
/// @param value LOW or HIGH.
void digitalWrite(uint8_t pin, uint8_t value);
/// @brief Reads a simulated button (World::buttonDown), as the board's would
/// read.
///
/// The buttons are on GPIO 0, 1 and 2. A pin set up as INPUT_PULLUP (D0)
/// reads LOW while its button is pressed; the others (D1, D2) read HIGH.
/// @param pin The GPIO number.
/// @return HIGH or LOW; LOW for any other pin.
int digitalRead(uint8_t pin);

// macOS has strlcpy, and so does glibc since 2.38; Windows doesn't.
#if !defined(__APPLE__) && \
    !(defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38)))
/// @brief Copies a string as BSD's strlcpy does: as much as fits, always
/// terminated.
/// @param[out] dst Where to: `size` bytes.
/// @param src The string to copy.
/// @param size The size of `dst`, in bytes; 0 writes nothing.
/// @return The length of `src`: `size` or more means it was cut short.
inline size_t strlcpy(char* dst, const char* src, size_t size) {
  const size_t length = strlen(src);
  if (size > 0) {
    const size_t n = std::min(length, size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return length;
}
#endif

/// Arduino's serial port, as far as the firmware's UI uses it: its output goes
/// to stderr.
class HardwareSerial {
 public:
  /// @brief Writes text.
  /// @param text The text.
  void print(const char* text) { fputs(text, stderr); }
  /// @brief Writes text.
  /// @param text The text.
  void print(const String& text) { print(text.c_str()); }
  /// @brief Writes text and a line break: `\n`, where Arduino's sends `\r\n`.
  /// @param text The text; none for just the line break.
  void println(const char* text = "") { fprintf(stderr, "%s\n", text); }
  /// @brief Writes text and a line break (see println(const char*)).
  /// @param text The text.
  void println(const String& text) { println(text.c_str()); }
  /// @brief Writes formatted text, as printf() does.
  /// @param format The format, as printf()'s.
  /// @param ... What it formats.
  void printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
  }
};
/// The serial port, which writes to stderr.
extern HardwareSerial Serial;

/// Arduino-ESP32's chip functions, with only restart().
class EspClass {
 public:
  /// @brief Restarts the simulated device once the current main-loop pass is
  /// over.
  ///
  /// Unlike the real one, it returns. The simulator then starts the UI afresh
  /// (Simulator::powerOn()), and the clock and the history carry on.
  void restart();
};
/// The chip (see EspClass).
extern EspClass ESP;
