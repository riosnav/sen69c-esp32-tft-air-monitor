// SPDX-License-Identifier: GPL-3.0-or-later
//
// LovyanGFX on the desktop: the real library, built with its SDL back end
// (see SDL.h), plus stand-ins for the ESP32 SPI bus and PWM backlight that
// the firmware's Lgfx.h configures. The bus drops what it's sent; the
// renderer reads the frame from the UI's sprite instead.
#pragma once

#include "msvc_compat.h"

#include <lgfx/v1_init.hpp>

#include <cstdint>
#include <vector>

// ESP-IDF's names for the SPI host and its DMA channel.
enum spi_host_device_t { SPI1_HOST = 0, SPI2_HOST = 1, SPI3_HOST = 2 };
enum spi_common_dma_t { SPI_DMA_DISABLED = 0, SPI_DMA_CH1 = 1, SPI_DMA_CH2 = 2, SPI_DMA_CH_AUTO = 3 };

namespace lgfx {
inline namespace v1 {

class Bus_SPI : public Bus_NULL {
 public:
  // The fields of LovyanGFX's ESP32 Bus_SPI.
  struct config_t {
    uint32_t freq_write = 16000000;
    uint32_t freq_read = 8000000;
    int16_t pin_sclk = -1;
    int16_t pin_miso = -1;
    int16_t pin_mosi = -1;
    int16_t pin_dc = -1;
    uint8_t spi_mode = 0;
    bool spi_3wire = true;
    bool use_lock = true;
    uint8_t dma_channel = SPI_DMA_CH_AUTO;
    spi_host_device_t spi_host = SPI2_HOST;
  };

  const config_t& config() const { return cfg_; }
  void config(const config_t& cfg) { cfg_ = cfg; }

  bus_type_t busType() const override { return bus_type_t::bus_spi; }
  bool init() override { return true; }
  // The panel converts pixels into this before sending them.
  uint8_t* getDMABuffer(uint32_t length) override {
    buffer_.resize(length);
    return buffer_.data();
  }

 private:
  config_t cfg_;
  std::vector<uint8_t> buffer_;
};

class Light_PWM : public ILight {
 public:
  // The fields of LovyanGFX's ESP32 Light_PWM.
  struct config_t {
    uint32_t freq = 1200;
    int16_t pin_bl = -1;
    uint8_t offset = 0;
    uint8_t pwm_channel = 7;
    bool invert = false;
  };

  const config_t& config() const { return cfg_; }
  void config(const config_t& cfg) { cfg_ = cfg; }

  bool init(uint8_t brightness) override {
    brightness_ = brightness;
    return true;
  }
  void setBrightness(uint8_t brightness) override { brightness_ = brightness; }

 private:
  config_t cfg_;
  uint8_t brightness_ = 0;
};

}  // namespace v1
}  // namespace lgfx
