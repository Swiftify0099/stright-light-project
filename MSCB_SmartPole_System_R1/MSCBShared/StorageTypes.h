/*
 * =====================================================================
 *  STORAGETYPES.H — MSCB SMART POLE UNIFIED STORAGE DATA TYPES
 *  Prompt 19 Architecture: Central Storage + Node Registry + Event DB
 *  Authoritative schemas for Gateway & Secondary Pole Nodes
 * =====================================================================
 */

#ifndef MSCB_STORAGE_TYPES_H
#define MSCB_STORAGE_TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(ARDUINO) || defined(ARDUINO_MOCK_H)
#include <Arduino.h>
#else
#include <string>
typedef std::string String;
#endif

// ─────────────────────────────────────────────────────────────
//  1. CONFIRMED SYSTEM CAPACITY LIMITS (PROMPT 19)
// ─────────────────────────────────────────────────────────────
#define MSCB_MAX_POLE_NODES 50
#define MSCB_FEEDERS_PER_POLE 6
#define MSCB_TOTAL_FEEDERS (MSCB_MAX_POLE_NODES * MSCB_FEEDERS_PER_POLE) // 300
#define MSCB_CURRENT_CHANNELS_PER_POLE 18
#define MSCB_TOTAL_CURRENT_CHANNELS                                            \
  (MSCB_MAX_POLE_NODES * MSCB_CURRENT_CHANNELS_PER_POLE) // 900
#define MSCB_VOLTAGE_CHANNELS_PER_POLE 3
#define MSCB_TOTAL_VOLTAGE_CHANNELS                                            \
  (MSCB_MAX_POLE_NODES * MSCB_VOLTAGE_CHANNELS_PER_POLE) // 150
#define MSCB_TOTAL_CHANNELS_PER_POLE                                           \
  (MSCB_CURRENT_CHANNELS_PER_POLE + MSCB_VOLTAGE_CHANNELS_PER_POLE) // 21

// ─────────────────────────────────────────────────────────────
//  2. STORAGE TECHNOLOGY TARGETS & ALLOCATION
// ─────────────────────────────────────────────────────────────
enum StorageTechTarget {
  STORAGE_TECH_NVS = 0,      // Key-value, fast slot mapping, configuration
  STORAGE_TECH_LITTLEFS = 1, // Filesystem, bounded logs, large tables
  STORAGE_TECH_SD = 2,       // External storage if hardware present
  STORAGE_TECH_RAM_CACHE = 3 // High-frequency telemetry (no flash wear!)
};

// ─────────────────────────────────────────────────────────────
//  PERSISTENT NODE REGISTRATION & REVOCATION STATES (PROMPT R2)
// ─────────────────────────────────────────────────────────────
enum NodeRegClientState : uint8_t {
  REG_CLIENT_UNREGISTERED = 0,
  REG_CLIENT_JOIN_REQUEST_SENT = 1,
  REG_CLIENT_PENDING_APPROVAL = 2,
  REG_CLIENT_REGISTERED = 3,
  REG_CLIENT_REVOKED_LOCKED = 4,
  REG_CLIENT_REJECTED = 5,
  REG_CLIENT_FAULT = 6
};

#define REG_CLIENT_REQUEST_SENT REG_CLIENT_JOIN_REQUEST_SENT
#define REG_CLIENT_PENDING REG_CLIENT_PENDING_APPROVAL

inline const char *regClientStateToString(NodeRegClientState st) {
  switch (st) {
  case REG_CLIENT_UNREGISTERED:
    return "UNREGISTERED";
  case REG_CLIENT_JOIN_REQUEST_SENT:
    return "JOIN_REQUEST_SENT";
  case REG_CLIENT_PENDING_APPROVAL:
    return "PENDING_ADMIN_APPROVAL";
  case REG_CLIENT_REGISTERED:
    return "REGISTERED_ACTIVE";
  case REG_CLIENT_REVOKED_LOCKED:
    return "REVOKED_LOCKED";
  case REG_CLIENT_REJECTED:
    return "REJECTED_UNAUTHORIZED";
  case REG_CLIENT_FAULT:
    return "REGISTRATION_FAULT";
  default:
    return "UNKNOWN";
  }
}

// ─────────────────────────────────────────────────────────────
//  3. MEASUREMENT FRESHNESS (SECTION 10)
// ─────────────────────────────────────────────────────────────
enum MeasurementFreshness {
  MEAS_FRESHNESS_UNKNOWN = 0,
  MEAS_FRESHNESS_LIVE = 1,   // Currently updating
  MEAS_FRESHNESS_STALE = 2,  // Node offline; values preserved, not zeroed!
  MEAS_FRESHNESS_EXPIRED = 3 // Beyond maximum retention
};

inline const char *measurementFreshnessToString(uint8_t f) {
  switch (f) {
  case MEAS_FRESHNESS_LIVE:
    return "LIVE";
  case MEAS_FRESHNESS_STALE:
    return "STALE";
  case MEAS_FRESHNESS_EXPIRED:
    return "EXPIRED";
  default:
    return "UNKNOWN";
  }
}

// ─────────────────────────────────────────────────────────────
//  4. CALIBRATION DATA STRUCTURES (21 CHANNELS PER POLE)
// ─────────────────────────────────────────────────────────────
#define CALIBRATION_MAGIC 0x43414C49UL // "CALI"
#define CALIBRATION_VERSION 0x0100     // V1.0

struct ChannelCalibRecord {
  char channelId[16];       // e.g. "POLE-R-Voltage", "F01-R-Current"
  char sensorIdentity[24];  // e.g. "CT_SCT013_50A", "ZMPT101B_PT"
  float zeroOffset;         // Zero-input ADC offset (V or counts)
  float gainMultiplier;     // Gain calibration factor (nominal 1.000)
  float sensorSensitivity;  // Transfer multiplier (PT ratio for V, mV/A for I)
  float phaseCorrectionDeg; // Phase shift compensation (degrees)
  uint16_t calVersion;      // Calibration revision
  uint32_t calTimestamp;    // Unix timestamp
  bool isValid;             // Verified calibration flag
  char calReference[24];    // Calibration reference e.g. "FLUKE_87V_REF"
  uint32_t crc32;           // Record CRC
};

struct PoleCalibTable {
  uint32_t magic;
  uint16_t tableVersion;
  char poleId[16];
  uint16_t channelCount; // 21
  ChannelCalibRecord channels[MSCB_TOTAL_CHANNELS_PER_POLE];
  uint32_t crc32;
};

// Gateway side Calibration Metadata
struct GwCalibMeta {
  char poleId[16];
  uint16_t calVersion;
  uint32_t calTimestamp;
  uint32_t tableCrc32;
  bool isValid;
  char calReference[24];
};

// ─────────────────────────────────────────────────────────────
//  5. FEEDER REGISTRY (300 LOGICAL FEEDERS)
// ─────────────────────────────────────────────────────────────
struct LogicalFeederRecord {
  char owningPoleId[16];    // References owning Pole e.g. "POLE-0001"
  uint8_t feederId;         // 1 to 6
  char logicalFeederId[24]; // e.g. "POLE-0001-F01"
  char name[24];            // e.g. "Feeder 1 - North"
  uint8_t status;           // 0=ACTIVE, 1=DEACTIVATED, 2=REMOVED
  float currentRating;      // Protection rating (A), e.g. 50.0
  uint8_t relayState;       // 0=OFF, 1=ON
  bool mcbR;                // MCB R phase OK
  bool mcbY;                // MCB Y phase OK
  bool mcbB;                // MCB B phase OK
  bool contactor;           // Contactor aux state
  bool protectionEnabled;
  uint8_t currentChannelR; // 0..17
  uint8_t currentChannelY; // 0..17
  uint8_t currentChannelB; // 0..17
  uint32_t tripCount;
  float accumulatedKwh;
  uint32_t lastTripTimestamp;
  uint32_t crc32;
};

// ─────────────────────────────────────────────────────────────
//  6. CONFIGURATION VERSIONING & DUAL-BANK MIGRATION
// ─────────────────────────────────────────────────────────────
#define MSCB_CONFIG_MAGIC 0x4D434647UL // "MCFG"
#define MSCB_CONFIG_SCHEMA_V1 1
#define MSCB_CONFIG_SCHEMA_V2 2
#define MSCB_CURRENT_CONFIG_SCHEMA MSCB_CONFIG_SCHEMA_V2

enum ConfigValidity {
  CONFIG_INVALID = 0,
  CONFIG_VALID = 1,
  CONFIG_CORRUPTED = 2,
  CONFIG_SAFE_DEFAULTS = 3
};

struct SystemConfigRecord {
  uint32_t magic;
  uint16_t schemaVersion;
  uint16_t configVersion; // Monotonic version
  char nodeId[16];        // "GATEWAY" or "POLE-0001"
  uint32_t creationTime;
  uint32_t updateTime;
  // Electrical limits
  float nominalVoltage;      // 230.0V
  float maxOverVoltage;      // 265.0V
  float minUnderVoltage;     // 175.0V
  float maxCurrentRating;    // 60.0A
  float feederDefaultRating; // 50.0A
  // Mesh / network settings
  uint16_t heartbeatIntervalSec;
  uint16_t beaconIntervalSec;
  int8_t loraTxPower;
  uint32_t loraChannel;
  // Autonomous schedule
  bool schedEnabled;
  int16_t schedOnMinute;
  int16_t schedOffMinute;
  // State & integrity
  uint8_t validity;    // ConfigValidity
  uint32_t generation; // Ping-pong generation counter
  uint32_t crc32;
};

// ─────────────────────────────────────────────────────────────
//  7. TRANSACTIONAL WRITES & 2PC JOURNAL
// ─────────────────────────────────────────────────────────────
#define TX_JOURNAL_MAGIC 0x54584A4EUL // "TXJN"

enum TxState {
  TX_STATE_IDLE = 0,
  TX_STATE_PREPARED = 1,   // Intent persisted
  TX_STATE_COMMITTED = 2,  // Applied and verified
  TX_STATE_ROLLED_BACK = 3 // Rolled back on fault
};

enum TxType {
  TX_TYPE_NONE = 0,
  TX_TYPE_NODE_REGISTER = 1,
  TX_TYPE_NODE_DELETE = 2,
  TX_TYPE_FEEDER_DEACTIVATE = 3,
  TX_TYPE_CONFIG_UPDATE = 4,
  TX_TYPE_CALIB_UPDATE = 5,
  TX_TYPE_REVOCATION = 6
};

struct TxJournalRecord {
  uint32_t magic;
  char txId[16];
  uint8_t txType;    // TxType
  uint8_t state;     // TxState
  char targetId[16]; // Node ID or Feeder ID
  char actorId[24];  // Admin ID or System
  uint32_t timestamp;
  uint16_t payloadSize;
  uint8_t payload[128]; // Snapshot / transaction arguments
  uint32_t crc32;
};

// ─────────────────────────────────────────────────────────────
//  8. AUDIT HISTORY & BOUNDED EVENT DATABASE
// ─────────────────────────────────────────────────────────────
enum AuditEventType {
  AUDIT_EVENT_NONE = 0,
  AUDIT_EVENT_ADMIN_ACTION = 1,
  AUDIT_EVENT_POLE_REGISTER = 2,
  AUDIT_EVENT_POLE_DELETE = 3,
  AUDIT_EVENT_FEEDER_DEACTIVATE = 4,
  AUDIT_EVENT_NODE_REJOIN = 5,
  AUDIT_EVENT_CMD_REQUEST = 6,
  AUDIT_EVENT_CMD_EXEC_RESULT = 7,
  AUDIT_EVENT_WIFI_CONFIG_CHANGE = 8,
  AUDIT_EVENT_CALIBRATION_CHANGE = 9,
  AUDIT_EVENT_THRESHOLD_CHANGE = 10,
  AUDIT_EVENT_PARENT_ROUTE_CHANGE = 11,
  AUDIT_EVENT_FIRMWARE_UPDATE = 12,
  AUDIT_EVENT_SECURITY_FAILURE = 13,
  AUDIT_EVENT_GATEWAY_RESTART = 14,
  AUDIT_EVENT_CRITICAL_FAULT = 15,
  AUDIT_EVENT_OFFLINE_STALE = 16
};

enum AuditResult {
  AUDIT_RES_SUCCESS = 0,
  AUDIT_RES_FAILED = 1,
  AUDIT_RES_TIMEOUT = 2,
  AUDIT_RES_REJECTED = 3,
  AUDIT_RES_ROLLBACK = 4
};

enum AuditStatus {
  AUDIT_STATUS_COMMITTED = 0,
  AUDIT_STATUS_PENDING = 1,
  AUDIT_STATUS_RECOVERED = 2
};

// Retention Policy Categories
#define RETENTION_CATEGORY_RECENT_EVENT 0x01
#define RETENTION_CATEGORY_CRITICAL_FAULT 0x02
#define RETENTION_CATEGORY_ADMIN_AUDIT 0x04
#define RETENTION_CATEGORY_REG_REVOCATION                                      \
  0x08 // Permanent security, never purged
#define RETENTION_CATEGORY_FW_DEPLOYMENT 0x10

#define MSCB_MAX_AUDIT_EVENTS 128 // Bounded persistent ring buffer
#define MSCB_MAX_REVOCATION_RECORDS                                            \
  64 // Dedicated permanent revocation capacity (Supports 50 poles + expansion)

struct AuditEventRecord {
  uint32_t eventId;     // Monotonic sequence ID
  char txId[16];        // Correlating transaction ID
  uint16_t eventType;   // AuditEventType
  uint8_t category;     // RETENTION_CATEGORY_*
  char deviceId[16];    // e.g. "POLE-0001", "GATEWAY"
  char actorId[24];     // e.g. "ADMIN_5505296197", "SYS_AUTO"
  uint32_t timestamp;   // Unix timestamp
  uint8_t result;       // AuditResult
  uint8_t status;       // AuditStatus
  char description[48]; // Sanitized (NO PASSWORDS/TOKENS)
  uint32_t crc32;
};

// Dedicated Permanent Revocation Record
#define REVOKE_DELIVERY_CONFIRMED  0
#define REVOKE_DELIVERY_PENDING    1
#define REVOKE_STATUS_UNREVOKED    2

struct PermanentRevocationRecord {
  char hwId[17];
  char logicalId[16];
  char txId[16];
  char reason[32];
  char adminId[24];
  uint32_t timestamp;
  uint8_t  deliveryStatus;   // REVOKE_DELIVERY_CONFIRMED, REVOKE_DELIVERY_PENDING, REVOKE_STATUS_UNREVOKED
  char     unrevokeAdmin[24];
  char     unrevokeReason[32];
  uint32_t unrevokeTimestamp;
  uint32_t crc32;
};

// Helper function to calculate CRC32 for a PermanentRevocationRecord
inline uint32_t calculateRevocationCrc(const PermanentRevocationRecord &rec) {
  const uint8_t *p = (const uint8_t *)&rec;
  size_t len = offsetof(PermanentRevocationRecord, crc32);
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

// ─────────────────────────────────────────────────────────────
//  9. SECONDARY NODE STORAGE MODELS
// ─────────────────────────────────────────────────────────────
#define MSCB_NODE_FAULT_HISTORY_SIZE 16

struct NodeFaultRecord {
  uint32_t timestamp;
  uint8_t faultCode;
  uint8_t feederId;
  float triggerValue;
  char description[24];
};

struct NodeSensorMapping {
  uint8_t adcChannel;  // 0..20
  uint8_t feederIndex; // 0..5 (or 255 for Common Voltage)
  char phase;          // 'R', 'Y', 'B'
  bool isVoltage;
};

struct NodeRelayMapping {
  uint8_t feederIndex; // 0..5 (or 255 for Main Relay)
  int8_t gpioPin;      // Hardware GPIO pin
  bool activeLow;
};

// ─────────────────────────────────────────────────────────────
//  10. UTILITY FUNCTIONS (CRC & REDACTION)
// ─────────────────────────────────────────────────────────────
inline uint32_t mscbStorageCrc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  return ~crc;
}

// Credentials Redaction Helper: Strips passwords, bot tokens, and keys
inline void mscbSanitizeAuditString(const char *input, char *output,
                                    size_t maxLen) {
  if (!input || !output || maxLen == 0)
    return;
  output[0] = '\0';

  // Check for common secret markers
  const char *botTokenPrefix = "8823335578:";
  const char *wifiPass = "Saaj_7550";
  const char *tokenMatch = strstr(input, botTokenPrefix);
  const char *passMatch = strstr(input, wifiPass);

  if (tokenMatch || passMatch || strstr(input, "BOT_TOKEN") ||
      strstr(input, "PASS=") || strstr(input, "KEY=")) {
    // Redact
    strncpy(output, "[REDACTED_SENSITIVE_CREDENTIAL]", maxLen - 1);
    output[maxLen - 1] = '\0';
  } else {
    strncpy(output, input, maxLen - 1);
    output[maxLen - 1] = '\0';
  }
}

#endif // MSCB_STORAGE_TYPES_H
