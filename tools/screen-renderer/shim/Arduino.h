// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stand-in for the Arduino core, with what the firmware's UI uses: String,
// the clock, the button pins and the board's pin names. The clock and the
// pins belong to the simulated device (src/Platform.cpp).
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

#define LOW 0x0
#define HIGH 0x1
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09

// Adafruit ESP32-S3 Reverse TFT Feather (the board variant's pins_arduino.h).
static const uint8_t MOSI = 35;
static const uint8_t SCK = 36;
#define TFT_CS 42
#define TFT_RST 41
#define TFT_DC 40
#define TFT_BACKLITE 45

uint32_t millis();
uint32_t micros();
void delay(uint32_t ms);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);

// macOS has strlcpy, and so does glibc since 2.38; Windows doesn't.
#if !defined(__APPLE__) && \
    !(defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 38)))
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

// Serial output goes to stderr.
class HardwareSerial {
 public:
  void print(const char* text) { fputs(text, stderr); }
  void print(const String& text) { print(text.c_str()); }
  void println(const char* text = "") { fprintf(stderr, "%s\n", text); }
  void println(const String& text) { println(text.c_str()); }
  void printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
  }
};
extern HardwareSerial Serial;

class EspClass {
 public:
  // Restarts the simulated device once the current main-loop pass is over.
  void restart();
};
extern EspClass ESP;
