// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Saving the screen as a PNG file.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

/// @brief Saves RGB rows as a PNG, each pixel blown up to `scale` x `scale`.
/// @param path The file to write.
/// @param rgb The pixels, row by row, 3 bytes (R, G, B) each.
/// @param width The image's width, in pixels.
/// @param height Its height, in pixels.
/// @param scale How many times larger to save it: 1 or more.
/// @return Whether the file was written.
bool writePng(const std::filesystem::path& path, const std::vector<uint8_t>& rgb, int width,
              int height, int scale);
