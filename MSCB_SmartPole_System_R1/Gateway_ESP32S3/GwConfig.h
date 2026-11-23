/**
 * @file GwConfig.h
 * @brief MSCB Smart Pole Gateway - Main Configuration Header
 * @version 1.0.0
 * 
 * Provides configuration parameters, credential placeholders, network parameters,
 * and includes shared protocol headers with automated fallback resolution.
 */

#ifndef GW_CONFIG_H
#define GW_CONFIG_H

#include <Arduino.h>
#include "GwBoardPins.h"

// =====================================================================
//  DISCOVER & INCLUDE SHARED PROTOCOL DEFINITIONS
// =====================================================================
#if __has_include("../MSCBShared/BuildConfig.h")
  #include "../MSCBShared/BuildConfig.h"
  #include "../MSCBShared/NetworkTypes.h"
  #include "../MSCBShared/Protocol.h"
  #include "../MSCBShared/StorageTypes.h"
  #include "../MSCBShared/CommandSecurity.h"
  #include "../MSCBShared/ReplayProtection.h"
#elif __has_include(<BuildConfig.h>)
  #include <BuildConfig.h>
  #include <NetworkTypes.h>
  #include <Protocol.h>
  #include <StorageTypes.h>
  #include <CommandSecurity.h>
  #include <ReplayProtection.h>
#else
  #error "MSCBShared headers not found! Ensure MSCBShared directory is adjacent to sketch or installed in Arduino/libraries."
#endif

// Compile-time target confirmation
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
  #warning "GwConfig.h is designed for ESP32-S3. Please select 'ESP32S3 Dev Module' in Arduino IDE."
#endif

// =====================================================================
//  GATEWAY IDENTIFIER & VERSIONING
// =====================================================================
#define GW_FIRMWARE_NAME              "MSCB_Gateway_ESP32S3"
#define GW_NODE_ADDRESS               MSCB_GATEWAY_NODE_ID // 0x0001
#define GW_STRING_ID                  "GATEWAY_01"

// =====================================================================
//  NETWORK DATABASE CAPACITIES (ESP32-S3 8MB PSRAM Optimized)
// =====================================================================
#define GW_MAX_MANAGED_POLES          MSCB_MAX_POLE_NODES  // Up to 50 Pole nodes
#define GW_ROUTING_TABLE_ENTRIES      64
#define GW_PACKET_QUEUE_DEPTH         32
#define GW_EVENT_LOG_CAPACITY         100

// =====================================================================
//  SECURITY & CREDENTIAL PLACEHOLDERS
//  (STRICT EMBEDDED POLICY: NEVER COMMIT RAW PASSWORDS OR TOKENS)
// =====================================================================
#if __has_include("GwSecrets.h")
  #include "GwSecrets.h"
#endif

#ifndef MSCB_WIFI_SSID
  #define MSCB_WIFI_SSID              "PS"
#endif

#ifndef MSCB_WIFI_PASS
  #define MSCB_WIFI_PASS              "12345678"
#endif

#ifndef MSCB_TELEGRAM_BOT_TOKEN
  #define MSCB_TELEGRAM_BOT_TOKEN     "9149585073:AAH86ZZCfoB069FBRH0WEwtCoCWB6sjG59o"
#endif

#ifndef MSCB_TELEGRAM_CHAT_ID
  #define MSCB_TELEGRAM_CHAT_ID       "5505296197"
#endif

#ifndef MSCB_TELEGRAM_ADMIN_ALLOWLIST
  #define MSCB_TELEGRAM_ADMIN_ALLOWLIST { 5505296197LL }
  #define MSCB_TELEGRAM_ADMIN_COUNT     1
#endif

// =====================================================================
//  TIMING & WATCHDOG CONFIGURATION
// =====================================================================
#define GW_WIFI_RECONNECT_INTERVAL_MS 15000UL
#define GW_TELEGRAM_POLL_INTERVAL_MS  500UL    // 500ms fast-polling for ultra-responsive bot
#define GW_POLE_OFFLINE_TIMEOUT_MS    120000UL // 2 minutes without packet = pole offline
#define GW_WATCHDOG_TIMEOUT_SEC       30

#endif // GW_CONFIG_H
