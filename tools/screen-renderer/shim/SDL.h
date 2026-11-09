// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Not SDL: just the few SDL names LovyanGFX's desktop back end
/// mentions, so the renderer needs no SDL.
///
/// LovyanGFX only compiles its desktop back end (chosen with `LGFX_SDL` in
/// CMakeLists.txt) when SDL's header is present, and its headers name a few
/// SDL types. This provides just those names: the back end's implementation
/// (lgfx_v1_platforms.cpp) isn't built, and src/Platform.cpp supplies the
/// functions it would have.
#pragma once

/// SDL.h's include guard, which LovyanGFX checks to see that SDL is there.
#define SDL_h_

// Only pointers to these appear, so they're never defined.
struct SDL_Window;    ///< A window.
struct SDL_Renderer;  ///< What draws into a window.
struct SDL_Texture;   ///< An image a renderer draws.
struct SDL_mutex;     ///< A mutex.
/// SDL's keyboard modifiers: just "none".
typedef enum { KMOD_NONE = 0 } SDL_Keymod;
/// SDL's key codes: just "unknown".
typedef enum { SDLK_UNKNOWN = 0 } SDL_KeyCode;
