// SPDX-License-Identifier: GPL-3.0-or-later
//
// Panel bring-up probe for the Heltec V4 + a bare 2.8" 240x320 SPI module.
//
// Answers four questions before any variant work starts:
//   1. Is the wiring right?            -> anything at all appears
//   2. ILI9341 or ST7789?              -> only the matching env draws clean bars
//   3. Are R and B swapped?            -> the labelled colour bars say so
//   4. Which rotation, and is there a  -> the corner markers and the 1px border
//      pixel offset? (ST7789 240x320     must all be fully visible, no gap and
//      panels often need one)            nothing cut off
//
// The V4 gates its peripheral rail behind Vext (GPIO36, active HIGH). If the
// module's VCC is on that switched rail rather than a permanent 3V3, the panel
// stays dark until this runs -- a very easy evening to lose.

#include <Arduino.h>
#include <TFT_eSPI.h>

static constexpr int PIN_VEXT_EN = 36;   // active HIGH on the V4
static constexpr int PIN_BL      = 21;   // TFT_BL, active HIGH

TFT_eSPI tft = TFT_eSPI();

static void reportControllerId() {
#if TFT_MISO < 0
  Serial.println("MISO not wired (TFT_MISO=-1) -- skipping ID read.");
  Serial.println("Judge the controller by which env draws a clean picture.");
#else
  // ILI9341 answers 0xD3 with 00 93 41. ST7789 does not implement 0xD3
  // meaningfully; its RDDID (0x04) returns a vendor/module triplet instead.
  const uint8_t d3[3] = { tft.readcommand8(0xD3, 1),
                          tft.readcommand8(0xD3, 2),
                          tft.readcommand8(0xD3, 3) };
  const uint8_t id[3] = { tft.readcommand8(0x04, 1),
                          tft.readcommand8(0x04, 2),
                          tft.readcommand8(0x04, 3) };
  Serial.printf("0xD3 -> %02X %02X %02X   (ILI9341 answers 00 93 41)\n",
                d3[0], d3[1], d3[2]);
  Serial.printf("0x04 -> %02X %02X %02X   (RDDID)\n", id[0], id[1], id[2]);
  if (d3[1] == 0x93 && d3[2] == 0x41) Serial.println(">>> ILI9341 confirmed.");
  else                                Serial.println(">>> Not an ILI9341 -- try the ST7789 env.");
#endif
}

// Full-bleed frame: a 1px border plus a filled square in each corner. If any
// corner square is clipped or the border is missing on one edge, the panel has
// a row/column offset for this rotation and the variant needs an offset fix.
static void drawFrame(uint8_t rotation) {
  tft.setRotation(rotation);
  const int w = tft.width(), h = tft.height();

  tft.fillScreen(TFT_BLACK);
  tft.drawRect(0, 0, w, h, TFT_WHITE);

  const int m = 12;
  tft.fillRect(0,     0,     m, m, TFT_RED);     // top-left
  tft.fillRect(w - m, 0,     m, m, TFT_GREEN);   // top-right
  tft.fillRect(0,     h - m, m, m, TFT_BLUE);    // bottom-left
  tft.fillRect(w - m, h - m, m, m, TFT_YELLOW);  // bottom-right

  // Labelled bars. If the word RED sits on a blue bar, the colour order is
  // swapped -- add (or drop) -D TFT_RGB_ORDER=TFT_BGR in the real variant.
  struct { uint16_t c; const char* n; } bars[] = {
    { TFT_RED, "RED" }, { TFT_GREEN, "GREEN" }, { TFT_BLUE, "BLUE" },
    { TFT_WHITE, "WHITE" }, { TFT_BLACK, "BLACK" },
  };
  const int bh = (h - 2 * m - 40) / 5;
  for (int i = 0; i < 5; i++) {
    const int y = m + 30 + i * bh;
    tft.fillRect(m, y, w - 2 * m, bh - 2, bars[i].c);
    tft.setTextColor(bars[i].c == TFT_WHITE ? TFT_BLACK : TFT_WHITE, bars[i].c);
    tft.drawString(bars[i].n, m + 6, y + 4, 2);
  }

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(String("rot ") + rotation + "  " + w + "x" + h, m + 4, m + 6, 2);
}

void setup() {
  Serial.begin(115200);
  delay(2500);                       // USB-CDC needs a moment to enumerate

  pinMode(PIN_VEXT_EN, OUTPUT);
  digitalWrite(PIN_VEXT_EN, HIGH);   // peripheral rail on
  delay(120);

  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, HIGH);        // backlight on

  Serial.println();
#if defined(ILI9341_DRIVER)
  Serial.println("=== panel probe: built as ILI9341 ===");
#elif defined(ST7789_DRIVER)
  Serial.println("=== panel probe: built as ST7789 ===");
#endif
  Serial.printf("CS=%d DC=%d SCLK=%d MOSI=%d RST=%d BL=%d MISO=%d @ %d Hz\n",
                TFT_CS, TFT_DC, TFT_SCLK, TFT_MOSI, TFT_RST, TFT_BL,
                TFT_MISO, SPI_FREQUENCY);

  // Stage markers with flush+delay: a crash loop kills USB CDC instantly, so
  // the last marker seen is the last line that completed.
  Serial.println("STAGE A: before tft.init()"); Serial.flush(); delay(400);
  tft.init();
  Serial.println("STAGE B: tft.init() returned"); Serial.flush(); delay(400);
  reportControllerId();
  Serial.println("STAGE C: controller id done"); Serial.flush(); delay(400);
#ifdef TOUCH_CS
  Serial.printf("XPT2046 touch enabled: T_CS=%d MISO=%d @ %d Hz\n",
                TOUCH_CS, TFT_MISO, SPI_TOUCH_FREQUENCY);
#endif

  Serial.println("Cycling rotations 0-3, ~4s each. All four corner squares must");
  Serial.println("be fully visible with an unbroken white border around them.");
}

#ifdef TOUCH_CS
// XPT2046 is resistive: raw ADC counts, not pixels. TFT_eSPI ships an
// interactive corner-tap calibration that yields the five constants any
// driver needs. Capture them here so the real variant starts from measured
// numbers instead of someone else's module's.
static uint16_t s_cal[5] = {0};
static bool     s_calibrated = false;
static bool     s_diagnosed   = false;

// Stage 1 -- raw chip check, no calibration, nothing that can block.
// XPT2046 reports Z (pressure) even with zero calibration, so this separates
// "the chip isn't talking" from "the chip talks but the mapping is wrong".
static void rawTouchHeader() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("RAW TOUCH TEST", 10, 40, 2);
  tft.drawString("Press HARD with a", 10, 70, 2);
  tft.drawString("fingernail or stylus.", 10, 90, 2);
  tft.drawString("Resistive != capacitive.", 10, 110, 2);
  Serial.println("=== raw XPT2046 read, free-running (no calibration) ===");
  Serial.println("Press the panel firmly. Heartbeat every 1s; PRESS on contact.");
}

// Free-running: one heartbeat per second plus a line per press. Never blocks,
// so it does not matter when a terminal attaches -- the state is always live.
static void rawTouchTick() {
  static uint32_t last_beat = 0;
  static uint16_t zmin = 0xFFFF, zmax = 0;
  static uint32_t hits = 0, beats = 0;

  const uint16_t z = tft.getTouchRawZ();
  if (z < zmin) zmin = z;
  if (z > zmax) zmax = z;

  if (z > 500) {                          // TFT_eSPI's own press threshold
    uint16_t rx = 0, ry = 0;
    tft.getTouchRaw(&rx, &ry);
    Serial.printf("  PRESS  z=%-5u raw x=%-5u y=%-5u\n", z, rx, ry);
    tft.fillCircle(120, 260, 6, TFT_GREEN);
    hits++;
  }

  if (millis() - last_beat >= 1000) {
    last_beat = millis();
    Serial.printf("[%3lu] z=%-5u  z-range %u..%u  presses=%lu  %s\n",
                  (unsigned long)++beats, z, zmin, zmax, (unsigned long)hits,
                  hits ? "CHIP OK"
                       : (zmax == 0 ? "z stuck 0 -> check T_DO->40"
                                    : (zmin > 4000 ? "z stuck high -> check T_CS->41"
                                                   : "no press yet -> press harder / check T_CLK->17, T_DIN->33")));
  }
  delay(10);
}

static void runTouchCalibration() {
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Touch each corner arrow", 12, 100, 2);
  tft.drawString("PRESS FIRMLY.", 12, 122, 2);
  delay(1500);

  tft.calibrateTouch(s_cal, TFT_MAGENTA, TFT_BLACK, 15);
  tft.setTouch(s_cal);
  s_calibrated = true;

  Serial.println("=== XPT2046 calibration (rotation 0) ===");
  Serial.printf("uint16_t calData[5] = { %u, %u, %u, %u, %u };\n",
                s_cal[0], s_cal[1], s_cal[2], s_cal[3], s_cal[4]);
  Serial.println();

  tft.fillScreen(TFT_BLACK);
  tft.drawString("Draw to test.", 8, 6, 2);
}

static void touchDrawLoop() {
  uint16_t x = 0, y = 0, rx = 0, ry = 0;
  if (!tft.getTouch(&x, &y)) return;

  tft.getTouchRaw(&rx, &ry);
  tft.fillCircle(x, y, 3, TFT_GREEN);
  Serial.printf("raw %4u,%4u  ->  px %3u,%3u   z=%u\n",
                rx, ry, x, y, tft.getTouchRawZ());
  delay(15);
}

#endif

void loop() {
#ifdef TOUCH_CS
  if (!s_diagnosed) { rawTouchHeader(); s_diagnosed = true; }
  rawTouchTick();
#else
  // Isolation test: full-screen fills vs a sub-rectangle, each held 3s with a
  // serial marker. fillScreen(c) is literally fillRect(0,0,w,h,c) inside
  // TFT_eSPI -- so if the whole screen changes colour but a half-size rect does
  // not appear, the fault is in address-window setup, not in pixel writes.
  struct Step { uint16_t c; const char* n; };
  static const Step fills[] = {
    { TFT_RED,   "RED"   }, { TFT_GREEN, "GREEN" },
    { TFT_BLUE,  "BLUE"  }, { TFT_BLACK, "BLACK" },
  };
  static uint8_t n = 0;

  Serial.printf("[%3u] size=%dx%d  ", n, tft.width(), tft.height());

  if (n % 5 < 4) {
    const Step& f = fills[n % 5];
    Serial.printf("fillScreen(%s) -- WHOLE screen should be %s\n", f.n, f.n);
    tft.fillScreen(f.c);
  } else {
    Serial.println("fillRect(0,0,120,160,WHITE) -- WHITE block, TOP-LEFT QUARTER");
    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, 120, 160, TFT_WHITE);
  }
  n++;
  delay(3000);
#endif
}
