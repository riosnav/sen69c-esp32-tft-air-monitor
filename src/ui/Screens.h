// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Screens that are also drawn outside the UI, before it starts.
#pragma once

#include "Lgfx.h"

/// @brief Draws the Battery empty screen: the monitor switched off because its
/// battery is empty, and how it starts again.
/// @param[out] frame The sprite to draw on, the size of the screen.
void drawBatteryEmpty(LGFX_Sprite& frame);
