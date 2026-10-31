// SPDX-License-Identifier: GPL-3.0-or-later
//
// LovyanGFX's panel code, for the board's ST7789 only. The library's own hub
// (lgfx_v1_panel.cpp) builds every panel driver, and one of them uses a
// variable-length array, which MSVC doesn't support.

#define LGFX_V1_IMPLEMENTATION

#include <lgfx/v1/panel/Panel_Device.inl>
#include <lgfx/v1/panel/Panel_LCD.inl>

#undef LGFX_V1_IMPLEMENTATION
