// SPDX-License-Identifier: GPL-3.0-or-later
//
// M0 hardware bring-up: TFT, buttons, NeoPixel, I2C scan and SEN69C readout.
// Prints one line per second over USB serial and mirrors it on the screen.

#include <Arduino.h>
#include <SensirionI2cSen69c.h>
#include <Wire.h>
#include <esp_system.h>

#include "ui/Lgfx.h"

namespace {

// D0 doubles as BOOT and is pulled up; D1/D2 are pulled down (needed for
// waking from deep sleep), so they read HIGH when pressed.
constexpr uint8_t kButtonPins[] = {0, 1, 2};
constexpr bool kPressedLevel[] = {LOW, HIGH, HIGH};

constexpr uint8_t kAddrLc709203f = 0x0B;
constexpr uint8_t kAddrMax17048 = 0x36;

// Raw values the SEN69C returns while a measurement isn't available yet.
constexpr int32_t kUnknownU16 = 0xFFFF;
constexpr int32_t kUnknownI16 = 0x7FFF;

constexpr uint32_t kReadIntervalMs = 1000;
constexpr uint32_t kStatusIntervalMs = 10000;
constexpr uint32_t kDrawIntervalMs = 100;

struct RawReading {
  uint16_t pm1 = kUnknownU16;   // ug/m3 x10
  uint16_t pm25 = kUnknownU16;  // ug/m3 x10
  uint16_t pm4 = kUnknownU16;   // ug/m3 x10
  uint16_t pm10 = kUnknownU16;  // ug/m3 x10
  int16_t rh = kUnknownI16;     // % x100
  int16_t t = kUnknownI16;      // degC x200
  int16_t voc = kUnknownI16;    // index x10
  int16_t nox = kUnknownI16;    // index x10
  uint16_t hcho = kUnknownU16;  // ppb x10
  int16_t co2 = kUnknownI16;    // ppm
};

Lgfx tft;
LGFX_Sprite frame(&tft);
SensirionI2cSen69c sen;

RawReading reading;
bool sensorFound = false;
int16_t lastError = 0;
SEN69CDeviceStatus deviceStatus{};
String i2cDevices;
bool pressed[3] = {};

const char* errorText(int16_t error) {
  static char text[64];
  errorToString(error, text, sizeof text);
  return text;
}

// Scaled value as text, or "--" while the sensor reports it as unknown.
String fmt(int32_t raw, int32_t unknown, float scale, int decimals) {
  if (raw == unknown) return "--";
  return String(raw / scale, decimals);
}

int16_t readRaw(RawReading& r) {
  return sen.readMeasuredValuesAsIntegers(r.pm1, r.pm25, r.pm4, r.pm10, r.rh,
                                          r.t, r.voc, r.nox, r.hcho, r.co2);
}

void printBanner() {
  Serial.printf("\nSEN69C Air Monitor %s - M0 bring-up\n", FW_VERSION);
  Serial.printf("%s rev %u, %u cores, flash %u KB, PSRAM %u KB, reset reason %d\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                ESP.getFlashChipSize() / 1024, ESP.getPsramSize() / 1024,
                static_cast<int>(esp_reset_reason()));
}

void scanI2c() {
  Serial.println("I2C scan:");
  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() != 0) continue;
    const char* name = addr == SEN69C_I2C_ADDR_6B ? "SEN6x sensor"
                       : addr == kAddrMax17048    ? "MAX17048 battery monitor"
                       : addr == kAddrLc709203f   ? "LC709203F battery monitor"
                                                  : "unknown";
    Serial.printf("  0x%02X  %s\n", addr, name);
    i2cDevices += String(i2cDevices.isEmpty() ? "" : " ") + "0x" + String(addr, HEX);
  }
  if (i2cDevices.isEmpty()) {
    Serial.println("  no devices found");
    i2cDevices = "none";
  }
}

void initSensor() {
  sen.begin(Wire, SEN69C_I2C_ADDR_6B);

  int8_t name[32] = {};
  int8_t serial[32] = {};
  uint8_t fwMajor = 0;
  uint8_t fwMinor = 0;
  lastError = sen.getProductName(name, sizeof name);
  if (lastError != 0) {
    Serial.printf("SEN69C not responding: %s\n", errorText(lastError));
    return;
  }
  sensorFound = true;
  sen.getSerialNumber(serial, sizeof serial);
  sen.getVersion(fwMajor, fwMinor);
  Serial.printf("Sensor %s, serial %s, firmware %u.%u\n",
                reinterpret_cast<char*>(name), reinterpret_cast<char*>(serial),
                fwMajor, fwMinor);

  // If only the ESP32 was reset, the sensor may still be measuring. Restarting
  // it would restart CO2 conditioning and the VOC/NOx learning, so only start
  // it when idle: an idle sensor reports every value as unknown.
  RawReading probe;
  if (readRaw(probe) == 0 && probe.t != kUnknownI16) {
    Serial.println("Sensor was already measuring; continuing");
    return;
  }
  lastError = sen.startContinuousMeasurement();
  if (lastError != 0) {
    Serial.printf("startContinuousMeasurement failed: %s\n", errorText(lastError));
    return;
  }
  Serial.println("Measurement started (NOx ~11 s, CO2 ~24 s, HCHO ~60 s until first value)");
}

void readSensor() {
  if (!sensorFound) return;
  RawReading r;
  lastError = readRaw(r);
  if (lastError != 0) {
    Serial.printf("read failed: %s\n", errorText(lastError));
    return;
  }
  reading = r;
  Serial.printf(
      "t=%lus PM1=%s PM2.5=%s PM4=%s PM10=%s ug/m3 | RH=%s%% T=%sC | VOC=%s NOx=%s | "
      "HCHO=%sppb | CO2=%sppm\n",
      millis() / 1000, fmt(r.pm1, kUnknownU16, 10, 1).c_str(),
      fmt(r.pm25, kUnknownU16, 10, 1).c_str(), fmt(r.pm4, kUnknownU16, 10, 1).c_str(),
      fmt(r.pm10, kUnknownU16, 10, 1).c_str(), fmt(r.rh, kUnknownI16, 100, 1).c_str(),
      fmt(r.t, kUnknownI16, 200, 2).c_str(), fmt(r.voc, kUnknownI16, 10, 0).c_str(),
      fmt(r.nox, kUnknownI16, 10, 0).c_str(), fmt(r.hcho, kUnknownU16, 10, 1).c_str(),
      fmt(r.co2, kUnknownI16, 1, 0).c_str());
}

void checkStatus() {
  if (!sensorFound) return;
  SEN69CDeviceStatus s{};
  if (sen.readDeviceStatus(s) != 0) return;
  deviceStatus = s;
  if (s.value == 0) return;
  Serial.printf("Device status 0x%08lX:%s%s%s%s%s%s%s%s\n",
                static_cast<unsigned long>(s.value), s.fanError ? " fan" : "",
                s.rhtError ? " RH/T" : "", s.gasError ? " gas" : "",
                s.co21Error ? " CO2-1" : "", s.co22Error ? " CO2-2" : "",
                s.hchoError ? " HCHO" : "", s.pmError ? " PM" : "",
                s.fanSpeedWarning ? " fan-speed-warning" : "");
}

void pollButtons() {
  for (size_t i = 0; i < 3; ++i) {
    bool now = digitalRead(kButtonPins[i]) == kPressedLevel[i];
    if (now != pressed[i]) {
      pressed[i] = now;
      Serial.printf("D%u %s\n", static_cast<unsigned>(i), now ? "pressed" : "released");
    }
  }
}

void updateLed() {
  bool warmingUp = reading.t == kUnknownI16 || reading.co2 == kUnknownI16 ||
                   reading.hcho == kUnknownU16 || reading.nox == kUnknownI16;
  if (!sensorFound || lastError != 0 || deviceStatus.value != 0) {
    rgbLedWrite(PIN_NEOPIXEL, 24, 0, 0);
  } else if (warmingUp) {
    rgbLedWrite(PIN_NEOPIXEL, 16, 10, 0);
  } else {
    rgbLedWrite(PIN_NEOPIXEL, 0, 16, 0);
  }
}

void draw() {
  const RawReading& r = reading;
  frame.fillScreen(TFT_BLACK);
  // 1-px border: all four edges must be visible if the panel offsets are right.
  frame.drawRect(0, 0, frame.width(), frame.height(), TFT_DARKGREY);

  frame.setFont(&fonts::Font2);
  frame.setTextColor(TFT_CYAN);
  frame.setCursor(4, 2);
  frame.printf("M0 bring-up  %s", FW_VERSION);
  frame.setTextDatum(top_right);
  frame.drawString(String(millis() / 1000) + " s", frame.width() - 4, 2);
  frame.setTextDatum(top_left);

  frame.setTextColor(TFT_WHITE);
  int y = 19;
  constexpr int kLine = 16;
  auto line = [&](const String& text) {
    frame.drawString(text, 4, y);
    y += kLine;
  };
  if (!sensorFound) {
    frame.setTextColor(TFT_RED);
    line("SEN69C not found");
    line(String(errorText(lastError)));
  } else {
    line("PM2.5 " + fmt(r.pm25, kUnknownU16, 10, 1) + "  PM10 " + fmt(r.pm10, kUnknownU16, 10, 1) +
         " ug/m3");
    line("PM1 " + fmt(r.pm1, kUnknownU16, 10, 1) + "  PM4 " + fmt(r.pm4, kUnknownU16, 10, 1));
    line("CO2 " + fmt(r.co2, kUnknownI16, 1, 0) + " ppm  HCHO " + fmt(r.hcho, kUnknownU16, 10, 1) +
         " ppb");
    line("VOC " + fmt(r.voc, kUnknownI16, 10, 0) + "  NOx " + fmt(r.nox, kUnknownI16, 10, 0));
    line("T " + fmt(r.t, kUnknownI16, 200, 2) + " C  RH " + fmt(r.rh, kUnknownI16, 100, 1) + " %");
    if (deviceStatus.value != 0) {
      frame.setTextColor(TFT_RED);
      line("Status 0x" + String(deviceStatus.value, HEX));
    } else {
      frame.setTextColor(TFT_DARKGREY);
      line("I2C " + i2cDevices);
    }
  }

  // Button indicators along the bottom edge.
  for (size_t i = 0; i < 3; ++i) {
    int x = 4 + static_cast<int>(i) * 60;
    uint16_t colour = pressed[i] ? TFT_YELLOW : TFT_DARKGREY;
    frame.drawRect(x, frame.height() - 17, 52, 14, colour);
    if (pressed[i]) frame.fillRect(x + 2, frame.height() - 15, 48, 10, colour);
    frame.setTextColor(pressed[i] ? TFT_BLACK : TFT_LIGHTGREY);
    frame.setTextDatum(middle_center);
    frame.drawString("D" + String(i), x + 26, frame.height() - 10);
    frame.setTextDatum(top_left);
  }
  frame.pushSprite(0, 0);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  // Give a serial monitor a moment to attach, but never block standalone use.
  for (uint32_t start = millis(); !Serial && millis() - start < 2000;) delay(10);
  printBanner();

  for (size_t i = 0; i < 3; ++i) {
    pinMode(kButtonPins[i], kPressedLevel[i] == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  }

  tft.init();
  tft.setRotation(1);  // landscape, buttons on the left (D0 top, D2 bottom)
  tft.setBrightness(128);
  frame.setColorDepth(16);
  if (!frame.createSprite(tft.width(), tft.height())) Serial.println("sprite allocation failed");

  // initVariant() just powered the STEMMA QT port; the sensor needs ~100 ms.
  delay(150);
  Wire.begin(SDA, SCL, 100000);
  scanI2c();
  initSensor();
  draw();
}

void loop() {
  static uint32_t lastRead = 0;
  static uint32_t lastStatus = 0;
  static uint32_t lastDraw = 0;
  uint32_t now = millis();

  pollButtons();
  if (now - lastRead >= kReadIntervalMs) {
    lastRead = now;
    readSensor();
    updateLed();
  }
  if (now - lastStatus >= kStatusIntervalMs) {
    lastStatus = now;
    checkStatus();
  }
  if (now - lastDraw >= kDrawIntervalMs) {
    lastDraw = now;
    draw();
  }
  delay(5);
}
