// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the battery monitor's readings mean: whether the battery is charging,
// when to warn that it's running low, when to switch off before it's empty,
// and whether USB power is back after that.
//
// There's no way to see USB power directly on this board; the battery
// monitor's charge rate tells instead. While the monitor runs on battery it
// draws far more than kRateThreshold, and while USB power charges an empty
// battery the rate is clearly positive. But the rate is worked out from the
// charge over time, so it takes a minute or more to follow when USB power is
// plugged in or unplugged. The voltage jumps at once then, and stands in for
// the rate until it follows.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace power {

enum class BatteryState : uint8_t {
  Unknown,
  Charging,
  Discharging,  // running the monitor
  Idle,         // full on USB power, or hardly used
};

// The charge rate in % per hour, negative while discharging; NaN if unknown.
BatteryState stateOf(float ratePerHour);

class BatteryWatch {
 public:
  static constexpr float kRateThreshold = 0.5f;  // % per hour
  // Above this the battery is full and on USB power: running the monitor
  // pulls it lower at once. The rate wanders a few % per hour either side of
  // zero then, which isn't the battery running the monitor.
  static constexpr float kFullVolts = 4.1f;
  // A jump this big between readings is USB power plugged in or unplugged.
  // Wi-Fi traffic dips the voltage by up to about 45 mV for single readings.
  static constexpr float kJumpVolts = 0.06f;
  // How long the rate may take to follow such a jump.
  static constexpr uint32_t kSettleMs = 5 * 60000;
  static constexpr float kLowPercent = 20;
  static constexpr float kVeryLowPercent = 10;
  static constexpr float kWarningHysteresis = 2;  // % above a limit before its warning ends
  // The battery monitor's percentage is for a battery at rest. Running the
  // monitor, a small or worn battery reaches kEmptyVolts while it still
  // reads 20 % or more, so the warnings also come at these voltages. With a
  // 400 mAh battery that was 20 and 11 minutes before switching off.
  static constexpr float kLowVolts = 3.6f;
  static constexpr float kVeryLowVolts = 3.55f;
  // The sensor needs 3.15 V, which the board's regulator can't hold with the
  // battery much below this.
  static constexpr float kEmptyVolts = 3.5f;
  // How long the voltage must stay below a limit, so a brief dip under load
  // doesn't count.
  static constexpr uint32_t kLowForMs = 60000;
  // After switching off: a battery this full can only be charging.
  static constexpr float kBackVolts = 3.8f;
  // What the battery monitor reads when the monitor switches itself off is
  // where "empty" is for this battery: about 20 % for a 400 mAh one. More
  // than this means the reading was off, e.g. soon after a battery was
  // connected to a running monitor, and it isn't learned.
  static constexpr float kMaxEmptyPercent = 40;
  // Without a battery, the battery monitor reads the charger instead: 4.0 to
  // 4.2 V, jumping about 30 mV from one reading to the next. A battery's
  // voltage moves a few mV, except under Wi-Fi traffic, and only a battery
  // reads lower. Judged on the last 2 minutes of readings.
  static constexpr size_t kPresenceReadings = 24;
  static constexpr float kSteadyVolts = 0.005f;   // median jump: a battery
  static constexpr float kJitteryVolts = 0.010f;  // median jump: the charger alone
  static constexpr float kChargerVolts = 4.0f;    // the lowest the charger alone reads
  static constexpr float kBatteryVolts = 3.95f;   // lower is a battery

  // With each new reading (NaN where there is none), every few seconds.
  void update(float volts, float gaugePercent, float ratePerHour, uint32_t nowMs);
  // USB power has just come back, e.g. it's why the monitor started again.
  void pluggedIn(uint32_t nowMs);
  // Whether a battery is connected. Until the readings tell, what was set,
  // e.g. what it was before the monitor restarted.
  bool present() const { return present_; }
  void setPresent(bool present) { present_ = present; }
  // Whether the readings have just shown a battery connected after none: the
  // battery monitor's charge then still comes from reading the charger, and
  // may be a different battery's. Forgets where "empty" was. True once.
  bool takeConnected();
  BatteryState state() const { return state_; }
  // The charge left before the monitor switches itself off, in %; NaN if
  // unknown. Until it has learned where that is, the battery monitor's own
  // percentage.
  float percent() const;
  // What the battery monitor read the last time the monitor switched itself
  // off (0 if never), as learnEmpty() found it.
  float emptyPercent() const { return emptyPercent_; }
  void setEmptyPercent(float gaugePercent);
  // On switching itself off: learns the latest reading as where "empty" is.
  // Returns whether that changed it.
  bool learnEmpty();
  // 0: fine, charging, or no battery; 1: low; 2: very low.
  // Both this and empty() are only ever set with a battery present.
  uint8_t warning() const { return warning_; }
  // Low for long enough while discharging: time to switch off.
  bool empty() const { return isEmpty_; }

 private:
  enum class Jump : uint8_t { None, Up, Down };

  // Whether the voltage has been below a limit for kLowForMs.
  class Below {
   public:
    bool update(bool below, uint32_t nowMs);

   private:
    bool below_ = false;
    uint32_t sinceMs_ = 0;
  };

  void noteJump(float volts, uint32_t nowMs);
  void notePresence(float volts);
  BatteryState stateFrom(float volts, float ratePerHour, uint32_t nowMs);

  bool present_ = false;
  bool sawNone_ = false;    // the readings showed no battery
  bool connected_ = false;  // ... and then one, see takeConnected()
  float readings_[kPresenceReadings] = {};  // for notePresence(), oldest at next_
  size_t next_ = 0;
  size_t count_ = 0;
  BatteryState state_ = BatteryState::Unknown;
  float gaugePercent_ = NAN;  // the battery monitor's latest reading
  float emptyPercent_ = 0;
  float recent_[3] = {NAN, NAN, NAN};  // the last readings' voltages
  Jump jump_ = Jump::None;  // the last jump, until the rate follows it
  uint32_t jumpMs_ = 0;
  uint8_t warning_ = 0;
  uint8_t voltsWarning_ = 0;  // from the voltage; lasts while discharging
  Below low_;
  Below veryLow_;
  Below empty_;
  bool isEmpty_ = false;
};

// After switching off for an empty battery: whether USB power is back, so
// the monitor can start again.
bool powerIsBack(float volts, float ratePerHour);

}  // namespace power
