// SPDX-License-Identifier: GPL-3.0-or-later
#include "ILI9341LCDDisplay.h"

#if defined(WADA_BBDECK) && defined(ESP32)

#if !defined(ILI9341_DRIVER)
#error "ILI9341LCDDisplay requires -D ILI9341_DRIVER=1 in this env's build_flags."
#endif
#if !defined(USE_FSPI_PORT)
#error "ILI9341LCDDisplay requires -D USE_FSPI_PORT=1 on ESP32-S3. Without it \
TFT_eSPI_ESP32_S3.h falls back to SPI_PORT=FSPI -- the Arduino enum (0), not the IDF \
host index its raw register macros need -- so _spi_user resolves to 0x10 and the first \
writecommand() in init() panics with StoreProhibited on every boot. Confirmed on hardware."
#endif
#if !defined(LOAD_GLCD)
#error "ILI9341LCDDisplay requires -D LOAD_GLCD=1 -- its DisplayDriver text API uses TFT_eSPI font 1."
#endif
#if defined(TFT_MISO) && defined(TFT_MOSI) && (TFT_MISO == TFT_MOSI)
#error "TFT_MISO must be a pin DISTINCT from TFT_MOSI. TFT_eSPI rewrites TFT_MISO=-1 to \
TFT_MOSI on S3, binding one GPIO as both SPI input and output; the panel then gets no \
usable MOSI and stays white. Point TFT_MISO at the pin carrying the XPT2046's T_DO."
#endif

#ifndef DISPLAY_SCALE_X
  #define DISPLAY_SCALE_X 1.0f
#endif
#ifndef DISPLAY_SCALE_Y
  #define DISPLAY_SCALE_Y 1.0f
#endif
#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

// Backlight: plain GPIO on PIN_TFT_LEDA_CTL. The bare module drives its LED pin
// through a transistor (Q1 + R5/R6 on the 2.8" V1.2 board), so the GPIO only
// sources base current -- no external driver IC, unlike the pager's AW9364.
#ifndef PIN_TFT_LEDA_CTL
  #define PIN_TFT_LEDA_CTL 21
#endif
#define BL_LEDC_CHANNEL 7
#define BL_LEDC_FREQ    5000
#define BL_LEDC_BITS    8

bool ILI9341LCDDisplay::begin() {
  if (!_isOn) {
    // Peripheral rail first: the V4 gates Vext (GPIO36, active HIGH) and the
    // panel is dead until it is up. Ref-counted because the radio shares it.
    if (_peripher_power) _peripher_power->claim();
    display.init();   // reads TFT_WIDTH/HEIGHT/ILI9341_DRIVER/TFT_* from build flags
    display.setRotation(DISPLAY_ROTATION);
    setLogicalSize((int)(display.width() / DISPLAY_SCALE_X),
                   (int)(display.height() / DISPLAY_SCALE_Y));
    display.setAttribute(CP437_SWITCH, true);
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE);
    display.setTextSize((uint8_t)(2 * DISPLAY_SCALE_X));

    ledcSetup(BL_LEDC_CHANNEL, BL_LEDC_FREQ, BL_LEDC_BITS);
    ledcAttachPin(PIN_TFT_LEDA_CTL, BL_LEDC_CHANNEL);
    setBrightness(_brightness_pct);

    _isOn = true;
  }
  return true;
}

void ILI9341LCDDisplay::turnOn() { ILI9341LCDDisplay::begin(); }

void ILI9341LCDDisplay::turnOff() {
  if (_isOn) {
    const uint8_t keep = _brightness_pct;
    ledcWrite(BL_LEDC_CHANNEL, 0);
    _brightness_pct = keep;          // begin() must restore the pre-off level, not 0
    display.writecommand(TFT_DISPOFF);
    if (_peripher_power) _peripher_power->release();
    _isOn = false;
  }
}

void ILI9341LCDDisplay::clear() { display.fillScreen(TFT_BLACK); }

void ILI9341LCDDisplay::startFrame(ColorVal bkg) { (void)bkg; display.fillScreen(TFT_BLACK); }

void ILI9341LCDDisplay::setTextSize(int sz) { display.setTextSize((uint8_t)(sz * DISPLAY_SCALE_X)); }

void ILI9341LCDDisplay::setColor(ColorVal c) {
  _color = c;   // ColorVal IS RGB565 (1.17 UIColor)
  display.setTextColor(_color);
}

void ILI9341LCDDisplay::setCursor(int x, int y) {
  display.setCursor((int16_t)(x * DISPLAY_SCALE_X), (int16_t)(y * DISPLAY_SCALE_Y));
}

void ILI9341LCDDisplay::print(const char* str) { display.print(str); }

void ILI9341LCDDisplay::fillRect(int x, int y, int w, int h) {
  display.fillRect(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y,
                   w * DISPLAY_SCALE_X, h * DISPLAY_SCALE_Y, _color);
}

void ILI9341LCDDisplay::drawRect(int x, int y, int w, int h) {
  display.drawRect(x * DISPLAY_SCALE_X, y * DISPLAY_SCALE_Y,
                   w * DISPLAY_SCALE_X, h * DISPLAY_SCALE_Y, _color);
}

void ILI9341LCDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  // TFT_eSPI has no native XBM primitive -- same manual bit-unpack as ST7789/ST7796.
  uint8_t byteWidth = (w + 7) / 8;
  for (int j = 0; j < h; j++) {
    for (int i = 0; i < w; i++) {
      uint8_t byte = bits[j * byteWidth + i / 8];
      if (byte & (0x80 >> (i & 7))) {
        for (int dy = 0; dy < DISPLAY_SCALE_Y; dy++) {
          for (int dx = 0; dx < DISPLAY_SCALE_X; dx++) {
            display.drawPixel((int32_t)(x * DISPLAY_SCALE_X + i * DISPLAY_SCALE_X + dx),
                              (int32_t)(y * DISPLAY_SCALE_Y + j * DISPLAY_SCALE_Y + dy), _color);
          }
        }
      }
    }
  }
}

uint16_t ILI9341LCDDisplay::getTextWidth(const char* str) {
  return (uint16_t)(display.textWidth(str) / DISPLAY_SCALE_X);
}

void ILI9341LCDDisplay::writePixelsRGB565(int x, int y, int w, int h, const uint16_t* pixels) {
  if (!_isOn || !pixels || w <= 0 || h <= 0) return;
  display.startWrite();
  display.setAddrWindow(x, y, w, h);
  display.pushColors(const_cast<uint16_t*>(pixels), (uint32_t)(w * h));  // swap=true matches LVGL RGB565
  display.endWrite();
}

void ILI9341LCDDisplay::endFrame() { /* pushColors' endWrite() already closed the transaction */ }

void ILI9341LCDDisplay::setDisplayRotation(uint8_t r) {
  display.setRotation(r);
  setLogicalSize((int)(display.width() / DISPLAY_SCALE_X),
                 (int)(display.height() / DISPLAY_SCALE_Y));
}

// XPT2046 access -- MUST go through TFT_eSPI's own helpers.
//
// Hand-rolling the SPI transaction here does NOT work, and the reason is worth
// recording: TFT_eSPI puts the ESP32 SPI peripheral into a write-only mode for
// panel updates by writing SPI_USER_REG directly, and only its own
// SET_BUS_READ_MODE (inside begin_touch_read_write) restores duplex. An
// Arduino-level spi.transfer16() on the same bus therefore never samples MISO:
// measured on hardware as z1=0000 z2=0000 y=0000 x=0000 on every read, while
// this path returns touch-correlated values on the same wiring.
//
// The remaining wrinkle is that begin_touch_read_write() only applies
// SPI_TOUCH_FREQUENCY `if (locked)`, and LVGL's flush leaves the bus in a
// transaction nearly always -- so endWrite() first, which closes any open
// transaction, restores locked=true, and is a safe no-op otherwise.
//
// KNOWN INCOMPLETE: with this path z tracks presses but the x/y counts come back
// implausible (0/2/18/8191 rather than a clean 12-bit spread). Root cause not
// yet identified. See bbdeck-project memory notes.
void ILI9341LCDDisplay::touchGetRaw(uint16_t* x, uint16_t* y) {
#ifdef TOUCH_CS
  display.endWrite();
  display.getTouchRaw(x, y);
#else
  if (x) *x = 0;
  if (y) *y = 0;
#endif
}

uint16_t ILI9341LCDDisplay::touchGetRawZ() {
#ifdef TOUCH_CS
  display.endWrite();
  return display.getTouchRawZ();
#else
  return 0;
#endif
}

void ILI9341LCDDisplay::setBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(BL_LEDC_CHANNEL, (uint32_t)((pct * 255 + 50) / 100));
  _brightness_pct = pct;
}

#endif
