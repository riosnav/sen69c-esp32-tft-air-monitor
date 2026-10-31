// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

// Saves RGB rows as a PNG, each pixel blown up to `scale` x `scale`.
bool writePng(const std::filesystem::path& path, const std::vector<uint8_t>& rgb, int width,
              int height, int scale);
