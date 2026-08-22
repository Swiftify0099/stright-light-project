/*
 * =====================================================================
 *  CONFIG.H — MASTER AUTHORITATIVE CONFIGURATION FILE (V10)
 *  Smart Street Light / Farmer Power Distribution Mesh System
 *  Platform : ESP32 + SX1278 RA-02 433 MHz + Mobile Hotspot + Telegram
 *
 *  SOLID ARCHITECTURE: Single source of truth for both Gateway & Pole.
 * =====================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────
//  FIRMWARE VERSIONS
// ─────────────────────────────────────────────────────────────
#define POLE_VERSION     "POLE_V10"
#define GW_VERSION       "GW_V10"
#define FW_VERSION_INT   10

// ─────────────────────────────────────────────────────────────
//  TELEGRAM CREDENTIALS (MAIN GATEWAY ONLY)
// ─────────────────────────────────────────────────────────────
#define BOT_TOKEN        "8823335578:AAHKZag8e0p2gspUtBhgnDk96qZiDRc7dr8"
#define OWNER_CHAT       "5505296197"
#define TELEGRAM_MAX_MESSAGE_SIZE 8192

// ─────────────────────────────────────────────────────────────
//  WiFi CREDENTIALS — MOBILE HOTSPOT / ACCESS POINT
// ─────────────────────────────────────────────────────────────
#define WIFI_DEF_SSID    "Saaj"
#define WIFI_DEF_PASS    "Saaj_7550"

// ─────────────────────────────────────────────────────────────
//  LORA HARDWARE PIN ASSIGNMENTS (ESP32 VSPI)
//  Both Gateway and Pole use standard VSPI wiring.
// ─────────────────────────────────────────────────────────────
#define LORA_SS          5   // VSPI Chip Select (CS)
#define LORA_RST         14  // Hardware Reset
#define LORA_DIO0        2   // Packet Ready Interrupt (DIO0)
#define LORA_SCK         18  // VSPI Clock
#define LORA_MISO        19  // VSPI MISO
#define LORA_MOSI        23  // VSPI MOSI

// ─────────────────────────────────────────────────────────────
//  LORA RF CONFIGURATION (SX1278 @ 433 MHz)
//  MUST MATCH IDENTICALLY ON GATEWAY AND ALL FIELD POLES
// ─────────────────────────────────────────────────────────────
#define LORA_FREQ        433E6    // 433.0 MHz Carrier Frequency
#define LORA_SF          10       // Spreading Factor 10 (High range, robust link)
#define LORA_BW          125E3    // 125.0 kHz Signal Bandwidth
#define LORA_CR          5        // Coding Rate 4/5 (CR=5 means 4/5)
#define LORA_TXPOW       20       // TX Power 20 dBm (PA_BOOST)
#define LORA_SYNC_WORD   0x12     // Private Mesh Sync Word
#define LORA_CRC_ENABLED true     // Hardware CRC payload protection

// ─────────────────────────────────────────────────────────────
//  MESH NETWORK IDENTIFIERS & LIMITS
// ─────────────────────────────────────────────────────────────
#define GATEWAY_ID       "GATEWAY"
#define BROADCAST_ID     "BROADCAST"
#define MAX_NODES        20       // Max registered nodes in Gateway database (Optimized for ESP32 DRAM)
#define MAX_ROUTES       20       // Max routing table entries in Gateway
#define MAX_GW_LINKS     30       // Max gateway topology link database
#define MAX_PENDING_REG  4        // Max simultaneous pending registrations
#define MAX_NEIGHBORS    8        // Max neighbor entries per Field Pole
#define MAX_ROUTES_POLE  15       // Max routing table entries per Field Pole
#define MAX_SEQ_CACHE    32       // Deduplication cache size
#define DEFAULT_TTL      6        // Maximum mesh hop limit

// ─────────────────────────────────────────────────────────────
//  TIMING INTERVALS (milliseconds)
// ─────────────────────────────────────────────────────────────
#define TG_POLL_INTERVAL           1000UL   // Telegram polling cycle (1.0s)
#define HEARTBEAT_INTERVAL_MS      30000UL  // Gateway heartbeat poll interval (30s)
#define BEACON_INTERVAL_MS         30000UL  // Local pole discovery beacon interval (30s)
#define SENSOR_INTERVAL_MS         5000UL   // Pole sensor reading update interval (5s)
#define NVS_SAVE_INTERVAL_MS       60000UL  // Background NVS persistence flush (60s)
#define NEW_NODE_RETRY_INTERVAL_MS 10000UL  // Registration retry interval on boot (10s)
#define NEW_NODE_MAX_RETRIES       10       // Max registration attempts
#define NODE_OFFLINE_TIMEOUT_MS    180000UL // 3 min without packet before offline
#define NEIGHBOR_TIMEOUT_MS        120000UL // 2 min neighbor expiry
#define ROUTE_TIMEOUT_MS           300000UL // 5 min route expiry
#define NEIGH_REPORT_INTERVAL_MS   60000UL  // Pole sends neighbour report every 60s
#define POLES_PER_PAGE             4        // Telegram pagination limit
#define HEALTH_CHECK_TIMEOUT_MS    4000UL   // Local neighbor health response window (4s)
#define FEEDER_DEBOUNCE_MS         50UL     // MCB/Contactor sampling interval (50ms)
#define FEEDER_DEBOUNCE_SAMPLES    5        // Consecutive stable samples (250ms)
#define NTP_SYNC_INTERVAL_MS       3600000UL// NTP re-sync every 1 hour
#define TIME_PUSH_INTERVAL_MS      600000UL // Time sync broadcast every 10 min
#define WIFI_WATCHDOG_INTERVAL_MS  15000UL  // WiFi reconnect check interval (15s)

// ─────────────────────────────────────────────────────────────
//  ACK & RETRY TIMING
// ─────────────────────────────────────────────────────────────
#define ACK_TIMEOUT_MS   5000UL   // 5.0s command ACK timeout
#define MAX_CMD_RETRIES  3        // Retries before declaring command failure

// ─────────────────────────────────────────────────────────────
//  HARDWARE RELAY POLARITY & DEFINITIONS (FIELD POLE)
// ─────────────────────────────────────────────────────────────
#define RELAY_ACTIVE_LOW false    // Set true for Active-LOW relay module, false for Active-HIGH

#if RELAY_ACTIVE_LOW
  #define RELAY_STATE_ON   0      // LOW energizes coil
  #define RELAY_STATE_OFF  1      // HIGH releases coil
#else
  #define RELAY_STATE_ON   1      // HIGH energizes coil (Active HIGH)
  #define RELAY_STATE_OFF  0      // LOW releases coil
#endif

// Main Relay / Feeder 0 Output GPIO
#define RELAY_PIN        26       // Main Street Light / Pole Relay (GPIO 26)

// Analog Sensor Inputs (All on ADC1 — 100% WiFi / Radio Safe)
#define CURRENT_PIN      39       // ACS712 analog input (ADC1_CH3 / GPIO39)
#define VOLTAGE_PIN      34       // Legacy alias -> R Phase
#define VOLTAGE_R_PIN    34       // R Phase ZMPT101B (ADC1_CH6 / GPIO34)
#define VOLTAGE_Y_PIN    35       // Y Phase ZMPT101B (ADC1_CH7 / GPIO35)
#define VOLTAGE_B_PIN    32       // B Phase ZMPT101B (ADC1_CH4 / GPIO32)

// ─────────────────────────────────────────────────────────────
//  MULTI-FEEDER THREE-PHASE SYSTEM CONFIGURATION
// ─────────────────────────────────────────────────────────────
#define MAX_FEEDERS 6

// Feeder Relay Output GPIOs
static const uint8_t FEEDER_RELAY_PINS[MAX_FEEDERS] = {
    26, // Feeder 0 (Main Street Light / Pump Relay)
    25, // Feeder 1 Relay
    27, // Feeder 2 Relay
    13, // Feeder 3 Relay
    12, // Feeder 4 Relay (MTDI strapping pin — initialized LOW)
    15  // Feeder 5 Relay
};

// MCB Status Input GPIOs — R Phase (HIGH = Normal, LOW = Tripped, 0 = Disabled)
static const uint8_t FEEDER_MCB_R_PINS[MAX_FEEDERS] = {
    0,  // Feeder 0 MCB R (0 = disabled)
    4,  // Feeder 1 MCB R (Internal PULLUP)
    16, // Feeder 2 MCB R (Internal PULLUP)
    17, // Feeder 3 MCB R (Internal PULLUP)
    21, // Feeder 4 MCB R (Internal PULLUP)
    22  // Feeder 5 MCB R (Internal PULLUP)
};

// MCB Status Input GPIOs — Y Phase (HIGH = Normal, LOW = Tripped, 0 = Disabled)
static const uint8_t FEEDER_MCB_Y_PINS[MAX_FEEDERS] = {
    33, // Feeder 0 MCB Y (Internal PULLUP)
    0, 0, 0, 0, 0
};

// MCB Status Input GPIOs — B Phase (HIGH = Normal, LOW = Tripped, 0 = Disabled)
static const uint8_t FEEDER_MCB_B_PINS[MAX_FEEDERS] = {
    0, 0, 0, 0, 0, 0
};

// Contactor Status Input GPIOs (0 = Disabled / Not wired)
static const uint8_t FEEDER_CONT_R_PINS[MAX_FEEDERS] = {0, 0, 0, 0, 0, 0};
static const uint8_t FEEDER_CONT_Y_PINS[MAX_FEEDERS] = {0, 0, 0, 0, 0, 0};
static const uint8_t FEEDER_CONT_B_PINS[MAX_FEEDERS] = {0, 0, 0, 0, 0, 0};

// Default Feeder Names
static const char* const FEEDER_DEFAULT_NAMES[MAX_FEEDERS] = {
    "Feeder 1", "Feeder 2", "Feeder 3", "Feeder 4", "Feeder 5", "Feeder 6"
};

// ─────────────────────────────────────────────────────────────
//  SENSOR CALIBRATION & VOLTAGE THRESHOLDS
// ─────────────────────────────────────────────────────────────
#define ACS712_SENSITIVITY          0.100f  // 100 mV/A for ACS712-20A module
#define ACS712_ZERO_ADC             2048    // Midpoint ADC value for zero AC current
#define ZMPT101B_CALIBRATION_FACTOR 234.0f  // Mains RMS calibration multiplier for ZMPT101B
#define VMPT_CALIBRATION            ZMPT101B_CALIBRATION_FACTOR // Backward-compatibility alias
#define ADC_MAX                     4095.0f
#define ADC_VREF                    3.3f

// Voltage sensor validity boundaries
#define MIN_VALID_MAINS_VOLTAGE     80.0f   // Minimum valid AC mains voltage (VAC)
#define MAX_VALID_MAINS_VOLTAGE     280.0f  // Maximum valid AC mains voltage (VAC)
#define ZMPT101B_MIN_P2P_ADC        25      // Minimum ADC peak-to-peak amplitude for AC signal detection
#define DEBUG_ZMPT101B              true    // Enable Serial diagnostic banner for ZMPT101B

// ─────────────────────────────────────────────────────────────
//  NVS NAMESPACES & LOG LIMITS
// ─────────────────────────────────────────────────────────────
#define NVS_NS_POLE      "pole"     // Field pole state
#define NVS_NS_FEEDER    "fdr"      // Feeder states
#define NVS_NS_CMDLOG    "cmdlog"   // Circular command audit log
#define NVS_NS_MESH      "meshdb"   // Gateway node database
#define NVS_NS_WIFI      "wifi"     // Gateway WiFi credentials
#define NVS_NS_GWLOG     "gwlog"    // Gateway audit log
#define NVS_NS_EVENTS    "events"   // Offline event buffer
#define NVS_NS_NBRS      "nbrs"     // Pole neighbors

#define MAX_CMD_LOG_ENTRIES   20
#define MAX_EVENT_LOG_ENTRIES 10

// ─────────────────────────────────────────────────────────────
//  PACKET PRIORITY LEVELS
// ─────────────────────────────────────────────────────────────
#define PRIO_LOW    0    // Periodic telemetry, beacons
#define PRIO_MEDIUM 1    // User control commands, heartbeats, status queries
#define PRIO_HIGH   2    // Safety/fault alerts, emergency commands, ACKs
typedef uint8_t PacketPriority;

// ─────────────────────────────────────────────────────────────
//  PARSED PACKET DATA STRUCTURE (COMMON TO GATEWAY & POLE)
// ─────────────────────────────────────────────────────────────
struct ParsedPacket {
  String   type;
  String   src;
  String   dst;
  String   nextHop;
  int      ttl;
  uint16_t seq;
  String   data;
  int      rssi;
  int      snr;
  bool     valid;
};

// ─────────────────────────────────────────────────────────────
//  OTA FIRMWARE CONFIGURATION
// ─────────────────────────────────────────────────────────────
#define OTA_CHUNK_SIZE        100
#define OTA_TX_INTERVAL_MS    3000UL
#define OTA_IDLE_TIMEOUT_MS   20000UL
#define GW_OTA_MAX_SIZE       (4 * 1024 * 1024)

#endif // CONFIG_H
