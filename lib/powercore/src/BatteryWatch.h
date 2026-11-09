// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief What the battery monitor's readings mean: whether the battery is
/// charging, when to warn that it's running low, when to switch off before
/// it's empty, and whether USB power is back after that.
///
/// There's no way to see USB power directly on this board; the battery
/// monitor's charge rate tells instead. While the monitor runs on battery it
/// draws far more than BatteryWatch::kRateThreshold, and while USB power
/// charges an empty battery the rate is clearly positive. But the rate is
/// worked out from the charge over time, so it takes a minute or more to
/// follow when USB power is plugged in or unplugged. The voltage jumps at once
/// then, and stands in for the rate until it follows.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

/// @brief Battery and power: what the battery readings mean
/// (power::BatteryWatch) and switching the monitor off and on again.
namespace power {

/// What the battery is doing.
enum class BatteryState : uint8_t {
  Unknown,      ///< No reading yet.
  Charging,     ///< On USB power, charging.
  Discharging,  ///< Running the monitor.
  Idle,         ///< Full on USB power, or hardly used.
};

/// @brief The state a charge rate means, with BatteryWatch::kRateThreshold.
/// @param ratePerHour The charge rate in % per hour, negative while
///   discharging; NaN if unknown.
/// @return Unknown for NaN; otherwise Charging, Discharging or Idle.
BatteryState stateOf(float ratePerHour);

/// @brief Follows the battery monitor's readings: the battery's state, low
/// battery warnings, when to switch off, and whether a battery is connected
/// at all.
///
/// Feed it each new reading with update(); it keeps no other time.
class BatteryWatch {
 public:
  /// A charge rate this far from zero, in % per hour, is charging or
  /// discharging.
  static constexpr float kRateThreshold = 0.5f;
  /// @brief Above this, in V, the battery is full and on USB power.
  ///
  /// Running the monitor pulls it lower at once. The rate wanders a few % per
  /// hour either side of zero then, which isn't the battery running the
  /// monitor.
  static constexpr float kFullVolts = 4.1f;
  /// @brief A jump this big between readings, in V, is USB power plugged in
  /// or unplugged.
  ///
  /// It's above the dips Wi-Fi traffic causes, up to about 45 mV in a single
  /// reading.
  static constexpr float kJumpVolts = 0.06f;
  /// How long the rate may take to follow such a jump, in ms.
  static constexpr uint32_t kSettleMs = 5 * 60000;
  static constexpr float kLowPercent = 20;      ///< Charge left for the first warning, in %.
  static constexpr float kVeryLowPercent = 10;  ///< Charge left for the second warning, in %.
  /// How far above a limit the charge must be before its warning ends, in %.
  static constexpr float kWarningHysteresis = 2;
  /// @brief Voltage for the first warning, in V, held for kLowForMs.
  ///
  /// The battery monitor's percentage is for a battery at rest. Running the
  /// monitor, a small or worn battery reaches kEmptyVolts while it still
  /// reads 20 % or more, so the warnings also come at these voltages: for a
  /// 400 mAh battery, about 20 and 10 minutes before switching off.
  static constexpr float kLowVolts = 3.6f;
  /// Voltage for the second warning, in V, held for kLowForMs (see kLowVolts).
  static constexpr float kVeryLowVolts = 3.55f;
  /// @brief Voltage at which the monitor switches itself off, in V, held for
  /// kLowForMs.
  ///
  /// The sensor needs 3.15 V, which the board's regulator can't hold with the
  /// battery much below this.
  static constexpr float kEmptyVolts = 3.5f;
  /// How long the voltage must stay below a limit, in ms, so a brief dip
  /// under load doesn't count.
  static constexpr uint32_t kLowForMs = 60000;
  /// After switching off: a battery this full, in V, can only be charging.
  static constexpr float kBackVolts = 3.8f;
  /// @brief The highest battery monitor reading, in %, learned as "empty".
  ///
  /// What the battery monitor reads when the monitor switches itself off is
  /// where "empty" is for this battery: about 20 % for a 400 mAh one. More
  /// than this means the reading was off, e.g. soon after a battery was
  /// connected to a running monitor, and it isn't learned.
  static constexpr float kMaxEmptyPercent = 40;
  /// @brief How many readings tell whether a battery is connected: the last
  /// 2 minutes.
  ///
  /// Without a battery, the battery monitor reads the charger instead: 4.0 to
  /// 4.2 V, jumping about 30 mV from one reading to the next. A battery's
  /// voltage moves a few mV, except under Wi-Fi traffic, and only a battery
  /// reads lower.
  static constexpr size_t kPresenceReadings = 24;
  static constexpr float kSteadyVolts = 0.005f;   ///< Median jump, in V: a battery.
  static constexpr float kJitteryVolts = 0.010f;  ///< Median jump, in V: the charger alone.
  static constexpr float kChargerVolts = 4.0f;    ///< The lowest the charger alone reads, in V.
  static constexpr float kBatteryVolts = 3.95f;   ///< Lower than this, in V, is a battery.

  /// @brief Takes a new reading, every few seconds.
  /// @param volts The battery's voltage in V; NaN if there's no reading.
  /// @param gaugePercent The battery monitor's charge in %; NaN if none.
  /// @param ratePerHour The charge rate in % per hour, negative while
  ///   discharging; NaN if none.
  /// @param nowMs The time, as millis().
  void update(float volts, float gaugePercent, float ratePerHour, uint32_t nowMs);
  /// @brief Says USB power has just come back, e.g. it's why the monitor
  /// started again: the state is Charging until the rate follows, for
  /// kSettleMs at most.
  /// @param nowMs The time, as millis().
  void pluggedIn(uint32_t nowMs);
  /// @brief Whether a battery is connected.
  ///
  /// Until the readings tell, what setPresent() said, e.g. what it was before
  /// the monitor restarted.
  bool present() const { return present_; }
  /// @brief Sets whether a battery is connected, until the readings tell.
  /// @param present Whether one is.
  void setPresent(bool present) { present_ = present; }
  /// @brief Whether the readings have just shown a battery connected after
  /// none. True once.
  ///
  /// The battery monitor's charge then still comes from reading the charger,
  /// and may be a different battery's. If there was none for at least
  /// kPresenceReadings more readings, update() has also forgotten where
  /// "empty" was: it may be another battery.
  bool takeConnected();
  /// What the battery is doing.
  BatteryState state() const { return state_; }
  /// @brief The charge left before the monitor switches itself off, in %;
  /// NaN if unknown.
  ///
  /// Until it has learned where that is, the battery monitor's own
  /// percentage.
  float percent() const;
  /// @brief What the battery monitor read the last time the monitor switched
  /// itself off, in % (0 if never), as learnEmpty() found it.
  float emptyPercent() const { return emptyPercent_; }
  /// @brief Sets where "empty" is, e.g. as saved before a restart.
  /// @param gaugePercent The battery monitor's reading at "empty", in %.
  ///   Anything outside 0 to kMaxEmptyPercent, NaN included, counts as 0.
  void setEmptyPercent(float gaugePercent);
  /// @brief On switching itself off: learns the latest reading as where
  /// "empty" is.
  /// @return Whether that changed it.
  bool learnEmpty();
  /// @brief The warning to show: 0 fine, charging or no battery; 1 low;
  /// 2 very low.
  ///
  /// Both this and empty() are only ever set with a battery present.
  uint8_t warning() const { return warning_; }
  /// @brief Whether the voltage has been at or below kEmptyVolts for
  /// kLowForMs while discharging: time to switch off.
  ///
  /// Discharging as the state says or as the rate says: a jump up in voltage
  /// that makes the state Charging may only be a load ending.
  bool empty() const { return isEmpty_; }

 private:
  /// A jump in voltage between readings (see kJumpVolts).
  enum class Jump : uint8_t { None, Up, Down };

  /// Whether the voltage has been below a limit for kLowForMs.
  class Below {
   public:
    /// @brief Takes whether the voltage is below the limit now.
    /// @param below Whether it is.
    /// @param nowMs The time, as millis().
    /// @return Whether it has been for kLowForMs.
    bool update(bool below, uint32_t nowMs);

   private:
    bool below_ = false;
    uint32_t sinceMs_ = 0;  ///< When it went below.
  };

  /// Notes a jump against the last readings.
  void noteJump(float volts, uint32_t nowMs);
  /// Keeps the reading for telling whether a battery is connected, and tells.
  void notePresence(float volts);
  /// The state from the rate, the voltage and the last jump.
  BatteryState stateFrom(float volts, float ratePerHour, uint32_t nowMs);

  bool present_ = false;
  bool sawNone_ = false;    ///< The readings showed no battery...
  bool connected_ = false;  ///< ...and then one; see takeConnected().
  size_t noneVerdicts_ = 0;  ///< Readings in a row that said no battery, up to kPresenceReadings.
  float readings_[kPresenceReadings] = {};  ///< For notePresence(), oldest at next_.
  size_t next_ = 0;
  size_t count_ = 0;
  BatteryState state_ = BatteryState::Unknown;
  float gaugePercent_ = NAN;  ///< The battery monitor's latest reading.
  float emptyPercent_ = 0;
  float recent_[3] = {NAN, NAN, NAN};  ///< The last readings' voltages.
  Jump jump_ = Jump::None;  ///< The last jump, until the rate follows it.
  uint32_t jumpMs_ = 0;
  uint8_t warning_ = 0;
  uint8_t voltsWarning_ = 0;  ///< From the voltage; lasts while discharging.
  Below low_;
  Below veryLow_;
  Below empty_;
  bool isEmpty_ = false;
};

/// @brief After switching off for an empty battery: whether USB power is back,
/// so the monitor can start again.
/// @param volts The battery's voltage in V; NaN if unknown.
/// @param ratePerHour The charge rate in % per hour; NaN if unknown.
/// @return Whether it's charging, or the voltage is at least kBackVolts.
bool powerIsBack(float volts, float ratePerHour);

}  // namespace power
