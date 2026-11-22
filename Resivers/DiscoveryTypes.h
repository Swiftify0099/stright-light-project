/*
 * =====================================================================
 *  DISCOVERYTYPES.H — AUTOMATIC NODE DISCOVERY & LINK QUALITY DATA TYPES
 *  Smart Street Light / Farmer Power Distribution Mesh System
 *  Platform : ESP32 + SX1278 RA-02 433 MHz
 *  Standards: Bounded Table, EWMA Filtering, 433MHz Duty-Cycle Airtime
 * =====================================================================
 */

#ifndef DISCOVERY_TYPES_H
#define DISCOVERY_TYPES_H

#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// ─────────────────────────────────────────────────────────────
//  NETWORK DOMAIN & CRYPTOGRAPHIC IDENTIFIERS
// ─────────────────────────────────────────────────────────────
#define MESH_NETWORK_ID             0x4331      // 16-bit Network Mesh Domain ID
#define MESH_AUTH_SECRET            0x5A8F2C1D  // 32-bit Network Authentication Secret
#define PROTOCOL_VERSION            0x01        // Binary Discovery Protocol V1

// ─────────────────────────────────────────────────────────────
//  DISCOVERY TIMING CONSTANTS (Adaptive Trickle / Backoff)
// ─────────────────────────────────────────────────────────────
#define DISCOVERY_BOOT_INTERVAL_MS  15000UL     // Fast discovery on boot (15s)
#define DISCOVERY_MIN_INTERVAL_MS   30000UL     // Seeking parent interval (30s)
#define DISCOVERY_MAX_INTERVAL_MS   180000UL    // Steady-state maintenance interval (3 min)
#define DISCOVERY_JITTER_PCT        30          // +/- 30% randomization to prevent collisions
#define NEIGHBOR_TIMEOUT_MS         180000UL    // 3 min without advertisement before STALE
#define NEIGHBOR_DEAD_TIMEOUT_MS    300000UL    // 5 min without advertisement before OFFLINE
#define NEIGHBOR_EVICTION_MS        600000UL    // 10 min before table slot can be reclaimed
#define DUPLICATE_SUPPRESS_MS       8000UL      // Minimum gap between identical advertisements

// ─────────────────────────────────────────────────────────────
//  433 MHz ISM AIRTIME & DUTY CYCLE LIMITS
// ─────────────────────────────────────────────────────────────
// ETSI / ITU-R 433.05 - 434.79 MHz typical duty cycle = 1.0%
#define AIRTIME_WINDOW_MS           3600000UL   // 1 hour rolling budget window (ms)
#define AIRTIME_MAX_BUDGET_MS       36000UL     // 1% of 1 hour = 36,000 ms transmission time
#define MIN_PACKET_INTERVAL_MS      2500UL      // Minimum physical inter-packet gap (ms)

// ─────────────────────────────────────────────────────────────
//  TABLE SIZING (BOUNDED MEMORY ALLOCATION)
// ─────────────────────────────────────────────────────────────
#define MAX_NEIGHBORS_TABLE         16          // Maximum direct neighbors per Pole Node
#define MAX_GW_NEIGHBORS_TABLE      32          // Maximum direct neighbors for Gateway

// ─────────────────────────────────────────────────────────────
//  DISCOVERY PACKET TYPE IDENTIFIERS
// ─────────────────────────────────────────────────────────────
#define PKT_TYPE_DISC_BEACON        "DISC_BEACON" // Broadcast solicit / discovery probe
#define PKT_TYPE_NEIGH_ADV          "NEIGH_ADV"   // Periodic or triggered node advertisement
#define PKT_TYPE_NEIGH_RSP          "NEIGH_RSP"   // Directed reciprocal link response
#define PKT_TYPE_LQ_REPORT          "LQ_REPORT"   // Aggregated link quality report to Gateway

// ─────────────────────────────────────────────────────────────
//  REGISTRATION & TRUST STATUS
// ─────────────────────────────────────────────────────────────
enum TrustStatus : uint8_t {
  TRUST_UNREGISTERED = 0, // Valid hardware detected, but not authorized by Admin
  TRUST_REGISTERED   = 1, // Authenticated & authorized in Network Database
  TRUST_REVOKED      = 2, // Explicitly blacklisted/revoked; discarded immediately
  TRUST_UNTRUSTED    = 3  // Mismatched Network ID or invalid authentication token
};

// ─────────────────────────────────────────────────────────────
//  NEIGHBOR AVAILABILITY STATE
// ─────────────────────────────────────────────────────────────
enum NeighborAvailability : uint8_t {
  AVAIL_OFFLINE = 0,      // Dead / unreachable
  AVAIL_STALE   = 1,      // Missed expected beacons, degraded confidence
  AVAIL_ONLINE  = 2       // Actively communicating within timeout
};

// ─────────────────────────────────────────────────────────────
//  NODE CAPABILITY FLAGS (16-bit Bitmask)
// ─────────────────────────────────────────────────────────────
#define CAP_GATEWAY         (1 << 0) // Is Master Gateway (Rank 0)
#define CAP_ROUTER          (1 << 1) // Capable of forwarding mesh packets
#define CAP_MAINS_POWERED   (1 << 2) // Powered by AC mains grid
#define CAP_SOLAR_BATTERY   (1 << 3) // Powered by solar/battery storage
#define CAP_FEEDER_CTRL     (1 << 4) // Equipped with 3-phase feeder switching
#define CAP_SENSOR_NODE     (1 << 5) // Equipped with Voltage/Current sensors
#define CAP_PARENT_ELIGIBLE (1 << 6) // Willing and able to accept child nodes
#define CAP_DEGRADED_STATE  (1 << 7) // Local faults detected (MCB trip / sensor error)

#pragma pack(push, 1)
struct MscbDiscoveryBeaconPayload {
  uint16_t node_id;             // 16-bit Node ID
  uint16_t network_id;          // Network Mesh Domain ID
  uint8_t  trust_status;        // 0: Unregistered, 1: Registered, 2: Revoked, 3: Untrusted
  uint16_t capabilities;        // Capability flags
  uint8_t  firmware_version;    // FW version
  uint8_t  node_health;         // 0-100%
  uint8_t  network_rank;        // 0 = Gateway, 1 = 1-hop, 255 = unranked
  uint8_t  available_capacity;  // Child slots
  uint16_t uplink_parent_id;    // Parent node ID
  uint32_t auth_token;          // 32-bit auth tag
};
#pragma pack(pop)

// ─────────────────────────────────────────────────────────────
//  DISCOVERY ADVERTISEMENT PAYLOAD STRUCTURE (Compact String/Binary)
// ─────────────────────────────────────────────────────────────
struct DiscoveryPayload {
  char     nodeId[16];            // Assigned Logical ID or HW ID (null-terminated)
  uint16_t networkId;             // Network domain ID (0x4331)
  uint8_t  trustStatus;           // TrustStatus enum
  uint16_t capabilities;          // Capability bitmask
  uint8_t  firmwareVersion;       // Firmware revision integer (e.g. 10)
  uint8_t  nodeHealth;            // Composite health score 0-100%
  uint8_t  networkRank;           // 0=Gateway, 1=1-hop, 255=unranked
  uint8_t  availableCapacity;     // Number of child slots available (0-10)
  char     uplinkParentId[16];    // Current parent ID (to avoid fate sharing)
  uint32_t authTag;               // Integrity/authenticity token

  DiscoveryPayload() {
    memset(nodeId, 0, sizeof(nodeId));
    networkId = MESH_NETWORK_ID;
    trustStatus = TRUST_UNREGISTERED;
    capabilities = 0;
    firmwareVersion = 10;
    nodeHealth = 100;
    networkRank = 255;
    availableCapacity = 0;
    memset(uplinkParentId, 0, sizeof(uplinkParentId));
    authTag = 0;
  }

  // Compute 32-bit CRC-based authentication token
  static uint32_t computeAuthTag(const char* id, uint16_t netId, uint8_t rank, uint32_t secret) {
    uint32_t crc = 0xFFFFFFFF;
    crc ^= (uint32_t)netId;
    crc ^= ((uint32_t)rank << 16);
    crc ^= secret;
    for (size_t i = 0; id[i] != '\0' && i < 16; i++) {
      crc ^= (uint8_t)id[i];
      for (int b = 0; b < 8; b++) {
        crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
      }
    }
    return ~crc;
  }

  // Serialize to compact ASCII string for LoRa transmission
  String serialize() const {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "ID=%s|NET=%04X|TR=%u|CAP=%04X|FW=%u|HL=%u|RK=%u|CP=%u|PR=%s|ATH=%08lX",
             nodeId, networkId, (unsigned int)trustStatus, capabilities,
             (unsigned int)firmwareVersion, (unsigned int)nodeHealth,
             (unsigned int)networkRank, (unsigned int)availableCapacity,
             (uplinkParentId[0] != '\0') ? uplinkParentId : "NONE",
             (unsigned long)authTag);
    return String(buf);
  }

  // Deserialize from compact ASCII string
  bool deserialize(const String &str) {
    int pId  = str.indexOf("ID=");
    int pNet = str.indexOf("NET=");
    int pTr  = str.indexOf("TR=");
    int pCap = str.indexOf("CAP=");
    int pFw  = str.indexOf("FW=");
    int pHl  = str.indexOf("HL=");
    int pRk  = str.indexOf("RK=");
    int pCp  = str.indexOf("CP=");
    int pPr  = str.indexOf("PR=");
    int pAth = str.indexOf("ATH=");

    if (pId < 0 || pNet < 0 || pTr < 0) return false;

    auto getSub = [&](int start, char delim) -> String {
      int end = str.indexOf(delim, start);
      return (end > 0) ? str.substring(start, end) : str.substring(start);
    };

    String sId = getSub(pId + 3, '|');
    strncpy(nodeId, sId.c_str(), sizeof(nodeId) - 1);
    nodeId[sizeof(nodeId) - 1] = '\0';

    networkId = (uint16_t)strtoul(getSub(pNet + 4, '|').c_str(), nullptr, 16);
    trustStatus = (uint8_t)getSub(pTr + 3, '|').toInt();

    if (pCap >= 0) capabilities = (uint16_t)strtoul(getSub(pCap + 4, '|').c_str(), nullptr, 16);
    if (pFw  >= 0) firmwareVersion = (uint8_t)getSub(pFw + 3, '|').toInt();
    if (pHl  >= 0) nodeHealth = (uint8_t)getSub(pHl + 3, '|').toInt();
    if (pRk  >= 0) networkRank = (uint8_t)getSub(pRk + 3, '|').toInt();
    if (pCp  >= 0) availableCapacity = (uint8_t)getSub(pCp + 3, '|').toInt();
    if (pPr  >= 0) {
      String sPr = getSub(pPr + 3, '|');
      strncpy(uplinkParentId, sPr.c_str(), sizeof(uplinkParentId) - 1);
      uplinkParentId[sizeof(uplinkParentId) - 1] = '\0';
    }
    if (pAth >= 0) authTag = (uint32_t)strtoul(getSub(pAth + 4, '|').c_str(), nullptr, 16);

    return true;
  }
};

// ─────────────────────────────────────────────────────────────
//  BOUNDED NEIGHBOR TABLE RECORD
// ─────────────────────────────────────────────────────────────
struct NeighborTableEntry {
  char     nodeId[16];            // Neighbor Node ID (Unique logical or hardware ID)
  uint16_t networkId;             // Network domain ID
  uint32_t firstSeenMs;           // Initial discovery timestamp
  uint32_t lastSeenMs;            // Last advertisement timestamp
  int16_t  rssi;                  // Last received raw RSSI (dBm)
  int8_t   snr;                   // Last received raw SNR (dB)
  float    avgRssi;               // Exponentially smoothed average RSSI (EWMA)
  float    avgSnr;                // Exponentially smoothed average SNR (EWMA)
  float    rssiVariance;          // RSSI variance (stability indicator)
  uint16_t rxPackets;             // Total successful packets received
  uint16_t failedPackets;         // Missed expected packets
  float    pdr;                   // Packet Delivery Ratio (0.0 to 1.0)
  uint8_t  linkStability;         // Stability percentage (0 - 100%)
  TrustStatus trustStatus;        // TRUST_REGISTERED, UNREGISTERED, REVOKED, UNTRUSTED
  uint16_t capabilities;          // Advertised capabilities
  uint8_t  firmwareVersion;       // Advertised firmware version
  uint8_t  nodeHealth;            // Reported health (0 - 100%)
  uint8_t  networkRank;           // 0 = Gateway, 1 = 1 hop, ..., 255 = unranked
  uint8_t  advertisedCapacity;    // Available forwarding capacity (child slots)
  char     uplinkParentId[16];    // Neighbor's upstream parent
  NeighborAvailability availability; // AVAIL_ONLINE, AVAIL_STALE, AVAIL_OFFLINE
  uint16_t linkQualityScore;      // Composite link quality score (0 to 1000)
  uint8_t  consecutiveSuccess;    // Consecutive successful packet receipts
  uint8_t  consecutiveMisses;     // Consecutive missed beacon cycles
  bool     isPrimaryCandidate;    // Identified as primary parent candidate
  bool     isBackupCandidate;     // Identified as backup parent candidate
  bool     occupied;              // Entry slot active

  NeighborTableEntry() {
    memset(nodeId, 0, sizeof(nodeId));
    networkId = 0;
    firstSeenMs = 0;
    lastSeenMs = 0;
    rssi = -128;
    snr = -20;
    avgRssi = -128.0f;
    avgSnr = -20.0f;
    rssiVariance = 0.0f;
    rxPackets = 0;
    failedPackets = 0;
    pdr = 0.0f;
    linkStability = 0;
    trustStatus = TRUST_UNREGISTERED;
    capabilities = 0;
    firmwareVersion = 0;
    nodeHealth = 0;
    networkRank = 255;
    advertisedCapacity = 0;
    memset(uplinkParentId, 0, sizeof(uplinkParentId));
    availability = AVAIL_OFFLINE;
    linkQualityScore = 0;
    consecutiveSuccess = 0;
    consecutiveMisses = 0;
    isPrimaryCandidate = false;
    isBackupCandidate = false;
    occupied = false;
  }
};

// ─────────────────────────────────────────────────────────────
//  LINK QUALITY WEIGHTING CONFIGURATION
// ─────────────────────────────────────────────────────────────
struct LinkQualityWeights {
  float wPdr;        // Weight for Packet Delivery Ratio (Default: 3.0)
  float wSnr;        // Weight for Signal-to-Noise Ratio (Default: 2.5)
  float wRssi;       // Weight for RSSI signal power    (Default: 1.5)
  float wStability;  // Weight for Link Stability       (Default: 1.0)
  float wCapacity;   // Weight for Available Capacity   (Default: 1.0)
  float wRank;       // Weight for Network Rank / Hops  (Default: 1.0)

  LinkQualityWeights() :
    wPdr(3.0f), wSnr(2.5f), wRssi(1.5f),
    wStability(1.0f), wCapacity(1.0f), wRank(1.0f)
  {}
};

// ─────────────────────────────────────────────────────────────
//  GATEWAY READABLE DIAGNOSTICS STRUCTURE
// ─────────────────────────────────────────────────────────────
struct DiscoveryDiagnostics {
  uint8_t  totalDiscovered;
  uint8_t  authenticatedNeighbors;
  uint8_t  unregisteredDevices;
  uint8_t  revokedDevices;
  uint8_t  unstableCandidates;
  int16_t  bestRssi;
  int16_t  worstRssi;
  int8_t   bestSnr;
  int8_t   worstSnr;
  uint32_t discoveryFailures;
  uint32_t airtimeUsedMs;
  float    dutyCyclePercent;
  uint32_t lastAdvSentMs;
  char     primaryCandidateId[16];
  char     backupCandidateId[16];
  uint16_t primaryCandidateScore;
  uint16_t backupCandidateScore;
};

// ─────────────────────────────────────────────────────────────
//  EXACT LORA TIME-ON-AIR (ToA) CALCULATION FUNCTION
// ─────────────────────────────────────────────────────────────
inline uint32_t calculateLoRaAirtimeMs(uint8_t payloadBytes, uint8_t sf = 10, uint32_t bw = 125000, uint8_t cr = 1) {
  // Symbol duration in milliseconds: Tsym = (2^SF) / BW * 1000
  double tSym = ((double)(1 << sf) / (double)bw) * 1000.0; // 8.192 ms for SF10, 125kHz
  // Preamble duration: (N_preamble + 4.25) * Tsym
  double tPreamble = (8.0 + 4.25) * tSym; // 12.25 * 8.192 = 100.352 ms

  // Low Data Rate Optimize (DE): 1 if symbol duration > 16ms, else 0. (For SF10, 125k, Tsym=8.192ms -> DE=0)
  int de = (tSym > 16.0) ? 1 : 0;
  // Explicit header: H=0. CRC enabled: CRC=1
  double temp = (8.0 * payloadBytes - 4.0 * sf + 28.0 + 16.0) / (4.0 * (sf - 2.0 * de));
  int nPayloadSym = 8 + (int)ceil(temp) * (cr + 4);
  if (nPayloadSym < 8) nPayloadSym = 8;

  double tPayload = nPayloadSym * tSym;
  return (uint32_t)ceil(tPreamble + tPayload);
}

#endif // DISCOVERY_TYPES_H
