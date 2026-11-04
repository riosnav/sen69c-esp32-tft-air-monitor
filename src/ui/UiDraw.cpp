// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing of every screen. The display is 240x135 in landscape; the strip on
// the left holds the button hints, so content starts at x = kLeft.

#include <Comfort.h>

#include <algorithm>
#include <cstring>

#include "Device.h"
#include "Power.h"
#include "Screens.h"
#include "Text.h"
#include "Theme.h"
#include "Ui.h"
#include "net/Web.h"

using aq::Level;
using aq::Metric;

namespace {

constexpr int kLeft = 22;    // content left edge
constexpr int kRight = 226;  // content right edge; the page dots sit beyond it
constexpr int kTitleBaseline = 13;
// Vertical centres of the D0, D1 and D2 buttons beside the screen.
constexpr int kButtonY[] = {11, 67, 124};
constexpr int kMenuRows = 6;
constexpr int kMenuRowHeight = 19;
constexpr uint16_t kQrLight = theme::rgb(255, 255, 255);

const lgfx::IFont* const kSmall = &fonts::DejaVu9;
const lgfx::IFont* const kBody = &fonts::DejaVu12;
const lgfx::IFont* const kMedium = &fonts::DejaVu18;
const lgfx::IFont* const kLarge = &fonts::DejaVu24;
const lgfx::IFont* const kHuge = &fonts::DejaVu40;

// In Ui::Item order.
constexpr const char* kItemLabels[] = {
    "Brightness",   "Dim after",    "Idle screen",    "Units",          "Status LED",
    "Wi-Fi",        "Set up Wi-Fi", "Forget Wi-Fi",
    "Temp. offset", "Altitude",     "CO_2 self-calibration", "Calibrate CO_2",
    "Reset CO_2 calibration", "Clean fan", "Heat humidity sensor", "Restart VOC learning",
    "Restart",      "Power off",    "Factory reset", "Close",
};

const char* label(Metric m) {
  switch (m) {
    case Metric::Pm1:
      return "PM1";
    case Metric::Pm25:
      return "PM2.5";
    case Metric::Pm4:
      return "PM4";
    case Metric::Pm10:
      return "PM10";
    case Metric::Co2:
      return "CO_2";
    case Metric::Hcho:
      return "HCHO";
    case Metric::Voc:
      return "VOC";
    case Metric::Nox:
      return "NO_x";
    case Metric::Temperature:
      return "Temperature";
    case Metric::Humidity:
      return "Humidity";
    default:
      return "";
  }
}

// Range of the level bars, and the smallest span a trend graph zooms into
// (so sensor noise doesn't look like a trend).
struct Scale {
  float barMin;
  float barMax;
  float minSpan;
};

Scale scaleOf(Metric m) {
  switch (m) {
    case Metric::Pm1:
    case Metric::Pm25:
      return {0, 75, 5};
    case Metric::Pm4:
      return {0, 100, 5};
    case Metric::Pm10:
      return {0, 200, 5};
    case Metric::Co2:
      return {400, 3200, 100};
    case Metric::Hcho:
      return {0, 120, 10};
    case Metric::Voc:
      return {0, 500, 20};
    case Metric::Nox:
      return {0, 300, 5};
    case Metric::Temperature:
      return {10, 35, 1};
    case Metric::Humidity:
      return {0, 100, 5};
    default:
      return {0, 1, 1};
  }
}

// String(value, decimals) pads to decimals + 2 characters, which puts a space
// before whole numbers with one digit (" 1"); this doesn't.
String number(float value, unsigned int decimals) {
  if (decimals > 0 || !std::isfinite(value)) return String(value, decimals);
  return String(std::lround(value));
}

String duration(uint32_t ms) {
  const uint32_t minutes = ms / 60000;
  if (minutes < 1) return "<1 min";
  if (minutes < 60) return String(minutes) + " min";
  const uint32_t hours = minutes / 60;
  if (hours < 48) return String(hours) + " h " + String(minutes % 60) + " min";
  return String(hours / 24) + " d " + String(hours % 24) + " h";
}

String statusText(uint32_t value) {
  SEN69CDeviceStatus s{};
  s.value = value;
  String text = "Error:";
  if (s.fanError) text += " fan";
  if (s.fanSpeedWarning) text += " fan speed";
  if (s.rhtError) text += " RH/T";
  if (s.gasError) text += " gas";
  if (s.co21Error || s.co22Error) text += " CO_2";
  if (s.hchoError) text += " HCHO";
  if (s.pmError) text += " PM";
  return text;
}

bool isHigh(const aq::Thresholds& t, float value) {
  return std::isnan(t.warnLow) || value >= (t.warnLow + t.warnHigh) / 2;
}

struct Verdict {
  const char* text;
  Level level;
};

Verdict comfortVerdict(const aq::AirQuality& aq, const aq::Measurement& m) {
  const Level t = aq.level(Metric::Temperature);
  const Level rh = aq.level(Metric::Humidity);
  if (t == Level::Unknown || rh == Level::Unknown) return {"", Level::Unknown};
  if (rh >= Level::Warning && rh >= t) {
    return {isHigh(aq.thresholds(Metric::Humidity), m[Metric::Humidity]) ? "Too humid" : "Too dry",
            rh};
  }
  if (t >= Level::Warning) {
    return {isHigh(aq.thresholds(Metric::Temperature), m[Metric::Temperature]) ? "Too warm"
                                                                                 : "Too cold",
            t};
  }
  return {"Comfortable", Level::Good};
}

}  // namespace

void drawBatteryEmpty(LGFX_Sprite& frame) {
  frame.fillScreen(theme::kBackground);
  frame.setFont(kMedium);
  drawRich(frame, "Battery empty", kLeft, 30, theme::kAlarm);
  frame.setFont(kBody);
  drawRich(frame, "Switched off to protect it.", kLeft, 54, theme::kText);
  drawRich(frame, "With USB power connected,", kLeft, 78, theme::kMuted);
  const String back = "it starts again within " + String(power::kCheckEveryMinutes) + " min,";
  drawRich(frame, back.c_str(), kLeft, 94, theme::kMuted);
  drawRich(frame, "or at once with D1 or D2.", kLeft, 110, theme::kMuted);
}

const std::array<Metric, 10> Ui::kTrendMetrics = {
    Metric::Co2,  Metric::Pm1, Metric::Pm25, Metric::Pm4,         Metric::Pm10,
    Metric::Hcho, Metric::Voc, Metric::Nox,  Metric::Temperature, Metric::Humidity,
};

void Ui::draw(uint32_t nowMs) {
  static constexpr Metric kParticles[] = {Metric::Pm1, Metric::Pm25, Metric::Pm4, Metric::Pm10};
  static constexpr Metric kGases[] = {Metric::Co2, Metric::Hcho, Metric::Voc, Metric::Nox};

  frame_.fillScreen(theme::kBackground);
  // These take over the whole screen.
  if (batteryEmpty_ || poweringOff_) {
    if (batteryEmpty_) {
      drawBatteryEmpty(frame_);
    } else {
      drawPoweringOff();
    }
    frame_.pushSprite(0, 0);
    return;
  }
  const OtaState ota = ota_->state();
  if (ota == OtaState::Confirming) {
    drawUpdatePrompt(nowMs);
    drawHints(Hint::Check, Hint::Check, Hint::Check);
    frame_.pushSprite(0, 0);
    return;
  }
  if (ota == OtaState::Receiving || ota == OtaState::Accepted) {
    drawUpdate();
    frame_.pushSprite(0, 0);
    return;
  }
  if (wifi_->setupActive()) {
    drawSetup();
    const bool done = wifi_->setupState() == SetupState::Succeeded;
    drawHints(Hint::None, done ? Hint::Check : Hint::Cross, Hint::None);
    frame_.pushSprite(0, 0);
    return;
  }

  Hint hints[3] = {Hint::Up, Hint::Select, Hint::Down};
  switch (mode_) {
    case Mode::Pages:
      switch (page_) {
        case Page::Overview:
          drawOverview();
          break;
        case Page::Particles:
          drawTitle("Particles  ~ug/m^3");
          drawMetricRows(kParticles, 4);
          break;
        case Page::Gases:
          drawTitle("Gases");
          drawMetricRows(kGases, 4);
          break;
        case Page::Comfort:
          drawComfort();
          break;
        case Page::TrendHour:
        case Page::TrendDay:
        case Page::TrendWeek:
          drawTrend();
          break;
        case Page::Network:
          drawNetwork();
          break;
        case Page::Device:
          drawDevice(nowMs);
          break;
        default:
          break;
      }
      drawPageDots();
      break;
    case Mode::Menu:
      drawMenu();
      break;
    case Mode::Edit:
      drawMenu();
      hints[0] = Hint::Plus, hints[1] = Hint::Check, hints[2] = Hint::Minus;
      break;
    case Mode::Confirm:
      drawConfirm();
      hints[0] = Hint::Cross, hints[1] = Hint::Check, hints[2] = Hint::Cross;
      break;
    case Mode::Link:
      drawLink();
      hints[0] = hints[1] = hints[2] = Hint::Cross;
      break;
  }
  if (alertVisible_) {
    drawAlert();
    hints[0] = hints[1] = hints[2] = Hint::Cross;
  } else if (batteryAlert_ != 0) {
    drawBatteryAlert();
    hints[0] = hints[1] = hints[2] = Hint::Cross;
  }
  if (static_cast<int32_t>(toastUntilMs_ - nowMs) > 0) drawToast();
  drawHints(hints[0], hints[1], hints[2]);
  frame_.pushSprite(0, 0);
}

void Ui::drawTitle(const char* title) {
  frame_.setFont(kBody);
  const int titleRight = kLeft + drawRich(frame_, title, kLeft, kTitleBaseline, theme::kAccent);
  // Right side: what the sensor is doing if it's busy, else the overall level.
  if (snapshot_.state == SensorState::Busy) {
    // Beside a long title there's only room for a short word.
    const char* activity = snapshot_.activity;
    if (richWidth(frame_, activity) > kRight - titleRight - 8) activity = "Busy";
    drawRichRight(frame_, activity, kRight, kTitleBaseline, theme::kWarning);
  } else if (snapshot_.state == SensorState::NotFound) {
    drawRichRight(frame_, "No sensor", kRight, kTitleBaseline, theme::kAlarm);
  } else {
    const Level level = aq_->overall();
    const char* name = theme::levelName(level);
    const int width = richWidth(frame_, name) + 12;
    frame_.fillRoundRect(kRight - width, 2, width, 14, 4, theme::levelColor(level));
    drawRich(frame_, name, kRight - width + 6, kTitleBaseline, theme::kBackground);
    // Wi-Fi status beside it: grey when connected, faint while looking.
    if (wifi_->state() != WifiState::Off) {
      const bool connected = wifi_->state() == WifiState::Connected;
      drawWifiIcon(kRight - width - 11, 15, connected ? theme::kMuted : theme::kFaint);
    }
  }
  frame_.drawFastHLine(kLeft, 18, kRight - kLeft, theme::kFaint);
}

void Ui::drawHints(Hint d0, Hint d1, Hint d2) {
  const Hint hints[] = {d0, d1, d2};
  for (size_t i = 0; i < 3; ++i) drawHint(hints[i], kButtonY[i]);
}

void Ui::drawHint(Hint hint, int y) {
  constexpr int x = 8;
  const uint16_t c = theme::kMuted;
  switch (hint) {
    case Hint::Up:
      frame_.fillTriangle(x, y - 4, x - 5, y + 3, x + 5, y + 3, c);
      break;
    case Hint::Down:
      frame_.fillTriangle(x, y + 4, x - 5, y - 3, x + 5, y - 3, c);
      break;
    case Hint::Select:
      frame_.drawCircle(x, y, 4, c);
      break;
    case Hint::Plus:
      frame_.fillRect(x - 4, y - 1, 9, 2, c);
      frame_.fillRect(x - 1, y - 4, 2, 9, c);
      break;
    case Hint::Minus:
      frame_.fillRect(x - 4, y - 1, 9, 2, c);
      break;
    case Hint::Check:
      for (int d = 0; d < 2; ++d) {
        frame_.drawLine(x - 5, y + d, x - 2, y + 3 + d, c);
        frame_.drawLine(x - 2, y + 3 + d, x + 4, y - 4 + d, c);
      }
      break;
    case Hint::Cross:
      frame_.drawLine(x - 4, y - 4, x + 4, y + 4, c);
      frame_.drawLine(x - 4, y + 4, x + 4, y - 4, c);
      break;
    case Hint::None:
      break;
  }
}

void Ui::drawPageDots() {
  constexpr int kCount = static_cast<int>(Page::Count);
  constexpr int kSpacing = 9;
  const int top = kButtonY[1] - (kCount - 1) * kSpacing / 2;
  for (int i = 0; i < kCount; ++i) {
    const bool current = i == static_cast<int>(page_);
    frame_.fillCircle(234, top + i * kSpacing, 2, current ? theme::kAccent : theme::kFaint);
  }
}

void Ui::drawOverview() {
  drawTitle("Air quality");
  const aq::Measurement& m = snapshot_.measurement;

  // Headline numbers: CO2 on the left, PM2.5 on the right. Both shrink when
  // they wouldn't fit side by side (four-digit CO2 readings).
  const String co2 = valueText(Metric::Co2, m[Metric::Co2]);
  const String pm25 = valueText(Metric::Pm25, m[Metric::Pm25]);
  frame_.setFont(kBody);
  const int w = drawRich(frame_, label(Metric::Co2), kLeft, 33, theme::kMuted);
  drawRich(frame_, unitText(Metric::Co2), kLeft + w + 4, 33, theme::kMuted);
  const String pmLabel = String(label(Metric::Pm25)) + " " + unitText(Metric::Pm25);
  drawRichRight(frame_, pmLabel.c_str(), kRight, 33, theme::kMuted);
  frame_.setFont(kHuge);
  if (richWidth(frame_, co2.c_str()) + richWidth(frame_, pm25.c_str()) + 16 > kRight - kLeft) {
    frame_.setFont(kLarge);
  }
  drawRich(frame_, co2.c_str(), kLeft, 72, theme::levelColor(aq_->level(Metric::Co2)));
  drawRichRight(frame_, pm25.c_str(), kRight, 72, theme::levelColor(aq_->level(Metric::Pm25)));

  // Other gases in fixed columns, values coloured by level.
  frame_.setFont(kBody);
  const struct {
    Metric metric;
    int x;
  } gases[] = {{Metric::Hcho, kLeft}, {Metric::Voc, 104}, {Metric::Nox, 174}};
  for (const auto& gas : gases) {
    const int lw = drawRich(frame_, label(gas.metric), gas.x, 92, theme::kMuted);
    drawRich(frame_, valueText(gas.metric, m[gas.metric]).c_str(), gas.x + lw + 4, 92,
             theme::levelColor(aq_->level(gas.metric)));
  }

  // Comfort. The values are coloured by level; a verdict only appears when
  // something is off (and there is room for it).
  int x = kLeft;
  frame_.setFont(kLarge);
  x += drawRich(frame_, valueText(Metric::Temperature, m[Metric::Temperature]).c_str(), x, 126,
                theme::levelColor(aq_->level(Metric::Temperature)));
  frame_.setFont(kBody);
  x += 2 + drawRich(frame_, unitText(Metric::Temperature), x + 2, 126, theme::kMuted) + 14;
  frame_.setFont(kLarge);
  x += drawRich(frame_, valueText(Metric::Humidity, m[Metric::Humidity]).c_str(), x, 126,
                theme::levelColor(aq_->level(Metric::Humidity)));
  frame_.setFont(kBody);
  x += 2 + drawRich(frame_, "%", x + 2, 126, theme::kMuted);
  const Verdict verdict = comfortVerdict(*aq_, m);
  if (verdict.level >= Level::Warning && richWidth(frame_, verdict.text) <= kRight - x - 8) {
    drawRichRight(frame_, verdict.text, kRight, 126, theme::levelColor(verdict.level));
  }
}

void Ui::drawMetricRows(const Metric* metrics, size_t count) {
  const bool unitsInTitle = page_ == Page::Particles;
  for (size_t i = 0; i < count; ++i) {
    const Metric metric = metrics[i];
    const int top = 21 + static_cast<int>(i) * 28;
    const float value = snapshot_.measurement[metric];
    const uint16_t color = theme::levelColor(aq_->level(metric));

    frame_.setFont(kBody);
    const int w = drawRich(frame_, label(metric), kLeft, top + 13, theme::kText);
    if (!unitsInTitle) drawRich(frame_, unitText(metric), kLeft + w + 5, top + 13, theme::kMuted);
    frame_.setFont(kMedium);
    drawRichRight(frame_, valueText(metric, value).c_str(), kRight, top + 15, color);

    // Bar with ticks at the warning and alarm limits.
    const Scale scale = scaleOf(metric);
    const int barTop = top + 20;
    const int barWidth = kRight - kLeft;
    auto xOf = [&](float v) {
      const float f = std::clamp((v - scale.barMin) / (scale.barMax - scale.barMin), 0.0f, 1.0f);
      return kLeft + static_cast<int>(f * barWidth);
    };
    frame_.fillRect(kLeft, barTop, barWidth, 4, theme::kFaint);
    if (!std::isnan(value)) frame_.fillRect(kLeft, barTop, std::max(1, xOf(value) - kLeft), 4, color);
    const aq::Thresholds& t = aq_->thresholds(metric);
    if (!std::isnan(t.warnHigh)) frame_.drawFastVLine(xOf(t.warnHigh), barTop - 2, 8, theme::kWarning);
    if (!std::isnan(t.alarmHigh)) frame_.drawFastVLine(xOf(t.alarmHigh), barTop - 2, 8, theme::kAlarm);
  }
}

void Ui::drawComfort() {
  drawTitle("Comfort");
  const aq::Measurement& m = snapshot_.measurement;
  const float t = m[Metric::Temperature];
  const float rh = m[Metric::Humidity];

  // Temperature on the left, humidity right-aligned, so they never meet.
  frame_.setFont(kBody);
  drawRich(frame_, "Temperature", kLeft, 33, theme::kMuted);
  drawRichRight(frame_, "Humidity", kRight, 33, theme::kMuted);
  frame_.setFont(kHuge);
  const int w = drawRich(frame_, valueText(Metric::Temperature, t).c_str(), kLeft, 72,
                         theme::levelColor(aq_->level(Metric::Temperature)));
  frame_.setFont(kMedium);
  drawRich(frame_, unitText(Metric::Temperature), kLeft + w + 3, 72, theme::kMuted);
  const int percentWidth = drawRichRight(frame_, "%", kRight, 72, theme::kMuted);
  frame_.setFont(kHuge);
  drawRichRight(frame_, valueText(Metric::Humidity, rh).c_str(), kRight - percentWidth - 3, 72,
                theme::levelColor(aq_->level(Metric::Humidity)));

  frame_.setFont(kBody);
  const float dew = aq::dewPoint(t, rh);
  const float water = aq::absoluteHumidity(t, rh);
  String dewText = "Dew point  " + (std::isnan(dew) ? String("--") : String(displayValue(Metric::Temperature, dew), 1)) + " " + unitText(Metric::Temperature);
  String waterText = "Water vapour  " + (std::isnan(water) ? String("--") : String(water, 1)) + " g/m^3";
  drawRich(frame_, dewText.c_str(), kLeft, 93, theme::kMuted);
  drawRich(frame_, waterText.c_str(), kLeft, 109, theme::kMuted);

  const Verdict verdict = comfortVerdict(*aq_, m);
  frame_.setFont(kMedium);
  drawRich(frame_, verdict.text, kLeft, 130, theme::levelColor(verdict.level));
}

void Ui::drawTrend() {
  using aq::MetricHistory;
  const Metric metric = kTrendMetrics[trendMetric_];
  const bool hour = page_ == Page::TrendHour;
  const bool week = page_ == Page::TrendWeek;
  // Short enough to leave room for the Wi-Fi icon and the level.
  const char* name = metric == Metric::Temperature ? "Temp." : label(metric);
  const char* period = hour ? "  last hour" : week ? "  last 7 d" : "  last 24 h";
  drawTitle((String(name) + period).c_str());

  // The plot sits 2 px inside the frame so the newest data isn't hidden by it.
  constexpr int y0 = 24;
  constexpr int y1 = 116;
  frame_.drawRect(kLeft, y0, kRight - kLeft + 1, y1 - y0 + 1, theme::kFaint);
  if (hour) {
    drawChart(history_->fine(metric), MetricHistory::Fine::capacity(), metric, kLeft + 2, y0 + 2,
              kRight - 2, y1 - 2);
  } else {
    const size_t span = week ? MetricHistory::Coarse::capacity() : 24 * 60;
    drawChart(history_->coarse(metric), span, metric, kLeft + 2, y0 + 2, kRight - 2, y1 - 2);
  }
  frame_.setFont(kSmall);
  drawRich(frame_, hour ? "-1 h" : week ? "-7 d" : "-24 h", kLeft, 129, theme::kMuted);
  const char* middle = hour ? "-30 min" : week ? "-3.5 d" : "-12 h";
  drawRich(frame_, middle, (kLeft + kRight - richWidth(frame_, middle)) / 2, 129, theme::kMuted);
  drawRichRight(frame_, "now", kRight, 129, theme::kMuted);
}

template <typename Ring>
void Ui::drawChart(const Ring& ring, size_t span, Metric metric, int x0, int y0, int x1, int y1) {
  // The newest `span` samples, averaged over each pixel column (about 50 for
  // the 7 days). The right edge is now; columns older than the oldest sample
  // stay empty.
  const int width = x1 - x0 + 1;
  const size_t shown = std::min(span, ring.size());
  const size_t missing = span - shown;
  const size_t first = ring.size() - shown;
  float columns[240];
  float lo = INFINITY;
  float hi = -INFINITY;
  for (int c = 0; c < width; ++c) {
    const size_t s0 = static_cast<size_t>(c) * span / width;
    const size_t s1 = static_cast<size_t>(c + 1) * span / width;
    double sum = 0;
    int n = 0;
    for (size_t s = std::max(s0, missing); s < s1; ++s) {
      const float v = ring[first + s - missing];
      if (std::isnan(v)) continue;
      sum += v;
      ++n;
    }
    columns[c] = n ? displayValue(metric, static_cast<float>(sum / n)) : NAN;
    if (n) {
      lo = std::min(lo, columns[c]);
      hi = std::max(hi, columns[c]);
    }
  }

  if (lo > hi) {
    frame_.setFont(kBody);
    const char* text = "Collecting data...";
    drawRich(frame_, text, (x0 + x1 - richWidth(frame_, text)) / 2, (y0 + y1) / 2 + 4,
             theme::kMuted);
    return;
  }

  const float minSpan = scaleOf(metric).minSpan;
  if (hi - lo < minSpan) {
    const float middle = (hi + lo) / 2;
    lo = middle - minSpan / 2;
    hi = middle + minSpan / 2;
  }
  const float pad = (hi - lo) * 0.1f;
  lo -= pad;
  hi += pad;
  if (metric != Metric::Temperature) lo = std::max(lo, 0.0f);
  auto yOf = [&](float v) { return y1 - static_cast<int>(std::lround((v - lo) / (hi - lo) * (y1 - y0))); };

  // Warning and alarm limits inside the visible range, as dotted lines.
  const aq::Thresholds& t = aq_->thresholds(metric);
  const struct {
    float limit;
    uint16_t color;
  } limits[] = {{t.warnHigh, theme::kWarning},
                {t.alarmHigh, theme::kAlarm},
                {t.warnLow, theme::kWarning},
                {t.alarmLow, theme::kAlarm}};
  for (const auto& l : limits) {
    const float v = displayValue(metric, l.limit);
    if (std::isnan(v) || v <= lo || v >= hi) continue;
    const int y = yOf(v);
    for (int x = x0 + 1; x < x1; x += 4) frame_.drawPixel(x, y, l.color);
  }

  for (int c = 0; c < width; ++c) {
    if (std::isnan(columns[c])) continue;
    const int y = yOf(columns[c]);
    if (c > 0 && !std::isnan(columns[c - 1])) {
      frame_.drawLine(x0 + c - 1, yOf(columns[c - 1]), x0 + c, y, theme::kAccent);
    } else {
      frame_.fillRect(x0 + c - 1, y - 1, 2, 2, theme::kAccent);  // isolated point: keep it visible
    }
  }

  frame_.setFont(kSmall);
  const int decimals = hi - lo < 10 ? 1 : 0;
  drawRich(frame_, number(hi, decimals).c_str(), x0 + 3, y0 + 10, theme::kMuted);
  drawRich(frame_, number(lo, decimals).c_str(), x0 + 3, y1 - 3, theme::kMuted);
}

void Ui::drawDevice(uint32_t nowMs) {
  drawTitle("Device");
  const SensorSnapshot& s = snapshot_;
  frame_.setFont(kBody);
  int y = 33;
  auto line = [&](const String& text, uint16_t color) {
    drawRich(frame_, text.c_str(), kLeft, y, color);
    y += 15;
  };

  if (s.product[0] == '\0') {
    line("Sensor not found", theme::kAlarm);
  } else {
    line(String(s.product) + "  firmware " + s.firmwareMajor + "." + s.firmwareMinor, theme::kText);
    line(String("S/N ") + s.serial, theme::kMuted);
  }
  const bool error = s.state != SensorState::Busy && s.deviceStatus != 0;
  if (s.state == SensorState::Busy) {
    line(s.activity, theme::kWarning);
  } else if (error) {
    line(statusText(s.deviceStatus), theme::kAlarm);
  } else if (s.lastError != 0) {
    line("I2C error " + String(s.lastError), theme::kAlarm);
  } else if (s.state == SensorState::Measuring) {
    // Formaldehyde needs ~10 min after power-up to reach its specified accuracy.
    const bool settling = nowMs - s.measuringSinceMs < 10 * 60000;
    line(settling ? "Sensor OK, HCHO settling" : "Sensor OK", theme::kGood);
  }
  if (s.state == SensorState::Measuring) {
    line("Measuring " + duration(nowMs - s.measuringSinceMs), theme::kMuted);
  }
  if (battery_->present()) {
    if (std::isnan(s.batteryVolts)) {
      line("No battery monitor", theme::kMuted);
    } else {
      // What it's doing if anything, else its voltage.
      String text = "Battery " + number(battery_->percent(), 0) + " %";
      const power::BatteryState state = battery_->state();
      if (state == power::BatteryState::Charging) {
        text += ", charging";
      } else if (state == power::BatteryState::Discharging) {
        text += ", in use";
      } else {
        text += ", " + String(s.batteryVolts, 2) + " V";
      }
      line(text, battery_->warning() != 0 ? theme::kWarning : theme::kMuted);
    }
  }
  line("Uptime " + duration(nowMs) + ", v" FW_VERSION, theme::kMuted);
  if (const uint32_t failures = web::failedLogins()) {
    line(String(failures) + (failures == 1 ? " wrong web password" : " wrong web passwords"),
         theme::kWarning);
  }
  // Most error flags stay set once the error is gone, until cleared.
  if (error && y <= 123) {
    frame_.setFont(kSmall);
    drawRich(frame_, "D1: clear the error", kLeft, 129, theme::kMuted);
  }
}

void Ui::drawNetwork() {
  drawTitle("Wi-Fi");
  const WifiService& w = *wifi_;
  constexpr int kWidth = kRight - kLeft;
  frame_.setFont(kBody);
  int y = 36;
  auto line = [&](const char* text, uint16_t color) {
    drawPlain(frame_, text, kLeft, y, color, kWidth);
    y += 16;
  };
  auto footer = [&](const char* text) {
    frame_.setFont(kSmall);
    drawRich(frame_, text, kLeft, 129, theme::kMuted);
  };

  if (!settings_->wifiEnabled || w.saved().size() == 0) {
    line(settings_->wifiEnabled ? "Not set up yet" : "Wi-Fi is off", theme::kText);
    line("Works fine without it.", theme::kMuted);
    footer(w.saved().size() > 0 ? "D1: turn Wi-Fi on" : "D1: set up Wi-Fi with a phone");
    return;
  }
  switch (w.state()) {
    case WifiState::Connected: {
      frame_.setFont(kMedium);
      drawPlain(frame_, w.ssid(), kLeft, 39, theme::kText, kWidth - 22);
      drawSignal(kRight, 38, w.rssi());
      frame_.setFont(kBody);
      y = 58;
      line((String(device::hostname()) + ".local").c_str(), theme::kMuted);
      line((w.ip().toString() + (w.fixedAddress() ? "  fixed" : "")).c_str(), theme::kMuted);
      const String password = settings_->webPassword[0] != '\0'
                                  ? String("Web password  ") + settings_->webPassword
                                  : String("No web password");
      line(password.c_str(), theme::kMuted);
      switch (mqtt_->state()) {
        case MqttState::Connected:
          line("Home Assistant connected", theme::kMuted);
          break;
        case MqttState::Failed:
          line((String("HA: ") + mqtt_->error()).c_str(), theme::kWarning);
          break;
        case MqttState::Connecting:
        case MqttState::Waiting:
          line("Connecting to Home Assistant", theme::kMuted);
          break;
        case MqttState::Off:
          break;
      }
      footer("D1: QR code to open the web page");
      break;
    }
    case WifiState::Connecting:
      line("Connecting to", theme::kMuted);
      line(w.ssid(), theme::kText);
      footer("D1: add a network with a phone");
      break;
    default: {
      if (w.failedSsid()[0] != '\0') {
        line("Couldn't join", theme::kMuted);
        line(w.failedSsid(), theme::kText);
        line(w.failedReason(), theme::kText);
        line("Trying again now and then", theme::kMuted);
        footer("D1: set up Wi-Fi with a phone");
        break;
      }
      line("No saved network in range", theme::kText);
      String names = "Saved: ";
      for (size_t i = 0; i < w.saved().size(); ++i) {
        if (i > 0) names += ", ";
        names += w.saved()[i].ssid;
      }
      line(names.c_str(), theme::kMuted);
      line("Looking again now and then", theme::kMuted);
      footer("D1: add a network with a phone");
      break;
    }
  }
}

// Four bars, filled by signal strength, ending at `right`.
void Ui::drawSignal(int right, int baseline, int8_t rssi) {
  const int strength = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  for (int i = 0; i < 4; ++i) {
    const int height = 3 + 2 * i;
    frame_.fillRect(right - (4 - i) * 4 + 1, baseline - height, 3, height,
                    i < strength ? theme::kText : theme::kFaint);
  }
}

// A dot and two arcs opening upwards, 11 px wide.
void Ui::drawWifiIcon(int cx, int bottom, uint16_t color) {
  const int cy = bottom - 1;
  frame_.fillCircle(cx, cy, 1, color);
  frame_.fillArc(cx, cy, 4, 5, 225, 315, color);
  frame_.fillArc(cx, cy, 7, 8, 225, 315, color);
}

void Ui::drawSetup() {
  const WifiService& w = *wifi_;
  frame_.setFont(kBody);
  drawRich(frame_, "Wi-Fi setup", kLeft, kTitleBaseline, theme::kAccent);
  const String ip = w.hotspotIp().toString();
  int width = 0;  // of the text column beside the QR code
  auto small = [&](const char* text, int baseline, uint16_t color) {
    frame_.setFont(kSmall);
    drawPlain(frame_, text, kLeft, baseline, color, width);
  };

  switch (w.setupState()) {
    case SetupState::Waiting: {
      // Phones join a network from a QR code in this format.
      const String code = String("WIFI:T:WPA;S:") + w.hotspotSsid() + ";P:" + w.hotspotPassword() + ";;";
      width = drawQr(code.c_str()) - 4 - kLeft;
      small("Scan this", 34, theme::kText);
      small("with your", 46, theme::kText);
      small("camera", 58, theme::kText);
      small("or join", 76, theme::kMuted);
      // The hotspot's name, split after its dash when it doesn't fit.
      const String ssid = w.hotspotSsid();
      const int dash = ssid.lastIndexOf('-');
      frame_.setFont(kSmall);
      if (frame_.textWidth(ssid.c_str()) > width && dash > 0) {
        small(ssid.substring(0, dash + 1).c_str(), 88, theme::kText);
        small(ssid.substring(dash + 1).c_str(), 100, theme::kText);
      } else {
        small(ssid.c_str(), 88, theme::kText);
      }
      small("password", 113, theme::kMuted);
      frame_.setFont(kBody);
      drawPlain(frame_, w.hotspotPassword(), kLeft, 127, theme::kText, width);
      break;
    }
    case SetupState::Joined:
    case SetupState::Failed: {
      width = drawQr(("http://" + ip + "/").c_str()) - 4 - kLeft;
      if (w.setupState() == SetupState::Failed) {
        small("Couldn't connect:", 36, theme::kAlarm);
        small(w.setupMessage(), 48, theme::kText);
        small("Try again on", 66, theme::kMuted);
        small("your phone.", 78, theme::kMuted);
      } else {
        small("Phone joined.", 36, theme::kGood);
        small("The setup page", 50, theme::kText);
        small("should open.", 62, theme::kText);
        small("If not, scan", 80, theme::kMuted);
        small("this, or open", 92, theme::kMuted);
      }
      small(ip.c_str(), 110, theme::kText);
      break;
    }
    case SetupState::Testing:
      frame_.setFont(kBody);
      drawRich(frame_, "Connecting to", kLeft, 50, theme::kMuted);
      frame_.setFont(kMedium);
      drawPlain(frame_, w.trialSsid(), kLeft, 76, theme::kText, kRight - kLeft);
      frame_.setFont(kSmall);
      drawRich(frame_, "This takes up to 20 s", kLeft, 100, theme::kMuted);
      break;
    case SetupState::Succeeded: {
      frame_.setFont(kMedium);
      drawRich(frame_, "Connected", kLeft, 40, theme::kGood);
      frame_.setFont(kBody);
      drawPlain(frame_, w.ssid(), kLeft, 58, theme::kText, kRight - kLeft);
      const String host = String(device::hostname()) + ".local";
      drawPlain(frame_, host.c_str(), kLeft, 78, theme::kMuted);
      drawPlain(frame_, w.ip().toString().c_str(), kLeft, 94, theme::kMuted);
      const String password = String("Web password  ") + settings_->webPassword;
      drawPlain(frame_, password.c_str(), kLeft, 112, theme::kMuted, kRight - kLeft);
      frame_.setFont(kSmall);
      drawRich(frame_, "D1: done", kLeft, 129, theme::kMuted);
      break;
    }
    case SetupState::Off:
      break;
  }
}

// QR code of the web page, to open it on a phone.
void Ui::drawLink() {
  const String ip = wifi_->ip().toString();
  const int width = drawQr(("http://" + ip + "/").c_str()) - 4 - kLeft;
  frame_.setFont(kBody);
  drawRich(frame_, "Web page", kLeft, kTitleBaseline, theme::kAccent);
  frame_.setFont(kSmall);
  drawPlain(frame_, "Scan to open it", kLeft, 36, theme::kText, width);
  drawPlain(frame_, "on your phone,", kLeft, 48, theme::kText, width);
  drawPlain(frame_, "or go to", kLeft, 66, theme::kMuted, width);
  drawPlain(frame_, ip.c_str(), kLeft, 78, theme::kText, width);
  drawPlain(frame_, "Any button", kLeft, 112, theme::kMuted, width);
  drawPlain(frame_, "closes this.", kLeft, 124, theme::kMuted, width);
}

// An update arriving from the web page, stored beside the running firmware
// until it is confirmed; then installing.
void Ui::drawUpdate() {
  const bool done = ota_->state() == OtaState::Accepted;
  const int percent = done ? 100 : ota_->percent();
  constexpr int kWidth = kRight - kLeft;
  frame_.setFont(kMedium);
  drawRich(frame_, done ? "Installing update" : "Receiving update", kLeft, 40, theme::kText);
  frame_.drawRect(kLeft, 56, kWidth, 14, theme::kMuted);
  frame_.fillRect(kLeft + 2, 58, (kWidth - 4) * percent / 100, 10,
                  done ? theme::kGood : theme::kAccent);
  frame_.setFont(kBody);
  if (done) {
    drawRich(frame_, "Done, restarting...", kLeft, 92, theme::kGood);
  } else {
    drawRich(frame_, (String(percent) + " %").c_str(), kLeft, 92, theme::kMuted);
  }
  drawRich(frame_, done ? "Keep the power on" : "Asks before installing", kLeft, 120, theme::kMuted);
}

// An uploaded update, waiting for a yes on the monitor itself.
void Ui::drawUpdatePrompt(uint32_t nowMs) {
  constexpr int kWidth = kRight - kLeft;
  frame_.setFont(kMedium);
  drawRich(frame_, "Install update?", kLeft, 30, theme::kText);
  frame_.setFont(kBody);
  drawRich(frame_, "New firmware was sent", kLeft, 52, theme::kMuted);
  drawRich(frame_, "from the web page.", kLeft, 67, theme::kMuted);
  if (holding_) {
    const uint32_t held = std::min<uint32_t>(nowMs - holdStartMs_, Ota::kConfirmHoldMs);
    drawRich(frame_, "Keep holding...", kLeft, 92, theme::kAccent);
    frame_.drawRect(kLeft, 100, kWidth, 10, theme::kMuted);
    frame_.fillRect(kLeft + 2, 102, (kWidth - 4) * held / Ota::kConfirmHoldMs, 6, theme::kAccent);
  } else {
    drawRich(frame_, "Hold any button 5 s: install", kLeft, 92, theme::kAccent);
    drawRich(frame_, "Press briefly: refuse", kLeft, 108, theme::kText);
  }
  frame_.setFont(kSmall);
  const String left = "Refused by itself in " + String(ota_->secondsLeft(nowMs)) + " s";
  drawRich(frame_, left.c_str(), kLeft, 129, theme::kMuted);
}

// A QR code at the right edge, in a white square with a quiet zone of two
// modules; returns the square's left edge. 4 px modules scan well with phones.
int Ui::drawQr(const char* text) {
  constexpr int kModule = 4;
  // What versions 1 to 4 hold in byte mode with low error correction.
  constexpr size_t kCapacity[] = {17, 32, 53, 78};
  const size_t length = strlen(text);
  int version = 1;
  while (version < 4 && length > kCapacity[version - 1]) ++version;
  const int code = (17 + 4 * version) * kModule;
  const int size = code + 4 * kModule;
  const int left = frame_.width() - 1 - size;
  const int top = (frame_.height() - size) / 2;
  frame_.fillRect(left, top, size, size, kQrLight);
  frame_.qrcode(text, left + 2 * kModule, top + 2 * kModule, code, version);
  return left;
}

void Ui::drawMenu() {
  static_assert(std::size(kItemLabels) == static_cast<size_t>(Item::Count), "one label per item");
  const bool editing = mode_ == Mode::Edit;
  frame_.setFont(kBody);
  drawRich(frame_, "Settings", kLeft, kTitleBaseline, theme::kAccent);
  drawRichRight(frame_, editing ? "D1 done" : "hold D1 to close", kRight, kTitleBaseline,
                theme::kMuted);
  frame_.drawFastHLine(kLeft, 18, kRight - kLeft, theme::kFaint);

  const int selected = static_cast<int>(item_);
  if (selected < menuTop_) menuTop_ = selected;
  if (selected >= menuTop_ + kMenuRows) menuTop_ = selected - kMenuRows + 1;
  for (int row = 0; row < kMenuRows; ++row) {
    const int i = menuTop_ + row;
    if (i >= static_cast<int>(Item::Count)) break;
    const int top = 20 + row * kMenuRowHeight;
    uint16_t fg = theme::kText;
    uint16_t valueColor = theme::kMuted;
    if (i == selected) {
      frame_.fillRoundRect(kLeft - 2, top, kRight - kLeft + 4, kMenuRowHeight - 1, 3,
                           editing ? theme::kWarning : theme::kAccent);
      fg = valueColor = theme::kBackground;
    }
    drawRich(frame_, kItemLabels[i], kLeft + 2, top + 13, fg);
    drawRichRight(frame_, itemValue(static_cast<Item>(i)).c_str(), kRight - 2, top + 13,
                  valueColor);
  }
}

void Ui::drawConfirm() {
  const char* title = "";
  const char* line1 = "";
  const char* line2 = "";
  switch (confirmItem_) {
    case Item::CleanFan:
      title = "Clean the fan?";
      line1 = "Full speed for 10 s; readings";
      line2 = "pause for about 40 s.";
      break;
    case Item::HeatHumiditySensor:
      title = "Heat RH sensor?";
      line1 = "Fixes high humidity readings";
      line2 = "after damp air. About 1 min.";
      break;
    case Item::RestartVoc:
      title = "Restart VOC?";
      line1 = "VOC and NO_x relearn the air";
      line2 = "over 12 h, like unplugging.";
      break;
    case Item::CalibrateCo2:
      title = "Calibrate CO_2?";
      line1 = "Sets 420 ppm. Only outdoors,";
      line2 = "after 3 min in fresh air.";
      break;
    case Item::ResetCo2:
      title = "Reset CO_2?";
      line1 = "Erases every CO_2 calibration;";
      line2 = "self-calibration starts over.";
      break;
    case Item::ForgetWifi:
      title = "Forget Wi-Fi?";
      line1 = "Removes every saved network.";
      line2 = "Set up again from the Wi-Fi page.";
      break;
    case Item::Restart:
      title = "Restart?";
      break;
    case Item::PowerOff:
      title = "Power off?";
      line1 = "Uses almost no battery.";
      line2 = "Hold D1 or D2 to turn it on.";
      break;
    case Item::FactoryReset:
      title = "Factory reset?";
      line1 = "Restores the default settings";
      line2 = "and restarts.";
      break;
    default:
      break;
  }
  frame_.setFont(kMedium);
  drawRich(frame_, title, kLeft, 30, theme::kText);
  frame_.setFont(kBody);
  drawRich(frame_, line1, kLeft, 56, theme::kMuted);
  drawRich(frame_, line2, kLeft, 72, theme::kMuted);
  drawRich(frame_, "Hold D1 to confirm", kLeft, 102, theme::kAccent);
  drawRich(frame_, "D0 or D2 to cancel", kLeft, 120, theme::kMuted);
}

void Ui::drawAlert() {
  // Covers the whole content area so nothing of the page behind peeks out.
  constexpr int x = kLeft;
  constexpr int y = 20;
  constexpr int w = kRight - kLeft;
  constexpr int h = 115;
  const uint16_t color = theme::levelColor(alertLevel_);
  frame_.fillRect(x, y, w, h, theme::kBackground);
  frame_.drawRect(x, y, w, h, color);
  frame_.drawRect(x + 1, y + 1, w - 2, h - 2, color);

  const Metric m = alertMetric_;
  const float value = snapshot_.measurement[m];
  const bool high = isHigh(aq_->thresholds(m), value);
  const char* title = "";
  const char* advice = "";
  switch (m) {
    case Metric::Co2:
      title = "CO_2 high";
      advice = "Open a window";
      break;
    case Metric::Pm25:
    case Metric::Pm10:
      title = "Particles high";
      advice = "Ventilate or filter the air";
      break;
    case Metric::Hcho:
      title = "Formaldehyde high";
      advice = "Ventilate the room";
      break;
    case Metric::Voc:
      title = "VOCs high";
      advice = "Ventilate the room";
      break;
    case Metric::Nox:
      title = "NO_x high";
      advice = "Check stoves; ventilate";
      break;
    case Metric::Temperature:
      title = high ? "Too warm" : "Too cold";
      break;
    case Metric::Humidity:
      title = high ? "Too humid" : "Too dry";
      advice = high ? "Ventilate; mould risk" : "Consider a humidifier";
      break;
    default:
      break;
  }
  frame_.setFont(kMedium);
  drawRich(frame_, title, x + 8, y + 23, color);
  frame_.setFont(kHuge);
  const int vw = drawRich(frame_, valueText(m, value).c_str(), x + 8, y + 65, theme::kText);
  frame_.setFont(kBody);
  drawRich(frame_, unitText(m), x + 8 + vw + 4, y + 65, theme::kMuted);
  drawRich(frame_, advice, x + 8, y + 85, theme::kText);
  frame_.setFont(kSmall);
  drawRich(frame_, "Press any button", x + 8, y + 104, theme::kMuted);
}

void Ui::drawPoweringOff() {
  frame_.setFont(kMedium);
  drawRich(frame_, "Switching off", kLeft, 30, theme::kText);
  frame_.setFont(kBody);
  drawRich(frame_, "To turn it on again, hold", kLeft, 62, theme::kMuted);
  const String hold = "D1 or D2 for " + String(power::kHoldToStartMs / 1000) + " s.";
  drawRich(frame_, hold.c_str(), kLeft, 78, theme::kMuted);
  const String countdown = "Off in " + String(offInS_) + " s";
  drawRich(frame_, countdown.c_str(), kLeft, 110, theme::kAccent);
}

// Like an air alert, for a battery running low.
void Ui::drawBatteryAlert() {
  constexpr int x = kLeft;
  constexpr int y = 20;
  constexpr int w = kRight - kLeft;
  constexpr int h = 115;
  const bool veryLow = batteryAlert_ >= 2;
  const uint16_t color = veryLow ? theme::kAlarm : theme::kWarning;
  frame_.fillRect(x, y, w, h, theme::kBackground);
  frame_.drawRect(x, y, w, h, color);
  frame_.drawRect(x + 1, y + 1, w - 2, h - 2, color);
  frame_.setFont(kMedium);
  drawRich(frame_, veryLow ? "Battery very low" : "Battery low", x + 8, y + 23, color);
  frame_.setFont(kHuge);
  const int vw = drawRich(frame_, number(battery_->percent(), 0).c_str(), x + 8, y + 65, theme::kText);
  frame_.setFont(kBody);
  drawRich(frame_, "%", x + 8 + vw + 4, y + 65, theme::kMuted);
  drawRich(frame_, "Connect USB power", x + 8, y + 85, theme::kText);
  frame_.setFont(kSmall);
  drawRich(frame_, "Press any button", x + 8, y + 104, theme::kMuted);
}

// Over the title bar: the one band every screen has in the same place, so the
// message never cuts through a row. It ends above the line under the title,
// and above the first row of a QR code.
void Ui::drawToast() {
  frame_.fillRect(kLeft - 2, 0, kRight - kLeft + 4, 17, theme::kFaint);
  frame_.setFont(kBody);
  drawRich(frame_, toast_, kLeft + 2, kTitleBaseline, theme::kText);
}

float Ui::displayValue(Metric metric, float value) const {
  return metric == Metric::Temperature && settings_->fahrenheit ? aq::toFahrenheit(value) : value;
}

String Ui::valueText(Metric metric, float value) const {
  if (std::isnan(value)) return "--";
  value = displayValue(metric, value);
  switch (metric) {
    case Metric::Temperature:
      return number(value, 1);
    case Metric::Humidity:
    case Metric::Co2:
    case Metric::Voc:
    case Metric::Nox:
      return number(value, 0);
    default:  // particles and formaldehyde
      return number(value, value < 100 ? 1 : 0);
  }
}

const char* Ui::unitText(Metric metric) const {
  switch (metric) {
    case Metric::Pm1:
    case Metric::Pm25:
    case Metric::Pm4:
    case Metric::Pm10:
      return "~ug/m^3";
    case Metric::Co2:
      return "ppm";
    case Metric::Hcho:
      return "ppb";
    case Metric::Temperature:
      return settings_->fahrenheit ? "~dF" : "~dC";
    case Metric::Humidity:
      return "%";
    default:  // VOC and NOx are unitless indices
      return "";
  }
}

String Ui::itemValue(Item item) const {
  const Settings& s = *settings_;
  switch (item) {
    case Item::Brightness:
      return String(s.brightness) + " %";
    case Item::DimAfter:
      if (s.dimAfterS == 0) return "Never";
      return s.dimAfterS < 60 ? String(s.dimAfterS) + " s" : String(s.dimAfterS / 60) + " min";
    case Item::IdleScreen:
      return s.screenOffWhenIdle ? "Off" : "Dim";
    case Item::Units:
      return s.fahrenheit ? "~dF" : "~dC";
    case Item::Led:
      return s.led == LedMode::Off ? "Off" : s.led == LedMode::Dim ? "Dim" : "Bright";
    case Item::TempOffset:
      return (s.tempOffsetC >= 0 ? "+" : "") + String(s.tempOffsetC, 1) + " ~dC";
    case Item::Altitude:
      return String(s.altitudeM) + " m";
    case Item::Co2SelfCalibration:
      return s.co2SelfCalibration ? "On" : "Off";
    case Item::Wifi:
      return s.wifiEnabled ? "On" : "Off";
    case Item::Close:
      return "";
    default:
      return ">";
  }
}
