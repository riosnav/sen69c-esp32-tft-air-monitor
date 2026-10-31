// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screens, buttons, settings menu, alerts, backlight and status LED.
//
// Buttons sit on the left edge: D0 (top), D1 (middle), D2 (bottom).
//   Pages:   D0/D2 previous/next page, D1 page action, hold D1 for settings.
//   Menu:    D0/D2 move, D1 select, hold D1 to close.
//   Editing: D0/D2 change the value (hold to repeat), D1 done.
// The Wi-Fi setup and firmware update screens take over the whole display.
#pragma once

#include <AirQuality.h>
#include <ButtonLogic.h>
#include <History.h>

#include "Lgfx.h"
#include "Settings.h"
#include "StatusLed.h"
#include "net/MqttService.h"
#include "net/Ota.h"
#include "net/WifiService.h"
#include "sensor/SensorService.h"

class Ui {
 public:
  void begin(SensorService& sensor, Settings& settings, const aq::AirQuality& airQuality,
             const aq::MetricHistory& history, WifiService& wifi, const Ota& ota,
             const MqttService& mqtt);
  // Call often from loop(); `snapshot` is the sensor's latest state.
  void update(const SensorSnapshot& snapshot, uint32_t nowMs);
  // The settings were changed elsewhere (web page): apply and save them.
  void settingsChanged() { settingsChanged_ = true; }

 private:
  enum class Mode : uint8_t { Pages, Menu, Edit, Confirm, Link };
  enum class Page : uint8_t {
    Overview,
    Particles,
    Gases,
    Comfort,
    TrendHour,
    TrendDay,
    Network,
    Device,
    Count
  };
  // Menu order, grouped: display, Wi-Fi, sensor, device.
  enum class Item : uint8_t {
    Brightness,
    DimAfter,
    Units,
    Led,
    Wifi,
    WifiSetup,
    ForgetWifi,
    TempOffset,
    Altitude,
    Co2SelfCalibration,
    CalibrateCo2,
    CleanFan,
    HeatHumiditySensor,
    Restart,
    FactoryReset,
    Close,
    Count
  };
  enum class Hint : uint8_t { None, Up, Down, Select, Plus, Minus, Check, Cross };

  // Metrics the trend pages cycle through with D1.
  static const std::array<aq::Metric, 10> kTrendMetrics;

  // Input and state (Ui.cpp)
  void handleButton(size_t button, aq::ButtonEvent event, uint32_t nowMs);
  void onPageEvent(size_t button, aq::ButtonEvent event);
  void onMenuEvent(size_t button, aq::ButtonEvent event);
  void onEditEvent(size_t button, aq::ButtonEvent event);
  void onConfirmEvent(size_t button, aq::ButtonEvent event);
  void onNetworkAction();
  void select(Item item);
  void adjust(Item item, int direction);
  void execute(Item item);
  void commitSettings();
  void updateAlerts(uint32_t nowMs);
  void dismissAlert();
  void updateBacklight(uint32_t nowMs);
  void showToast(const char* text, uint32_t nowMs);
  bool warmingUp() const;

  // Drawing (UiDraw.cpp)
  void draw(uint32_t nowMs);
  void drawTitle(const char* title);
  void drawHints(Hint d0, Hint d1, Hint d2);
  void drawHint(Hint hint, int y);
  void drawPageDots();
  void drawOverview();
  void drawMetricRows(const aq::Metric* metrics, size_t count);
  void drawComfort();
  void drawTrend(bool day);
  template <typename Ring>
  void drawChart(const Ring& ring, aq::Metric metric, int x0, int y0, int x1, int y1);
  void drawDevice(uint32_t nowMs);
  void drawNetwork();
  void drawSignal(int right, int baseline, int8_t rssi);
  void drawWifiIcon(int cx, int bottom, uint16_t color);
  void drawSetup();
  void drawLink();
  void drawUpdate();
  int drawQr(const char* text);
  void drawMenu();
  void drawConfirm();
  void drawAlert();
  void drawToast();

  // Formatting (UiDraw.cpp)
  float displayValue(aq::Metric metric, float value) const;
  String valueText(aq::Metric metric, float value) const;
  const char* unitText(aq::Metric metric) const;
  String itemValue(Item item) const;

  Lgfx tft_;
  LGFX_Sprite frame_{&tft_};
  StatusLed led_;
  aq::ButtonLogic buttons_[3];

  SensorService* sensor_ = nullptr;
  Settings* settings_ = nullptr;
  const aq::AirQuality* aq_ = nullptr;
  const aq::MetricHistory* history_ = nullptr;
  WifiService* wifi_ = nullptr;
  const Ota* ota_ = nullptr;
  const MqttService* mqtt_ = nullptr;
  SensorSnapshot snapshot_;
  bool settingsChanged_ = false;

  Mode mode_ = Mode::Pages;
  Page page_ = Page::Overview;
  Item item_ = Item::Brightness;
  int menuTop_ = 0;
  Item confirmItem_ = Item::Close;
  size_t trendMetric_ = 0;
  uint32_t editRepeats_ = 0;

  // Alerts: the level each metric was last acknowledged at.
  std::array<aq::Level, aq::kMetricCount> acknowledged_{};
  bool alertVisible_ = false;
  aq::Metric alertMetric_ = aq::Metric::Co2;
  aq::Level alertLevel_ = aq::Level::Unknown;
  uint32_t alertShownMs_ = 0;

  uint32_t lastInputMs_ = 0;
  bool dimmed_ = false;
  uint32_t lastDrawMs_ = 0;
  bool dirty_ = true;

  char toast_[48] = "";
  uint32_t toastUntilMs_ = 0;
  uint32_t resultSequence_ = 0;
};
