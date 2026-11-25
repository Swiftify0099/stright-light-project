/**
 * @file PoleBoardPins.h
 * @brief MSCB Smart Pole Node - ESP32-WROOM-32 Pinout & Peripheral Assignment
 * @version 1.1.0
 *
 * Target Board: Standard ESP32 ESP-WROOM-32 (30-pin / 38-pin DevKit)
 *
 * THIRD-PASS REMEDIATED PIN ARCHITECTURE:
 * 1. INPUT-ONLY PINS (GPI):
 *    - GPIO 34, 35, 36 (VP), 39 (VN) are INPUT ONLY (no internal
 * pull-up/pull-down).
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
 *    - Enables multi-peripheral SPI expansion without exceeding ESP32 pin
 * budget:
 *      * GPIO 13 = Decoder /1E (Active-Low Chip Select Enable)
 *      * GPIO 21 = Decoder A0 (Address bit 0)
 *      * GPIO 22 = Decoder A1 (Address bit 1)
 *      * Outputs: /1Y0 -> ADC1 CS, /1Y1 -> ADC2 CS, /1Y2 -> ADC3 CS, /1Y3 ->
 * MCP23S17 CS.
 * 5. MCP23S17 SPI GPIO EXPANDER (DAUGHTERBOARD):
 *    - Port A: 6x MCB Auxiliary Inputs (GPA0..GPA5) + Door Tamper + SPD Alarm
 *    - Port B: 6x Contactor Auxiliary Inputs (GPB0..GPB5) + Spare Inputs
 */

#ifndef POLE_BOARD_PINS_H
#define POLE_BOARD_PINS_H

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "../Tests/ArduinoMock.h"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>
#include <cstddef>

// =====================================================================
//  PRIMARY SPI BUS (VSPI NATIVE PINS)
// =====================================================================
#define POLE_SPI_SCK_PIN 18  // VSPI SCK
#define POLE_SPI_MISO_PIN 19 // VSPI MISO
#define POLE_SPI_MOSI_PIN 23 // VSPI MOSI

// Aliases for radio subsystem
#define POLE_LORA_SCK_PIN POLE_SPI_SCK_PIN
#define POLE_LORA_MISO_PIN POLE_SPI_MISO_PIN
#define POLE_LORA_MOSI_PIN POLE_SPI_MOSI_PIN

// =====================================================================
//  LORA SX1278 RA-02 CONTROL PINS
// =====================================================================
#define POLE_LORA_NSS_PIN 5  // Dedicated LoRa CS (10k pull-up to 3.3V)
#define POLE_LORA_RST_PIN 14 // Hardware Reset (Active LOW output)
#define POLE_LORA_DIO0_PIN 4 // Packet Ready Interrupt (Safe GPIO)

// =====================================================================
//  SIX FEEDER RELAY OUTPUT PINS (OPTO-ISOLATED, ACTIVE-LOW)
// =====================================================================
// Confirmed Hardware Schematic Mapping for 30-Pin ESP32-WROOM-32 DevKit:
// Feeder 1: Physical Pin 24 | Board Label D26 | GPIO 26 (Safe DAC2)
// Feeder 2: Physical Pin 23 | Board Label D25 | GPIO 25 (Safe DAC1)
// Feeder 3: Physical Pin 25 | Board Label D27 | GPIO 27 (Safe Output)
// Feeder 4: Physical Pin 6  | Board Label RX2 | GPIO 16 (Safe UART2 RX)
// Feeder 5: Physical Pin 7  | Board Label TX2 | GPIO 17 (Safe UART2 TX)
// Feeder 6: Physical Pin 21 | Board Label D32 | GPIO 32 (Safe Output)
//
// CRITICAL SILICON DISTINCTION:
// Physical Header Position 24 on the ESP32 DevKit is Board Label D26 = GPIO 26.
// Physical Pin 24 is NOT GPIO 24! GPIO 24 is an internal flash line and
// MUST NEVER be used in firmware.

#define POLE_RELAY_F0_PHYS_PIN                                                 \
  24 // Feeder 1 Physical Header Pin (Right side pin 24)
#define POLE_RELAY_F0_LABEL "D26"
#define POLE_RELAY_F0_PIN 26 // Feeder 1 Relay (GPIO 26)

#define POLE_RELAY_F1_PHYS_PIN                                                 \
  23 // Feeder 2 Physical Header Pin (Right side pin 23)
#define POLE_RELAY_F1_LABEL "D25"
#define POLE_RELAY_F1_PIN 25 // Feeder 2 Relay (GPIO 25)

#define POLE_RELAY_F2_PHYS_PIN                                                 \
  25 // Feeder 3 Physical Header Pin (Right side pin 25)
#define POLE_RELAY_F2_LABEL "D27"
#define POLE_RELAY_F2_PIN 27 // Feeder 3 Relay (GPIO 27)

#define POLE_RELAY_F3_PHYS_PIN                                                 \
  6 // Feeder 4 Physical Header Pin (Left side pin 6)
#define POLE_RELAY_F3_LABEL "RX2"
#define POLE_RELAY_F3_PIN 16 // Feeder 4 Relay (GPIO 16)

#define POLE_RELAY_F4_PHYS_PIN                                                 \
  7 // Feeder 5 Physical Header Pin (Left side pin 7)
#define POLE_RELAY_F4_LABEL "TX2"
#define POLE_RELAY_F4_PIN 17 // Feeder 5 Relay (GPIO 17)

#define POLE_RELAY_F5_PHYS_PIN                                                 \
  21 // Feeder 6 Physical Header Pin (Right side pin 21)
#define POLE_RELAY_F5_LABEL "D32"
#define POLE_RELAY_F5_PIN 32 // Feeder 6 Relay (GPIO 32)

static const uint8_t POLE_FEEDER_RELAY_PINS[6] = {
    POLE_RELAY_F0_PIN, POLE_RELAY_F1_PIN, POLE_RELAY_F2_PIN,
    POLE_RELAY_F3_PIN, POLE_RELAY_F4_PIN, POLE_RELAY_F5_PIN};

static const uint8_t POLE_FEEDER_RELAY_PHYS_PINS[6] = {
    POLE_RELAY_F0_PHYS_PIN, POLE_RELAY_F1_PHYS_PIN, POLE_RELAY_F2_PHYS_PIN,
    POLE_RELAY_F3_PHYS_PIN, POLE_RELAY_F4_PHYS_PIN, POLE_RELAY_F5_PHYS_PIN};

static const char *const POLE_FEEDER_RELAY_LABELS[6] = {
    POLE_RELAY_F0_LABEL, POLE_RELAY_F1_LABEL, POLE_RELAY_F2_LABEL,
    POLE_RELAY_F3_LABEL, POLE_RELAY_F4_LABEL, POLE_RELAY_F5_LABEL};

// =====================================================================
//  74HC139 SPI ADDRESS DECODER & METROLOGY CS ALLOCATION
// =====================================================================
// 74HC139 SPI Address Decoder Control Pins
#define POLE_DECODER_ENABLE_PIN                                                \
  13 // 74HC139 /1E Enable (Active LOW, physical GPIO 13)
#define POLE_DECODER_A0_PIN 21 // 74HC139 1A0 Address 0 (physical GPIO 21)
#define POLE_DECODER_A1_PIN 22 // 74HC139 1A1 Address 1 (physical GPIO 22)

// Compatibility aliases for direct control / pin definitions
#define POLE_ADC1_CS_PIN POLE_DECODER_ENABLE_PIN // Legacy alias (GPIO 13)
#define POLE_ADC2_CS_PIN POLE_DECODER_A1_PIN     // Legacy alias (GPIO 22)
#define POLE_ADC3_CS_PIN POLE_DECODER_A0_PIN     // Legacy alias (GPIO 21)

// Decoder Truth Table Selection Constants
#define POLE_SPI_DEV_DESELECT 0xFF // /1E = HIGH (All CS high)
#define POLE_SPI_DEV_ADC1                                                      \
  0x00 // /1E = LOW, A1=0, A0=0 -> /1Y0 (ADC1: Feeders 1..3 Currents)
#define POLE_SPI_DEV_ADC2                                                      \
  0x01 // /1E = LOW, A1=0, A0=1 -> /1Y1 (ADC2: Feeders 4..6 Currents)
#define POLE_SPI_DEV_ADC3                                                      \
  0x02 // /1E = LOW, A1=1, A0=0 -> /1Y2 (ADC3: 3-Phase Bus Voltages)
#define POLE_SPI_DEV_MCP23S17                                                  \
  0x03 // /1E = LOW, A1=1, A0=1 -> /1Y3 (MCP23S17 Expander)

#define POLE_ADC_SYNC_PIN 33 // Common ADC Sampling Synchronization Pin

// Data Ready Interrupt Inputs (Using Input-Only GPIOs!)
#define POLE_ADC1_DRDY_PIN 34 // ADC 1 Data Ready Interrupt (Input Only)
#define POLE_ADC2_DRDY_PIN 35 // ADC 2 Data Ready Interrupt (Input Only)
#define POLE_ADC3_DRDY_PIN 36 // ADC 3 Data Ready Interrupt (Input Only / VP)

// =====================================================================
//  HARDWARE PHYSICAL COMMISSIONING AUTHORIZATION PIN (GPIO 39 / Sensor_VN)
// =====================================================================
// ELECTRICAL CIRCUIT DESIGN SPECIFICATION:
// 1. ESP32 Silicon Boundary: GPIO 34-39 are strictly INPUT-ONLY without internal
//    pull-up or pull-down resistors! Calling pinMode(POLE_PHYSICAL_AUTH_PIN, INPUT_PULLUP)
//    is electrically invalid and leaves the pin floating.
// 2. External Pull-Up: A physical 10 kΩ 1% (0.125W) pull-up resistor MUST be installed
//    between GPIO 39 and the +3.3V power rail.
// 3. Noise Filter Capacitor: A 100 nF (0.1 µF) ceramic X7R capacitor connected from GPIO 39
//    to Ground (RC time constant tau ~ 1 ms) to reject industrial RF noise and contact bounce.
// 4. Series Protection: A 1 kΩ 1% series resistor between header pin and GPIO 39 to limit
//    clamp diode surge current during ESD and electrostatic events.
// 5. Physical Jumper / Interlock Header: 2-pin header connected between input terminal and GND.
// 6. Active Logic Level: Active-LOW.
//    - Jumper Shorted to GND = 0V / LOW -> Authorized (Commissioning Mode).
//    - Jumper Open / Removed / Disconnected = 3.3V / HIGH -> Locked (Production Safe).
// 7. Safe Failure Mode: Fail-Closed. If the jumper wire cuts, disconnects, open-circuits,
//    or floats, the 10k external resistor pulls the pin to 3.3V (HIGH), immediately revoking
//    authorization and tripping the failsafe emergency shutdown.
// 8. Emergency Hardware Interlock Truth: The discrete relay driver board lacks an optocoupler
//    VCC kill line directly controlled by ESP32. Firmware documents hardware emergency stop line
//    as NOT IMPLEMENTED on the current discrete relay PCB revision; software fail-safe is enforced.
#define POLE_PHYSICAL_AUTH_PIN 39 // Hardware Physical Authorization Jumper (GPI 39 / Sensor_VN)
#define POLE_PHYSICAL_AUTH_TIMEOUT_MS 300000UL // 5-minute auto-expiry window
#define POLE_PHYSICAL_AUTH_DEBOUNCE_MS 50UL    // 50ms continuous elapsed-time debounce requirement

// =====================================================================
//  ONBOARD DIAGNOSTIC LED
// =====================================================================
#define POLE_STATUS_LED_PIN 2 // Onboard Blue LED (Active HIGH)

// =====================================================================
//  MCP23S17 SPI EXPANDER CHANNELS (DAUGHTERBOARD DAUGHTER-IO)
// =====================================================================
// MCP23S17 Hardware SPI address pins tied to GND (Opcode 0x40 Write / 0x41
// Read)
#define POLE_MCP23S17_HW_ADDR 0x00

// Port A: 6x MCB Auxiliary Feedback + Enclosure Sensors
#define POLE_EXP_GPA_MCB_F0 0    // Feeder 1 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F1 1    // Feeder 2 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F2 2    // Feeder 3 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F3 3    // Feeder 4 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F4 4    // Feeder 5 MCB Aux Contact
#define POLE_EXP_GPA_MCB_F5 5    // Feeder 6 MCB Aux Contact
#define POLE_EXP_GPA_TAMPER 6    // Enclosure Door Tamper Switch
#define POLE_EXP_GPA_SPD_FAULT 7 // Mains SPD Surge Arrestor Fault

// Port B: 6x Contactor Auxiliary Feedback + Phase Monitors
#define POLE_EXP_GPB_AUX_F0 0  // Feeder 1 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F1 1  // Feeder 2 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F2 2  // Feeder 3 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F3 3  // Feeder 4 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F4 4  // Feeder 5 Contactor Closed Feedback
#define POLE_EXP_GPB_AUX_F5 5  // Feeder 6 Contactor Closed Feedback
#define POLE_EXP_GPB_PHASE_R 6 // Phase R Presence Detect
#define POLE_EXP_GPB_PHASE_Y 7 // Phase Y Presence Detect

// Hardware Feature Flag
#define POLE_MCB_AUX_ENABLED 1 // Restored via MCP23S17 daughterboard
#define POLE_CONTACTOR_AUX_ENABLED 1

// Dynamic decoder pin storage (defaults to compiled constants)
#if !defined(POLE_DYNAMIC_DECODER_PINS)
#define POLE_DYNAMIC_DECODER_PINS
extern uint8_t g_poleDecoderEnablePin;
extern uint8_t g_poleDecoderA0Pin;
extern uint8_t g_poleDecoderA1Pin;
#endif

// Helper functions for SPI Decoder Selection (Glitch-Free Sequence)
// Sequence per hardware audit:
// 1. Disable decoder (/1E = HIGH)
// 2. Set address lines A0, A1
// 3. Settle logic propagation delay (t_pd ~18 ns)
// 4. Enable decoder (/1E = LOW)
// 5. [SPI Transfer executed]
// 6. Disable decoder (/1E = HIGH)
static inline void poleSelectSpiDevice(uint8_t dev) {
  uint8_t enPin = (g_poleDecoderEnablePin != 0) ? g_poleDecoderEnablePin : POLE_DECODER_ENABLE_PIN;
  uint8_t a0Pin = (g_poleDecoderA0Pin != 0) ? g_poleDecoderA0Pin : POLE_DECODER_A0_PIN;
  uint8_t a1Pin = (g_poleDecoderA1Pin != 0) ? g_poleDecoderA1Pin : POLE_DECODER_A1_PIN;

  if (dev == POLE_SPI_DEV_DESELECT) {
    digitalWrite(enPin, HIGH);
  } else {
    // Step 1: Ensure decoder is disabled first to prevent transient decode glitches
    digitalWrite(enPin, HIGH);
    // Step 2: Set address lines
    digitalWrite(a0Pin, (dev & 0x01) ? HIGH : LOW);
    digitalWrite(a1Pin, (dev & 0x02) ? HIGH : LOW);
    // Step 3: Wait for stable logic settling
#if defined(ARDUINO)
    delayMicroseconds(1);
#endif
    // Step 4: Enable decoder to activate target peripheral CS
    digitalWrite(enPin, LOW);
  }
}

// HARDWARE TERMINATION & BOOT PULL-UP DIRECTIVES:
// 1. External 10k pull-up resistor required on GPIO 13 (/1E) to 3.3V.
//    Guarantees all CS lines remain inactive (HIGH) during ESP32 boot, reset,
//    and deep sleep.
// 2. 74HC139 Unused Unit 2 Pin Termination:
//    - Pin 15 (/2E) tied to VCC (3.3V) -> Disables Unit 2.
//    - Pin 14 (2A0) tied to GND.
//    - Pin 13 (2A1) tied to GND.
//    - Outputs (/2Y0../2Y3) left unconnected.

// =====================================================================
//  COMPILE-TIME HARDWARE CONFLICT AUDIT
// =====================================================================
#if defined(CONFIG_IDF_TARGET_ESP32) || defined(ARDUINO) || defined(__cplusplus)
#define IS_FLASH_PIN(p) ((p) >= 6 && (p) <= 11)
static_assert(!IS_FLASH_PIN(POLE_SPI_SCK_PIN),
              "POLE_SPI_SCK_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_SPI_MISO_PIN),
              "POLE_SPI_MISO_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_SPI_MOSI_PIN),
              "POLE_SPI_MOSI_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_LORA_NSS_PIN),
              "POLE_LORA_NSS_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_DECODER_ENABLE_PIN),
              "POLE_DECODER_ENABLE_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_DECODER_A0_PIN),
              "POLE_DECODER_A0_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_DECODER_A1_PIN),
              "POLE_DECODER_A1_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_RELAY_F0_PIN),
              "POLE_RELAY_F0_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_RELAY_F1_PIN),
              "POLE_RELAY_F1_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_RELAY_F2_PIN),
              "POLE_RELAY_F2_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_RELAY_F3_PIN),
              "POLE_RELAY_F3_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_RELAY_F4_PIN),
              "POLE_RELAY_F4_PIN in forbidden Flash range 6-11!");
static_assert(!IS_FLASH_PIN(POLE_RELAY_F5_PIN),
              "POLE_RELAY_F5_PIN in forbidden Flash range 6-11!");

// 1. Critical Feeder 1 Pin Verification: Physical Pin 24 must be GPIO 26, NEVER
// GPIO 24!
static_assert(POLE_RELAY_F0_PIN == 26, "Feeder 1 MUST be assigned to GPIO 26!");
static_assert(POLE_RELAY_F0_PHYS_PIN == 24,
              "Feeder 1 physical board header position must be 24!");
static_assert(POLE_RELAY_F0_PIN != 24, "Feeder 1 MUST NOT be assigned to GPIO "
                                       "24! Physical pin 24 is D26 = GPIO 26.");
static_assert(POLE_RELAY_F1_PIN != 24, "Feeder 2 cannot be on GPIO 24!");
static_assert(POLE_RELAY_F2_PIN != 24, "Feeder 3 cannot be on GPIO 24!");
static_assert(POLE_RELAY_F3_PIN != 24, "Feeder 4 cannot be on GPIO 24!");
static_assert(POLE_RELAY_F4_PIN != 24, "Feeder 5 cannot be on GPIO 24!");
static_assert(POLE_RELAY_F5_PIN != 24, "Feeder 6 cannot be on GPIO 24!");

// 2. Relay GPIO Uniqueness Checks (Pairwise Conflict Elimination)
static_assert(POLE_RELAY_F0_PIN != POLE_RELAY_F1_PIN,
              "Relay F0 and F1 pin collision!");
static_assert(POLE_RELAY_F0_PIN != POLE_RELAY_F2_PIN,
              "Relay F0 and F2 pin collision!");
static_assert(POLE_RELAY_F0_PIN != POLE_RELAY_F3_PIN,
              "Relay F0 and F3 pin collision!");
static_assert(POLE_RELAY_F0_PIN != POLE_RELAY_F4_PIN,
              "Relay F0 and F4 pin collision!");
static_assert(POLE_RELAY_F0_PIN != POLE_RELAY_F5_PIN,
              "Relay F0 and F5 pin collision!");
static_assert(POLE_RELAY_F1_PIN != POLE_RELAY_F2_PIN,
              "Relay F1 and F2 pin collision!");
static_assert(POLE_RELAY_F1_PIN != POLE_RELAY_F3_PIN,
              "Relay F1 and F3 pin collision!");
static_assert(POLE_RELAY_F1_PIN != POLE_RELAY_F4_PIN,
              "Relay F1 and F4 pin collision!");
static_assert(POLE_RELAY_F1_PIN != POLE_RELAY_F5_PIN,
              "Relay F1 and F5 pin collision!");
static_assert(POLE_RELAY_F2_PIN != POLE_RELAY_F3_PIN,
              "Relay F2 and F3 pin collision!");
static_assert(POLE_RELAY_F2_PIN != POLE_RELAY_F4_PIN,
              "Relay F2 and F4 pin collision!");
static_assert(POLE_RELAY_F2_PIN != POLE_RELAY_F5_PIN,
              "Relay F2 and F5 pin collision!");
static_assert(POLE_RELAY_F3_PIN != POLE_RELAY_F4_PIN,
              "Relay F3 and F4 pin collision!");
static_assert(POLE_RELAY_F3_PIN != POLE_RELAY_F5_PIN,
              "Relay F3 and F5 pin collision!");
static_assert(POLE_RELAY_F4_PIN != POLE_RELAY_F5_PIN,
              "Relay F4 and F5 pin collision!");

// 3. Strapping Pin Elimination (GPIO 0, 2, 12, 15)
#define IS_STRAPPING_PIN(p) ((p) == 0 || (p) == 2 || (p) == 12 || (p) == 15)
static_assert(!IS_STRAPPING_PIN(POLE_RELAY_F0_PIN),
              "Relay F0 cannot use strapping pin!");
static_assert(!IS_STRAPPING_PIN(POLE_RELAY_F1_PIN),
              "Relay F1 cannot use strapping pin!");
static_assert(!IS_STRAPPING_PIN(POLE_RELAY_F2_PIN),
              "Relay F2 cannot use strapping pin!");
static_assert(!IS_STRAPPING_PIN(POLE_RELAY_F3_PIN),
              "Relay F3 cannot use strapping pin!");
static_assert(!IS_STRAPPING_PIN(POLE_RELAY_F4_PIN),
              "Relay F4 cannot use strapping pin!");
static_assert(!IS_STRAPPING_PIN(POLE_RELAY_F5_PIN),
              "Relay F5 cannot use strapping pin!");

// 4. Input-Only (GPI) Pin Elimination (GPIO 34, 35, 36, 39)
#define IS_GPI_INPUT_ONLY(p) ((p) == 34 || (p) == 35 || (p) == 36 || (p) == 39)
static_assert(!IS_GPI_INPUT_ONLY(POLE_RELAY_F0_PIN),
              "Relay F0 cannot use input-only pin!");
static_assert(!IS_GPI_INPUT_ONLY(POLE_RELAY_F1_PIN),
              "Relay F1 cannot use input-only pin!");
static_assert(!IS_GPI_INPUT_ONLY(POLE_RELAY_F2_PIN),
              "Relay F2 cannot use input-only pin!");
static_assert(!IS_GPI_INPUT_ONLY(POLE_RELAY_F3_PIN),
              "Relay F3 cannot use input-only pin!");
static_assert(!IS_GPI_INPUT_ONLY(POLE_RELAY_F4_PIN),
              "Relay F4 cannot use input-only pin!");
static_assert(!IS_GPI_INPUT_ONLY(POLE_RELAY_F5_PIN),
              "Relay F5 cannot use input-only pin!");

// 5. LoRa Radio Conflict Elimination
#define CONFLICTS_WITH_LORA(p)                                                 \
  ((p) == POLE_LORA_SCK_PIN || (p) == POLE_LORA_MISO_PIN ||                    \
   (p) == POLE_LORA_MOSI_PIN || (p) == POLE_LORA_NSS_PIN ||                    \
   (p) == POLE_LORA_RST_PIN || (p) == POLE_LORA_DIO0_PIN)
static_assert(!CONFLICTS_WITH_LORA(POLE_RELAY_F0_PIN),
              "Relay F0 conflicts with LoRa pin!");
static_assert(!CONFLICTS_WITH_LORA(POLE_RELAY_F1_PIN),
              "Relay F1 conflicts with LoRa pin!");
static_assert(!CONFLICTS_WITH_LORA(POLE_RELAY_F2_PIN),
              "Relay F2 conflicts with LoRa pin!");
static_assert(!CONFLICTS_WITH_LORA(POLE_RELAY_F3_PIN),
              "Relay F3 conflicts with LoRa pin!");
static_assert(!CONFLICTS_WITH_LORA(POLE_RELAY_F4_PIN),
              "Relay F4 conflicts with LoRa pin!");
static_assert(!CONFLICTS_WITH_LORA(POLE_RELAY_F5_PIN),
              "Relay F5 conflicts with LoRa pin!");

// 6. 74HC139 Decoder & Metrology Sync Conflict Elimination
#define CONFLICTS_WITH_DECODER(p)                                              \
  ((p) == POLE_DECODER_ENABLE_PIN || (p) == POLE_DECODER_A0_PIN ||             \
   (p) == POLE_DECODER_A1_PIN || (p) == POLE_ADC_SYNC_PIN)
static_assert(!CONFLICTS_WITH_DECODER(POLE_RELAY_F0_PIN),
              "Relay F0 conflicts with Decoder/ADC pin!");
static_assert(!CONFLICTS_WITH_DECODER(POLE_RELAY_F1_PIN),
              "Relay F1 conflicts with Decoder/ADC pin!");
static_assert(!CONFLICTS_WITH_DECODER(POLE_RELAY_F2_PIN),
              "Relay F2 conflicts with Decoder/ADC pin!");
static_assert(!CONFLICTS_WITH_DECODER(POLE_RELAY_F3_PIN),
              "Relay F3 conflicts with Decoder/ADC pin!");
static_assert(!CONFLICTS_WITH_DECODER(POLE_RELAY_F4_PIN),
              "Relay F4 conflicts with Decoder/ADC pin!");
static_assert(!CONFLICTS_WITH_DECODER(POLE_RELAY_F5_PIN),
              "Relay F5 conflicts with Decoder/ADC pin!");
#endif // COMPILE TIME CHECKS

// =====================================================================
//  HARDWARE PROFILE & TRANSACTIONAL PIN CONFIGURATION SCHEMA
// =====================================================================
#define MSCB_HW_PROFILE_IDENTIFIER "ESP32_WROOM32_DOIT_POLE_R3_2"
#define MSCB_HW_CONFIG_VERSION 0x0302
#define MSCB_PIN_CONFIG_MAGIC 0x50494E43UL // "PINC"
#define MSCB_PIN_SCHEMA_VERSION 2

struct PoleHardwarePinConfig {
  uint32_t magic;           // MSCB_PIN_CONFIG_MAGIC
  uint16_t schemaVersion;   // MSCB_PIN_SCHEMA_VERSION (2)
  uint16_t configVersion;   // Monotonic configuration revision
  char profileId[32];       // Hardware profile tag
  uint8_t feederPins[6];    // 6 Feeder Relay GPIO outputs
  uint8_t loraNssPin;       // Dedicated LoRa NSS
  uint8_t loraRstPin;       // LoRa Reset
  uint8_t loraDio0Pin;      // LoRa DIO0 IRQ
  uint8_t decoderEnablePin; // 74HC139 /1E Enable
  uint8_t decoderA0Pin;     // 74HC139 A0 Address
  uint8_t decoderA1Pin;     // 74HC139 A1 Address
  uint8_t adcSyncPin;       // ADC Sync
  uint8_t adcDrdyPins[3];   // ADS131M08 DRDY1..3 (Input Only)
  uint8_t adcChannelMap[24];// 24 raw ADC channels -> logical channel 1..21 (0 = spare)
  uint32_t crc32;           // CRC32 of fields above
};

inline uint32_t calculatePinConfigCrc(const PoleHardwarePinConfig &cfg) {
  const uint8_t *p = (const uint8_t *)&cfg;
  size_t len = offsetof(PoleHardwarePinConfig, crc32);
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xEDB88320UL;
      else
        crc >>= 1;
    }
  }
  return ~crc;
}

inline PoleHardwarePinConfig getDefaultHardwarePinConfig() {
  static const uint8_t DEFAULT_ADC_MAP[24] = {
    4, 5, 6, 7, 8, 9, 10, 11,
    12, 13, 14, 15, 16, 17, 18, 0,
    19, 20, 21, 1, 2, 3, 0, 0
  };
  PoleHardwarePinConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = MSCB_PIN_CONFIG_MAGIC;
  cfg.schemaVersion = MSCB_PIN_SCHEMA_VERSION;
  cfg.configVersion = MSCB_HW_CONFIG_VERSION;
  strncpy(cfg.profileId, MSCB_HW_PROFILE_IDENTIFIER, sizeof(cfg.profileId) - 1);
  for (uint8_t i = 0; i < 6; i++) {
    cfg.feederPins[i] = POLE_FEEDER_RELAY_PINS[i];
  }
  cfg.loraNssPin = POLE_LORA_NSS_PIN;
  cfg.loraRstPin = POLE_LORA_RST_PIN;
  cfg.loraDio0Pin = POLE_LORA_DIO0_PIN;
  cfg.decoderEnablePin = POLE_DECODER_ENABLE_PIN;
  cfg.decoderA0Pin = POLE_DECODER_A0_PIN;
  cfg.decoderA1Pin = POLE_DECODER_A1_PIN;
  cfg.adcSyncPin = POLE_ADC_SYNC_PIN;
  cfg.adcDrdyPins[0] = POLE_ADC1_DRDY_PIN;
  cfg.adcDrdyPins[1] = POLE_ADC2_DRDY_PIN;
  cfg.adcDrdyPins[2] = POLE_ADC3_DRDY_PIN;
  memcpy(cfg.adcChannelMap, DEFAULT_ADC_MAP, sizeof(DEFAULT_ADC_MAP));
  cfg.crc32 = calculatePinConfigCrc(cfg);
  return cfg;
}

inline bool isValidEsp32WroomGpio(uint8_t p) {
  if (p > 39) return false;
  if (p >= 6 && p <= 11) return false;   // Internal SPI flash
  if (p == 20 || p == 24) return false;  // Non-exposed / flash
  if (p >= 28 && p <= 31) return false;  // Non-existent on ESP32-WROOM-32
  if (p == 37 || p == 38) return false;  // Not pinned out on ESP32-WROOM-32
  return true;
}

inline bool isReservedBoardSystemPin(uint8_t p) {
  // Strapping / boot / UART0 / dedicated VSPI bus / dedicated physical auth GPIO39
  if (p == 0 || p == 2 || p == 12 || p == 15) return true;
  if (p == 1 || p == 3) return true;
  if (p == POLE_SPI_SCK_PIN || p == POLE_SPI_MISO_PIN || p == POLE_SPI_MOSI_PIN) return true;
  if (p == POLE_PHYSICAL_AUTH_PIN) return true;
  return false;
}

inline bool isOutputCapableBoardPin(uint8_t p) {
  if (!isValidEsp32WroomGpio(p) || isReservedBoardSystemPin(p)) return false;
  if (p == 34 || p == 35 || p == 36 || p == 39) return false; // Input-only GPI
  return true;
}

inline bool isInputCapableBoardPin(uint8_t p) {
  return isValidEsp32WroomGpio(p) && !isReservedBoardSystemPin(p);
}

inline bool validateHardwarePinConfig(const PoleHardwarePinConfig &cfg) {
  if (cfg.magic != MSCB_PIN_CONFIG_MAGIC)
    return false;
  if (cfg.schemaVersion != MSCB_PIN_SCHEMA_VERSION)
    return false;
  if (cfg.configVersion == 0)
    return false;
  if (memchr(cfg.profileId, '\0', sizeof(cfg.profileId)) == nullptr || cfg.profileId[0] == '\0')
    return false;
  if (calculatePinConfigCrc(cfg) != cfg.crc32)
    return false;

  // 1. Validate all output-capable peripheral pins
  for (uint8_t i = 0; i < 6; i++) {
    if (!isOutputCapableBoardPin(cfg.feederPins[i]))
      return false;
  }
  if (!isOutputCapableBoardPin(cfg.loraNssPin) ||
      !isOutputCapableBoardPin(cfg.loraRstPin) ||
      !isOutputCapableBoardPin(cfg.decoderEnablePin) ||
      !isOutputCapableBoardPin(cfg.decoderA0Pin) ||
      !isOutputCapableBoardPin(cfg.decoderA1Pin) ||
      !isOutputCapableBoardPin(cfg.adcSyncPin)) {
    return false;
  }

  // 2. Validate input-capable peripheral pins
  if (!isInputCapableBoardPin(cfg.loraDio0Pin))
    return false;
  for (uint8_t i = 0; i < 3; i++) {
    if (!isInputCapableBoardPin(cfg.adcDrdyPins[i]))
      return false;
  }

  // 3. Enforce strict global GPIO uniqueness across all 16 dynamic pins
  uint8_t allPins[16] = {
    cfg.feederPins[0], cfg.feederPins[1], cfg.feederPins[2],
    cfg.feederPins[3], cfg.feederPins[4], cfg.feederPins[5],
    cfg.loraNssPin, cfg.loraRstPin, cfg.loraDio0Pin,
    cfg.decoderEnablePin, cfg.decoderA0Pin, cfg.decoderA1Pin,
    cfg.adcSyncPin, cfg.adcDrdyPins[0], cfg.adcDrdyPins[1], cfg.adcDrdyPins[2]
  };
  for (uint8_t i = 0; i < 16; i++) {
    for (uint8_t j = i + 1; j < 16; j++) {
      if (allPins[i] == allPins[j])
        return false;
    }
  }

  // 4. Validate 21-channel ADC logical mapping + 3 spares across 24 raw slots
  uint8_t seenCount[22];
  memset(seenCount, 0, sizeof(seenCount));
  for (uint8_t r = 0; r < 24; r++) {
    uint8_t logicalCh = cfg.adcChannelMap[r];
    if (logicalCh > 21)
      return false;
    seenCount[logicalCh]++;
  }
  if (seenCount[0] != 3)
    return false;
  for (uint8_t ch = 1; ch <= 21; ch++) {
    if (seenCount[ch] != 1)
      return false;
  }

  return true;
}

#endif // POLE_BOARD_PINS_H
