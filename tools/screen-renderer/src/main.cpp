// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief screen-renderer: draws the air monitor's screen with the firmware's
/// own UI code, following a script, and saves it as PNG files.
///
/// See README.md for building and running it; `--help` lists the script
/// commands.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Png.h"
#include "Simulator.h"

namespace fs = std::filesystem;
using aq::Metric;

namespace {

/// The `--help` text, before the lists of names that printUsage() adds.
constexpr char kUsage[] = R"(Draws the air monitor's screen with the firmware's own UI code.

Usage:
  screen-renderer [--scale N] [--out DIR] SCRIPT...
  screen-renderer [--scale N] [--out DIR] -e "COMMANDS"

Each script runs on a fresh simulated monitor: up for 30 h, in good air, on a
home network. Commands go one per line, or are separated by ';'. "Quotes" keep
spaces in a word, and # starts a comment. Times: 500ms, 3s, 2m, 1h30m (a bare
number is milliseconds).

Pictures
  shot FILE.png [SCALE]     save the screen (FILE relative to --out; SCALE
                            defaults to --scale, which defaults to 1)

Buttons (the firmware's own button logic sees each press)
  click d0|d1|d2            press and release
  hold d0|d1|d2 [TIME]      hold down (default 800ms; a long press is 600ms)
  down d0|d1|d2             press and keep it down (for shots while held)
  up d0|d1|d2               release
  wait TIME                 let time pass
  page PAGE                 press D0/D2 until PAGE shows (closes menus, alerts)
  menu [ITEM]               open the settings and move to ITEM
  trend METRIC              press D1 on a trend page until it shows METRIC
  dismiss                   close every alert
  power-on                  restart the UI (the clock and history carry on)

Readings
  air good|fair|poor|warm-up          set every reading
  set METRIC VALUE|--                 set one (-- = not measured yet)
  sensor measuring|starting|not-found|busy "ACTIVITY"
  sensor-status HEX                   device status register (0x10: fan error)
  i2c-error CODE
  message "TEXT"                      a message from the sensor, e.g. "Fan cleaned"
  history TIME|full|none              how far back the trend pages reach
  uptime TIME                         set the clock (restarts the UI, refills
                                      the history)

Settings
  setting brightness|dim-after|temp-offset|altitude NUMBER   (dim-after in s)
  setting units c|f
  setting idle-screen dim|off
  setting led off|dim|bright
  setting silence off|warnings|all
  setting co2-self-calibration|wifi on|off
  setting web-password|name TEXT
  battery VOLTS PERCENT [RATE]        a battery reading (connects one); RATE in % per hour,
                                      negative while it runs the monitor (default 0)
  battery none                        a battery that gives no reading
  battery off                         no battery connected

Network and updates
  wifi searching | connecting SSID | connected [SSID] [RSSI] | failed SSID [REASON]
  saved [SSID...]                     the saved networks (none: not set up)
  setup off|waiting|joined|testing [SSID]|succeeded|failed [MESSAGE]
  ha off|waiting|connecting|connected|failed [ERROR]
  fixed-ip on|off                     whether the network in use has a fixed address
  logins COUNT                        wrong web passwords so far
  update PERCENT|confirm|done|off     the firmware update screens: receiving,
                                      waiting for a button, accepted
)";

/// The metrics' names in scripts (see parseMetric()).
const std::pair<const char*, Metric> kMetrics[] = {
    {"pm1", Metric::Pm1},   {"pm2.5", Metric::Pm25}, {"pm4", Metric::Pm4},
    {"pm10", Metric::Pm10}, {"co2", Metric::Co2},    {"hcho", Metric::Hcho},
    {"voc", Metric::Voc},   {"nox", Metric::Nox},    {"temp", Metric::Temperature},
    {"rh", Metric::Humidity},
};

/// Prints the `--help` text: kUsage, then the names of the pages, menu items
/// and metrics.
void printUsage() {
  std::string text = kUsage;
  auto list = [&](const char* title, const std::vector<std::string>& names) {
    text += std::string("\n") + title + ":";
    for (const std::string& name : names) text += " " + name;
  };
  list("Pages", Simulator::pageNames());
  list("Menu items", Simulator::itemNames());
  std::vector<std::string> metrics;
  for (const auto& m : kMetrics) metrics.push_back(m.first);
  list("Metrics", metrics);
  printf("%s\n", text.c_str());
}

/// @brief Text in lowercase.
/// @param s The text.
/// @return `s`, with the ASCII letters in lowercase.
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

/// @brief Reads a number.
/// @param s The text, which must be just the number.
/// @param[out] out The number; set even if `s` isn't one.
/// @return Whether `s` is a number.
bool parseNumber(const std::string& s, double& out) {
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return !s.empty() && *end == '\0';
}

/// @brief Reads a metric's name (see kMetrics); "pm25" works for "pm2.5"
/// too.
/// @param s The name, in lowercase.
/// @param[out] out The metric; left as it was if `s` isn't one.
/// @return Whether `s` names a metric.
bool parseMetric(const std::string& s, Metric& out) {
  for (const auto& m : kMetrics) {
    if (s == m.first || (s == "pm25" && m.second == Metric::Pm25)) {
      out = m.second;
      return true;
    }
  }
  return false;
}

/// @brief Reads a time: "1h30m", "90s", "500ms"; a bare number is
/// milliseconds.
///
/// The units are ms, s, m (or min), h and d, and the numbers may have
/// decimals.
/// @param s The text.
/// @param[out] ms The time, in ms; left as it was if `s` isn't one.
/// @return Whether `s` is a time of 4,000,000,000 ms (46 days) or less, so it
///   fits in `ms`.
bool parseDuration(const std::string& s, uint32_t& ms) {
  double total = 0;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = i;
    while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) ++j;
    size_t k = j;
    while (k < s.size() && std::isalpha(static_cast<unsigned char>(s[k]))) ++k;
    const std::string unit = s.substr(j, k - j);
    double factor = 0;
    if (unit.empty() || unit == "ms") factor = 1;
    if (unit == "s") factor = 1000;
    if (unit == "m" || unit == "min") factor = 60000;
    if (unit == "h") factor = 3600000;
    if (unit == "d") factor = 86400000;
    if (j == i || factor == 0) return false;
    total += std::atof(s.substr(i, j - i).c_str()) * factor;
    i = k;
  }
  if (s.empty() || total > 4e9) return false;
  ms = static_cast<uint32_t>(std::lround(total));
  return true;
}

/// @brief Reads a button's name: d0, d1 or d2.
/// @param s The name, in lowercase.
/// @param[out] button 0 to 2; left as it was if `s` isn't one.
/// @return Whether `s` names a button.
bool parseButton(const std::string& s, int& button) {
  if (s.size() != 2 || s[0] != 'd' || s[1] < '0' || s[1] > '2') return false;
  button = s[1] - '0';
  return true;
}

/// @brief Reads "on" or "off".
/// @param s The text.
/// @param[out] on Whether it's "on"; left as it was if it's neither.
/// @return Whether it's either.
bool parseSwitch(const std::string& s, bool& on) {
  if (s != "on" && s != "off") return false;
  on = s == "on";
  return true;
}

/// @brief Adds a network to the saved ones, first, unless it's there already.
/// @param ssid The network's name.
void remember(const std::string& ssid) {
  auto& saved = world.wifi.saved;
  if (std::find(saved.begin(), saved.end(), ssid) == saved.end()) saved.insert(saved.begin(), ssid);
}

/// One command from a script.
struct Command {
  std::vector<std::string> words;  ///< The command, then its arguments.
  int line = 0;                    ///< The line it starts on, from 1, for error messages.
};

/// @brief Splits a script into commands.
///
/// Commands go one per line, or are separated by `;`. "Quotes" keep spaces in
/// a word, and `#` starts a comment that runs to the end of the line.
/// @param text The script.
/// @param[out] error What's wrong with it (a quote isn't closed); left as it
///   was otherwise.
/// @return The commands; none if there's an error.
std::vector<Command> parse(const std::string& text, std::string& error) {
  std::vector<Command> commands;
  Command current;
  std::string word;
  bool inWord = false;
  bool quoted = false;
  int line = 1;
  auto endWord = [&] {
    if (!inWord) return;
    if (current.words.empty()) current.line = line;
    current.words.push_back(word);
    word.clear();
    inWord = false;
  };
  auto endCommand = [&] {
    endWord();
    if (!current.words.empty()) commands.push_back(current);
    current = Command();
  };
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (quoted) {
      if (c == '\n') {
        error = "line " + std::to_string(line) + ": missing closing quote";
        return {};
      }
      if (c == '"') {
        quoted = false;
      } else {
        word += c;
      }
    } else if (c == '"') {
      quoted = inWord = true;
    } else if (c == '#') {
      while (i + 1 < text.size() && text[i + 1] != '\n') ++i;
    } else if (c == '\n' || c == ';') {
      endCommand();
      if (c == '\n') ++line;
    } else if (std::isspace(static_cast<unsigned char>(c))) {
      endWord();
    } else {
      word += c;
      inWord = true;
    }
  }
  if (quoted) {
    error = "line " + std::to_string(line) + ": missing closing quote";
    return {};
  }
  endCommand();
  return commands;
}

/// Runs scripts, each on a fresh simulated monitor.
class Runner {
 public:
  /// @brief A runner whose shots go to `outDir`, at `scale`.
  /// @param outDir The folder `shot` file names start from (`--out`).
  /// @param scale The scale for shots that don't give one, 1 to 16
  ///   (`--scale`).
  Runner(fs::path outDir, int scale) : outDir_(std::move(outDir)), scale_(scale) {}

  /// @brief Runs `script` on a fresh monitor; errors name `source` and the
  /// line.
  /// @param script The script.
  /// @param source Where it's from: a file name, or `-e`.
  /// @return Whether it ran to the end. If not, the error is on stderr.
  bool run(const std::string& script, const std::string& source) {
    std::string error;
    const std::vector<Command> commands = parse(script, error);
    if (!error.empty()) {
      fprintf(stderr, "%s: %s\n", source.c_str(), error.c_str());
      return false;
    }
    world = World();
    sim_ = std::make_unique<Simulator>();
    for (const Command& command : commands) {
      error = execute(command.words);
      if (!error.empty()) {
        fprintf(stderr, "%s:%d: %s: %s\n", source.c_str(), command.line,
                command.words[0].c_str(), error.c_str());
        return false;
      }
    }
    return true;
  }

 private:
  /// @brief Runs one command (see `--help`).
  /// @param words The command, then its arguments.
  /// @return An error message, or "".
  std::string execute(const std::vector<std::string>& words) {
    const std::string command = lower(words[0]);
    const size_t n = words.size() - 1;  // arguments
    auto arg = [&](size_t i) { return i <= n ? words[i] : std::string(); };
    Simulator& sim = *sim_;
    SensorSnapshot& sensor = world.sensor;
    auto& wifi = world.wifi;
    int button = 0;
    uint32_t ms = 0;
    double number = 0;
    bool on = false;
    Metric metric = Metric::Co2;

    // Pictures and buttons.
    if (command == "shot") {
      int scale = scale_;
      if (n == 2 && parseNumber(arg(2), number) && number >= 1 && number <= 16) {
        scale = static_cast<int>(number);
      } else if (n != 1) {
        return "expected: shot FILE.png [SCALE 1-16]";
      }
      const fs::path path = (outDir_ / arg(1)).lexically_normal();
      std::error_code ignored;
      if (path.has_parent_path()) fs::create_directories(path.parent_path(), ignored);
      if (!writePng(path, sim.screen(), Simulator::kWidth, Simulator::kHeight, scale)) {
        return "couldn't write " + path.string();
      }
      printf("%s\n", path.string().c_str());
      return "";
    }
    if (command == "click" || command == "press") {
      if (n != 1 || !parseButton(lower(arg(1)), button)) return "expected: click d0|d1|d2";
      sim.click(button);
      return "";
    }
    if (command == "hold") {
      ms = 800;
      if (n < 1 || n > 2 || !parseButton(lower(arg(1)), button) ||
          (n == 2 && !parseDuration(arg(2), ms))) {
        return "expected: hold d0|d1|d2 [TIME]";
      }
      sim.hold(button, ms);
      return "";
    }
    if (command == "down" || command == "up") {
      if (n != 1 || !parseButton(lower(arg(1)), button)) return "expected: down|up d0|d1|d2";
      world.buttonDown[button] = command == "down";
      sim.run(30);  // past the debounce time
      return "";
    }
    if (command == "wait") {
      if (n != 1 || !parseDuration(arg(1), ms)) return "expected: wait TIME";
      sim.run(ms);
      return "";
    }
    if (command == "page") {
      if (n != 1) return "expected: page PAGE";
      return sim.goToPage(lower(arg(1)));
    }
    if (command == "menu") {
      if (n > 1) return "expected: menu [ITEM]";
      return sim.openMenu(n == 1 ? lower(arg(1)) : "brightness");
    }
    if (command == "trend") {
      if (n != 1 || !parseMetric(lower(arg(1)), metric)) return "expected: trend METRIC";
      return sim.showTrend(metric);
    }
    if (command == "dismiss") return sim.dismissAlerts();
    if (command == "power-on") {
      sim.powerOn();
      return "";
    }

    // Readings.
    if (command == "air") {
      if (n != 1 || !world.setAir(lower(arg(1)))) return "expected: air good|fair|poor|warm-up";
      return "";
    }
    if (command == "set") {
      if (n != 2 || !parseMetric(lower(arg(1)), metric)) return "expected: set METRIC VALUE|--";
      if (arg(2) == "--") {
        number = NAN;
      } else if (!parseNumber(arg(2), number)) {
        return "not a number: " + arg(2);
      }
      sensor.measurement[metric] = static_cast<float>(number);
      return "";
    }
    if (command == "sensor") {
      const std::string what = lower(arg(1));
      world.busyUntilMs = 0;
      if (what == "busy" && n == 2) {
        sensor.state = SensorState::Busy;
        world.activity = arg(2);
      } else if (what == "not-found" && n == 1) {
        sensor.state = SensorState::NotFound;
        sensor.product[0] = '\0';
      } else if ((what == "measuring" || what == "starting") && n == 1) {
        sensor.state = what == "measuring" ? SensorState::Measuring : SensorState::Starting;
        strlcpy(sensor.product, "SEN69C", sizeof sensor.product);
      } else {
        return "expected: sensor measuring|starting|not-found|busy \"ACTIVITY\"";
      }
      return "";
    }
    if (command == "sensor-status") {
      char* end = nullptr;
      const unsigned long value = std::strtoul(arg(1).c_str(), &end, 16);
      if (n != 1 || *end != '\0') return "expected: sensor-status HEX";
      sensor.deviceStatus = static_cast<uint32_t>(value);
      return "";
    }
    if (command == "i2c-error") {
      if (n != 1 || !parseNumber(arg(1), number)) return "expected: i2c-error CODE";
      sensor.lastError = static_cast<int16_t>(number);
      return "";
    }
    if (command == "message") {
      if (n != 1) return "expected: message \"TEXT\"";
      world.showResult(arg(1).c_str());
      return "";
    }
    if (command == "history") {
      const std::string what = lower(arg(1));
      if (n == 1 && what == "full") {
        world.historyMinutes = 7 * 24 * 60;
      } else if (n == 1 && what == "none") {
        world.historyMinutes = 0;
      } else if (n == 1 && parseDuration(what, ms)) {
        world.historyMinutes = ms / 60000;
      } else {
        return "expected: history TIME|full|none";
      }
      sim.fillHistory();
      return "";
    }
    if (command == "uptime") {
      if (n != 1 || !parseDuration(arg(1), ms) || ms < 10000) return "expected: uptime TIME (10s or more)";
      // What's due at a time moves with the clock.
      const uint32_t shift = ms - world.nowMs;
      if (world.ota.deadlineMs != 0) world.ota.deadlineMs += shift;
      if (world.busyUntilMs != 0) world.busyUntilMs += shift;
      world.nowMs = ms;
      sensor.measuringSinceMs = 5000;
      sim.fillHistory();
      sim.powerOn();
      return "";
    }

    // Settings.
    if (command == "setting") {
      if (n != 2) return "expected: setting NAME VALUE";
      Settings& s = world.settings;
      const std::string name = lower(arg(1));
      const std::string value = lower(arg(2));
      const bool isNumber = parseNumber(value, number);
      if (name == "brightness" && isNumber && number >= 10 && number <= 100) {
        s.brightness = static_cast<uint8_t>(number);
      } else if (name == "dim-after" && isNumber && number >= 0 && number < 65536) {
        s.dimAfterS = static_cast<uint16_t>(number);
      } else if (name == "idle-screen" && (value == "dim" || value == "off")) {
        s.screenOffWhenIdle = value == "off";
      } else if (name == "units" && (value == "c" || value == "f")) {
        s.fahrenheit = value == "f";
      } else if (name == "led" && (value == "off" || value == "dim" || value == "bright")) {
        s.led = value == "off" ? LedMode::Off : value == "dim" ? LedMode::Dim : LedMode::Bright;
      } else if (name == "silence" && (value == "off" || value == "warnings" || value == "all")) {
        s.silence = value == "off" ? Silence::Off
                    : value == "warnings" ? Silence::Warnings
                                          : Silence::All;
      } else if (name == "temp-offset" && isNumber) {
        s.tempOffsetC = static_cast<float>(number);
      } else if (name == "altitude" && isNumber && number >= 0 && number <= 3000) {
        s.altitudeM = static_cast<uint16_t>(number);
      } else if (name == "co2-self-calibration" && parseSwitch(value, on)) {
        s.co2SelfCalibration = on;
      } else if (name == "wifi" && parseSwitch(value, on)) {
        s.wifiEnabled = on;
      } else if (name == "web-password") {
        strlcpy(s.webPassword, arg(2).c_str(), sizeof s.webPassword);
      } else if (name == "name") {
        strlcpy(s.name, arg(2).c_str(), sizeof s.name);
      } else {
        return "unknown setting, or a value it can't take";
      }
      return "";
    }
    if (command == "battery") {
      double percent = 0;
      double rate = 0;
      if (n == 1 && lower(arg(1)) == "off") {
        world.batteryConnected = false;
        return "";
      }
      if (n == 1 && lower(arg(1)) == "none") {
        number = percent = rate = NAN;
      } else if (n < 2 || n > 3 || !parseNumber(arg(1), number) || !parseNumber(arg(2), percent) ||
                 (n == 3 && !parseNumber(arg(3), rate))) {
        return "expected: battery VOLTS PERCENT [RATE] | none | off";
      }
      world.batteryConnected = true;
      sensor.batteryVolts = static_cast<float>(number);
      sensor.batteryPercent = static_cast<float>(percent);
      sensor.batteryRate = static_cast<float>(rate);
      return "";
    }

    // Network and updates.
    if (command == "wifi") {
      const std::string what = lower(arg(1));
      if (what == "searching" && n == 1) {
        wifi.state = WifiState::Searching;
        wifi.failedSsid.clear();
      } else if (what == "connecting" && n == 2) {
        wifi.state = WifiState::Connecting;
        wifi.ssid = arg(2);
      } else if (what == "connected" && n <= 3) {
        if (n == 3 && (!parseNumber(arg(3), number) || number < -100 || number > 0)) {
          return "RSSI goes from -100 to 0 dBm";
        }
        wifi.state = WifiState::Connected;
        if (n >= 2) wifi.ssid = arg(2);
        if (n == 3) wifi.rssi = static_cast<int8_t>(number);
      } else if (what == "failed" && (n == 2 || n == 3)) {
        wifi.state = WifiState::Searching;
        wifi.failedSsid = arg(2);
        wifi.failedReason = n == 3 ? arg(3) : "Wrong password?";
      } else {
        return "expected: wifi searching | connecting SSID | connected [SSID] [RSSI] | failed SSID "
               "[REASON]";
      }
      if (wifi.state != WifiState::Searching) {
        wifi.failedSsid.clear();
        remember(wifi.ssid);
      }
      if (!wifi.failedSsid.empty()) remember(wifi.failedSsid);
      world.settings.wifiEnabled = true;
      return "";
    }
    if (command == "saved") {
      wifi.saved.assign(words.begin() + 1, words.end());
      return "";
    }
    if (command == "setup") {
      const std::string what = lower(arg(1));
      if (what == "off" && n == 1) {
        wifi.setup = SetupState::Off;
      } else if (what == "waiting" && n == 1) {
        wifi.setup = SetupState::Waiting;
      } else if (what == "joined" && n == 1) {
        wifi.setup = SetupState::Joined;
      } else if (what == "testing" && n <= 2) {
        wifi.setup = SetupState::Testing;
        if (n == 2) wifi.trialSsid = arg(2);
      } else if (what == "succeeded" && n == 1) {
        // Joined the network the phone chose, and saved it.
        wifi.setup = SetupState::Succeeded;
        wifi.state = WifiState::Connected;
        wifi.ssid = wifi.trialSsid;
        remember(wifi.ssid);
        world.settings.wifiEnabled = true;
      } else if (what == "failed" && n <= 2) {
        wifi.setup = SetupState::Failed;
        if (n == 2) wifi.setupMessage = arg(2);
      } else {
        return "expected: setup off|waiting|joined|testing [SSID]|succeeded|failed [MESSAGE]";
      }
      return "";
    }
    if (command == "ha") {
      const std::string what = lower(arg(1));
      static const std::pair<const char*, MqttState> kStates[] = {
          {"off", MqttState::Off},
          {"waiting", MqttState::Waiting},
          {"connecting", MqttState::Connecting},
          {"connected", MqttState::Connected},
          {"failed", MqttState::Failed},
      };
      const auto* state = std::find_if(std::begin(kStates), std::end(kStates),
                                       [&](const auto& s) { return what == s.first; });
      if (state == std::end(kStates) || n > 2 || (n == 2 && state->second != MqttState::Failed)) {
        return "expected: ha off|waiting|connecting|connected|failed [ERROR]";
      }
      world.mqtt.state = state->second;
      if (n == 2) world.mqtt.error = arg(2);
      world.settings.mqttEnabled = state->second != MqttState::Off;
      return "";
    }
    if (command == "update") {
      const std::string what = lower(arg(1));
      if (n == 1 && what == "off") {
        world.ota = {};
      } else if (n == 1 && what == "confirm") {
        world.ota = {OtaState::Confirming, 100, world.nowMs + Ota::kConfirmWindowMs};
      } else if (n == 1 && what == "done") {
        world.ota = {OtaState::Accepted, 100, 0};
      } else if (n == 1 && parseNumber(what, number) && number >= 0 && number <= 100) {
        world.ota = {OtaState::Receiving, static_cast<uint8_t>(number), 0};
      } else {
        return "expected: update PERCENT|confirm|done|off";
      }
      return "";
    }
    if (command == "fixed-ip") {
      if (n != 1 || !parseSwitch(lower(arg(1)), on)) return "expected: fixed-ip on|off";
      world.wifi.fixedAddress = on;
      return "";
    }
    if (command == "logins") {
      if (n != 1 || !parseNumber(arg(1), number) || number < 0) return "expected: logins COUNT";
      world.failedLogins = static_cast<uint32_t>(number);
      return "";
    }
    return "unknown command (see --help)";
  }

  fs::path outDir_;                 ///< The folder `shot` file names start from.
  int scale_;                       ///< The scale for shots that don't give one.
  std::unique_ptr<Simulator> sim_;  ///< The monitor the current script runs on.
};

/// @brief Reads a whole file.
/// @param path The file.
/// @param[out] text What it holds; left as it was if it can't be read.
/// @return Whether it could be opened.
bool readFile(const std::string& path, std::string& text) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::stringstream buffer;
  buffer << file.rdbuf();
  text = buffer.str();
  return true;
}

}  // namespace

/// @brief Runs the scripts given on the command line, in order (see
/// `--help`).
/// @param argc How many arguments there are, the program's name included.
/// @param argv The arguments.
/// @return 0 if every script ran to the end, 1 if one didn't, 2 for wrong
///   arguments or none.
int main(int argc, char** argv) {
  int scale = 1;
  fs::path out = ".";
  std::vector<std::pair<std::string, std::string>> scripts;  // text, where from
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    const bool hasValue = i + 1 < argc;
    if (a == "--help" || a == "-h") {
      printUsage();
      return 0;
    } else if (a == "--scale" && hasValue) {
      scale = std::atoi(argv[++i]);
      if (scale < 1 || scale > 16) {
        fprintf(stderr, "--scale goes from 1 to 16\n");
        return 2;
      }
    } else if (a == "--out" && hasValue) {
      out = argv[++i];
    } else if (a == "-e" && hasValue) {
      scripts.emplace_back(argv[++i], "-e");
    } else if (!a.empty() && a[0] == '-') {
      fprintf(stderr, "Unknown option %s (see --help)\n", a.c_str());
      return 2;
    } else {
      std::string text;
      if (!readFile(a, text)) {
        fprintf(stderr, "Can't read %s\n", a.c_str());
        return 2;
      }
      scripts.emplace_back(text, a);
    }
  }
  if (scripts.empty()) {
    printUsage();
    return 2;
  }
  Runner runner(out, scale);
  for (const auto& [text, source] : scripts) {
    if (!runner.run(text, source)) return 1;
  }
  return 0;
}
