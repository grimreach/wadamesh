// Probe-only variant shim.
//
// The heltec_v4 board JSON declares variant "heltec_v4", so the Arduino core
// needs a pins_arduino.h. Pointing board_build.variants_dir at the repo's real
// variants/ folder also drags LoRaFEMControl.cpp / HeltecV4Board.cpp into the
// framework build, and those need the P_LORA_* defines only the WADAMESH env
// supplies. So this folder holds the header and nothing else -- no .cpp for
// PlatformIO to compile -- and forwards to the real one.
#pragma once
#include "../../../../variants/heltec_v4/pins_arduino.h"
