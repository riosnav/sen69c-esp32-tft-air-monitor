// SPDX-License-Identifier: GPL-3.0-or-later
//
// The device, simulated: the firmware's UI, with its own state machine and
// drawing code, run as the firmware's main loop runs it, on a simulated
// clock. Buttons are pressed by holding the simulated pins down for a while.
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

class Simulator {
 public:
  static constexpr int kWidth = 240;
  static constexpr int kHeight = 135;

  Simulator();
  ~Simulator();

  // Starts the UI afresh, as after a restart (keeps `world`).
  void powerOn();
  // Lets `ms` pass, running the main loop every few milliseconds.
  void run(uint32_t ms);
  void click(int button);
  void hold(int button, uint32_t ms);

  // Fills the trend pages with made-up data that ends at the current
  // readings, as far back as world.historyMinutes (and the uptime) allow.
  void fillHistory();

  // These press buttons until the UI shows what's asked; each returns an
  // error message, or "" once it's there. Names as listed in --help.
  std::string goToPage(const std::string& name);
  std::string openMenu(const std::string& item);
  std::string showTrend(aq::Metric metric);
  std::string dismissAlerts();

  // The screen as it is now, as RGB rows.
  std::vector<uint8_t> screen();

  static std::vector<std::string> pageNames();
  static std::vector<std::string> itemNames();

 private:
  static constexpr uint32_t kStepMs = 10;

  void step();

  aq::AirQuality air_;
  std::unique_ptr<aq::MetricHistory> history_;
  SensorService sensor_;
  WifiService wifi_;
  Ota ota_;
  MqttService mqtt_;
  power::BatteryWatch battery_;
  std::unique_ptr<Ui> ui_;
};
