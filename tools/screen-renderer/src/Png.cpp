// SPDX-License-Identifier: GPL-3.0-or-later

#include "Png.h"

#include <cstdlib>
#include <fstream>

// The PNG encoder in LovyanGFX's copy of miniz (lgfx/utility/lgfx_miniz.c).
extern "C" void* tdefl_write_image_to_png_file_in_memory_ex(const void* image, int width,
                                                           int height, int channels,
                                                           size_t* length, unsigned int level,
                                                           int flip);

bool writePng(const std::filesystem::path& path, const std::vector<uint8_t>& rgb, int width,
              int height, int scale) {
  std::vector<uint8_t> scaled;
  scaled.reserve(rgb.size() * scale * scale);
  for (int y = 0; y < height * scale; ++y) {
    const uint8_t* row = rgb.data() + static_cast<size_t>(y / scale) * width * 3;
    for (int x = 0; x < width * scale; ++x) {
      const uint8_t* pixel = row + (x / scale) * 3;
      scaled.insert(scaled.end(), pixel, pixel + 3);
    }
  }
  size_t length = 0;
  void* png = tdefl_write_image_to_png_file_in_memory_ex(scaled.data(), width * scale,
                                                         height * scale, 3, &length, 9, 0);
  if (png == nullptr) return false;
  std::ofstream file(path, std::ios::binary);
  file.write(static_cast<const char*>(png), static_cast<std::streamsize>(length));
  free(png);
  return static_cast<bool>(file);
}
