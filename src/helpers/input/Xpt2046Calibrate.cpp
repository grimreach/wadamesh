// SPDX-License-Identifier: GPL-3.0-or-later
#include "Xpt2046Calibrate.h"

#if defined(WADA_BBDECK) && defined(ESP32)

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <helpers/ui/ILI9341LCDDisplay.h>

extern ILI9341LCDDisplay display;   // variants/heltec_v4/target.cpp

// Panel native (pre-rotation) geometry. Raw X maps to the 240 axis, raw Y to 320.
static const int RAW_W = 240;
static const int RAW_H = 320;

// Compile-time fallback, same values the driver documents.
#ifndef XPT_CAL_X_MIN
  #define XPT_CAL_X_MIN 300
#endif
#ifndef XPT_CAL_X_MAX
  #define XPT_CAL_X_MAX 3800
#endif
#ifndef XPT_CAL_Y_MIN
  #define XPT_CAL_Y_MIN 300
#endif
#ifndef XPT_CAL_Y_MAX
  #define XPT_CAL_Y_MAX 3800
#endif
#ifndef XPT_Z_THRESHOLD
  #define XPT_Z_THRESHOLD 400
#endif

static const char* NS   = "xptcal";
static const char* K_MAGIC = "magic";
static const uint32_t MAGIC = 0x58505431;   // 'XPT1'

static XptCal s_cal = { XPT_CAL_X_MIN, XPT_CAL_X_MAX, XPT_CAL_Y_MIN, XPT_CAL_Y_MAX, false };

// ---- persistence -------------------------------------------------------
bool xptCalHaveStored() {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  const bool ok = (p.getUInt(K_MAGIC, 0) == MAGIC);
  p.end();
  return ok;
}

static bool calRead(XptCal* out) {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  bool ok = false;
  if (p.getUInt(K_MAGIC, 0) == MAGIC) {
    out->x_min = p.getUShort("x0", XPT_CAL_X_MIN);
    out->x_max = p.getUShort("x1", XPT_CAL_X_MAX);
    out->y_min = p.getUShort("y0", XPT_CAL_Y_MIN);
    out->y_max = p.getUShort("y1", XPT_CAL_Y_MAX);
    // Sanity: a degenerate or inverted range would divide by ~0 in the driver.
    ok = (abs((int)out->x_max - (int)out->x_min) > 200) &&
         (abs((int)out->y_max - (int)out->y_min) > 200);
    out->valid = ok;
  }
  p.end();
  return ok;
}

static bool calWrite(const XptCal& c) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putUShort("x0", c.x_min);
  p.putUShort("x1", c.x_max);
  p.putUShort("y0", c.y_min);
  p.putUShort("y1", c.y_max);
  p.putUInt(K_MAGIC, MAGIC);
  p.end();
  return true;
}

void xptCalClear() {
  Preferences p;
  if (p.begin(NS, false)) { p.clear(); p.end(); }
}

void xptCalInit() {
  XptCal c;
  if (calRead(&c)) {
    s_cal = c;
    Serial.printf("[CAL] stored: x[%u..%u] y[%u..%u]\n",
                  s_cal.x_min, s_cal.x_max, s_cal.y_min, s_cal.y_max);
  } else {
    Serial.printf("[CAL] none stored; using build defaults x[%u..%u] y[%u..%u]\n",
                  s_cal.x_min, s_cal.x_max, s_cal.y_min, s_cal.y_max);
  }
}

const XptCal& xptCalActive() { return s_cal; }

// ---- wizard ------------------------------------------------------------

// Inverse of the driver's rotation step: screen coords -> native portrait.
static void screenToPortrait(int sx, int sy, uint8_t rot, int* px, int* py) {
  switch (rot) {
    case 1:  *px = sy;                *py = (RAW_H - 1) - sx; break;
    case 3:  *px = (RAW_W - 1) - sy;  *py = sx;               break;
    default: *px = sx;                *py = sy;               break;
  }
}

static void drawTarget(int cx, int cy, uint16_t colour) {
  display.setColor((ColorVal)colour);
  display.fillRect(cx - 12, cy - 1, 25, 3);   // horizontal arm
  display.fillRect(cx - 1, cy - 12, 3, 25);   // vertical arm
  display.drawRect(cx - 7, cy - 7, 15, 15);   // ring
}

static void banner(const char* l1, const char* l2) {
  display.setColor((ColorVal)0xFFFF);
  display.setTextSize(1);
  display.setCursor(8, 8);   display.print(l1);
  if (l2) { display.setCursor(8, 22); display.print(l2); }
}

// Wait for a firm, settled press; returns its raw pair. Debounces by requiring
// several consecutive samples within a small window -- a resistive panel is
// noisy on contact and release.
static bool sampleTarget(uint16_t* rx, uint16_t* ry, uint32_t timeout_ms) {
  const uint32_t t0 = millis();
  uint16_t lx = 0, ly = 0;
  int stable = 0;

  while (millis() - t0 < timeout_ms) {
    const uint16_t z = display.touchGetRawZ();
    if (z >= XPT_Z_THRESHOLD) {
      uint16_t x = 0, y = 0;
      display.touchGetRaw(&x, &y);
      if (stable > 0 && abs((int)x - (int)lx) < 60 && abs((int)y - (int)ly) < 60) {
        if (++stable >= 6) {          // ~6 consecutive agreeing samples
          *rx = x; *ry = y;
          // wait for release so the next target does not double-trigger
          while (display.touchGetRawZ() >= XPT_Z_THRESHOLD && millis() - t0 < timeout_ms) delay(10);
          delay(150);
          return true;
        }
      } else {
        stable = 1;
      }
      lx = x; ly = y;
    } else {
      stable = 0;
    }
    delay(12);
  }
  return false;
}

void xptCalPromptWindow(uint32_t ms) {
  (void)ms;
  display.setColor((ColorVal)0x07FF);
  display.setTextSize(1);
  display.setCursor(8, 8);
  display.print("Press PRG now to recalibrate touch");
}

bool xptCalRunWizard(uint8_t rot) {
  const int W = (rot == 1 || rot == 3) ? RAW_H : RAW_W;   // logical screen size
  const int H = (rot == 1 || rot == 3) ? RAW_W : RAW_H;
  const int inset = 28;

  struct { int sx, sy; } targets[4] = {
    { inset,         inset         },
    { W - 1 - inset, inset         },
    { W - 1 - inset, H - 1 - inset },
    { inset,         H - 1 - inset },
  };

  uint16_t rawx[4] = {0}, rawy[4] = {0};
  int px[4] = {0}, py[4] = {0};

  Serial.println("[CAL] wizard start");

  for (int i = 0; i < 4; i++) {
    display.startFrame((ColorVal)0x0000);
    banner("TOUCH CALIBRATION", "Tap the centre of each target");
    drawTarget(targets[i].sx, targets[i].sy, 0xF800);   // red = active
    display.endFrame();

    if (!sampleTarget(&rawx[i], &rawy[i], 30000)) {
      Serial.println("[CAL] timed out — keeping previous calibration");
      return false;
    }
    screenToPortrait(targets[i].sx, targets[i].sy, rot, &px[i], &py[i]);
    Serial.printf("[CAL] t%d screen(%d,%d) portrait(%d,%d) raw(%u,%u)\n",
                  i, targets[i].sx, targets[i].sy, px[i], py[i], rawx[i], rawy[i]);
  }

  // Each axis has exactly two distinct portrait coordinates across the four
  // targets; average the pair at each to cancel noise and panel skew, then
  // solve the straight line through them.
  auto solve = [](int c_lo, int c_hi, long r_lo, long r_hi, int span,
                  uint16_t* out_min, uint16_t* out_max) -> bool {
    if (c_hi == c_lo) return false;
    const double units = (double)(r_hi - r_lo) / (double)(c_hi - c_lo);  // raw per pixel
    if (units == 0.0) return false;
    // Endpoints are stored in SCREEN order: the raw value at pixel 0 first, the
    // raw value at the last pixel second. On a panel whose raw counts run
    // opposite to screen coordinates that means min > max, and THAT IS CORRECT --
    // it is how the axis flip is encoded. Sorting them here would silently drop
    // the direction and leave the axis inverted (seen on hardware: pressing the
    // top of the screen registered at the bottom).
    const double v_at_0   = (double)r_lo - units * (double)c_lo;
    const double v_at_end = v_at_0 + units * (double)(span - 1);
    if (fabs(v_at_end - v_at_0) < 200) return false;
    auto clamp4095 = [](double v) -> uint16_t {
      return (uint16_t)(v < 0 ? 0 : (v > 4095 ? 4095 : v));
    };
    *out_min = clamp4095(v_at_0);
    *out_max = clamp4095(v_at_end);
    return true;
  };

  // X axis: targets 0,1 share one px; 2,3 share the other.
  const int  pxA = px[0],                   pxB = px[2];
  const long rxA = ((long)rawx[0] + rawx[1]) / 2;
  const long rxB = ((long)rawx[2] + rawx[3]) / 2;
  // Y axis: targets 0,3 share one py; 1,2 share the other.
  const int  pyA = py[0],                   pyB = py[1];
  const long ryA = ((long)rawy[0] + rawy[3]) / 2;
  const long ryB = ((long)rawy[1] + rawy[2]) / 2;

  XptCal c = s_cal;
  const bool okx = solve(pxA, pxB, rxA, rxB, RAW_W, &c.x_min, &c.x_max);
  const bool oky = solve(pyA, pyB, ryA, ryB, RAW_H, &c.y_min, &c.y_max);

  if (!okx || !oky) {
    Serial.println("[CAL] solve failed (degenerate samples) — keeping previous");
    display.startFrame((ColorVal)0x0000);
    banner("Calibration failed", "Keeping previous values");
    display.endFrame();
    delay(1500);
    return false;
  }

  c.valid = true;
  s_cal = c;
  calWrite(c);
  Serial.printf("[CAL] solved: x[%u..%u] y[%u..%u] (saved)\n",
                c.x_min, c.x_max, c.y_min, c.y_max);

  display.startFrame((ColorVal)0x0000);
  banner("Calibration saved", nullptr);
  display.endFrame();
  delay(900);
  return true;
}

#endif
