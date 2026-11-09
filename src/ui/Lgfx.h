// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief LovyanGFX driver configuration for the 1.14 inch 240x135 ST7789 IPS
/// panel on the Adafruit ESP32-S3 Reverse TFT Feather.
///
/// Pin numbers come from the board variant (`pins_arduino.h`).
#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

/// @brief The display: LovyanGFX set up for this board's panel, its SPI bus and
/// its PWM backlight.
///
/// The panel is 135x240 portrait; rotation 1 makes it 240x135 landscape with
/// the buttons on the left, as Ui::begin() sets it.
class Lgfx : public lgfx::LGFX_Device {
 public:
  /// Configures the bus, the panel and the backlight; init() starts them.
  Lgfx() {
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = SCK;
      cfg.pin_mosi = MOSI;
      cfg.pin_miso = -1;  // panel is write-only
      cfg.pin_dc = TFT_DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }
    {
      auto cfg = panel_.config();
      cfg.pin_cs = TFT_CS;
      cfg.pin_rst = TFT_RST;
      cfg.pin_busy = -1;
      // The 135x240 window sits inside the controller's 240x320 memory, 52
      // columns in (53 leaves an unwritten row at one edge of the screen).
      cfg.panel_width = 135;
      cfg.panel_height = 240;
      cfg.offset_x = 52;
      cfg.offset_y = 40;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      panel_.config(cfg);
    }
    {
      auto cfg = light_.config();
      cfg.pin_bl = TFT_BACKLITE;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      light_.config(cfg);
      panel_.setLight(&light_);
    }
    setPanel(&panel_);
  }

 private:
  lgfx::Bus_SPI bus_;  ///< The SPI bus to the panel, write-only.
  lgfx::Panel_ST7789 panel_;  ///< The panel's ST7789 controller.
  lgfx::Light_PWM light_;  ///< The backlight, dimmed by PWM.
};
