// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// BB-Deck: bare 2.8" 240x320 ILI9341 SPI module on a Heltec V4 (HT-WB32LAF V4.3),
// driven via TFT_eSPI. Shape mirrored from ST7796LCDDisplay (the repo's other
// TFT_eSPI consumer); only the panel specifics differ.
//
// Bring-up facts verified on hardware -- see the env in platformio.ini:
//   * USE_FSPI_PORT=1 is MANDATORY. Without it TFT_eSPI's raw register macros
//     resolve _spi_user to 0x10 and the first writecommand() in init() panics
//     with StoreProhibited on every boot. Same trap the T-Lora Pager env
//     documents at length.
//   * TFT_MISO must be a pin DISTINCT from TFT_MOSI. TFT_eSPI_ESP32_S3.h
//     rewrites TFT_MISO=-1 to TFT_MOSI, binding one GPIO as both SPI input and
//     output; the panel then never receives a usable MOSI and stays white.
//   * Display sits on FSPI/SPI2; the SX1262 keeps HSPI/SPI3 (its SPIClass in
//     variants/heltec_v4/target.cpp takes the default ctor arg). No conflict.
//   * The XPT2046 shares SCK/MOSI with the panel. Each chip needs its own wire
//     to those GPIOs -- the module does not bridge them internally.
#if defined(WADA_BBDECK) && defined(ESP32)

#include <helpers/ui/DisplayDriver.h>
#include <helpers/RefCountedDigitalPin.h>
#include <SPI.h>
#include <TFT_eSPI.h>

class ILI9341LCDDisplay : public DisplayDriver {
  TFT_eSPI display;
  RefCountedDigitalPin* _peripher_power;   // Vext on the V4 -- ref-counted, shared with the radio
  bool     _isOn;
  uint16_t _color;
  uint8_t  _brightness_pct;

public:
  static const int LOGICAL_WIDTH  = 240;
  static const int LOGICAL_HEIGHT = 320;

  // Signature matches ST7789LCDDisplay/LGFXDisplay: variants/heltec_v4/target.cpp
  // constructs every display as DISPLAY_CLASS display(NULL).
  ILI9341LCDDisplay(RefCountedDigitalPin* peripher_power = NULL)
    : DisplayDriver(LOGICAL_WIDTH, LOGICAL_HEIGHT),
      _peripher_power(peripher_power),
      _isOn(false), _color(0xFFFF), _brightness_pct(100) { }

  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;

  // Beyond DisplayDriver, called on the concrete type (same as ST7789/ST7796).
  void writePixelsRGB565(int x, int y, int w, int h, const uint16_t* pixels);
  void setDisplayRotation(uint8_t r);

  // XPT2046 raw access. The touch controller shares this panel's SPI bus and
  // TFT_eSPI owns that bus, so the touch driver cannot open its own SPIClass
  // (two hosts on one set of pins is the conflict the T-Lora Pager variant
  // documents). It goes through here instead. Raw ADC counts, not pixels --
  // Xpt2046Touch.cpp does the calibration mapping.
  void     touchGetRaw(uint16_t* x, uint16_t* y);
  uint16_t touchGetRawZ();

  void setBrightness(uint8_t pct);
  uint8_t getBrightness() const { return _brightness_pct; }
};

#endif
