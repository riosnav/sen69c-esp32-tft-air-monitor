// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not SDL. LovyanGFX only compiles its desktop back end when SDL's header is
// present, and its headers name a few SDL types. This provides just those
// names: the back end's implementation (lgfx_v1_platforms.cpp) isn't built,
// and src/Platform.cpp supplies the functions it would have, so the renderer
// needs no SDL.
#pragma once

#define SDL_h_

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_mutex;
typedef enum { KMOD_NONE = 0 } SDL_Keymod;
typedef enum { SDLK_UNKNOWN = 0 } SDL_KeyCode;
