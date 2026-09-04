// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// First-boot touch calibration for the BB-Deck's XPT2046 resistive panel.
//
// Resistive panels vary enough between units that compile-time constants are a
// guess even for two modules from the same batch. This runs a four-target
// wizard on a device with no stored calibration, solves the linear mapping from
// the samples, and persists it. Later boots skip it entirely.
//
// Runs from setup() AFTER the display is up and the UI rotation has been applied,
// but BEFORE LVGL exists -- so it draws through DisplayDriver directly, the same
// way the boot wordmark does, and needs no UI framework.
//
// Storage is NVS (namespace "xptcal"), not the SPIFFS prefs file, because this
// has to work before any filesystem is mounted. To force recalibration: hold the
// USER/BOOT button at power-on, use xptCalClear(), or erase the nvs partition.
#if defined(WADA_BBDECK) && defined(ESP32)

#include <stdint.h>

struct XptCal {
  uint16_t x_min, x_max;   // raw extents mapping to the 240px panel axis
  uint16_t y_min, y_max;   // raw extents mapping to the 320px panel axis
  bool     valid;
};

// Load stored calibration, else fall back to the compile-time XPT_CAL_* values.
// Call once early; the touch driver reads the result via xptCalActive().
void           xptCalInit();
const XptCal&  xptCalActive();

bool xptCalHaveStored();   // true if NVS holds a calibration
void xptCalClear();        // forget it (next boot runs the wizard)

// Blocking four-target wizard. Draws, samples, solves, saves, returns true on
// success. `rot` is the LVGL rotation code the UI runs at (0/1/3) so the targets
// are mapped back into the panel's native portrait frame correctly.
bool xptCalRunWizard(uint8_t rot);

// Brief on-screen 'press PRG to recalibrate' prompt shown at boot when a
// calibration already exists. GPIO0 is the strapping pin, so the trigger has
// to be a post-boot press, not a hold through reset.
void xptCalPromptWindow(uint32_t ms);

#endif
