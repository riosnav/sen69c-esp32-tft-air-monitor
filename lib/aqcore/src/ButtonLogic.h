// SPDX-License-Identifier: GPL-3.0-or-later
//
// Debounced button with click, long-press and auto-repeat events.
#pragma once

#include <cstdint>

namespace aq {

enum class ButtonEvent : uint8_t {
  None,
  Press,      // went down
  Click,      // released before the long-press time
  LongPress,  // held for kLongPressMs (once per press)
  Repeat,     // still held, every kRepeatMs after the long press
};

// Time is passed in rather than read, so the logic can be unit-tested.
class ButtonLogic {
 public:
  static constexpr uint32_t kDebounceMs = 25;
  static constexpr uint32_t kLongPressMs = 600;
  static constexpr uint32_t kRepeatMs = 150;

  // `down` is the raw pin state (true = pressed). Returns at most one event.
  ButtonEvent update(bool down, uint32_t nowMs);
  bool pressed() const { return stable_; }
  // Ignore the rest of the current press, e.g. because it only woke the screen.
  void swallow() { swallowed_ = stable_; }
  // The button is already down, and that press is to be ignored: e.g. the one
  // still held after switching the monitor on.
  void ignoreHeld() { raw_ = stable_ = swallowed_ = true; }

 private:
  bool raw_ = false;
  bool stable_ = false;
  bool swallowed_ = false;
  bool longFired_ = false;
  uint32_t rawChangedMs_ = 0;
  uint32_t pressedMs_ = 0;
  uint32_t lastRepeatMs_ = 0;
};

}  // namespace aq
