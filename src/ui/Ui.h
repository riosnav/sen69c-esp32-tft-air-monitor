// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screens, buttons, settings menu, alerts, backlight and status LED.
//
// Buttons sit on the left edge: D0 (top), D1 (middle), D2 (bottom).
//   Pages:   D0/D2 previous/next page, D1 page action, hold D1 for settings.
//   Menu:    D0/D2 move, D1 select, hold D1 to close.
//   Editing: D0/D2 change the value (hold to repeat), D1 done.
// The Wi-Fi setup and firmware update screens take over the whole display. An
// update sent from the web page waits there for an answer: holding any button
// for 5 s installs it, anything else refuses it.
#pragma once

#include <AirQuality.h>
#include <BatteryWatch.h>
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
             const aq::MetricHistory& history, WifiService& wifi, Ota& ota,
             const MqttService& mqtt, const power::BatteryWatch& battery);
  // Call often from loop(); `snapshot` is the sensor's latest state.
  void update(const SensorSnapshot& snapshot, uint32_t nowMs);
  // The settings were changed elsewhere (web page): apply and save them.
  void settingsChanged() { settingsChanged_ = true; }
  // Says the monitor is switching off for an empty battery, at once and for good.
  void showBatteryEmpty();
  // Power off was chosen in the menu; the screen says how to turn it on again.
  bool powerOffRequested() const { return poweringOff_; }
  // Counts down on that screen; the main loop switches off after it.
  void showPoweringOff(uint32_t secondsLeft);

 private:
  enum class Mode : uint8_t { Pages, Menu, Edit, Confirm, Link };
  enum class Page : uint8_t {
    Overview,
    Particles,
    Gases,
    Comfort,
    TrendHour,
    TrendDay,
    TrendWeek,
    Network,
    Device,
    Count
  };
  // Menu order, grouped: display, Wi-Fi, sensor, device.
  enum class Item : uint8_t {
    Brightness,
    DimAfter,
    IdleScreen,
    Units,
    Led,
    Wifi,
    WifiSetup,
    ForgetWifi,
    TempOffset,
    Altitude,
    Co2SelfCalibration,
    CalibrateCo2,
    ResetCo2,
    CleanFan,
    HeatHumiditySensor,
    RestartVoc,
    Restart,
    PowerOff,
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
  void onUpdatePromptEvent(size_t button, aq::ButtonEvent event, uint32_t nowMs);
  void updatePrompt(uint32_t nowMs);
  void onNetworkAction();
  void select(Item item);
  void adjust(Item item, int direction);
  void execute(Item item);
  void commitSettings();
  void updateAlerts(uint32_t nowMs);
  void dismissAlert();
  bool alertShowing() const { return alertVisible_ || batteryAlert_ != 0; }
  void updateBacklight(uint32_t nowMs);
  void applyBacklight();
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
  void drawTrend();
  template <typename Ring>
  void drawChart(const Ring& ring, size_t span, aq::Metric metric, int x0, int y0, int x1, int y1);
  void drawDevice(uint32_t nowMs);
  void drawNetwork();
  void drawSignal(int right, int baseline, int8_t rssi);
  void drawWifiIcon(int cx, int bottom, uint16_t color);
  void drawSetup();
  void drawLink();
  void drawUpdate();
  void drawUpdatePrompt(uint32_t nowMs);
  void drawPoweringOff();
  int drawQr(const char* text);
  void drawMenu();
  void drawConfirm();
  void drawAlert();
  void drawBatteryAlert();
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
  Ota* ota_ = nullptr;
  const MqttService* mqtt_ = nullptr;
  const power::BatteryWatch* battery_ = nullptr;
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
  // The battery's warning (BatteryWatch::warning()) showing, after any air
  // alert, and the last one acknowledged.
  uint8_t batteryAlert_ = 0;
  uint8_t batteryAcknowledged_ = 0;
  bool batteryEmpty_ = false;
  bool poweringOff_ = false;
  uint32_t offInS_ = 0;  // the Power off countdown

  uint32_t lastInputMs_ = 0;
  bool dimmed_ = false;
  bool screenOff_ = false;  // idle, with the screen switched off
  uint32_t lastDrawMs_ = 0;
  bool dirty_ = true;

  // Update prompt: the button held to say yes, and since when.
  OtaState otaState_ = OtaState::Idle;
  bool holding_ = false;
  size_t holdButton_ = 0;
  uint32_t holdStartMs_ = 0;

  char toast_[48] = "";
  uint32_t toastUntilMs_ = 0;
  uint32_t resultSequence_ = 0;
};
