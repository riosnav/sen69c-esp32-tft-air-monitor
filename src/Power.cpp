// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Switching off and on again: deep sleep, waking up, and the battery
/// check while switched off for an empty battery (see Power.h).

#include "Power.h"

#include <Arduino.h>
#include <BatteryWatch.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "ui/Lgfx.h"
#include "ui/Screens.h"

namespace power {

namespace {

constexpr uint8_t kAddrMax17048 = 0x36;  ///< The battery gauge's I2C address.
constexpr uint32_t kRailMs = 10;      ///< For the I2C rail to come up, in ms.
constexpr uint32_t kMessageMs = 4000;  ///< How long a button shows why nothing starts, in ms.
constexpr uint8_t kMessageBrightness = 150;  ///< Backlight for that, of 255.
constexpr uint32_t kReleaseTimeoutMs = 10000;  ///< The longest waitForRelease() waits, in ms.
constexpr uint8_t kWakeButtons[] = {1, 2};  ///< D1 and D2, which read high while pressed.

/// Off while asleep: the backlight, the regulator for the screen and the
/// STEMMA QT port (so also the sensor), and the NeoPixel.
const gpio_num_t kPowerPins[] = {static_cast<gpio_num_t>(TFT_BACKLITE),
                                 static_cast<gpio_num_t>(TFT_I2C_POWER),
                                 static_cast<gpio_num_t>(NEOPIXEL_POWER)};

/// Why the monitor is switched off.
enum class Off : uint8_t { No, ByUser, ForBattery };

/// Why it's asleep. Kept through deep sleep, but not through a reset or a power cut.
RTC_DATA_ATTR Off asleep = Off::No;

bool usbStart = false;  ///< Started again because USB power is back.

/// Whether D1 or D2 is pressed.
bool buttonPressed() {
  for (uint8_t pin : kWakeButtons) {
    if (digitalRead(pin) == HIGH) return true;
  }
  return false;
}

/// @brief Whether D1 or D2, which woke the chip, stays pressed until `ms`
/// after the wake.
/// @param ms The time, as millis(), which counts from the wake.
bool heldUntil(uint32_t ms) {
  for (uint8_t pin : kWakeButtons) pinMode(pin, INPUT_PULLDOWN);
  while (millis() < ms) {
    if (!buttonPressed()) return false;
    delay(10);
  }
  return true;
}

/// Waits until D1 and D2 are released, for kReleaseTimeoutMs at most.
void waitForRelease() {
  for (uint32_t start = millis(); buttonPressed() && millis() - start < kReleaseTimeoutMs;) delay(10);
}

/// @brief Reads one of the battery gauge's 16-bit registers.
/// @param reg The register.
/// @param[out] value What it holds.
/// @return Whether that worked.
bool readRegister16(uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(kAddrMax17048);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kAddrMax17048, static_cast<uint8_t>(2)) != 2) return false;
  value = static_cast<uint16_t>(Wire.read() << 8);
  value |= static_cast<uint16_t>(Wire.read());
  return true;
}

/// @brief Reads the battery while switched off.
///
/// The battery gauge runs from the battery and keeps measuring while the
/// rest sleeps (see SensorService::readBattery for the units).
/// @param[out] volts The voltage in V; left as it was if it can't be read.
/// @param[out] ratePerHour The charge rate in % per hour; the same.
void readBattery(float& volts, float& ratePerHour) {
  delay(kRailMs);
  Wire.begin(SDA, SCL, 100000);
  uint16_t vcell = 0;
  uint16_t crate = 0;
  if (readRegister16(0x02, vcell) && readRegister16(0x16, crate)) {
    volts = vcell * 78.125e-6f;
    ratePerHour = static_cast<int16_t>(crate) * 0.208f;
  }
  Wire.end();
}

/// Shows the Battery empty screen for kMessageMs.
void showEmpty() {
  Lgfx tft;
  tft.init();
  tft.setRotation(1);
  LGFX_Sprite frame(&tft);
  frame.setColorDepth(16);
  if (!frame.createSprite(tft.width(), tft.height())) return;
  drawBatteryEmpty(frame);
  frame.pushSprite(0, 0);
  tft.setBrightness(kMessageBrightness);
  delay(kMessageMs);
}

/// @brief Switches off: holds the power pins low and enters deep sleep.
/// @param why Why, which decides what wakes it: D1 or D2, and for the
///   battery also a timer every kCheckEveryMinutes.
[[noreturn]] void sleep(Off why) {
  asleep = why;
  for (gpio_num_t pin : kPowerPins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    gpio_hold_en(pin);
  }
  gpio_deep_sleep_hold_en();
  if (why == Off::ForBattery) {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(kCheckEveryMinutes) * 60 * 1000000);
  }
  // Asleep, the chip watches D1 and D2 through its RTC side, which has its own
  // pull-downs, separate from the ones pinMode() sets. Without them the pins
  // float and wake it several times a second.
  uint64_t buttons = 0;
  for (uint8_t pin : kWakeButtons) {
    rtc_gpio_pullup_dis(static_cast<gpio_num_t>(pin));
    rtc_gpio_pulldown_en(static_cast<gpio_num_t>(pin));
    buttons |= 1ULL << pin;
  }
  esp_sleep_enable_ext1_wakeup_io(buttons, ESP_EXT1_WAKEUP_ANY_HIGH);
  Serial.flush();
  esp_deep_sleep_start();
}

}  // namespace

void begin() {
  for (gpio_num_t pin : kPowerPins) gpio_hold_dis(pin);
  gpio_deep_sleep_hold_dis();
  const Off why = asleep;
  asleep = Off::No;
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (why == Off::No || (cause != ESP_SLEEP_WAKEUP_TIMER && cause != ESP_SLEEP_WAKEUP_EXT1)) {
    return;  // started as usual (power on, reset)
  }
  if (why == Off::ByUser) {
    if (heldUntil(kHoldToStartMs)) return;  // the UI ignores the rest of the press
    sleep(Off::ByUser);  // a brief press, e.g. in a bag
  }
  float volts = NAN;
  float ratePerHour = NAN;
  readBattery(volts, ratePerHour);
  if (powerIsBack(volts, ratePerHour)) {
    usbStart = true;
    return;
  }
  if (cause == ESP_SLEEP_WAKEUP_EXT1) showEmpty();
  sleep(Off::ForBattery);
}

bool startedOnUsbPower() { return usbStart; }

void powerOff() {
  waitForRelease();  // the press that confirmed would wake it at once
  WiFi.mode(WIFI_OFF);
  sleep(Off::ByUser);
}

void shutDownForBattery() {
  WiFi.mode(WIFI_OFF);
  sleep(Off::ForBattery);
}

}  // namespace power
