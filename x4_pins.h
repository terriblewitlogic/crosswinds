// Xteink X4 GPIO map (ESP32-C3). Shared by lgfx_config.h and x4.h.
// Sources: Adafruit "CircuitPython on the Xteink X4" pinout, the FreeInk SDK
// board profile used by CrossPoint, and the xteink-x4-sample sketch.
#pragma once

static constexpr int X4_PIN_EPD_SCLK = 8;
static constexpr int X4_PIN_EPD_MOSI = 10;
static constexpr int X4_PIN_EPD_CS = 21;
static constexpr int X4_PIN_EPD_DC = 4;
static constexpr int X4_PIN_EPD_RST = 5;
static constexpr int X4_PIN_EPD_BUSY = 6;
static constexpr int X4_PIN_SD_MISO = 7;       // shared SPI bus, unused by Crosswinds
static constexpr int X4_PIN_SD_CS = 12;        // held HIGH so the card stays deselected
static constexpr int X4_PIN_POWER_LATCH = 13;  // HIGH keeps the battery rail on
static constexpr int X4_PIN_BTN_ADC1 = 1;      // BACK / CONFIRM / LEFT / RIGHT ladder
static constexpr int X4_PIN_BTN_ADC2 = 2;      // UP / DOWN ladder
static constexpr int X4_PIN_BTN_POWER = 3;     // active low, deep-sleep wake source
static constexpr int X4_PIN_BATTERY = 0;       // half the battery voltage
static constexpr int X4_PIN_USB_DETECT = 20;   // HIGH while USB is connected
