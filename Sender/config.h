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
//  STRICT EMBEDDED SECURITY POLICY: Production credentials must NEVER
//  be committed in plaintext source code. Defaults are placeholders only.
//  In production, credentials are loaded dynamically from encrypted NVS.
// ─────────────────────────────────────────────────────────────
#ifndef BOT_TOKEN
  #define BOT_TOKEN        "" // Provisioned via Secure NVS / Commissioning
#endif

#ifndef OWNER_CHAT
  #define OWNER_CHAT       "" // Provisioned via Secure NVS / Commissioning
#endif

#define TELEGRAM_MAX_MESSAGE_SIZE 8192

// ─────────────────────────────────────────────────────────────
//  WiFi CREDENTIALS — MOBILE HOTSPOT / ACCESS POINT
//  Loaded from encrypted NVS "wifi" namespace at startup.
// ─────────────────────────────────────────────────────────────
#ifndef WIFI_DEF_SSID
  #define WIFI_DEF_SSID    "MSCB_GATEWAY_NET"
#endif

#ifndef WIFI_DEF_PASS
  #define WIFI_DEF_PASS    ""
#endif

// ─────────────────────────────────────────────────────────────
//  SECURE WI-FI CONFIGURATION & RECOVERY PARAMETERS (PROMPT 17)
// ─────────────────────────────────────────────────────────────
#define WIFI_SESSION_TIMEOUT_MS    120000UL // 2 min timeout for incomplete Telegram Wi-Fi sessions
#define WIFI_CONNECT_TIMEOUT_MS    20000UL  // 20s Wi-Fi association timeout
#define WIFI_DHCP_TIMEOUT_MS       6000UL   // 6s DHCP lease timeout
#define WIFI_DNS_TIMEOUT_MS        5000UL   // 5s DNS resolution timeout
#define WIFI_TG_TIMEOUT_MS         10000UL  // 10s Telegram API HTTPS handshake timeout
#define WIFI_RECOVERY_AP_SSID      "MSCB-GW-RECOVERY"
#ifndef WIFI_RECOVERY_AP_PASS
  #define WIFI_RECOVERY_AP_PASS    ""       // Must be provisioned; never commit production AP pass
#endif
#define WIFI_MAX_FAIL_BEFORE_AP    3        // Consecutive failures before activating recovery AP
#define NVS_WIFI_STABLE            0        // Fully verified and committed
#define NVS_WIFI_TESTING           1        // Under verification (rollback if power cut)

// ─────────────────────────────────────────────────────────────
//  TELEGRAM RATE LIMITING & SECURITY LIMITS (PROMPT 21)
// ─────────────────────────────────────────────────────────────
#define TG_RATE_LIMIT_MAX_CMDS     5        // Maximum 5 commands per time window
#define TG_RATE_LIMIT_WINDOW_MS    10000UL  // 10-second sliding rate limit window
#define TG_BUTTON_TTL_MS           60000UL  // 60-second token/button expiry for sensitive operations
#define TG_SESSION_TIMEOUT_MS      900000UL // 15-minute inactivity session expiry

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
//  INDIAN WPC G.S.R. 347(E) (2022) REGULATORY COMPLIANT:
//  - Frequency band: 433.050 MHz to 434.790 MHz
//  - Legal carrier frequency: 433.920 MHz (Center of license-exempt ISM window)
//  - Max ERP: 10 mW (+10.0 dBm) -> Max conducted power +10 dBm into 2.15 dBi whip
// ─────────────────────────────────────────────────────────────
#define LORA_FREQ        433920000UL // 433.920 MHz Carrier Frequency (WPC Compliant)
#define LORA_SF          10          // Spreading Factor 10 (High range, robust link)
#define LORA_BW          125E3       // 125.0 kHz Signal Bandwidth
#define LORA_CR          5           // Coding Rate 4/5 (CR=5 means 4/5)
#define LORA_TXPOW       10          // TX Power 10 dBm conducted (+10 mW ERP WPC limit)
#define LORA_SYNC_WORD   0x12        // Private Mesh Sync Word
#define LORA_CRC_ENABLED true        // Hardware CRC payload protection

// ─────────────────────────────────────────────────────────────
//  MESH NETWORK IDENTIFIERS & LIMITS
// ─────────────────────────────────────────────────────────────
#define GATEWAY_ID       "GATEWAY"
#define BROADCAST_ID     "BROADCAST"
#define MAX_NODES        50       // Max registered nodes in Gateway database (Supports up to 50 nodes)
#define MAX_ROUTES       50       // Max routing table entries in Gateway
#define MAX_GW_LINKS     60       // Max gateway topology link database
#define MAX_PENDING_REG  4        // Max simultaneous pending registrations
#define MAX_NEIGHBORS    12       // Max neighbor entries per Field Pole
#define MAX_ROUTES_POLE  30       // Max routing table entries per Field Pole
#define MAX_SEQ_CACHE    32       // Deduplication cache size
#define DEFAULT_TTL      6        // Maximum mesh hop limit

// ─────────────────────────────────────────────────────────────
//  ROUTING PROTOCOL & DAG PARAMETERS (PROMPT 10)
// ─────────────────────────────────────────────────────────────
#define ROOT_RANK                  0        // Gateway is Root (Rank 0)
#define INFINITY_RANK              255      // Orphan / unreachable rank
#define MAX_PARENT_FAILURES        3        // Consecutive failed transmissions before switching parent
#define PARENT_TIMEOUT_MS          90000UL  // 90s without hearing parent beacon
#define ROUTE_HYSTERESIS_MARGIN    20       // Cost difference needed to switch primary parent
#define ROUTE_STABILITY_TIME_MS    30000UL  // 30s stability requirement before switching back
#define ORPHAN_RREQ_BASE_INTERVAL  3000UL   // Initial RREQ backoff (3s)
#define ORPHAN_RREQ_MAX_INTERVAL   30000UL  // Maximum RREQ backoff (30s)
#define ORPHAN_MAX_ATTEMPTS        5        // Max RREQ attempts before backing off
#define MESH_NET_ID                "MSCB"   // Private Mesh Network Identifier

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
#define FEEDERS_PER_PAGE           6        // Feeders per page view
#define CURRENTS_PER_PAGE          18       // Current channels per page view
#define EVENTS_PER_PAGE            5        // Events per page view

// ─────────────────────────────────────────────────────────────
//  PROMPT 18 — INTELLIGENT ALERTS & CENTRAL MONITORING CONFIG
// ─────────────────────────────────────────────────────────────
#define ALERT_QUEUE_CAPACITY          32       // Bounded notification queue entries
#define EVENT_HISTORY_CAPACITY        64       // Circular event audit log entries
#define TG_ALERT_RATE_LIMIT_MS        1500UL   // Min interval between Telegram alert dispatches (1.5s)
#define TG_ALERT_MAX_RETRIES          3        // Max Telegram send retries for P0/P1 alerts
#define ALERT_COOLDOWN_MS             60000UL  // 60s duplicate alert suppression window
#define AGGREGATION_WINDOW_MS         10000UL  // 10s window to detect multi-node outage
#define AGGREGATION_MIN_NODES         3        // >=3 simultaneous failures triggers aggregated alert

// Electrical Protection & Fault Confirmation Thresholds
#define CONF_OVERCURRENT_THRESH_A     25.0f    // Default overcurrent trip rating (A)
#define CONF_OVERCURRENT_DUR_MS       3000UL   // 3s confirmation duration before generating incident
#define CONF_OVERCURRENT_HYS_A        1.0f     // 1.0A hysteresis margin to clear fault
#define CONF_OVERVOLT_THRESH_V        260.0f   // Indian grid overvoltage limit (VAC)
#define CONF_OVERVOLT_DUR_MS          2000UL   // 2s confirmation duration
#define CONF_OVERVOLT_HYS_V           5.0f     // 5V hysteresis margin
#define CONF_UNDERVOLT_THRESH_V       180.0f   // Indian grid undervoltage limit (VAC)
#define CONF_UNDERVOLT_DUR_MS         3000UL   // 3s confirmation duration
#define CONF_UNDERVOLT_HYS_V          5.0f     // 5V hysteresis margin
#define CONF_MCB_DEBOUNCE_MS          1000UL   // 1s debounce to confirm MCB trip
#define CONF_PHASE_LOSS_THRESH_V      20.0f    // Voltage threshold below which phase is missing
#define CONF_IMBALANCE_THRESH_PCT     30.0f    // 30% current unbalance threshold

// Communication & Offline Detection Intervals
#define COMM_UNREACHABLE_TIMEOUT_MS   60000UL  // 60s without packet = Unreachable (Route pending)
#define COMM_OFFLINE_CONFIRM_MS       180000UL // 180s (3 min) = Confirmed Device Offline
#define OFFLINE_CONFIRMATION_CYCLES   2        // Consecutive confirmation checks
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
#define NVS_NS_REVOC     "revoc"    // Persistent Revocation Registry
#define NVS_NS_DELTX     "deltx"    // Incomplete Deletion Transaction journal

#define MAX_CMD_LOG_ENTRIES   20
#define MAX_EVENT_LOG_ENTRIES 10
#define MAX_REVOKED_NODES     50    // Max revoked nodes in persistent registry
#define MAX_CURRENT_SENSORS_PER_POLE 18 // 6 Feeders x 3 Phases (R/Y/B)
#define COMMON_VOLTAGE_SENSORS_PER_POLE 3 // 3 Common R/Y/B Voltage Sensors

// ─────────────────────────────────────────────────────────────
//  SECURITY & LIFECYCLE PARAMETERS (PROMPTS 12 & 21)
// ─────────────────────────────────────────────────────────────
#ifndef MESH_AUTH_KEY
  #define MESH_AUTH_KEY                  "" // Provisioned during site commissioning
#endif

#ifndef ADMIN_ROLE_ID
  #define ADMIN_ROLE_ID                  "" // Provisioned via NVS
#endif

#define NVS_SCHEMA_VERSION             12
#define REVOCATION_RETENTION_DAYS      365
#define DELETE_CONFIRMATION_TIMEOUT_MS 60000UL // 60-second confirmation window

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
  String   sender;
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
