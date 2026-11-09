// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Running the firmware's UI on the simulated device, and pressing its
/// buttons until it shows a page, menu item or trend (see Simulator.h).
///
/// To know what shows, it reads private members of Ui. Only the explicit
/// template instantiation at the end of this file names them, so if the
/// firmware renames one, that's the one place to update.

#include "Simulator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

using aq::Metric;

namespace {

/// @brief What the simulator does with the UI's private parts: tell which page
/// or menu item shows, and read the frame.
///
/// Only an explicit template instantiation may name private members, so this
/// code lives in the template `Inside` below, and its one instantiation (at
/// the end of the file) fills in these hooks.
struct Hooks {
  LGFX_Sprite& (*frame)(Ui&) = nullptr;                     ///< Inside::frame().
  bool& (*dirty)(Ui&) = nullptr;                            ///< Inside::dirty().
  std::string (*dismissAlerts)(Simulator&, Ui&) = nullptr;  ///< Inside::dismissAlerts().
  std::string (*goToPage)(Simulator&, Ui&, const std::string&) = nullptr;  ///< Inside::goToPage().
  std::string (*openMenu)(Simulator&, Ui&, const std::string&) = nullptr;  ///< Inside::openMenu().
  std::string (*showTrend)(Simulator&, Ui&, Metric) = nullptr;  ///< Inside::showTrend().
  std::vector<std::string> (*pageNames)() = nullptr;            ///< Inside::pageNames().
  std::vector<std::string> (*itemNames)() = nullptr;            ///< Inside::itemNames().
};
/// @brief The hooks, filled in by `Inside::hooked` as the program starts.
///
/// `constinit`, so they're null before that, whatever order the program's
/// variables are set up in, and never reset after it.
constinit Hooks hooks;

/// @brief The shorter way from `from` to `to` in a cycle of `count`.
/// @param from Where it starts.
/// @param to Where it should get to.
/// @param count How many there are in the cycle.
/// @return How many steps: positive forwards, negative backwards.
int shortestSteps(int from, int to, int count) {
  const int forward = (to - from + count) % count;
  return forward <= count / 2 ? forward : forward - count;
}

/// @brief Why clicks can't reach the pages, if they can't: the Wi-Fi setup
/// and update screens cover them.
/// @return An error message that names the command closing the screen, or "".
std::string fullScreenError() {
  if (world.wifi.setup != SetupState::Off) return "the Wi-Fi setup screen is showing ('setup off')";
  const OtaState ota = world.ota.state;
  if (ota == OtaState::Receiving || ota == OtaState::Confirming || ota == OtaState::Accepted) {
    return "the update screen is showing ('update off')";
  }
  return "";
}

/// @brief The code that uses the UI's private members, which it gets as
/// template arguments (see Hooks).
/// @tparam Frame `&Ui::frame_`, the frame buffer the UI draws into.
/// @tparam ModeMember `&Ui::mode_`: the pages, the menu, an edit, a question
///   or the QR code.
/// @tparam PageMember `&Ui::page_`, the page showing.
/// @tparam ItemMember `&Ui::item_`, the menu item showing.
/// @tparam TrendMember `&Ui::trendMetric_`, the trend pages' metric, as an
///   index into `TrendMetrics`.
/// @tparam TrendMetrics `&Ui::kTrendMetrics`, the metrics the trend pages
///   cycle through.
/// @tparam AlertMember `&Ui::alertShowing`, whether an alert shows.
/// @tparam DirtyMember `&Ui::dirty_`, whether to draw at the next update.
template <auto Frame, auto ModeMember, auto PageMember, auto ItemMember, auto TrendMember,
          auto TrendMetrics, auto AlertMember, auto DirtyMember>
struct Inside {
  /// @brief The type of a Ui data member.
  /// @tparam Member A pointer to it.
  template <auto Member>
  using TypeOf = std::remove_reference_t<decltype(std::declval<Ui&>().*Member)>;
  using Mode = TypeOf<ModeMember>;  ///< Ui::Mode.
  using Page = TypeOf<PageMember>;  ///< Ui::Page.
  using Item = TypeOf<ItemMember>;  ///< Ui::Item.

  /// The pages' names in scripts.
  static constexpr std::pair<const char*, Page> kPages[] = {
      {"overview", Page::Overview}, {"particles", Page::Particles}, {"gases", Page::Gases},
      {"comfort", Page::Comfort},   {"hour", Page::TrendHour},      {"day", Page::TrendDay},
      {"week", Page::TrendWeek},    {"wifi", Page::Network},        {"device", Page::Device},
  };
  static_assert(std::size(kPages) == static_cast<size_t>(Page::Count), "a name for every page");

  /// The settings menu items' names in scripts.
  static constexpr std::pair<const char*, Item> kItems[] = {
      {"brightness", Item::Brightness},
      {"dim-after", Item::DimAfter},
      {"idle-screen", Item::IdleScreen},
      {"units", Item::Units},
      {"led", Item::Led},
      {"silence", Item::Silence},
      {"wifi", Item::Wifi},
      {"wifi-setup", Item::WifiSetup},
      {"forget-wifi", Item::ForgetWifi},
      {"temp-offset", Item::TempOffset},
      {"altitude", Item::Altitude},
      {"co2-self-calibration", Item::Co2SelfCalibration},
      {"calibrate-co2", Item::CalibrateCo2},
      {"reset-co2", Item::ResetCo2},
      {"clean-fan", Item::CleanFan},
      {"heat-humidity-sensor", Item::HeatHumiditySensor},
      {"restart-voc", Item::RestartVoc},
      {"restart", Item::Restart},
      {"power-off", Item::PowerOff},
      {"factory-reset", Item::FactoryReset},
      {"close", Item::Close},
  };
  static_assert(std::size(kItems) == static_cast<size_t>(Item::Count), "a name for every item");

  /// @brief Looks a name up in kPages or kItems.
  /// @tparam T Page or Item.
  /// @tparam N The table's length.
  /// @param table The table.
  /// @param name The name.
  /// @return What it names; nullptr if nothing.
  template <typename T, size_t N>
  static const T* find(const std::pair<const char*, T> (&table)[N], const std::string& name) {
    for (const auto& entry : table) {
      if (name == entry.first) return &entry.second;
    }
    return nullptr;
  }

  /// @brief The names in kPages or kItems, in order.
  /// @tparam T Page or Item.
  /// @tparam N The table's length.
  /// @param table The table.
  template <typename T, size_t N>
  static std::vector<std::string> names(const std::pair<const char*, T> (&table)[N]) {
    std::vector<std::string> result;
    for (const auto& entry : table) result.push_back(entry.first);
    return result;
  }

  /// @brief The frame buffer the UI draws into.
  /// @param ui The UI.
  static LGFX_Sprite& frame(Ui& ui) { return ui.*Frame; }
  /// @brief Whether the UI draws the screen at its next update; set it to
  /// have it drawn.
  /// @param ui The UI.
  static bool& dirty(Ui& ui) { return ui.*DirtyMember; }

  /// @brief Simulator::dismissAlerts(): clicks D1 until no alert shows, 20
  /// times at most.
  /// @param sim The simulator.
  /// @param ui Its UI.
  /// @return An error message, or "" once no alert shows.
  static std::string dismissAlerts(Simulator& sim, Ui& ui) {
    if (std::string e = fullScreenError(); !e.empty()) return e;
    for (int i = 0; i < 20; ++i) {
      if (!(ui.*AlertMember)()) return "";
      sim.click(1);
    }
    return "alerts keep coming";
  }

  /// @brief Closes alerts, the menu, an edit, a question or the QR code until
  /// the pages show.
  /// @param sim The simulator.
  /// @param ui Its UI.
  /// @return An error message, or "" once the pages show.
  static std::string backToPages(Simulator& sim, Ui& ui) {
    for (int i = 0; i < 8; ++i) {
      if (std::string e = dismissAlerts(sim, ui); !e.empty()) return e;
      switch (ui.*ModeMember) {
        case Mode::Pages:
          return "";
        case Mode::Menu:
          sim.hold(1, 800);  // holding D1 closes the menu
          break;
        case Mode::Edit:
          sim.click(1);  // done
          break;
        case Mode::Confirm:
        case Mode::Link:
          sim.click(0);  // cancel, close
          break;
      }
    }
    return "couldn't get back to the pages";
  }

  /// @brief Simulator::goToPage(): goes back to the pages, then clicks D2
  /// (forwards) or D0 (backwards), the shorter way, until the page shows.
  ///
  /// Alerts that come up on the way are closed.
  /// @param sim The simulator.
  /// @param ui Its UI.
  /// @param name The page's name in kPages.
  /// @return An error message, or "" once the page shows.
  static std::string goToPage(Simulator& sim, Ui& ui, const std::string& name) {
    const Page* target = find(kPages, name);
    if (target == nullptr) return "no page called '" + name + "'";
    if (std::string e = backToPages(sim, ui); !e.empty()) return e;
    constexpr int kCount = static_cast<int>(Page::Count);
    for (int i = 0; i < 3 * kCount; ++i) {
      if (std::string e = dismissAlerts(sim, ui); !e.empty()) return e;
      const Page page = ui.*PageMember;
      if (page == *target) return "";
      const int steps = shortestSteps(static_cast<int>(page), static_cast<int>(*target), kCount);
      sim.click(steps > 0 ? 2 : 0);
    }
    return "couldn't reach the page '" + name + "'";
  }

  /// @brief Simulator::openMenu(): goes back to the pages, holds D1 to open
  /// the menu at its first item, then clicks D2 or D0, the shorter way, until
  /// the item shows.
  /// @param sim The simulator.
  /// @param ui Its UI.
  /// @param name The item's name in kItems.
  /// @return An error message, or "" once the item shows.
  static std::string openMenu(Simulator& sim, Ui& ui, const std::string& name) {
    const Item* target = find(kItems, name);
    if (target == nullptr) return "no menu item called '" + name + "'";
    if (std::string e = backToPages(sim, ui); !e.empty()) return e;
    sim.hold(1, 800);  // opens the menu at its first item
    constexpr int kCount = static_cast<int>(Item::Count);
    for (int i = 0; i < 2 * kCount; ++i) {
      if (ui.*ModeMember != Mode::Menu) return "the menu didn't open";
      const Item item = ui.*ItemMember;
      if (item == *target) return "";
      const int steps = shortestSteps(static_cast<int>(item), static_cast<int>(*target), kCount);
      sim.click(steps > 0 ? 2 : 0);
    }
    return "couldn't reach the menu item '" + name + "'";
  }

  /// @brief Simulator::showTrend(): goes to the trend page that shows, or
  /// else the last hour's, then clicks D1 until it shows the metric, once
  /// round them all at most.
  /// @param sim The simulator.
  /// @param ui Its UI.
  /// @param metric The metric.
  /// @return An error message, or "" once the metric shows.
  static std::string showTrend(Simulator& sim, Ui& ui, Metric metric) {
    const Page page = ui.*PageMember;
    const char* trend = page == Page::TrendDay ? "day" : page == Page::TrendWeek ? "week" : "hour";
    if (std::string e = goToPage(sim, ui, trend); !e.empty()) return e;
    const auto& metrics = *TrendMetrics;
    for (size_t i = 0; i <= metrics.size(); ++i) {
      if (metrics[ui.*TrendMember] == metric) return "";
      sim.click(1);
    }
    return "the trend pages don't show that";
  }

  /// The pages' names, as in kPages.
  static std::vector<std::string> pageNames() { return names(kPages); }
  /// The menu items' names, as in kItems.
  static std::vector<std::string> itemNames() { return names(kItems); }

  /// @brief Fills in `hooks` with this instantiation's functions as the
  /// program starts. The value itself means nothing.
  static inline const bool hooked = [] {
    hooks = {&frame,     &dirty,     &dismissAlerts, &goToPage,
             &openMenu,  &showTrend, &pageNames,     &itemNames};
    return true;
  }();
};

// The one place that names the UI's private members.
template struct Inside<&Ui::frame_, &Ui::mode_, &Ui::page_, &Ui::item_, &Ui::trendMetric_,
                       &Ui::kTrendMetrics, &Ui::alertShowing, &Ui::dirty_>;

/// @brief Made-up history: slow and quicker swings around the reading `now`,
/// ending at it, with a little noise.
///
/// Values stay plausible: CO2 at least 400 ppm, the VOC and NOx indices at
/// least 1, and everything else but temperature at least 0.
/// @param metric Which metric.
/// @param hoursAgo How long ago, in hours; 0 is now.
/// @param now The reading now.
/// @return The value then; NaN if `now` is NaN.
float madeUp(Metric metric, double hoursAgo, float now) {
  if (std::isnan(now)) return NAN;
  constexpr double kTau = 6.283185307179586;
  // How far each metric swings, in the order of aq::Metric.
  static const float kSwing[] = {1.4f, 2.6f, 2.9f, 3.1f, 260, 6, 30, 2, 1.1f, 5};
  const double phase = 0.7 * static_cast<double>(aq::idx(metric));
  auto shape = [&](double h) {
    const double tick = std::floor(h * 360);  // one value per 10 s
    const double noise = std::sin(tick * 12.9898 + phase) * 43758.5453;
    return 0.55 * std::sin(kTau * h / 9.0 + phase) + 0.3 * std::sin(kTau * h / 2.3 + 2 * phase) +
           0.15 * std::sin(kTau * h / 0.4 + 3 * phase) + 0.06 * (noise - std::floor(noise) - 0.5);
  };
  double v = now + kSwing[aq::idx(metric)] * (shape(hoursAgo) - shape(0));
  switch (metric) {
    case Metric::Temperature:
      break;
    case Metric::Co2:
      v = std::max(v, 400.0);
      break;
    case Metric::Voc:
    case Metric::Nox:
      v = std::max(v, 1.0);
      break;
    default:
      v = std::max(v, 0.0);
      break;
  }
  return static_cast<float>(v);
}

}  // namespace

Simulator::Simulator() : history_(std::make_unique<aq::MetricHistory>()) {
  if (hooks.frame == nullptr) {
    fprintf(stderr, "Internal error: the UI hooks weren't set up\n");
    std::abort();
  }
  fillHistory();
  powerOn();
}

Simulator::~Simulator() = default;

void Simulator::powerOn() {
  world.restartRequested = false;
  world.sensor.resultSequence = 0;
  for (bool& down : world.buttonDown) down = false;
  air_ = aq::AirQuality();
  wifi_.begin(world.settings);
  ota_.begin();
  ui_ = std::make_unique<Ui>();
  battery_ = power::BatteryWatch();
  ui_->begin(sensor_, world.settings, air_, *history_, wifi_, ota_, mqtt_, battery_);
  step();
}

void Simulator::step() {
  world.nowMs += kStepMs;
  const uint32_t now = world.nowMs;
  if (world.sensor.state == SensorState::Busy && world.busyUntilMs != 0 &&
      static_cast<int32_t>(now - world.busyUntilMs) >= 0) {
    world.busyUntilMs = 0;
    world.sensor.state = SensorState::Measuring;
    world.showResult(world.busyResult.c_str());
  }
  world.sensor.activity = world.activity.c_str();
  wifi_.loop(now);
  ota_.loop(false, false, now);
  air_.update(world.sensor.measurement);
  history_->update(&world.sensor.measurement, now);
  // As the firmware's main loop does.
  const SensorSnapshot& s = world.sensor;
  if (world.batteryConnected) {
    battery_.setPresent(true);  // the firmware takes a couple of minutes to notice
    battery_.update(s.batteryVolts, s.batteryPercent, s.batteryRate, now);
  } else {
    battery_ = power::BatteryWatch();
  }
  ui_->update(world.sensor, now);
  if (battery_.empty()) ui_->showBatteryEmpty();
  if (world.restartRequested) powerOn();
}

void Simulator::run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t += kStepMs) step();
}

void Simulator::click(int button) {
  world.buttonDown[button] = true;
  run(80);
  world.buttonDown[button] = false;
  run(80);
}

void Simulator::hold(int button, uint32_t ms) {
  world.buttonDown[button] = true;
  run(ms);
  world.buttonDown[button] = false;
  run(80);
}

void Simulator::fillHistory() {
  // In place: the UI keeps a reference to it, and it is too big for the stack.
  std::destroy_at(history_.get());
  std::construct_at(history_.get());
  const uint32_t span =
      static_cast<uint32_t>(std::min<uint64_t>(world.historyMinutes * 60000ULL, world.nowMs));
  aq::Measurement m;
  for (uint32_t ago = span;; ago -= std::min<uint32_t>(ago, 10000)) {
    const double hours = ago / 3600000.0;
    for (size_t i = 0; i < aq::kMetricCount; ++i) {
      m.values[i] = madeUp(Metric(i), hours, world.sensor.measurement.values[i]);
    }
    history_->update(&m, world.nowMs - ago);
    if (ago == 0) break;
  }
}

std::string Simulator::goToPage(const std::string& name) { return hooks.goToPage(*this, *ui_, name); }

std::string Simulator::openMenu(const std::string& item) { return hooks.openMenu(*this, *ui_, item); }

std::string Simulator::showTrend(Metric metric) { return hooks.showTrend(*this, *ui_, metric); }

std::string Simulator::dismissAlerts() { return hooks.dismissAlerts(*this, *ui_); }

std::vector<uint8_t> Simulator::screen() {
  hooks.dirty(*ui_) = true;
  step();  // draws
  LGFX_Sprite& frame = hooks.frame(*ui_);
  std::vector<uint8_t> rgb;
  rgb.reserve(kWidth * kHeight * 3);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto c = frame.readPixelRGB(x, y);
      rgb.insert(rgb.end(), {c.r, c.g, c.b});
    }
  }
  return rgb;
}

std::vector<std::string> Simulator::pageNames() { return hooks.pageNames(); }

std::vector<std::string> Simulator::itemNames() { return hooks.itemNames(); }
