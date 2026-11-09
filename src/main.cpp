// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief SEN69C air-quality monitor: wires the sensor task, the air-quality
/// logic, the history, the UI, Wi-Fi, the web server and Home Assistant (MQTT)
/// together.
///
/// Everything but the network parts works offline.

#include <AirQuality.h>
#include <Arduino.h>
#include <BatteryWatch.h>
#include <History.h>
#include <esp_system.h>

#include <new>

#include "AppLock.h"
#include "Power.h"
#include "Settings.h"
#include "System.h"
#include "net/MqttService.h"
#include "net/Ota.h"
#include "net/Web.h"
#include "net/WifiService.h"
#include "sensor/SensorService.h"
#include "ui/Ui.h"

namespace {

constexpr uint32_t kLogIntervalMs = 10000;  ///< How often logMeasurement() runs, in ms.
/// How long the screen says why, before switching off for an empty battery, in ms.
constexpr uint32_t kEmptyMessageMs = 5000;
/// @brief How long to wait for the sensor task to stop before switching off,
/// in ms.
///
/// The sensor task may be busy for a while, e.g. heating the humidity sensor.
constexpr uint32_t kSensorStopMs = 2000;

/// @name The main loop's state
/// Owned by the main loop; the web server reaches them under AppLock.
/// @{
Settings userSettings;                 ///< The user's settings.
SensorService sensor;                  ///< The SEN69C and the battery gauge, in their own task.
SensorSnapshot latest;                 ///< The sensor task's latest readings.
aq::AirQuality airQuality;             ///< Every metric's level, and the overall one.
aq::MetricHistory* history = nullptr;  ///< For the trend graphs; see createHistory().
WifiService wifi;                      ///< Wi-Fi, and the setup hotspot.
Ota ota;                               ///< Firmware updates over Wi-Fi.
MqttService mqtt;                      ///< Home Assistant, over MQTT.
power::BatteryWatch battery;           ///< What the battery gauge's readings mean.
Ui ui;                                 ///< The screen, the buttons and the LED.
/// @}

/// @brief Creates the history in PSRAM: at ~420 KB, it's more than the
/// internal RAM has free.
///
/// Stops the monitor (abort()) if there isn't enough PSRAM.
/// @return The history.
aq::MetricHistory* createHistory() {
  void* memory = ps_malloc(sizeof(aq::MetricHistory));
  if (memory == nullptr) {
    Serial.println("Not enough PSRAM for the history");
    abort();
  }
  return new (memory) aq::MetricHistory();
}

/// @brief A word for the MQTT state, for the serial log.
/// @param state The state.
const char* mqttText(MqttState state) {
  switch (state) {
    case MqttState::Waiting:
      return "waiting";
    case MqttState::Connecting:
      return "connecting";
    case MqttState::Connected:
      return "connected";
    case MqttState::Failed:
      return "failing";
    default:
      return "off";
  }
}

/// @brief A word for the Wi-Fi state, for the serial log.
/// @param state The state.
const char* wifiText(WifiState state) {
  switch (state) {
    case WifiState::Searching:
      return "searching";
    case WifiState::Connecting:
      return "connecting";
    case WifiState::Connected:
      return "connected";
    default:
      return "off";
  }
}

/// @brief Logs the readings to serial: every metric, the overall air quality,
/// Wi-Fi, MQTT and the free memory.
/// @param s The readings.
void logMeasurement(const SensorSnapshot& s) {
  using aq::Metric;
  static const char* const kLevels[] = {"unknown", "good", "warning", "alarm"};
  const aq::Measurement& m = s.measurement;
  Serial.printf(
      "PM1 %.1f PM2.5 %.1f PM4 %.1f PM10 %.1f | CO2 %.0f HCHO %.1f VOC %.0f NOx %.0f | "
      "T %.2f RH %.1f | air %s | wifi %s %d dBm | mqtt %s | free RAM %u KB, PSRAM %u KB\n",
      m[Metric::Pm1], m[Metric::Pm25], m[Metric::Pm4], m[Metric::Pm10], m[Metric::Co2],
      m[Metric::Hcho], m[Metric::Voc], m[Metric::Nox], m[Metric::Temperature],
      m[Metric::Humidity], kLevels[static_cast<int>(airQuality.overall())],
      wifiText(wifi.state()), wifi.rssi(), mqttText(mqtt.state()),
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, ESP.getFreePsram() / 1024);
}

}  // namespace

/// @brief Starts the monitor, unless power::begin() sends it back to sleep.
///
/// Loads the settings and starts the history, the sensor, Wi-Fi, MQTT, the
/// battery's state and the UI. The web server starts later, in loop(), once
/// there's a network.
void setup() {
  Serial.begin(115200);
  power::begin();  // after switching off for an empty battery, may sleep again
  // Give a serial monitor a moment to attach, but never block standalone use,
  // nor keep someone who just switched it on with D1 or D2 waiting.
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
    for (uint32_t start = millis(); !Serial && millis() - start < 2000;) delay(10);
  }
  Serial.printf("\nSEN69C Air Monitor %s\n", FW_VERSION);
  Serial.printf("%s rev %u, flash %u KB, PSRAM %u KB, reset reason %d\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getFlashChipSize() / 1024, ESP.getPsramSize() / 1024,
                static_cast<int>(esp_reset_reason()));

  userSettings = settings::load();
  history = createHistory();
  ota.begin();
  sensor.begin(
      {userSettings.tempOffsetC, userSettings.altitudeM, userSettings.co2SelfCalibration});
  wifi.begin(userSettings);
  mqtt.begin(userSettings, wifi, battery);
  battery.setPresent(settings::loadBatteryPresent());
  battery.setEmptyPercent(settings::loadBatteryEmpty());
  if (power::startedOnUsbPower()) battery.pluggedIn(millis());
  ui.begin(sensor, userSettings, airQuality, *history, wifi, ota, mqtt, battery);
}

/// @brief One pass of the main loop, with a 5 ms pause after it.
///
/// Under AppLock: takes new readings into the levels and the history, runs
/// Wi-Fi, OTA, MQTT, the battery's state and the UI, starts the web server
/// once there's a network, and logs the readings every kLogIntervalMs. Then,
/// without the lock, switches off if the battery is empty or the menu asked
/// for it, and restarts if sys::restartLater() asked for that.
void loop() {
  static uint32_t lastSequence = 0;
  static uint32_t lastBatterySequence = 0;
  static uint32_t lastLogMs = 0;
  const uint32_t now = millis();
  bool batteryEmpty = false;
  bool powerOff = false;
  {
    AppLock lock;
    sensor.snapshot(latest);
    if (latest.sequence != lastSequence) {
      lastSequence = latest.sequence;
      airQuality.update(latest.measurement);
      history->update(&latest.measurement, now);
    } else {
      history->update(nullptr, now);
    }
    wifi.loop(now);
    if (!web::started() && wifi.networkStarted()) {
      web::begin(
          {&latest, &airQuality, history, &userSettings, &sensor, &wifi, &ota, &mqtt, &battery, &ui});
    }
    const bool offline = !userSettings.wifiEnabled || wifi.saved().size() == 0;
    ota.loop(wifi.state() == WifiState::Connected, offline, now);
    mqtt.loop(latest, airQuality, now);
    Silence silence;
    if (mqtt.takeSilence(silence) && silence != userSettings.silence) {
      userSettings.silence = silence;
      ui.settingsChanged();  // applies and saves it
    }
    if (latest.batterySequence != lastBatterySequence) {
      lastBatterySequence = latest.batterySequence;
      const bool hadBattery = battery.present();
      battery.update(latest.batteryVolts, latest.batteryPercent, latest.batteryRate, now);
      if (battery.present() != hadBattery) {
        Serial.println(battery.present() ? "Battery connected" : "No battery");
        settings::saveBatteryPresent(battery.present());
      }
      if (battery.takeConnected()) {
        sensor.restartBatteryGauge();
        settings::saveBatteryEmpty(battery.emptyPercent());
      }
    }
    ui.update(latest, now);

    if (now - lastLogMs >= kLogIntervalMs) {
      lastLogMs = now;
      logMeasurement(latest);
    }
    if (battery.empty()) {
      batteryEmpty = true;
      Serial.printf("Battery empty (%.2f V, %.1f %%): switching off until USB power is back\n",
                    latest.batteryVolts, latest.batteryPercent);
      if (battery.learnEmpty()) settings::saveBatteryEmpty(battery.emptyPercent());
      ui.showBatteryEmpty();
      mqtt.shutDown();  // so Home Assistant shows it as unavailable at once
    } else if (ui.powerOffRequested()) {
      powerOff = true;
      Serial.println("Powering off until D1 or D2 is held");
      mqtt.shutDown();
    }
  }
  if (batteryEmpty) {
    delay(kEmptyMessageMs);
    power::shutDownForBattery();
  }
  if (powerOff) {
    sensor.powerDown(kSensorStopMs);
    for (uint32_t left = power::kOffCountdownS; left > 0; --left) {
      {
        AppLock lock;
        ui.showPoweringOff(left);
      }
      delay(1000);
    }
    power::powerOff();
  }
  sys::loop(now);
  delay(5);
}
