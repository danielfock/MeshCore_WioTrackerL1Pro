#include <Arduino.h>
#include <Wire.h>

#include "WioTrackerL1Board.h"

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock = PWRMGT_VOLTAGE_BOOTLOCK
};

void WioTrackerL1Board::initiateShutdown(uint8_t reason) {
  bool enable_lpcomp = (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
                        reason == SHUTDOWN_REASON_BOOT_PROTECT);

  if (enable_lpcomp) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif // NRF52_POWER_MANAGEMENT

void WioTrackerL1Board::begin() {

  NRF52BoardDCDC::begin();
  btn_prev_state = HIGH;

  pinMode(PIN_VBAT_READ, INPUT); // VBAT ADC input
  // Set all button pins to INPUT_PULLUP
  pinMode(PIN_BUTTON1, INPUT_PULLUP);
  pinMode(PIN_BUTTON2, INPUT_PULLUP);
  pinMode(PIN_BUTTON3, INPUT_PULLUP);
  pinMode(PIN_BUTTON4, INPUT_PULLUP);
  pinMode(PIN_BUTTON5, INPUT_PULLUP);
  pinMode(PIN_BUTTON6, INPUT_PULLUP);
  

  #if defined(PIN_WIRE_SDA) && defined(PIN_WIRE_SCL)
    Wire.setPins(PIN_WIRE_SDA, PIN_WIRE_SCL);
  #endif

  Wire.begin();


  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

#ifdef NRF52_POWER_MANAGEMENT
  checkBootVoltage(&power_config);
#endif

  delay(10);   // give sx1262 some time to power up

}
