#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>
#include "LoRaFEMControl.h"

#ifndef ADC_MULTIPLIER
  #define ADC_MULTIPLIER 5.42
#endif

class HeltecV4Board : public ESP32Board {

protected:
  float adc_mult = ADC_MULTIPLIER;

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;
  HeltecV4Board() : periph_power(PIN_VEXT_EN,PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1);
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override {
    if (multiplier == 0.0f) {
      adc_mult = ADC_MULTIPLIER;
    } else {
      adc_mult = multiplier;
    }
    return true;
  }
  float getAdcMultiplier() const override { return adc_mult; }
  const char* getManufacturerName() const override;

  // V4.3 (KCT8103L FEM) high-gain receiver LNA (~17 dB). It is BYPASSED by default; this turns it
  // on/off and re-drives the FEM into RX so the change applies to the live receive immediately.
  // femLnaControllable() is true only on boards where the LNA is switchable (V4.3); a no-op elsewhere.
  void setFemLnaEnable(bool en) { loRaFEMControl.setLNAEnable(en); loRaFEMControl.setRxModeEnable(); }
  bool femLnaControllable() { return loRaFEMControl.isLnaCanControl(); }
};
