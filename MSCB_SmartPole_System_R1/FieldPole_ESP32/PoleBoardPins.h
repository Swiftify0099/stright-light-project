/**
 * @file PoleBoardPins.h
 * @brief MSCB Smart Pole Node - ESP32-WROOM-32 Pinout & Peripheral Assignment
 * @version 1.1.0
 * 
 * Target Board: Standard ESP32 ESP-WROOM-32 (30-pin / 38-pin DevKit)
 * 
 * THIRD-PASS REMEDIATED PIN ARCHITECTURE:
 * 1. INPUT-ONLY PINS (GPI):
 *    - GPIO 34, 35, 36 (VP), 39 (VN) are INPUT ONLY (no internal pull-up/pull-down).
 *    -> Mapped to ADS131M08 DRDY1, DRDY2, DRDY3 interrupts. GPIO 39 is spare.
 * 2. STRAPPING PINS (BOOT SAFETY ENFORCED):
 *    - GPIO 0: Boot strapping (High at boot). Not used.
 *    - GPIO 2: Strapping pin / Onboard diagnostic LED (Active HIGH).
 *    - GPIO 12 (MTDI): Flash voltage LDO select. STRICTLY FORBIDDEN.
 *    - GPIO 15 (MTDO): PWM / debug output at boot. STRICTLY FORBIDDEN.
 *    - GPIO 5: VSPI NSS / Strapping. Dedicated LoRa CS (10k pull-up).
 * 3. SPI FLASH PINS (DO NOT TOUCH):
 *    - GPIO 6..11 internally bonded to SPI flash. STRICTLY FORBIDDEN.
 * 4. 74HC139 SPI ADDRESS DECODER SCHEME:
 *    - Enables multi-peripheral SPI expansion without exceeding ESP32 pin budget:
 *      * GPIO 13 = Decoder /1E (Active-Low Chip Select Enable)
 *      * GPIO 21 = Decoder A0 (Address bit 0)
 *      * GPIO 22 = Decoder A1 (Address bit 1)
 *      * Outputs: /1Y0 -> ADC1 CS, /1Y1 -> ADC2 CS, /1Y2 -> ADC3 CS, /1Y3 -> MCP23S17 CS.
 * 5. MCP23S17 SPI GPIO EXPANDER (DAUGHTERBOARD):
 *    - Port A: 6x MCB Auxiliary Inputs (GPA0..GPA5) + Door Tamper + SPD Alarm
 *    - Port B: 6x Contactor Auxiliary Inputs (GPB0..GPB5) + Spare Inputs
 */

#ifndef POLE_BOARD_PINS_H
#define POLE_BOARD_PINS_H

#include <Arduino.h>

// =====================================================================
//  PRIMARY SPI BUS (VSPI NATIVE PINS)
// =====================================================================
#define POLE_SPI_SCK_PIN        18  // VSPI SCK
#define POLE_SPI_MISO_PIN       19  // VSPI MISO
#define POLE_SPI_MOSI_PIN       23  // VSPI MOSI

// Aliases for radio subsystem
#define POLE_LORA_SCK_PIN       POLE_SPI_SCK_PIN
#define POLE_LORA_MISO_PIN      POLE_SPI_MISO_PIN
#define POLE_LORA_MOSI_PIN      POLE_SPI_MOSI_PIN

// =====================================================================
//  LORA SX1278 RA-02 CONTROL PINS
// =====================================================================
#define POLE_LORA_NSS_PIN        5  // Dedicated LoRa CS (10k pull-up to 3.3V)
#define POLE_LORA_RST_PIN       14  // Hardware Reset (Active LOW output)
#define POLE_LORA_DIO0_PIN       4  // Packet Ready Interrupt (Safe GPIO)

// =====================================================================
//  SIX FEEDER RELAY OUTPUT PINS (OPTO-ISOLATED, ACTIVE-LOW)
// =====================================================================
#define POLE_RELAY_F0_PIN       26  // Feeder 1 Relay (Safe DAC2)
#define POLE_RELAY_F1_PIN       25  // Feeder 2 Relay (Safe DAC1)
#define POLE_RELAY_F2_PIN       27  // Feeder 3 Relay (Safe Output)
#define POLE_RELAY_F3_PIN       16  // Feeder 4 Relay (Safe Output on WROOM-32)
#define POLE_RELAY_F4_PIN       17  // Feeder 5 Relay (Safe Output on WROOM-32)
#define POLE_RELAY_F5_PIN       32  // Feeder 6 Relay (Safe Output)

static const uint8_t POLE_FEEDER_RELAY_PINS[6] = {
  POLE_RELAY_F0_PIN,
  POLE_RELAY_F1_PIN,
  POLE_RELAY_F2_PIN,
  POLE_RELAY_F3_PIN,
  POLE_RELAY_F4_PIN,
  POLE_RELAY_F5_PIN
};

// =====================================================================
//  74HC139 SPI ADDRESS DECODER & METROLOGY CS ALLOCATION
// =====================================================================
// 74HC139 SPI Address Decoder Control Pins
#define POLE_DECODER_ENABLE_PIN 13  // 74HC139 /1E Enable (Active LOW, physical GPIO 13)
#define POLE_DECODER_A0_PIN     21  // 74HC139 1A0 Address 0 (physical GPIO 21)
#define POLE_DECODER_A1_PIN     22  // 74HC139 1A1 Address 1 (physical GPIO 22)

// Compatibility aliases for direct control / pin definitions
#define POLE_ADC1_CS_PIN        POLE_DECODER_ENABLE_PIN // Legacy alias (GPIO 13)
#define POLE_ADC2_CS_PIN        POLE_DECODER_A1_PIN     // Legacy alias (GPIO 22)
#define POLE_ADC3_CS_PIN        POLE_DECODER_A0_PIN     // Legacy alias (GPIO 21)

// Decoder Truth Table Selection Constants
#define POLE_SPI_DEV_DESELECT   0xFF // /1E = HIGH (All CS high)
#define POLE_SPI_DEV_ADC1       0x00 // /1E = LOW, A1=0, A0=0 -> /1Y0 (ADC1: Feeders 1..3 Currents)
#define POLE_SPI_DEV_ADC2       0x01 // /1E = LOW, A1=0, A0=1 -> /1Y1 (ADC2: Feeders 4..6 Currents)
#define POLE_SPI_DEV_ADC3       0x02 // /1E = LOW, A1=1, A0=0 -> /1Y2 (ADC3: 3-Phase Bus Voltages)
#define POLE_SPI_DEV_MCP23S17   0x03 // /1E = LOW, A1=1, A0=1 -> /1Y3 (MCP23S17 Expander)

#define POLE_ADC_SYNC_PIN       33  // Common ADC Sampling Synchronization Pin

// Data Ready Interrupt Inputs (Using Input-Only GPIOs!)
#define POLE_ADC1_DRDY_PIN      34  // ADC 1 Data Ready Interrupt (Input Only)
#define POLE_ADC2_DRDY_PIN      35  // ADC 2 Data Ready Interrupt (Input Only)
#define POLE_ADC3_DRDY_PIN      36  // ADC 3 Data Ready Interrupt (Input Only / VP)

// =====================================================================
//  ONBOARD DIAGNOSTIC LED
// =====================================================================
#define POLE_STATUS_LED_PIN      2  // Onboard Blue LED (Active HIGH)

// =====================================================================
//  MCP23S17 SPI EXPANDER CHANNELS (DAUGHTERBOARD DAUGHTER-IO)
// =====================================================================
// MCP23S17 Hardware SPI address pins tied to GND (Opcode 0x40 Write / 0x41 Read)
#define POLE_MCP23S17_HW_ADDR   0x00

// Port A: 6x MCB Auxiliary Feedback + Enclosure Sensors
#define POLE_EXP_GPA_MCB_F0     0   // Feeder 1 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F1     1   // Feeder 2 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F2     2   // Feeder 3 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F3     3   // Feeder 4 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F4     4   // Feeder 5 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F5     5   // Feeder 6 MCB Aux Contact
#define POLE_EXP_GPA_TAMPER     6   // Enclosure Door Tamper Switch
#define POLE_EXP_GPA_SPD_FAULT  7   // Mains SPD Surge Arrestor Fault

// Port B: 6x Contactor Auxiliary Feedback + Phase Monitors
#define POLE_EXP_GPB_AUX_F0     0   // Feeder 1 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F1     1   // Feeder 2 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F2     2   // Feeder 3 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F3     3   // Feeder 4 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F4     4   // Feeder 5 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F5     5   // Feeder 6 Contactor Closed Feedback
#define POLE_EXP_GPB_PHASE_R    6   // Phase R Presence Detect
#define POLE_EXP_GPB_PHASE_Y    7   // Phase Y Presence Detect

// Hardware Feature Flag
#define POLE_MCB_AUX_ENABLED    1   // Restored via MCP23S17 daughterboard
#define POLE_CONTACTOR_AUX_ENABLED 1

// Helper functions for SPI Decoder Selection
static inline void poleSelectSpiDevice(uint8_t dev) {
  if (dev == POLE_SPI_DEV_DESELECT) {
    digitalWrite(POLE_DECODER_ENABLE_PIN, HIGH);
  } else {
    digitalWrite(POLE_DECODER_A0_PIN, (dev & 0x01) ? HIGH : LOW);
    digitalWrite(POLE_DECODER_A1_PIN, (dev & 0x02) ? HIGH : LOW);
    digitalWrite(POLE_DECODER_ENABLE_PIN, LOW);
  }
}

// =====================================================================
//  COMPILE-TIME HARDWARE CONFLICT AUDIT
// =====================================================================
#if defined(CONFIG_IDF_TARGET_ESP32)
  #define IS_FLASH_PIN(p) ((p) >= 6 && (p) <= 11)
  static_assert(!IS_FLASH_PIN(POLE_SPI_SCK_PIN),        "POLE_SPI_SCK_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_SPI_MISO_PIN),       "POLE_SPI_MISO_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_SPI_MOSI_PIN),       "POLE_SPI_MOSI_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_LORA_NSS_PIN),       "POLE_LORA_NSS_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_DECODER_ENABLE_PIN), "POLE_DECODER_ENABLE_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_DECODER_A0_PIN),     "POLE_DECODER_A0_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_DECODER_A1_PIN),     "POLE_DECODER_A1_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_RELAY_F0_PIN),       "POLE_RELAY_F0_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_RELAY_F1_PIN),       "POLE_RELAY_F1_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_RELAY_F2_PIN),       "POLE_RELAY_F2_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_RELAY_F3_PIN),       "POLE_RELAY_F3_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_RELAY_F4_PIN),       "POLE_RELAY_F4_PIN in forbidden Flash range 6-11!");
  static_assert(!IS_FLASH_PIN(POLE_RELAY_F5_PIN),       "POLE_RELAY_F5_PIN in forbidden Flash range 6-11!");

  // Strapping pin elimination verification
  static_assert(POLE_DECODER_ENABLE_PIN != 12, "GPIO 12 flash voltage hazard!");
  static_assert(POLE_DECODER_A0_PIN != 12,     "GPIO 12 flash voltage hazard!");
  static_assert(POLE_DECODER_A1_PIN != 12,     "GPIO 12 flash voltage hazard!");
  static_assert(POLE_RELAY_F3_PIN != 12,       "Feeder 4 cannot be on GPIO 12!");
  static_assert(POLE_RELAY_F4_PIN != 15,       "Feeder 5 cannot be on GPIO 15!");
#endif

#endif // POLE_BOARD_PINS_H
