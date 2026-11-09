// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Debounced button with click, long-press and auto-repeat events.
#pragma once

#include <cstdint>

namespace aq {

/// What a button just did, from ButtonLogic::update().
enum class ButtonEvent : uint8_t {
  None,       ///< Nothing new.
  Press,      ///< Went down.
  Click,      ///< Released before the long-press time.
  LongPress,  ///< Held for ButtonLogic::kLongPressMs (once per press).
  Repeat,     ///< Still held, every ButtonLogic::kRepeatMs after the long press.
};

/// @brief One button's logic: debouncing, and the events of each press.
///
/// Time is passed in rather than read, so the logic can be unit-tested.
class ButtonLogic {
 public:
  /// How long the pin must keep a new state before it counts, in ms.
  static constexpr uint32_t kDebounceMs = 25;
  /// How long a press must last to be a long press, in ms.
  static constexpr uint32_t kLongPressMs = 600;
  /// How often a button still held repeats after the long press, in ms.
  static constexpr uint32_t kRepeatMs = 150;

  /// @brief Takes the pin's state now.
  /// @param down The raw pin state (true = pressed).
  /// @param nowMs The time, as millis().
  /// @return At most one event; None if there's nothing new.
  ButtonEvent update(bool down, uint32_t nowMs);
  /// Whether the button is down, debounced.
  bool pressed() const { return stable_; }
  /// Ignores the rest of the current press, e.g. because it only woke the screen.
  void swallow() { swallowed_ = stable_; }
  /// @brief Says the button is already down, and that press is to be ignored:
  /// e.g. the one still held after switching the monitor on.
  void ignoreHeld() { raw_ = stable_ = swallowed_ = true; }

 private:
  bool raw_ = false;           ///< The pin's last state, not debounced.
  bool stable_ = false;        ///< The debounced state.
  bool swallowed_ = false;     ///< The rest of this press is ignored.
  bool longFired_ = false;     ///< This press has had its LongPress.
  uint32_t rawChangedMs_ = 0;  ///< When raw_ last changed.
  uint32_t pressedMs_ = 0;     ///< When this press began, debounced.
  uint32_t lastRepeatMs_ = 0;  ///< When the LongPress or the last Repeat came.
};

}  // namespace aq
