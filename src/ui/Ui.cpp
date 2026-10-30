// SPDX-License-Identifier: GPL-3.0-or-later

#include "Ui.h"

#include <algorithm>
#include <iterator>

namespace {

// D0 doubles as BOOT and is pulled up; D1/D2 are pulled down (so they can wake
// the chip from deep sleep), so they read HIGH when pressed.
constexpr uint8_t kButtonPins[] = {0, 1, 2};
constexpr bool kPressedLevel[] = {LOW, HIGH, HIGH};

constexpr uint8_t kDimmedBrightness = 4;  // of 255
constexpr uint32_t kDrawIntervalMs = 250;
constexpr uint32_t kToastMs = 4000;
constexpr uint32_t kMenuTimeoutMs = 60000;
constexpr uint32_t kAlertTimeoutMs = 60000;
constexpr uint16_t kOutdoorCo2Ppm = 420;

template <typename E>
E stepEnum(E value, int delta, E count) {
  const int n = static_cast<int>(count);
  return static_cast<E>((static_cast<int>(value) + delta + n) % n);
}

uint8_t backlight(uint8_t percent) { return static_cast<uint8_t>(percent * 255 / 100); }

}  // namespace

void Ui::begin(SensorService& sensor, Settings& settings, const aq::AirQuality& airQuality,
               const aq::MetricHistory& history, WifiService& wifi, const Ota& ota,
               const MqttService& mqtt) {
  sensor_ = &sensor;
  settings_ = &settings;
  aq_ = &airQuality;
  history_ = &history;
  wifi_ = &wifi;
  ota_ = &ota;
  mqtt_ = &mqtt;

  for (size_t i = 0; i < 3; ++i) {
    pinMode(kButtonPins[i], kPressedLevel[i] == LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  }
  tft_.init();
  tft_.setRotation(1);  // landscape, buttons on the left (D0 top, D2 bottom)
  tft_.setBrightness(backlight(settings.brightness));
  frame_.setColorDepth(16);
  if (!frame_.createSprite(tft_.width(), tft_.height())) Serial.println("UI: no memory for frame");
  lastInputMs_ = millis();
}

void Ui::update(const SensorSnapshot& snapshot, uint32_t nowMs) {
  snapshot_ = snapshot;
  if (settingsChanged_) {
    settingsChanged_ = false;
    if (!dimmed_) tft_.setBrightness(backlight(settings_->brightness));
    commitSettings();
    dirty_ = true;
  }
  if (snapshot.resultSequence != resultSequence_) {
    resultSequence_ = snapshot.resultSequence;
    showToast(snapshot.result, nowMs);
  }
  updateAlerts(nowMs);
  for (size_t i = 0; i < 3; ++i) {
    const bool down = digitalRead(kButtonPins[i]) == kPressedLevel[i];
    handleButton(i, buttons_[i].update(down, nowMs), nowMs);
  }
  // Left alone, menus close by themselves, saving a value being changed.
  if (mode_ != Mode::Pages && nowMs - lastInputMs_ >= kMenuTimeoutMs) {
    if (mode_ == Mode::Edit) commitSettings();
    mode_ = Mode::Pages;
    dirty_ = true;
  }
  updateBacklight(nowMs);
  led_.update(aq_->overall(), warmingUp(), alertVisible_ && alertLevel_ == aq::Level::Alarm,
              settings_->led, nowMs);
  if (dirty_ || nowMs - lastDrawMs_ >= kDrawIntervalMs) {
    draw(nowMs);
    lastDrawMs_ = nowMs;
    dirty_ = false;
  }
}

void Ui::handleButton(size_t button, aq::ButtonEvent event, uint32_t nowMs) {
  using aq::ButtonEvent;
  if (event == ButtonEvent::None) return;
  lastInputMs_ = nowMs;
  if (event == ButtonEvent::Press) {
    // A press that wakes the screen does nothing else.
    if (dimmed_) {
      buttons_[button].swallow();
      updateBacklight(nowMs);
    }
    return;
  }
  dirty_ = true;
  if (ota_->active() || ota_->succeeded()) return;  // nothing to do but wait
  if (wifi_->setupActive()) {
    // D1 cancels the setup, or closes it once it has worked.
    if (button == 1 && event == ButtonEvent::Click) wifi_->stopSetup();
    return;
  }
  if (alertVisible_) {
    if (event == ButtonEvent::Click) dismissAlert();
    return;
  }
  switch (mode_) {
    case Mode::Pages:
      onPageEvent(button, event);
      break;
    case Mode::Menu:
      onMenuEvent(button, event);
      break;
    case Mode::Edit:
      onEditEvent(button, event);
      break;
    case Mode::Confirm:
      onConfirmEvent(button, event);
      break;
    case Mode::Link:
      if (event == ButtonEvent::Click) mode_ = Mode::Pages;
      break;
  }
}

void Ui::onPageEvent(size_t button, aq::ButtonEvent event) {
  using aq::ButtonEvent;
  if (button == 1 && event == ButtonEvent::LongPress) {
    mode_ = Mode::Menu;
    item_ = Item::Brightness;
    menuTop_ = 0;
    return;
  }
  if (event != ButtonEvent::Click) return;
  if (button == 0) {
    page_ = stepEnum(page_, -1, Page::Count);
  } else if (button == 2) {
    page_ = stepEnum(page_, +1, Page::Count);
  } else if (page_ == Page::TrendHour || page_ == Page::TrendDay) {
    trendMetric_ = (trendMetric_ + 1) % kTrendMetrics.size();
  } else if (page_ == Page::Network) {
    onNetworkAction();
  }
}

// D1 on the Wi-Fi page takes the obvious next step.
void Ui::onNetworkAction() {
  if (!settings_->wifiEnabled && wifi_->saved().size() > 0) {
    settings_->wifiEnabled = true;
    commitSettings();
  } else if (wifi_->state() == WifiState::Connected) {
    mode_ = Mode::Link;  // QR code of the web page
  } else {
    wifi_->startSetup();
  }
}

void Ui::onMenuEvent(size_t button, aq::ButtonEvent event) {
  using aq::ButtonEvent;
  if (button == 1) {
    if (event == ButtonEvent::Click) {
      select(item_);
    } else if (event == ButtonEvent::LongPress) {
      mode_ = Mode::Pages;
    }
    return;
  }
  // Clicks move one row; holding keeps scrolling.
  item_ = stepEnum(item_, button == 0 ? -1 : +1, Item::Count);
}

void Ui::onEditEvent(size_t button, aq::ButtonEvent event) {
  using aq::ButtonEvent;
  if (button == 1) {
    if (event == ButtonEvent::Click) {
      mode_ = Mode::Menu;
      commitSettings();
    }
    return;
  }
  editRepeats_ = event == ButtonEvent::Click ? 0 : editRepeats_ + 1;
  adjust(item_, button == 0 ? +1 : -1);
}

void Ui::onConfirmEvent(size_t button, aq::ButtonEvent event) {
  using aq::ButtonEvent;
  if (button == 1 && event == ButtonEvent::LongPress) {
    mode_ = Mode::Pages;
    execute(confirmItem_);
  } else if (button != 1 && event == ButtonEvent::Click) {
    mode_ = Mode::Menu;
  }
}

void Ui::select(Item item) {
  switch (item) {
    case Item::Brightness:
    case Item::TempOffset:
    case Item::Altitude:
      mode_ = Mode::Edit;
      editRepeats_ = 0;
      break;
    case Item::DimAfter:
    case Item::Units:
    case Item::Led:
    case Item::Wifi:
    case Item::Co2SelfCalibration:
      adjust(item, +1);
      commitSettings();
      break;
    case Item::WifiSetup:
      mode_ = Mode::Pages;
      page_ = Page::Network;  // where the result shows once the setup ends
      wifi_->startSetup();
      break;
    case Item::CleanFan:
    case Item::CalibrateCo2:
    case Item::ForgetWifi:
    case Item::Restart:
    case Item::FactoryReset:
      confirmItem_ = item;
      mode_ = Mode::Confirm;
      break;
    case Item::Close:
    case Item::Count:
      mode_ = Mode::Pages;
      break;
  }
}

void Ui::adjust(Item item, int direction) {
  Settings& s = *settings_;
  switch (item) {
    case Item::Brightness:
      s.brightness = static_cast<uint8_t>(std::clamp(s.brightness + direction * 10, 10, 100));
      tft_.setBrightness(backlight(s.brightness));
      break;
    case Item::DimAfter: {
      const size_t n = std::size(kDimOptionsS);
      size_t i = 0;
      while (i < n && kDimOptionsS[i] != s.dimAfterS) ++i;
      s.dimAfterS = kDimOptionsS[(i + n + direction) % n];
      break;
    }
    case Item::Units:
      s.fahrenheit = !s.fahrenheit;
      break;
    case Item::Led:
      s.led = static_cast<LedMode>((static_cast<int>(s.led) + direction + 3) % 3);
      break;
    case Item::Wifi:
      s.wifiEnabled = !s.wifiEnabled;
      break;
    case Item::TempOffset:
      s.tempOffsetC = std::clamp(std::round(s.tempOffsetC * 10 + direction) / 10, -10.0f, 10.0f);
      break;
    case Item::Altitude: {
      // Holding the button speeds up after a second or two.
      const int step = editRepeats_ > 10 ? 100 : 10;
      s.altitudeM = static_cast<uint16_t>(
          std::clamp((s.altitudeM + direction * step) / step * step, 0, 3000));
      break;
    }
    case Item::Co2SelfCalibration:
      s.co2SelfCalibration = !s.co2SelfCalibration;
      break;
    default:
      break;
  }
}

void Ui::execute(Item item) {
  switch (item) {
    case Item::CleanFan:
      sensor_->cleanFan();
      break;
    case Item::CalibrateCo2:
      sensor_->recalibrateCo2(kOutdoorCo2Ppm);
      break;
    case Item::ForgetWifi:
      wifi_->forgetAll();
      showToast("Wi-Fi networks forgotten", millis());
      break;
    case Item::Restart:
      ESP.restart();
      break;
    case Item::FactoryReset:
      settings::erase();
      wifi_->forgetAll();
      ESP.restart();
      break;
    default:
      break;
  }
}

void Ui::commitSettings() {
  settings::save(*settings_);
  sensor_->applyConfig(
      {settings_->tempOffsetC, settings_->altitudeM, settings_->co2SelfCalibration});
}

void Ui::updateAlerts(uint32_t nowMs) {
  // Show the worst metric that is at warning or alarm and hasn't been
  // acknowledged at that level. Once a metric improves its acknowledgement is
  // lowered too, so it alerts again if it gets worse.
  bool found = false;
  aq::Metric metric = aq::Metric::Co2;
  aq::Level level = aq::Level::Good;
  for (aq::Metric m : aq::kPriority) {
    aq::Level& acknowledged = acknowledged_[aq::idx(m)];
    const aq::Level current = aq_->level(m);
    if (current < acknowledged) acknowledged = current;
    if (aq_->thresholds(m).any() && current >= aq::Level::Warning && current > acknowledged &&
        current > level) {
      found = true;
      metric = m;
      level = current;
    }
  }
  const bool isNew = found && (!alertVisible_ || metric != alertMetric_ || level != alertLevel_);
  alertVisible_ = found;
  alertMetric_ = metric;
  alertLevel_ = level;
  if (isNew) {
    alertShownMs_ = nowMs;
    lastInputMs_ = nowMs;  // wake the screen
    dirty_ = true;
  }
  // Left alone, an alert closes by itself, as if dismissed.
  if (alertVisible_ && nowMs - alertShownMs_ >= kAlertTimeoutMs) dismissAlert();
}

void Ui::dismissAlert() {
  acknowledged_[aq::idx(alertMetric_)] = alertLevel_;
  alertVisible_ = false;
  dirty_ = true;
}

void Ui::updateBacklight(uint32_t nowMs) {
  // Screens that show something to scan or wait for stay bright.
  const bool fullScreen = wifi_->setupActive() || ota_->active() || ota_->succeeded();
  const bool dim = settings_->dimAfterS != 0 && mode_ == Mode::Pages && !fullScreen &&
                   !alertVisible_ && nowMs - lastInputMs_ >= settings_->dimAfterS * 1000UL;
  if (dim == dimmed_) return;
  dimmed_ = dim;
  tft_.setBrightness(dim ? kDimmedBrightness : backlight(settings_->brightness));
}

void Ui::showToast(const char* text, uint32_t nowMs) {
  strlcpy(toast_, text, sizeof toast_);
  toastUntilMs_ = nowMs + kToastMs;
  dirty_ = true;
}

bool Ui::warmingUp() const {
  if (snapshot_.state != SensorState::Measuring) return true;
  for (float v : snapshot_.measurement.values) {
    if (std::isnan(v)) return true;
  }
  return false;
}
