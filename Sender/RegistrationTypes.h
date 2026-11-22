#ifndef REGISTRATION_TYPES_H
#define REGISTRATION_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <mbedtls/md.h>
#else
  #include "Arduino.h"
#endif

// System Limits & Schema
#define MSCB_MAX_REGISTRY_NODES        50
#define MSCB_FEEDERS_PER_POLE          6
#define MSCB_CURRENT_CHANNELS_PER_POLE 18
#define MSCB_VOLTAGE_CHANNELS_PER_POLE 3
#define MSCB_MAX_PENDING_APPROVALS     8
#define MSCB_REG_APPROVAL_TIMEOUT_MS   300000UL // 5 minutes
#define MSCB_REG_JOIN_RETRY_MS         8000UL   // 8 seconds
#define MSCB_REG_MAX_JOIN_RETRIES      5
#define MSCB_REG_MAGIC                 0x4D534342 // "MSCB"
#define MSCB_SCHEMA_VERSION            1
#define MSCB_DEFAULT_NETWORK_ID        0x4D53     // "MS"

// Device Types
enum MscbDeviceType {
  DEV_TYPE_UNKNOWN = 0,
  DEV_TYPE_POLE    = 1,
  DEV_TYPE_FEEDER  = 2
};

// Registration States
enum MscbRegState {
  REG_STATE_FACTORY          = 0,
  REG_STATE_DISCOVERING      = 1,
  REG_STATE_JOIN_REQUESTED   = 2,
  REG_STATE_PENDING_APPROVAL = 3,
  REG_STATE_APPROVED         = 4,
  REG_STATE_ACTIVE           = 5,
  REG_STATE_REJECTED         = 6,
  REG_STATE_REVOKED          = 7,
  REG_STATE_OFFLINE          = 8
};

// Authentication States
enum MscbAuthStatus {
  AUTH_STATUS_NONE          = 0,
  AUTH_STATUS_CHALLENGE     = 1,
  AUTH_STATUS_AUTHENTICATED = 2,
  AUTH_STATUS_FAILED        = 3,
  AUTH_STATUS_REVOKED       = 4
};

// Registration Rejection / Error Reasons
enum MscbRejectReason {
  REJECT_NONE                 = 0,
  REJECT_ADMIN_DECLINED       = 1,
  REJECT_REGISTRY_FULL        = 2,
  REJECT_AUTH_FAIL            = 3,
  REJECT_REPLAY_DETECTED      = 4,
  REJECT_DEVICE_REVOKED       = 5,
  REJECT_DUPLICATE_LOGICAL_ID = 6,
  REJECT_DUPLICATE_HW_ID      = 7,
  REJECT_PARENT_UNAVAILABLE   = 8,
  REJECT_APPROVAL_TIMEOUT     = 9
};

// Logical Feeder Record (6 per Pole)
struct MscbFeederRecord {
  uint8_t feederId;        // 1 to 6
  char    name[24];        // e.g. "Feeder 1 - Main North"
  bool    enabled;         // Administration enabled flag
  float   currentRating;   // Protection trip rating (A)
  bool    relayState;      // Current relay control state
  bool    mcbR;            // Phase R MCB status
  bool    mcbY;            // Phase Y MCB status
  bool    mcbB;            // Phase B MCB status
  bool    contactor;       // Contactor auxiliary status
};

// 50-Node Registry Record
struct MscbNodeRecord {
  char             logicalId[16];        // e.g. "POLE-0001"
  char             hwId[17];             // e.g. "A4CF12E834B0"
  uint8_t          nodeType;             // DEV_TYPE_POLE or DEV_TYPE_FEEDER
  char             friendlyName[32];     // e.g. "Main Market Pole"
  char             area[32];             // e.g. "Dattawad Sector 1"
  char             installInfo[48];      // e.g. "Lat:16.68,Lon:74.60,Ins:SP"
  uint32_t         registrationTime;     // Timestamp of approval
  uint8_t          regState;             // MscbRegState
  char             primaryParent[16];    // Logical ID of Primary Parent
  char             backupParent[16];     // Logical ID of Backup Parent
  uint16_t         firmwareVersion;      // Integer firmware version
  uint32_t         bootSessionId;        // Random ID per boot
  uint32_t         lastSeenTime;         // Timestamp of last verified packet
  uint16_t         configVersion;        // Monotonic config version
  uint8_t          feederCount;          // 6 for Pole
  MscbFeederRecord feeders[MSCB_FEEDERS_PER_POLE]; // 6 Feeder channel records
  uint8_t          authRecordRef[8];     // Truncated key / credential identifier
  int16_t          lastRssi;             // dBm
  int8_t           lastSnr;              // dB
  uint8_t          hopCount;             // Mesh distance to gateway
  uint32_t         crc32;                // Record integrity checksum
};

// Pending Registration Entry (Gateway Approval Queue)
struct MscbPendingRegistration {
  bool             active;
  char             hwId[17];
  uint8_t          nodeType;
  uint16_t         firmwareVersion;
  uint32_t         bootSessionId;
  char             priCandidate[16];
  char             bkpCandidate[16];
  int16_t          rssi;
  int8_t           snr;
  uint32_t         nodeNonce;
  char             authTag[17];
  uint32_t         requestTime;
  uint8_t          state;
  char             suggestedLogicalId[16];
  char             suggestedName[32];
  char             suggestedArea[32];
  char             suggestedInstallInfo[48];
};

// Helper: Convert state to string
inline const char* mscbRegStateToString(uint8_t st) {
  switch (st) {
    case REG_STATE_FACTORY:          return "FACTORY";
    case REG_STATE_DISCOVERING:      return "DISCOVERING";
    case REG_STATE_JOIN_REQUESTED:   return "JOIN_REQUESTED";
    case REG_STATE_PENDING_APPROVAL: return "PENDING_APPROVAL";
    case REG_STATE_APPROVED:         return "APPROVED";
    case REG_STATE_ACTIVE:           return "ACTIVE";
    case REG_STATE_REJECTED:         return "REJECTED";
    case REG_STATE_REVOKED:          return "REVOKED";
    case REG_STATE_OFFLINE:          return "OFFLINE";
    default:                         return "UNKNOWN";
  }
}

// Helper: Convert reject reason to string
inline const char* mscbRejectReasonToString(uint8_t r) {
  switch (r) {
    case REJECT_NONE:                 return "NONE";
    case REJECT_ADMIN_DECLINED:       return "ADMIN_DECLINED";
    case REJECT_REGISTRY_FULL:        return "REGISTRY_FULL_50_NODES";
    case REJECT_AUTH_FAIL:            return "AUTH_SIGNATURE_INVALID";
    case REJECT_REPLAY_DETECTED:      return "REPLAY_PACKET_DETECTED";
    case REJECT_DEVICE_REVOKED:       return "DEVICE_PERMANENTLY_REVOKED";
    case REJECT_DUPLICATE_LOGICAL_ID: return "DUPLICATE_LOGICAL_ID";
    case REJECT_DUPLICATE_HW_ID:      return "DUPLICATE_HW_ID";
    case REJECT_PARENT_UNAVAILABLE:   return "PARENT_UNAVAILABLE";
    case REJECT_APPROVAL_TIMEOUT:     return "APPROVAL_TIMEOUT";
    default:                          return "UNKNOWN_ERROR";
  }
}

// Fast CRC32 calculation
inline uint32_t mscbCalculateCrc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  return ~crc;
}

// Utility: Byte array to Hex string
inline void mscbBytesToHex(const uint8_t *bytes, size_t len, char *outHex) {
  static const char hexChars[] = "0123456789ABCDEF";
  for (size_t i = 0; i < len; i++) {
    outHex[i * 2]     = hexChars[(bytes[i] >> 4) & 0x0F];
    outHex[i * 2 + 1] = hexChars[bytes[i] & 0x0F];
  }
  outHex[len * 2] = '\0';
}

// Keyed Hash Authentication Tag computation (64-bit tag formatted as 16 hex characters)
inline bool mscbComputeAuthTag(const uint8_t *key, size_t keyLen,
                              const uint8_t *data, size_t dataLen,
                              char outTagHex[17]) {
#ifdef ARDUINO
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;
  if (mbedtls_md_setup(&ctx, info, 1) != 0) return false;
  if (mbedtls_md_hmac_starts(&ctx, key, keyLen) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }
  if (mbedtls_md_hmac_update(&ctx, data, dataLen) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }
  uint8_t fullMac[32];
  if (mbedtls_md_hmac_finish(&ctx, fullMac) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }
  mbedtls_md_free(&ctx);
  // Truncate to first 8 bytes (16 hex characters) for LoRa packet compactness
  mscbBytesToHex(fullMac, 8, outTagHex);
  return true;
#else
  // High-performance 64-bit keyed hash for host simulations & unit test harness
  uint64_t hash = 0xCBF29CE484222325ULL;
  for (size_t i = 0; i < keyLen; i++) {
    hash ^= (uint64_t)key[i];
    hash *= 0x100000001B3ULL;
  }
  for (size_t i = 0; i < dataLen; i++) {
    hash ^= (uint64_t)data[i];
    hash *= 0x100000001B3ULL;
  }
  uint8_t bytes[8];
  for (int i = 0; i < 8; i++) {
    bytes[i] = (uint8_t)((hash >> (i * 8)) & 0xFF);
  }
  mscbBytesToHex(bytes, 8, outTagHex);
  return true;
#endif
}

#endif // REGISTRATION_TYPES_H
