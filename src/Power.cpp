// SPDX-License-Identifier: GPL-3.0-or-later

#include "Power.h"

#include <Arduino.h>
#include <BatteryWatch.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "ui/Lgfx.h"
#include "ui/Screens.h"

namespace power {

namespace {

constexpr uint8_t kAddrMax17048 = 0x36;
constexpr uint32_t kRailMs = 10;      // for the I2C rail to come up
constexpr uint32_t kMessageMs = 4000;  // how long a button shows why nothing starts
constexpr uint8_t kMessageBrightness = 150;  // of 255
constexpr uint8_t kWakeButtons[] = {1, 2};  // D1 and D2 read high while pressed

// Off while asleep: the backlight, the regulator for the screen and the
// STEMMA QT port (so also the sensor), and the NeoPixel.
const gpio_num_t kPowerPins[] = {static_cast<gpio_num_t>(TFT_BACKLITE),
                                 static_cast<gpio_num_t>(TFT_I2C_POWER),
                                 static_cast<gpio_num_t>(NEOPIXEL_POWER)};

enum class Off : uint8_t { No, ForBattery };

// Why it's asleep. Kept through deep sleep, but not through a reset or a power cut.
RTC_DATA_ATTR Off asleep = Off::No;

bool readRegister16(uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(kAddrMax17048);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(kAddrMax17048, static_cast<uint8_t>(2)) != 2) return false;
  value = static_cast<uint16_t>(Wire.read() << 8);
  value |= static_cast<uint16_t>(Wire.read());
  return true;
}

// The battery monitor runs from the battery and keeps measuring while the
// rest sleeps (see SensorService::readBattery for the units).
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
  uint64_t buttons = 0;
  for (uint8_t pin : kWakeButtons) buttons |= 1ULL << pin;
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
  float volts = NAN;
  float ratePerHour = NAN;
  readBattery(volts, ratePerHour);
  if (powerIsBack(volts, ratePerHour)) return;
  if (cause == ESP_SLEEP_WAKEUP_EXT1) showEmpty();
  sleep(Off::ForBattery);
}

void shutDownForBattery() {
  WiFi.mode(WIFI_OFF);
  sleep(Off::ForBattery);
}

}  // namespace power
