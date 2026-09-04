// SPDX-License-Identifier: GPL-3.0-or-later
// BB-Deck touch input — implements the shared touch-input API
// (HeltecV4CapTouch.h) for the XPT2046 resistive controller on the bare 2.8"
// 240x320 module.
//
// BUS NOTE: the XPT2046 shares SCK/MOSI/MISO with the ILI9341 panel and
// TFT_eSPI owns that bus. This driver therefore does NOT open its own
// SPIClass -- it reads through ILI9341LCDDisplay::touchGetRaw/touchGetRawZ.
// Two SPIClass hosts pointed at the same pins is exactly the conflict the
// T-Lora Pager variant documents; TFT_eSPI's own touch path already
// serialises with the display's transactions.
//
// RESISTIVE, NOT CAPACITIVE: reported pressure (z) is the touch signal. On the
// verified hardware z rests at 10-26 untouched and reaches ~2700 under a firm
// press, so the threshold below is generous. A light fingertip that would work
// on a capacitive panel registers nothing -- that is the panel, not this code.
//
// Calibration: raw ADC counts (0..4095) map linearly to panel pixels using the
// XPT_CAL_* build flags. Defaults are conservative full-swing values; the real
// per-panel numbers come from the on-screen calibration page, which reads
// heltecV4CapTouchGetRaw().

#if defined(WADA_BBDECK) && defined(ESP32)

#include "HeltecV4CapTouch.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <helpers/ui/MomentaryButton.h>
#include <helpers/ui/ILI9341LCDDisplay.h>
#include "Xpt2046Calibrate.h"

extern ILI9341LCDDisplay display;   // variants/heltec_v4/target.cpp

// Panel geometry. RAW_* is the native portrait the controller sees, before any
// display rotation; SCR_* is the logical (rotated) output LVGL draws into.
static const int RAW_W = 240;
static const int RAW_H = 320;
static const int SCR_W = 320;   // landscape output
static const int SCR_H = 240;

// ---- Calibration (raw ADC extents). Override in platformio.ini. ----
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
// Pressure above which a contact counts. Rest is ~10-26 on this panel.
#ifndef XPT_Z_THRESHOLD
  #define XPT_Z_THRESHOLD 400
#endif

#ifndef XPT_TRACE
  #define XPT_TRACE 0
#endif

#ifndef XPT_SWIPE_MIN
  #define XPT_SWIPE_MIN 40
#endif
#ifndef XPT_TAP_MOVE_MAX
  #define XPT_TAP_MOVE_MAX 16
#endif
#ifndef XPT_LONG_MS
  #define XPT_LONG_MS 1000
#endif

static bool     s_init_ok = false;
static char     s_diag[96] = "xpt2046: (not started)";

static volatile uint16_t s_dbg_rawx = 0, s_dbg_rawy = 0, s_dbg_z = 0;

static bool     s_have_touch = false;
static uint16_t s_cur_x = 0, s_cur_y = 0;
static bool     s_down = false;
static unsigned long s_down_at = 0;
static uint16_t s_start_x = 0, s_start_y = 0;
static uint16_t s_last_x = 0, s_last_y = 0;
static bool     s_live = false;
static uint16_t s_live_x = 0, s_live_y = 0;
static bool     s_tap_pending = false;
static uint16_t s_tap_x = 0, s_tap_y = 0;
static bool     s_swiping_now = false;
static bool     s_swipe_pending = false;
static int8_t   s_swipe_x = 0, s_swipe_y = 0;

static uint8_t  s_ui_rotation = 0;
static uint8_t  s_point_rotation = 0;

// ---- Init -------------------------------------------------------------
// Nothing to probe: the XPT2046 has no ID register and no init sequence, and
// its bus is already up because the display brought it up. Success here means
// "the display is available to read through", not "a chip answered".
bool heltecV4CapTouchBegin() {
  if (s_init_ok) return true;
  s_init_ok = true;
  const XptCal& c = xptCalActive();
  snprintf(s_diag, sizeof s_diag, "xpt2046 cs=%d z>%d x[%u..%u] y[%u..%u]%s",
           TOUCH_CS, XPT_Z_THRESHOLD, c.x_min, c.x_max, c.y_min, c.y_max,
           c.valid ? " (calibrated)" : " (build defaults)");
  Serial.printf("[TOUCH] %s\n", s_diag);
  return true;
}

// ---- Coordinate mapping ----------------------------------------------
static void mapRaw(uint16_t rx, uint16_t ry, uint16_t* ox, uint16_t* oy) {
  // 1. raw ADC -> native portrait pixels, using the ACTIVE calibration: the
  // stored per-panel values from the wizard when present, otherwise the
  // compile-time XPT_CAL_* fallback.
  const XptCal& cal = xptCalActive();
  // Span is SIGNED on purpose: cal.x_min is the raw value at pixel 0 and
  // cal.x_max the raw value at the last pixel, so on a panel whose counts run
  // opposite to screen coordinates min > max and the span is negative. The
  // division handles that and the axis comes out the right way round -- no
  // separate flip flag needed.
  const long xspan = (long)cal.x_max - (long)cal.x_min;
  const long yspan = (long)cal.y_max - (long)cal.y_min;
  if (xspan == 0 || yspan == 0) { if (ox) *ox = 0; if (oy) *oy = 0; return; }
  long px = ((long)rx - (long)cal.x_min) * (RAW_W - 1) / xspan;
  long py = ((long)ry - (long)cal.y_min) * (RAW_H - 1) / yspan;
  if (px < 0) px = 0; if (px > RAW_W - 1) px = RAW_W - 1;
  if (py < 0) py = 0; if (py > RAW_H - 1) py = RAW_H - 1;

#if XPT_FLIP_X
  px = (RAW_W - 1) - px;
#endif
#if XPT_FLIP_Y
  py = (RAW_H - 1) - py;
#endif

  // 2. portrait -> rotated output, same transform as HeltecV4CapTouch/RakTapV2
  int sx, sy;
  switch (s_point_rotation) {
    case 1:  // ROT_90
      sx = (RAW_H - 1) - (int)py;
      sy = (int)px;
      break;
    case 3:  // ROT_270
      sx = (int)py;
      sy = (RAW_W - 1) - (int)px;
      break;
    default: // portrait / 180: identity
      sx = (int)px;
      sy = (int)py;
      break;
  }

  const int maxx = (s_point_rotation == 1 || s_point_rotation == 3) ? SCR_W : RAW_W;
  const int maxy = (s_point_rotation == 1 || s_point_rotation == 3) ? SCR_H : RAW_H;
  if (sx < 0) sx = 0; if (sx >= maxx) sx = maxx - 1;
  if (sy < 0) sy = 0; if (sy >= maxy) sy = maxy - 1;
  *ox = (uint16_t)sx;
  *oy = (uint16_t)sy;
}

static void xptPoll() {
  const uint16_t z = display.touchGetRawZ();
  s_dbg_z = z;

#if XPT_TRACE
  // Rate-limited so it cannot itself become the stall. Prints resting z once a
  // second and every crossing of the press threshold, so a dead read (z stuck
  // at 0) is distinguishable from a live read that maps off-screen.
  {
    static uint32_t last_beat = 0;
    static bool     was_down  = false;
    const bool down = (z >= XPT_Z_THRESHOLD);
    if (down != was_down) {
      was_down = down;
      uint16_t tx = 0, ty = 0;
      display.touchGetRaw(&tx, &ty);
      Serial.printf("[XPT] %s z=%u raw=%u,%u\n", down ? "DOWN" : "UP  ", z, tx, ty);
    } else if (millis() - last_beat >= 1000) {
      last_beat = millis();
      uint16_t tx = 0, ty = 0;
      display.touchGetRaw(&tx, &ty);
      Serial.printf("[XPT] idle z=%u raw=%u,%u rot=%u\n", z, tx, ty, s_point_rotation);
    }
  }
#endif

  if (z < XPT_Z_THRESHOLD) { s_have_touch = false; return; }

  uint16_t rx = 0, ry = 0;
  display.touchGetRaw(&rx, &ry);
  s_dbg_rawx = rx;
  s_dbg_rawy = ry;
  mapRaw(rx, ry, &s_cur_x, &s_cur_y);
  s_have_touch = true;
#if XPT_TRACE
  Serial.printf("[XPT] hit raw=%u,%u -> px=%u,%u\n", rx, ry, s_cur_x, s_cur_y);
#endif
}

void heltecV4CapTouchGetRaw(uint16_t* rx, uint16_t* ry) {
  if (rx) *rx = s_dbg_rawx;
  if (ry) *ry = s_dbg_rawy;
}

// ---- Gesture state machine (mirrors RakTapV2Touch) --------------------
int heltecV4CapTouchCheck() {
  if (!s_init_ok) return BUTTON_EVENT_NONE;
  xptPoll();

  if (s_have_touch) {
    s_live = true;
    s_live_x = s_cur_x;
    s_live_y = s_cur_y;
    if (!s_down) {
      s_down = true;
      s_down_at = millis();
      s_start_x = s_cur_x;
      s_start_y = s_cur_y;
      s_swiping_now = false;
    }
    s_last_x = s_cur_x;
    s_last_y = s_cur_y;
    if (!s_swiping_now) {
      int adx = abs((int)s_last_x - (int)s_start_x);
      int ady = abs((int)s_last_y - (int)s_start_y);
      if (adx >= XPT_SWIPE_MIN && adx > ady) s_swiping_now = true;
    }
    return BUTTON_EVENT_NONE;
  }

  if (s_down) {
    s_down = false;
    s_live = false;
    s_swiping_now = false;
    unsigned long dur = millis() - s_down_at;
    int adx = abs((int)s_last_x - (int)s_start_x);
    int ady = abs((int)s_last_y - (int)s_start_y);
    if (adx >= XPT_SWIPE_MIN && adx > (ady + 8)) {
      bool left = ((int)s_last_x - (int)s_start_x) < 0;
      s_swipe_x = left ? -1 : 1;
      s_swipe_y = 0;
      s_swipe_pending = true;
      return left ? BUTTON_EVENT_DOUBLE_CLICK : BUTTON_EVENT_TRIPLE_CLICK;
    }
    if (ady >= XPT_SWIPE_MIN && ady > (adx + 8)) {
      s_swipe_x = 0;
      s_swipe_y = ((int)s_last_y - (int)s_start_y) < 0 ? -1 : 1;
      s_swipe_pending = true;
      return BUTTON_EVENT_NONE;
    }
    if (dur >= 12 && dur < (unsigned long)XPT_LONG_MS &&
        adx <= XPT_TAP_MOVE_MAX && ady <= XPT_TAP_MOVE_MAX) {
      s_tap_x = s_last_x;
      s_tap_y = s_last_y;
      s_tap_pending = true;
      return BUTTON_EVENT_CLICK;
    }
    if (dur >= (unsigned long)XPT_LONG_MS) return BUTTON_EVENT_LONG_PRESS;
  } else {
    s_live = false;
  }
  return BUTTON_EVENT_NONE;
}

bool heltecV4CapTouchGetLive(uint16_t* x, uint16_t* y) {
  if (!s_live) return false;
  if (x) *x = s_live_x;
  if (y) *y = s_live_y;
  return true;
}
bool heltecV4CapTouchPopTap(uint16_t* x, uint16_t* y) {
  if (!s_tap_pending) return false;
  s_tap_pending = false;
  if (x) *x = s_tap_x;
  if (y) *y = s_tap_y;
  return true;
}
bool heltecV4CapTouchPopSwipe(int8_t* xd, int8_t* yd) {
  if (!s_swipe_pending) return false;
  s_swipe_pending = false;
  if (xd) *xd = s_swipe_x;
  if (yd) *yd = s_swipe_y;
  return true;
}

// ---- Background poll --------------------------------------------------
// NOTE: unlike the I2C boards, this shares the DISPLAY's SPI bus, so polling
// from a second core contends with LVGL's flush. Kept off by default --
// returning false makes UITask poll inline on the LVGL task instead, which is
// the safe arrangement for a shared bus.
bool heltecV4CapTouchStartBackgroundPoll(uint32_t period_ms) {
  (void)period_ms;
  return false;
}
bool heltecV4CapTouchIsAsyncPolling() { return false; }
bool heltecV4CapTouchIsSwiping() { return s_swiping_now; }
void heltecV4CapTouchSetRotation(uint8_t r) { s_ui_rotation = r & 3; }
void heltecV4CapTouchSetPointRotation(uint8_t r) { s_point_rotation = r & 3; }
void heltecV4CapTouchSetSlowPoll(bool slow) { (void)slow; }
const char* heltecV4CapTouchDebug() { return s_diag; }

#endif
