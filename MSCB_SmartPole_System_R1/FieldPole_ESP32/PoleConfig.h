/**
 * @file PoleConfig.h
 * @brief MSCB Smart Pole Node - Main Configuration Header
 * @version 1.0.0
 * 
 * Target Board: Classic ESP32 ESP-WROOM-32
 * Controls 6 Feeders, 18 Current Channels, 3 Voltage Channels (21 Total)
 */

#ifndef POLE_CONFIG_H
#define POLE_CONFIG_H

#include <Arduino.h>
#include "PoleBoardPins.h"

// =====================================================================
//  DISCOVER & INCLUDE SHARED PROTOCOL DEFINITIONS
// =====================================================================
#if __has_include("../MSCBShared/BuildConfig.h")
  #include "../MSCBShared/BuildConfig.h"
  #include "../MSCBShared/NetworkTypes.h"
  #include "../MSCBShared/Protocol.h"
#elif __has_include(<BuildConfig.h>)
  #include <BuildConfig.h>
  #include <NetworkTypes.h>
  #include <Protocol.h>
#else
  #error "MSCBShared headers not found! Ensure MSCBShared directory is adjacent to sketch or installed in Arduino/libraries."
#endif

// Compile-time target confirmation
#if !defined(CONFIG_IDF_TARGET_ESP32)
  #warning "PoleConfig.h is designed for classic ESP32. Please select 'ESP32 Dev Module' in Arduino IDE."
#endif

// =====================================================================
//  NODE IDENTIFIER & ADDRESSING
// =====================================================================
#define POLE_FIRMWARE_NAME            "MSCB_FieldPole_ESP32"
#define POLE_DEFAULT_NODE_ADDRESS     0x0002 // Default address for Pole #2 (modifiable via NVS)
#define POLE_DEFAULT_STRING_ID        "POLE_02"

// =====================================================================
//  RELAY DRIVE SAFETY SPECIFICATIONS
// =====================================================================
// Default relay module logic: Active-LOW (true) or Active-HIGH (false)
#define POLE_RELAY_ACTIVE_LOW         true
#define RELAY_ACTIVE_LOW              POLE_RELAY_ACTIVE_LOW

#if POLE_RELAY_ACTIVE_LOW
  #define POLE_RELAY_OFF              HIGH // Inactive (contacts open)
  #define POLE_RELAY_ON               LOW  // Active (contacts closed)
#else
  #define POLE_RELAY_OFF              LOW  // Inactive
  #define POLE_RELAY_ON               HIGH // Active
#endif

// =====================================================================
//  ANALOG MEASUREMENT & ADC SPECIFICATIONS
// =====================================================================
#define POLE_NUM_FEEDERS              MSCB_FEEDERS_PER_POLE           // 6
#define POLE_NUM_CURRENT_CHANNELS     MSCB_CURRENT_CHANNELS_PER_POLE  // 18
#define POLE_NUM_VOLTAGE_CHANNELS     MSCB_VOLTAGE_CHANNELS_PER_POLE  // 3
#define POLE_TOTAL_MEASUREMENT_CH     MSCB_TOTAL_ANALOG_CHANNELS_POLE // 21

// Nominal Mains Parameters (India Grid)
#define POLE_NOMINAL_MAINS_VOLTAGE_V  230.0f
#define POLE_NOMINAL_MAINS_FREQ_HZ    50.0f
#define POLE_MIN_VALID_VOLTAGE_V      80.0f
#define POLE_MAX_VALID_VOLTAGE_V      290.0f
#define POLE_OVERCURRENT_TRIP_A       32.0f

#endif // POLE_CONFIG_H
