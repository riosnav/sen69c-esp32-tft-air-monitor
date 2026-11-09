// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief LovyanGFX's panel code, for the board's ST7789 only (a Panel_LCD).
///
/// The library's own hub (lgfx_v1_panel.cpp) builds every panel driver, and
/// one of them uses a variable-length array, which MSVC doesn't support.

/// Lets LovyanGFX's `.inl` files compile: they're only meant to be part of a
/// hub that sets this.
#define LGFX_V1_IMPLEMENTATION

#include <lgfx/v1/panel/Panel_Device.inl>
#include <lgfx/v1/panel/Panel_LCD.inl>

#undef LGFX_V1_IMPLEMENTATION
