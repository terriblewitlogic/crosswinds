// LovyanGFX configuration for the Xteink X4 e-paper board and the SDL desktop host.
#pragma once

#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>

#if defined(SH_X4_PRO) && !defined(LGFX_SDL)
uint8_t* x4pro_framebuffer();
void x4pro_display_push(bool full);

class LGFX : public lgfx::LGFX_Sprite {
  lgfx::epd_mode_t mode = lgfx::epd_mode_t::epd_quality;
 public:
  bool init() {
    uint8_t* fb = x4pro_framebuffer();
    if (!fb) return false;
    // setBuffer(..., bpp) only retypes the color converter; the sprite panel
    // stays 16 bpp and would write past the 48000-byte buffer. Set the depth
    // with no buffer attached, then attach the SDK framebuffer.
    deleteSprite();
    setColorDepth(lgfx::color_depth_t::grayscale_1bit);
    setBuffer(fb, 800, 480);
    return true;
  }
  void setAutoDisplay(bool) {}
  void setEpdMode(lgfx::epd_mode_t m) { mode = m; }
  lgfx::epd_mode_t getEpdMode() const { return mode; }
  void display(int32_t, int32_t, int32_t, int32_t) {
    x4pro_display_push(mode != lgfx::epd_mode_t::epd_fast);
  }
  void display() { display(0, 0, 0, 0); }
  void waitDisplay() {}
  bool getTouch(int32_t*, int32_t*) { return false; }  // The board forwards classified touch events.

  // LovyanGFX copies palette indexes into sprites under 8 bpp, ignoring the
  // palette and swapping ink and paper in the art. These two pushImage forms
  // look each index up and keep its brightness. Rows are byte-aligned.
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const void* data,
                 lgfx::color_depth_t depth, const lgfx::rgb565_t* palette) {
    blit(x, y, w, h, static_cast<const uint8_t*>(data), UINT32_MAX, depth, palette);
  }
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const void* data, uint32_t transparent,
                 lgfx::color_depth_t depth, const lgfx::rgb565_t* palette) {
    blit(x, y, w, h, static_cast<const uint8_t*>(data), transparent, depth, palette);
  }

 private:
  static bool dark(const lgfx::rgb565_t& c) {
    uint32_t rgb = lgfx::color_convert<lgfx::rgb888_t, lgfx::rgb565_t>(c.get());
    uint32_t luma = ((rgb >> 16) & 0xFF) * 299 + ((rgb >> 8) & 0xFF) * 587 + (rgb & 0xFF) * 114;
    return luma < 128u * 1000u;
  }
  void blit(int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t* data, uint32_t transparent,
            lgfx::color_depth_t depth, const lgfx::rgb565_t* palette) {
    const uint32_t bits = depth & lgfx::color_depth_t::bit_mask;
    if (bits == 0 || bits > 8) return;
    const uint32_t mask = (1u << bits) - 1, stride = (w * bits + 7) / 8;
    startWrite();
    for (int32_t row = 0; row < h; ++row) {
      const uint8_t* line = data + row * stride;
      for (int32_t col = 0; col < w; ++col) {
        uint32_t bit = (uint32_t)col * bits;
        uint32_t index = (line[bit >> 3] >> (8 - bits - (bit & 7))) & mask;
        if (index == transparent) continue;
        drawPixel(x + col, y + row, dark(palette[index]) ? (uint16_t)0x0000 : (uint16_t)0xFFFF);
      }
    }
    endWrite();
  }
};

static constexpr int DISPLAY_ROTATION = 0;
extern LGFX display;
static inline void board_backlight_begin() {}
static inline void board_backlight_set(uint8_t) {}

#elif defined(LGFX_SDL)
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
