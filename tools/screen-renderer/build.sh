#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds screen-renderer with GCC or Clang, CMake, and Ninja if installed
# (else make). Run it again after the firmware's UI code changes.
#   ./build.sh [BUILD_DIR]
set -e

here=$(cd "$(dirname "$0")" && pwd)
build=${1:-"${XDG_CACHE_HOME:-$HOME/.cache}/sen69c-air-monitor/screen-renderer"}

if [ ! -f "$build/CMakeCache.txt" ] && command -v ninja >/dev/null 2>&1; then
  cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release
else
  cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$build" --parallel
echo "Built $build/screen-renderer"
