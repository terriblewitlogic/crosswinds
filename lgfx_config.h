// LovyanGFX configuration for the Xteink X4 e-paper board and the SDL desktop host.
#pragma once

#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>

#if   defined(LGFX_SDL)
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_sdl panel;
 public:
  LGFX() {
    auto c = panel.config();
    c.memory_width = c.panel_width = 800;
    c.memory_height = c.panel_height = 480;
    panel.config(c);
    panel.setScaling(1, 1);
    panel.setWindowTitle("Crosswinds Xteink X4 Preview");
    setPanel(&panel);
  }
};

static constexpr int DISPLAY_ROTATION = 0;
extern LGFX display;
static inline void board_backlight_begin() { display.setBrightness(255); }
static inline void board_backlight_set(uint8_t pct) {
  display.setBrightness((uint32_t)pct * 255 / 100);
}

#else
#include "x4_pins.h"
#include <lgfx/v1/panel/Panel_SSD1677.hpp>  // not part of the umbrella header

// Xteink X4: GDEQ0426T82 800x480 e-paper on an SSD1677, SPI2. Rotation 0 is
// landscape with the front buttons along the right edge; 2 flips it; 3 is the
// upright portrait CrossPoint uses.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_SSD1677 panel;
  lgfx::Bus_SPI bus;
 public:
  LGFX() {
    {
      auto c = bus.config();
      c.spi_host = SPI2_HOST;
      c.spi_mode = 0;
      c.freq_write = 20000000;  // SSD1677 datasheet maximum for writes
      c.freq_read = 8000000;
      c.spi_3wire = true;       // write-only panel, MOSI only
      c.use_lock = true;
      c.dma_channel = SPI_DMA_CH_AUTO;
      c.pin_sclk = X4_PIN_EPD_SCLK;
      c.pin_mosi = X4_PIN_EPD_MOSI;
      c.pin_miso = -1;
      c.pin_dc = X4_PIN_EPD_DC;
      bus.config(c);
      panel.setBus(&bus);
    }
    {
      auto c = panel.config();
      c.pin_cs = X4_PIN_EPD_CS;
      c.pin_rst = X4_PIN_EPD_RST;
      c.pin_busy = X4_PIN_EPD_BUSY;
      c.memory_width = 800;
      c.memory_height = 480;
      c.panel_width = 800;
      c.panel_height = 480;
      c.offset_x = 0;
      c.offset_y = 0;
      c.offset_rotation = 0;
      c.readable = false;
      c.invert = false;
      c.rgb_order = false;
      c.dlen_16bit = false;
      c.bus_shared = true;      // the SD card socket shares SCLK/MOSI
      panel.config(c);
    }
    setPanel(&panel);
  }
};

static constexpr int DISPLAY_ROTATION = 0;
extern LGFX display;
static inline void board_backlight_begin() {}
static inline void board_backlight_set(uint8_t) {}

#endif
