// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Debouncing and the events of a press (see ButtonLogic.h).

#include "ButtonLogic.h"

namespace aq {

ButtonEvent ButtonLogic::update(bool down, uint32_t nowMs) {
  if (down != raw_) {
    raw_ = down;
    rawChangedMs_ = nowMs;
  }
  if (raw_ != stable_ && nowMs - rawChangedMs_ >= kDebounceMs) {
    stable_ = raw_;
    if (stable_) {
      pressedMs_ = nowMs;
      longFired_ = false;
      return ButtonEvent::Press;
    }
    bool swallowed = swallowed_;
    swallowed_ = false;
    return swallowed || longFired_ ? ButtonEvent::None : ButtonEvent::Click;
  }
  if (!stable_ || swallowed_) return ButtonEvent::None;
  if (!longFired_ && nowMs - pressedMs_ >= kLongPressMs) {
    longFired_ = true;
    lastRepeatMs_ = nowMs;
    return ButtonEvent::LongPress;
  }
  if (longFired_ && nowMs - lastRepeatMs_ >= kRepeatMs) {
    lastRepeatMs_ = nowMs;
    return ButtonEvent::Repeat;
  }
  return ButtonEvent::None;
}

}  // namespace aq
