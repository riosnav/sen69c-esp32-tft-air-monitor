// SPDX-License-Identifier: GPL-3.0-or-later
//
// Included before LovyanGFX: first in each of its source files (see
// CMakeLists.txt), and by shim/LovyanGFX.hpp. Some of its structures are
// marked with GCC's __attribute__((packed)), which MSVC doesn't know; they
// are for image decoding and touch controllers, which the UI doesn't use.
#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(x)
#pragma warning(disable : 4068)  // its #pragma GCC lines
#endif
