// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief The device, simulated: the firmware's UI, with its own state
/// machine and drawing code, run as the firmware's main loop runs it, on a
/// simulated clock.
///
/// Buttons are pressed by holding the simulated pins down for a while, so the
/// firmware's own button logic sees every press.
#pragma once

#include <AirQuality.h>
#include <BatteryWatch.h>
#include <History.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "World.h"
#include "net/Ota.h"
#include "ui/Ui.h"

/// @brief Runs the firmware's UI on the simulated device (`world`) as the
/// firmware's main loop would, and presses its buttons.
class Simulator {
 public:
  static constexpr int kWidth = 240;   ///< The screen's width, in pixels.
  static constexpr int kHeight = 135;  ///< Its height, in pixels.

  /// Starts the UI on `world`, with made-up history (see fillHistory()).
  Simulator();
  /// Ends the simulation.
  ~Simulator();

  /// Starts the UI afresh, as after a restart, keeping `world` and the
  /// history.
  void powerOn();
  /// @brief Lets time pass, running the main loop every kStepMs.
  /// @param ms How long, in ms.
  void run(uint32_t ms);
  /// @brief Presses a button for 80 ms, then leaves it released for 80 ms.
  /// @param button 0 to 2, for D0 to D2.
  void click(int button);
  /// @brief Holds a button down, then leaves it released for 80 ms.
  /// @param button 0 to 2, for D0 to D2.
  /// @param ms How long to hold it, in ms.
  void hold(int button, uint32_t ms);

  /// Fills the trend pages with made-up data that ends at the current
  /// readings, as far back as world.historyMinutes (and the uptime) allow.
  void fillHistory();

  /// @brief Presses buttons until the UI shows a page: closes alerts, the
  /// menu and its screens, then steps through the pages the shorter way round.
  /// @param name The page's name, as listed in `--help` (see pageNames()).
  /// @return An error message (e.g. the Wi-Fi setup screen covers the pages),
  ///   or "" once it's there.
  std::string goToPage(const std::string& name);
  /// @brief Presses buttons until the settings menu shows an item: goes back
  /// to the pages, opens the menu and steps to the item.
  /// @param item The item's name, as listed in `--help` (see itemNames()).
  /// @return An error message, or "" once it's there.
  std::string openMenu(const std::string& item);
  /// @brief Presses buttons until a trend page shows `metric`: the trend page
  /// that's showing, or else the last hour's.
  /// @param metric The metric.
  /// @return An error message, or "" once it's there.
  std::string showTrend(aq::Metric metric);
  /// @brief Presses D1 until no alert shows.
  /// @return An error message, or "" once none does.
  std::string dismissAlerts();

  /// @brief The screen as it is now, as RGB rows.
  ///
  /// Runs the main loop once, which takes kStepMs, to draw it.
  /// @return kWidth by kHeight pixels, row by row, 3 bytes (R, G, B) each.
  std::vector<uint8_t> screen();

  /// The pages' names, for goToPage() and `--help`.
  static std::vector<std::string> pageNames();
  /// The settings menu items' names, for openMenu() and `--help`.
  static std::vector<std::string> itemNames();

 private:
  /// Simulated time from one pass of the main loop to the next, in ms.
  static constexpr uint32_t kStepMs = 10;

  /// One pass of the firmware's main loop, kStepMs after the last.
  void step();

  aq::AirQuality air_;  ///< Levels and alerts from the readings, as the firmware keeps them.
  /// The trend pages' data. Stays at one address: the UI keeps a reference.
  std::unique_ptr<aq::MetricHistory> history_;
  SensorService sensor_;         ///< Its commands are the stand-ins in Fakes.cpp.
  WifiService wifi_;             ///< Shows World::wifi (see Fakes.cpp).
  Ota ota_;                      ///< Shows World::ota (see Fakes.cpp).
  MqttService mqtt_;             ///< Shows World::mqtt (see Fakes.cpp).
  power::BatteryWatch battery_;  ///< Follows the battery readings, as the firmware's loop does.
  std::unique_ptr<Ui> ui_;       ///< Made afresh by powerOn().
};
