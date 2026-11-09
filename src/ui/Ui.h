// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Screens, buttons, settings menu, alerts, backlight and status LED.
///
/// Buttons sit on the left edge: D0 (top), D1 (middle), D2 (bottom).
/// - Pages: D0/D2 previous/next page, D1 page action, hold D1 for settings.
/// - Menu: D0/D2 move, D1 select, hold D1 to close.
/// - Editing: D0/D2 change the value (hold to repeat), D1 done.
/// - Before an action: hold D1 to confirm, D0 or D2 to cancel.
/// - An alert: any button closes it.
///
/// The Wi-Fi setup and firmware update screens take over the whole display. An
/// update sent from the web page waits there for an answer: holding any button
/// for 5 s installs it, anything else refuses it.
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

/// @brief The user interface: screens, buttons, settings menu, alerts,
/// backlight and status LED.
///
/// Call begin() once, then update() often from loop(). Its input and state
/// are in Ui.cpp, its drawing in UiDraw.cpp. Each screen is drawn into a frame
/// buffer first and sent to the display whole, so it doesn't flicker.
class Ui {
 public:
  /// @brief Sets up the buttons, the display and its frame buffer, and keeps
  /// the services it shows and controls, which must outlive it.
  ///
  /// A button already held now, e.g. D1 still held from switching on, is
  /// ignored until it's released, so it doesn't open the menu.
  /// @param sensor The sensor, for its maintenance operations and settings.
  /// @param settings The user settings, which the menu changes and saves.
  /// @param airQuality Every metric's level and the overall air quality.
  /// @param history The readings' history, for the trend pages.
  /// @param wifi Wi-Fi, for the Wi-Fi page and the setup.
  /// @param ota Firmware updates, which wait for an answer on the monitor.
  /// @param mqtt Home Assistant (MQTT), whose state the Wi-Fi page shows.
  /// @param battery The battery, for its warnings and the Device page.
  void begin(SensorService& sensor, Settings& settings, const aq::AirQuality& airQuality,
             const aq::MetricHistory& history, WifiService& wifi, Ota& ota,
             const MqttService& mqtt, const power::BatteryWatch& battery);
  /// @brief Handles the buttons, alerts, backlight and status LED, and
  /// redraws the screen; call it often from loop().
  ///
  /// Also applies settings changed elsewhere (see settingsChanged()), shows
  /// the outcome of a sensor operation as a toast, and closes a menu left
  /// alone for a minute.
  /// @param snapshot The sensor's latest state.
  /// @param nowMs The time, as millis().
  void update(const SensorSnapshot& snapshot, uint32_t nowMs);
  /// @brief Says the settings were changed elsewhere (the web page, Home
  /// Assistant): the next update() applies and saves them.
  void settingsChanged() { settingsChanged_ = true; }
  /// @brief Says the monitor is switching off for an empty battery, at once
  /// and for good: from now on the screen, undimmed, shows only that.
  void showBatteryEmpty();
  /// @brief Whether Power off was chosen and confirmed in the menu; the screen
  /// then says how to turn the monitor on again.
  bool powerOffRequested() const { return poweringOff_; }
  /// @brief Counts down on that screen; the main loop switches off after it.
  /// @param secondsLeft The time left until it switches off, in s.
  void showPoweringOff(uint32_t secondsLeft);

 private:
  /// @brief What the screen shows, and so what the buttons do: the pages, the
  /// settings menu, a menu item's value being changed (Edit), the question
  /// before a menu action (Confirm), or the web page's QR code (Link).
  enum class Mode : uint8_t { Pages, Menu, Edit, Confirm, Link };
  /// The pages, in the order D0 and D2 step through them.
  enum class Page : uint8_t {
    Overview,   ///< The main values at a glance.
    Particles,  ///< PM1 to PM10, with level bars.
    Gases,      ///< CO2, formaldehyde, VOC and NOx, with level bars.
    Comfort,    ///< Temperature and humidity, dew point, and a verdict.
    TrendHour,  ///< A trend graph of the last hour.
    TrendDay,   ///< A trend graph of the last 24 h.
    TrendWeek,  ///< A trend graph of the last 7 days.
    Network,    ///< Wi-Fi and Home Assistant.
    Device,     ///< The sensor, the battery, and the firmware version.
    Count       ///< How many pages there are.
  };
  /// The settings menu's items, in menu order, grouped: display, Wi-Fi, sensor, device.
  enum class Item : uint8_t {
    Brightness,          ///< Backlight, 10 to 100 %.
    DimAfter,            ///< Time without input before the screen dims (kDimOptionsS).
    IdleScreen,          ///< Dimmed or off when idle.
    Units,               ///< Temperatures in degC or degF.
    Led,                 ///< The status LED: off, dim or bright.
    Silence,             ///< Which alerts may turn the screen on (see Silence).
    Wifi,                ///< Wi-Fi on or off.
    WifiSetup,           ///< Sets up Wi-Fi with a phone.
    ForgetWifi,          ///< Forgets every saved network.
    TempOffset,          ///< Added to the measured temperature, -10 to +10 degC.
    Altitude,            ///< For the CO2 sensor's pressure compensation, 0 to 3000 m.
    Co2SelfCalibration,  ///< The CO2 sensor's automatic self-calibration, on or off.
    CalibrateCo2,        ///< Forced CO2 recalibration to fresh outdoor air.
    ResetCo2,            ///< Erases every CO2 calibration.
    CleanFan,            ///< Runs the fan at full speed to clean it.
    HeatHumiditySensor,  ///< Heats the humidity sensor after damp air.
    RestartVoc,          ///< Restarts the VOC and NOx learning.
    Restart,             ///< Restarts the monitor.
    PowerOff,            ///< Switches the monitor off (see power::powerOff()).
    FactoryReset,        ///< Erases the settings and Wi-Fi networks and restarts.
    Close,               ///< Closes the menu.
    Count                ///< How many items there are.
  };
  /// @brief What a button does now, drawn beside it: nothing, previous (a
  /// triangle up), next (down), select (a circle), raise or lower a value,
  /// confirm or done (a check mark), cancel or close (a cross).
  enum class Hint : uint8_t { None, Up, Down, Select, Plus, Minus, Check, Cross };

  /// Metrics the trend pages cycle through with D1.
  static const std::array<aq::Metric, 10> kTrendMetrics;

  // Input and state (Ui.cpp)
  /// @brief Passes a button's event on to what the screen shows.
  ///
  /// Any event counts as input, which keeps the screen awake, and a press
  /// that wakes the screen does nothing else. While an update waits for an
  /// answer, every event goes to onUpdatePromptEvent(); while one arrives or
  /// installs, none does anything. During the Wi-Fi setup, D1 cancels it, or
  /// closes it once it has worked; a click closes an alert.
  /// @param button The button: 0 for D0, 1 for D1, 2 for D2.
  /// @param event What it did.
  /// @param nowMs The time, as millis().
  void handleButton(size_t button, aq::ButtonEvent event, uint32_t nowMs);
  /// @brief Handles a button on the pages: D0 and D2 step through them
  /// (wrapping around), D1 does the page's action, holding D1 opens the menu.
  ///
  /// D1 cycles the trend pages through kTrendMetrics, takes the Wi-Fi page's
  /// next step (onNetworkAction()), and clears the sensor's errors on the
  /// Device page.
  /// @param button The button: 0 for D0, 1 for D1, 2 for D2.
  /// @param event What it did.
  void onPageEvent(size_t button, aq::ButtonEvent event);
  /// @brief Handles a button in the menu: D0 and D2 move (wrapping around), D1
  /// selects, holding D1 closes it.
  /// @param button The button: 0 for D0, 1 for D1, 2 for D2.
  /// @param event What it did.
  void onMenuEvent(size_t button, aq::ButtonEvent event);
  /// @brief Handles a button while a value is changed: D0 raises it, D2 lowers
  /// it (holding repeats), D1 saves it and goes back to the menu.
  /// @param button The button: 0 for D0, 1 for D1, 2 for D2.
  /// @param event What it did.
  void onEditEvent(size_t button, aq::ButtonEvent event);
  /// @brief Handles a button while asking before an action: holding D1 does
  /// it, D0 or D2 goes back to the menu.
  /// @param button The button: 0 for D0, 1 for D1, 2 for D2.
  /// @param event What it did.
  void onConfirmEvent(size_t button, aq::ButtonEvent event);
  /// @brief Handles a button while an update waits for an answer: holding any
  /// button says yes (see updatePrompt()); a short press says no.
  /// @param button The button: 0 for D0, 1 for D1, 2 for D2.
  /// @param event What it did.
  /// @param nowMs The time, as millis().
  void onUpdatePromptEvent(size_t button, aq::ButtonEvent event, uint32_t nowMs);
  /// @brief Notices an update arriving and ending, and how long the button is
  /// held.
  ///
  /// Wakes the screen for the question, and says in a toast when an update
  /// ends without being installed. Held for Ota::kConfirmHoldMs, the button
  /// confirms the update; let go sooner, it refuses it.
  /// @param nowMs The time, as millis().
  void updatePrompt(uint32_t nowMs);
  /// @brief D1 on the Wi-Fi page takes the obvious next step.
  ///
  /// That's turning Wi-Fi on if it's off with networks saved, showing the web
  /// page's QR code if connected, and starting the Wi-Fi setup otherwise.
  void onNetworkAction();
  /// @brief Selects a menu item with D1.
  ///
  /// A number starts changing it (Mode::Edit); a choice steps to the next one
  /// and is saved at once; an action is asked about first (Mode::Confirm);
  /// Set up Wi-Fi goes to the Wi-Fi page, where its result shows, and starts
  /// the setup; Close closes the menu.
  /// @param item The item.
  void select(Item item);
  /// @brief Changes an item's setting by one step, within its limits; choices
  /// wrap around.
  ///
  /// Doesn't save it (see commitSettings()), but the brightness shows at once.
  /// Holding the button changes the altitude in bigger steps (see
  /// editRepeats_).
  /// @param item The item.
  /// @param direction +1 for up or the next choice, -1 for down or the
  ///   previous one; on-off settings toggle either way.
  void adjust(Item item, int direction);
  /// @brief Does a confirmed menu action.
  ///
  /// Power off only sets powerOffRequested(): the main loop switches off.
  /// @param item The action's item.
  void execute(Item item);
  /// Saves the settings, and gives the sensor its part of them.
  void commitSettings();
  /// @brief Picks the alert to show: the worst metric at warning or alarm that
  /// hasn't been acknowledged at that level; else a battery warning.
  ///
  /// A new alert turns the screen on unless silenced (see Silence). Left
  /// alone, it closes by itself, as if dismissed, a minute after the screen
  /// came on to show it.
  /// @param nowMs The time, as millis().
  void updateAlerts(uint32_t nowMs);
  /// @brief Acknowledges the alert showing: the air alert if there's one, else
  /// the battery's.
  ///
  /// An alert waiting behind it gets its full time on screen.
  void dismissAlert();
  /// Whether an air or battery alert is showing.
  bool alertShowing() const { return alertVisible_ || batteryAlert_ != 0; }
  /// @brief Dims the screen, or switches it off, after idleAfterS() without
  /// input on the pages, and brings it back.
  /// @param nowMs The time, as millis().
  void updateBacklight(uint32_t nowMs);
  /// @brief How long without input until the screen dims or switches off, in
  /// s; 0 = never.
  ///
  /// Silenced, the screen still switches off when it's set never to dim, after
  /// kSilencedIdleS.
  uint32_t idleAfterS() const;
  /// @brief Sets the backlight: full brightness, dimmed, or (idle, if so set or
  /// silenced) the screen switched off.
  ///
  /// Only the backlight and the display chip sleep: the sensor shares the
  /// screen's power, which stays on.
  void applyBacklight();
  /// @brief Shows a short message over the title bar for kToastMs.
  /// @param text The message, with the markup (see Text.h); cut at 47
  ///   characters.
  /// @param nowMs The time, as millis().
  void showToast(const char* text, uint32_t nowMs);
  /// Whether some values aren't available yet, e.g. the sensor isn't measuring yet.
  bool warmingUp() const;

  // Drawing (UiDraw.cpp)
  /// @brief Draws the whole screen into the frame buffer and sends it to the
  /// display.
  ///
  /// Some screens take over the whole display: Battery empty, Power off, an
  /// update, the Wi-Fi setup. Otherwise it's the page, menu or question, any
  /// alert over it, the toast, and the button hints.
  /// @param nowMs The time, as millis().
  void draw(uint32_t nowMs);
  /// @brief Draws a page's title bar: the title on the left; on the right what
  /// the sensor is doing if it's busy, else the overall level and the Wi-Fi
  /// icon.
  /// @param title The title, with the markup (see Text.h).
  void drawTitle(const char* title);
  /// @brief Draws the hints beside the three buttons.
  /// @param d0 The hint beside D0.
  /// @param d1 The hint beside D1.
  /// @param d2 The hint beside D2.
  void drawHints(Hint d0, Hint d1, Hint d2);
  /// @brief Draws one button hint in the strip on the left.
  /// @param hint The hint.
  /// @param y Its vertical centre, in px.
  void drawHint(Hint hint, int y);
  /// Draws a dot for each page at the right edge, the current one highlighted.
  void drawPageDots();
  /// @brief Draws the Overview page: CO2 and PM2.5 large, the other gases
  /// below, then temperature and humidity, with a verdict if something's off.
  void drawOverview();
  /// @brief Draws a page of metrics, a row each: name, value, and a level bar
  /// with ticks at the upper warning and alarm limits.
  /// @param metrics The metrics, top to bottom.
  /// @param count How many there are; 4 fit.
  void drawMetricRows(const aq::Metric* metrics, size_t count);
  /// @brief Draws the Comfort page: temperature and humidity, the dew point and
  /// water vapour, and a verdict.
  void drawComfort();
  /// Draws a trend page: the metric chosen with D1 over the page's period.
  void drawTrend();
  /// @brief Draws a trend graph of a metric's history, with its warning and
  /// alarm limits as dotted lines.
  ///
  /// The newest `span` samples are averaged over each pixel column, so the
  /// graph shows the whole period whatever its length; the right edge is now,
  /// and columns older than the oldest sample stay empty. The scale fits the
  /// values shown, but spans at least the metric's smallest span, so sensor
  /// noise doesn't look like a trend.
  /// @tparam Ring The history's ring buffer: aq::MetricHistory::Fine or
  ///   aq::MetricHistory::Coarse.
  /// @param ring The metric's samples, oldest first.
  /// @param span How many of the newest samples the graph covers, whether
  ///   there are that many yet or not.
  /// @param metric The metric.
  /// @param x0 The plot's left edge, in px.
  /// @param y0 Its top edge, in px.
  /// @param x1 Its right edge, in px.
  /// @param y1 Its bottom edge, in px.
  template <typename Ring>
  void drawChart(const Ring& ring, size_t span, aq::Metric metric, int x0, int y0, int x1, int y1);
  /// @brief Draws the Device page: the sensor and its state, the battery, the
  /// uptime and firmware version, and wrong web passwords.
  /// @param nowMs The time, as millis().
  void drawDevice(uint32_t nowMs);
  /// @brief Draws the Wi-Fi page: the network, the monitor's addresses, the
  /// web password and Home Assistant; or why there's no connection.
  void drawNetwork();
  /// @brief Draws four bars, filled by signal strength, ending at `right`.
  /// @param right Their right edge, in px.
  /// @param baseline The line they stand on, in px.
  /// @param rssi The signal strength, in dBm: all four bars from -55, three
  ///   from -65, two from -75, else one.
  void drawSignal(int right, int baseline, int8_t rssi);
  /// @brief Draws the Wi-Fi icon: a dot and two arcs opening upwards, 11 px
  /// wide.
  /// @param cx Its centre, in px.
  /// @param bottom Its bottom edge, in px.
  /// @param color Its colour, as RGB565.
  void drawWifiIcon(int cx, int bottom, uint16_t color);
  /// @brief Draws the Wi-Fi setup screen: a QR code to join the monitor's
  /// hotspot, then one to open the setup page, the network it tries, and
  /// whether that worked.
  void drawSetup();
  /// Draws the QR code of the web page, to open it on a phone.
  void drawLink();
  /// @brief Draws an update arriving from the web page, stored beside the
  /// running firmware until it is confirmed; then installing.
  void drawUpdate();
  /// @brief Draws the question for an uploaded update, waiting for a yes on
  /// the monitor itself, and how long the button has been held.
  /// @param nowMs The time, as millis().
  void drawUpdatePrompt(uint32_t nowMs);
  /// Draws the Power off screen: how to turn the monitor on again, and the countdown.
  void drawPoweringOff();
  /// @brief Draws a QR code at the right edge, in a white square with a quiet
  /// zone of two modules.
  ///
  /// 4 px modules scan well with phones. The code is version 1 to 4, the
  /// smallest that holds `text`.
  /// @param text What it encodes, up to 78 bytes.
  /// @return The square's left edge, in px.
  int drawQr(const char* text);
  /// @brief Draws the settings menu: six rows with their values, scrolled to
  /// keep the selected item in view.
  void drawMenu();
  /// Draws the question before the action of confirmItem_: what it does, and how to answer.
  void drawConfirm();
  /// Draws the air alert over the page: the metric, its value, and what to do.
  void drawAlert();
  /// Draws an alert like the air alert, for a battery running low.
  void drawBatteryAlert();
  /// @brief Draws the toast over the title bar: the one band every screen has
  /// in the same place, so the message never cuts through a row.
  ///
  /// It ends above the line under the title, and above the first row of the
  /// web page's QR code.
  void drawToast();

  // Formatting (UiDraw.cpp)
  /// @brief A value in the units shown: a temperature in degF if so set.
  /// @param metric The metric.
  /// @param value Its value, in the sensor's units (degC for a temperature).
  /// @return The value to show.
  float displayValue(aq::Metric metric, float value) const;
  /// @brief A value as text, in the units shown, with as many decimals as its
  /// metric needs.
  /// @param metric The metric.
  /// @param value Its value, in the sensor's units; NaN if unknown.
  /// @return The text; `--` if unknown.
  String valueText(aq::Metric metric, float value) const;
  /// @brief The unit a metric is shown in, with the markup (see Text.h); `""`
  /// for the VOC and NOx indices, which have none.
  /// @param metric The metric.
  const char* unitText(aq::Metric metric) const;
  /// @brief A menu item's value, shown at the right of its row.
  /// @param item The item.
  /// @return The value, with the markup; `>` for an action, `""` for Close.
  String itemValue(Item item) const;

  Lgfx tft_;  ///< The display.
  LGFX_Sprite frame_{&tft_};  ///< The frame buffer, the size of the screen.
  StatusLed led_;
  aq::ButtonLogic buttons_[3];  ///< D0, D1 and D2.

  SensorService* sensor_ = nullptr;
  Settings* settings_ = nullptr;
  const aq::AirQuality* aq_ = nullptr;  ///< Every metric's level and the overall air quality.
  const aq::MetricHistory* history_ = nullptr;
  WifiService* wifi_ = nullptr;
  Ota* ota_ = nullptr;
  const MqttService* mqtt_ = nullptr;
  const power::BatteryWatch* battery_ = nullptr;
  SensorSnapshot snapshot_;  ///< The sensor's latest state, from update().
  bool settingsChanged_ = false;  ///< See settingsChanged().

  Mode mode_ = Mode::Pages;
  Page page_ = Page::Overview;
  Item item_ = Item::Brightness;  ///< The menu's selected item.
  int menuTop_ = 0;  ///< The item in the menu's top row.
  Item confirmItem_ = Item::Close;  ///< The action Mode::Confirm asks about.
  size_t trendMetric_ = 0;  ///< The trend pages' metric, as an index into kTrendMetrics.
  /// Events from holding a button while a value is changed, so adjust() can
  /// take bigger steps.
  uint32_t editRepeats_ = 0;

  /// Alerts: the level each metric was last acknowledged at.
  std::array<aq::Level, aq::kMetricCount> acknowledged_{};
  bool alertVisible_ = false;  ///< An air alert is showing.
  aq::Metric alertMetric_ = aq::Metric::Co2;  ///< Its metric.
  aq::Level alertLevel_ = aq::Level::Unknown;  ///< Its level.
  uint32_t alertShownMs_ = 0;  ///< When the alert's time on screen started, as millis().
  /// The battery's warning (BatteryWatch::warning()) showing, after any air
  /// alert; 0 if none.
  uint8_t batteryAlert_ = 0;
  uint8_t batteryAcknowledged_ = 0;  ///< The last battery warning acknowledged.
  bool batteryEmpty_ = false;  ///< See showBatteryEmpty().
  bool poweringOff_ = false;  ///< See powerOffRequested().
  uint32_t offInS_ = 0;  ///< The Power off countdown, in s.

  /// When a button was last used, as millis(); also set to wake the screen.
  uint32_t lastInputMs_ = 0;
  bool dimmed_ = false;  ///< Idle: the screen is dimmed, or switched off.
  bool screenOff_ = false;  ///< Idle, with the screen switched off.
  uint32_t lastDrawMs_ = 0;  ///< When the screen was last drawn, as millis().
  bool dirty_ = true;  ///< Something changed: draw at the next update().

  // Update prompt: the button held to say yes, and since when.
  OtaState otaState_ = OtaState::Idle;  ///< The update's state as updatePrompt() last saw it.
  bool holding_ = false;  ///< A button is held to say yes.
  size_t holdButton_ = 0;  ///< Which one.
  uint32_t holdStartMs_ = 0;  ///< Since when, as millis().

  char toast_[48] = "";  ///< The toast's message (see showToast()).
  uint32_t toastUntilMs_ = 0;  ///< When it goes, as millis().
  /// SensorSnapshot::resultSequence of the last outcome shown, so each shows once.
  uint32_t resultSequence_ = 0;
};
