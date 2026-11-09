// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief LovyanGFX on the desktop: the real library, built with its SDL back
/// end (see SDL.h), plus stand-ins for the ESP32 SPI bus and PWM backlight
/// that the firmware's Lgfx.h configures.
///
/// The bus drops what it's sent; the renderer reads the frame from the UI's
/// sprite instead.
#pragma once

#include "msvc_compat.h"

#include <lgfx/v1_init.hpp>

#include <cstdint>
#include <vector>

/// ESP-IDF's names for the SPI hosts.
enum spi_host_device_t { SPI1_HOST = 0, SPI2_HOST = 1, SPI3_HOST = 2 };
/// ESP-IDF's names for an SPI host's DMA channel.
enum spi_common_dma_t { SPI_DMA_DISABLED = 0, SPI_DMA_CH1 = 1, SPI_DMA_CH2 = 2, SPI_DMA_CH_AUTO = 3 };

/// @brief The LovyanGFX graphics library: the real one, plus the ESP32 parts
/// the firmware's Lgfx.h names (lgfx::Bus_SPI, lgfx::Light_PWM) as stand-ins.
///
/// src/Platform.cpp supplies the platform functions its desktop back end
/// would have.
namespace lgfx {
inline namespace v1 {

/// @brief Stand-in for LovyanGFX's ESP32 SPI bus: keeps the settings Lgfx.h
/// gives it and, as a Bus_NULL, drops whatever the panel sends.
class Bus_SPI : public Bus_NULL {
 public:
  /// The fields of LovyanGFX's ESP32 Bus_SPI, without its quad SPI pins.
  struct config_t {
    uint32_t freq_write = 16000000;          ///< Clock for writing, in Hz.
    uint32_t freq_read = 8000000;            ///< Clock for reading, in Hz.
    int16_t pin_sclk = -1;                   ///< Clock pin; -1 for none.
    int16_t pin_miso = -1;                   ///< Data in pin; -1 for none.
    int16_t pin_mosi = -1;                   ///< Data out pin; -1 for none.
    int16_t pin_dc = -1;                     ///< Data/command select pin; -1 for none.
    uint8_t spi_mode = 0;                    ///< SPI mode, 0 to 3.
    bool spi_3wire = true;                   ///< Whether reading also uses the data out pin.
    bool use_lock = true;                    ///< Whether to lock the bus for each transaction.
    uint8_t dma_channel = SPI_DMA_CH_AUTO;   ///< See spi_common_dma_t.
    spi_host_device_t spi_host = SPI2_HOST;  ///< Which SPI host.
  };

  /// The settings.
  const config_t& config() const { return cfg_; }
  /// @brief Sets the settings, which change nothing here.
  /// @param cfg The settings.
  void config(const config_t& cfg) { cfg_ = cfg; }

  /// An SPI bus, for LovyanGFX's panel code.
  bus_type_t busType() const override { return bus_type_t::bus_spi; }
  /// @brief Sets up nothing: there's no bus.
  /// @return Always true.
  bool init() override { return true; }
  /// @brief A buffer the panel converts pixels into before sending them.
  /// @param length Its size, in bytes.
  /// @return The buffer, until the next call.
  uint8_t* getDMABuffer(uint32_t length) override {
    buffer_.resize(length);
    return buffer_.data();
  }

 private:
  config_t cfg_;
  std::vector<uint8_t> buffer_;  ///< For getDMABuffer().
};

/// @brief Stand-in for LovyanGFX's ESP32 PWM backlight: keeps the brightness,
/// which the renderer doesn't show.
class Light_PWM : public ILight {
 public:
  /// The fields of LovyanGFX's ESP32 Light_PWM.
  struct config_t {
    uint32_t freq = 1200;     ///< PWM frequency, in Hz.
    int16_t pin_bl = -1;      ///< Backlight pin; -1 for none.
    uint8_t offset = 0;       ///< The duty at brightness 1, of 255; brighter spreads above it.
    uint8_t pwm_channel = 7;  ///< The ESP32's PWM (LEDC) channel.
    bool invert = false;      ///< Whether the backlight is brightest with the pin low.
  };

  /// The settings.
  const config_t& config() const { return cfg_; }
  /// @brief Sets the settings, which change nothing here.
  /// @param cfg The settings.
  void config(const config_t& cfg) { cfg_ = cfg; }

  /// @brief Takes the starting brightness.
  /// @param brightness 0 (off) to 255.
  /// @return Always true.
  bool init(uint8_t brightness) override {
    brightness_ = brightness;
    return true;
  }
  /// @brief Takes a new brightness.
  /// @param brightness 0 (off) to 255.
  void setBrightness(uint8_t brightness) override { brightness_ = brightness; }

 private:
  config_t cfg_;
  uint8_t brightness_ = 0;  ///< Kept, though nothing reads it.
};

}  // namespace v1
}  // namespace lgfx
