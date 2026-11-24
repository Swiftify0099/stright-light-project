/**
 * @file BuildConfig.h
 * @brief MSCB Smart Pole & Gateway System - Global Build & Architecture Configuration
 * @version 1.0.0
 * 
 * SOLID Embedded Architecture: Central system limits, firmware version definitions,
 * and target environment validation rules.
 */

#ifndef MSCB_BUILD_CONFIG_H
#define MSCB_BUILD_CONFIG_H

#include <Arduino.h>

// =====================================================================
//  FIRMWARE RELEASE METADATA
// =====================================================================
#define MSCB_FW_VERSION_MAJOR       1
#define MSCB_FW_VERSION_MINOR       0
#define MSCB_FW_VERSION_PATCH       0
#define MSCB_FW_BUILD_DATE          "2026-11-22"
#define MSCB_SYSTEM_NAME            "MSCB Smart Pole & Feeder Automation"
#define MSCB_SYSTEM_CODENAME        "System_R1"

// =====================================================================
//  NETWORK SCALE & TOPOLOGY SPECIFICATIONS
//  (Derived from confirmed engineering requirements)
// =====================================================================
#define MSCB_MAX_POLE_NODES               50    // Up to 50 Field Pole Nodes in mesh
#define MSCB_FEEDERS_PER_POLE             6     // 6 Independent Feeder channels per pole
#define MSCB_PHASES_PER_FEEDER            3     // 3 Phases: Red (R), Yellow (Y), Blue (B)
#define MSCB_CURRENT_CHANNELS_PER_POLE    18    // 6 Feeders * 3 Phases = 18 Current Channels
#define MSCB_VOLTAGE_CHANNELS_PER_POLE    3     // Common R, Y, B Mains Bus Voltage Channels
#define MSCB_TOTAL_ANALOG_CHANNELS_POLE   21    // 18 Current + 3 Voltage channels

#define MSCB_GATEWAY_NODE_ID              0x0001
#define MSCB_BROADCAST_NODE_ID            0xFFFF
#define MSCB_DEFAULT_MAX_HOPS             6     // Max mesh hop distance

// =====================================================================
//  RF / LORA WIRELESS CONFIGURATION (SX1278 RA-02 @ 433 MHz)
// =====================================================================
#define MSCB_LORA_FREQ_HZ                 433920000UL // 433.920 MHz Center Frequency (WPC G.S.R. 347(E))
#define MSCB_LORA_BANDWIDTH_HZ            125000UL    // 125 kHz Bandwidth
#define MSCB_LORA_SPREADING_FACTOR        10          // SF10 for robust long-range link
#define MSCB_LORA_CODING_RATE             5           // CR 4/5
#define MSCB_LORA_SYNC_WORD               0x12        // Private mesh synchronization word
#define MSCB_LORA_TX_POWER_DBM            8           // +8 dBm conducted (resolves BUG-R1-07: 8 dBm + 3 dBi antenna - 0.5 dB cable loss = 8.35 dBm ERP <= 10.0 mW WPC ceiling)
#define MSCB_LORA_PREAMBLE_LEN            8           // Preamble symbol length

// =====================================================================
//  TIMING & CADENCE INTERVALS (Milliseconds)
// =====================================================================
#define MSCB_SERIAL_BAUD_RATE             115200UL
#define MSCB_DIAGNOSTIC_BANNER_DELAY_MS   1500UL
#define MSCB_STATUS_BLINK_INTERVAL_MS     1000UL
#define MSCB_BEACON_INTERVAL_MS           30000UL
#define MSCB_TELEMETRY_INTERVAL_MS        15000UL
#define MSCB_HEARTBEAT_TIMEOUT_MS         60000UL

// =====================================================================
//  SAFETY LOCKS & HARDWARE EXECUTION ENFORCEMENT
// =====================================================================
#define MSCB_SAFE_BOOT_MODE               1     // 1 = Safe startup mode enabled
#define MSCB_RELAYS_LOCKED_DISABLED       1     // 1 = Production relay drive strictly prohibited by default (isolated DC bench test only when unlocked)
#ifndef MSCB_COMMISSIONING_LVDC_PROFILE
#define MSCB_COMMISSIONING_LVDC_PROFILE   0     // 0 = Production locked; 1 = Isolated low-voltage DC commissioning profile compiled
#endif
#ifndef MSCB_BENCH_TEST_PROFILE_ENABLED
#define MSCB_BENCH_TEST_PROFILE_ENABLED   MSCB_COMMISSIONING_LVDC_PROFILE
#endif
#define MSCB_SIMULATION_TELEMETRY_ONLY    0     // 0 = Real physical ADC sampling enforced; zero synthetic data allowed!

// =====================================================================
//  BUILD COMPILER VALIDATION CHECKS
// =====================================================================
#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #define MSCB_TARGET_IS_ESP32S3          1
  #define MSCB_TARGET_IS_CLASSIC_ESP32    0
  #define MSCB_CURRENT_TARGET_NAME        "ESP32-S3 (Main Gateway)"
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #define MSCB_TARGET_IS_ESP32S3          0
  #define MSCB_TARGET_IS_CLASSIC_ESP32    1
  #define MSCB_CURRENT_TARGET_NAME        "ESP32-WROOM-32 (Field Pole)"
#else
  #warning "Unrecognized ESP32 target variant! Ensure an ESP32 board is selected in Arduino IDE."
  #define MSCB_TARGET_IS_ESP32S3          0
  #define MSCB_TARGET_IS_CLASSIC_ESP32    0
  #define MSCB_CURRENT_TARGET_NAME        "Generic / Unknown Target"
#endif

#endif // MSCB_BUILD_CONFIG_H
