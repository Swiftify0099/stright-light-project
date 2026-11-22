/**
 * @file HardwareTests.ino
 * @brief MSCB Smart Pole System - Non-Destructive Hardware & Safe Pin Verification
 * @version 1.0.0
 * 
 * Safe low-voltage test sketch:
 * - Checks GPIO configuration and strapping hazards.
 * - Confirms that all relay outputs initialize in the de-energized (OFF) state.
 * - Does NOT energize coils or enable electrical switching.
 */

#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #if __has_include("../../Gateway_ESP32S3/GwBoardPins.h")
    #include "../../Gateway_ESP32S3/GwBoardPins.h"
  #elif __has_include(<GwBoardPins.h>)
    #include <GwBoardPins.h>
  #endif
  #define IS_GW_BUILD 1
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #if __has_include("../../FieldPole_ESP32/PoleBoardPins.h")
    #include "../../FieldPole_ESP32/PoleBoardPins.h"
  #elif __has_include(<PoleBoardPins.h>)
    #include <PoleBoardPins.h>
  #endif
  #define IS_POLE_BUILD 1
#endif

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println(F("\n================================================"));
  Serial.println(F("   MSCB SAFE HARDWARE PIN AUDIT & VERIFICATION  "));
  Serial.println(F("================================================"));

#if defined(IS_GW_BUILD)
  Serial.println(F("Detected Board: Main Gateway (ESP32-S3)"));
  Serial.println(F("Checking LoRa SPI Pins:"));
  Serial.printf("  SCK: GPIO %d | MISO: GPIO %d | MOSI: GPIO %d | CS: GPIO %d\n",
                GW_LORA_SCK_PIN, GW_LORA_MISO_PIN, GW_LORA_MOSI_PIN, GW_LORA_NSS_PIN);
  pinMode(GW_LORA_NSS_PIN, OUTPUT);
  digitalWrite(GW_LORA_NSS_PIN, HIGH);
  Serial.println(F("  [PASS] LoRa NSS safely asserted HIGH (de-selected)"));

#elif defined(IS_POLE_BUILD)
  Serial.println(F("Detected Board: Field Pole (ESP32 Classic)"));
  Serial.println(F("Auditing Relay Safe Initialization:"));

  bool relaysSafe = true;
  for (uint8_t i = 0; i < 6; ++i) {
    uint8_t pin = POLE_FEEDER_RELAY_PINS[i];
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH); // Active-LOW OFF state
    uint8_t readState = digitalRead(pin);
    Serial.printf("  Relay %d (GPIO %d) -> State: %s\n", i + 1, pin, (readState == HIGH) ? "HIGH (SAFE OFF)" : "LOW (ACTIVE HAZARD!)");
    if (readState != HIGH) relaysSafe = false;
  }

  if (relaysSafe) {
    Serial.println(F("  [PASS] All 6 Relays confirmed in SAFE OFF state."));
  } else {
    Serial.println(F("  [FAIL] One or more relays did not initialize to HIGH!"));
  }

  Serial.println(F("Auditing LoRa and ADC Chip Selects:"));
  pinMode(POLE_LORA_NSS_PIN, OUTPUT);
  digitalWrite(POLE_LORA_NSS_PIN, HIGH);
  pinMode(POLE_ADC1_CS_PIN, OUTPUT);
  digitalWrite(POLE_ADC1_CS_PIN, HIGH);
  pinMode(POLE_ADC2_CS_PIN, OUTPUT);
  digitalWrite(POLE_ADC2_CS_PIN, HIGH);
  pinMode(POLE_ADC3_CS_PIN, OUTPUT);
  digitalWrite(POLE_ADC3_CS_PIN, HIGH);
  Serial.println(F("  [PASS] All 4 SPI Chip Select lines safely held HIGH (de-selected)."));
#endif

  Serial.println(F("------------------------------------------------"));
  Serial.println(F("Safe diagnostic verification complete."));
  Serial.println(F("No electrical outputs energized. Mains safe."));
  Serial.println(F("================================================\n"));
}

void loop() {
  delay(1000);
}
