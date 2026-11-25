/**
 * @file Protocol.h
 * @brief MSCB Smart Pole & Gateway System - Binary Mesh Frame Protocol
 * @version 1.0.0
 * 
 * Defines over-the-air packet frames, packet type identifiers, message header
 * layouts, and validation routines with CRC-16 integrity checking.
 */

#ifndef MSCB_PROTOCOL_H
#define MSCB_PROTOCOL_H

#include <Arduino.h>
#include "BuildConfig.h"
#include "NetworkTypes.h"

// =====================================================================
//  PROTOCOL CONSTANTS & MAGIC BYTES
// =====================================================================
#define MSCB_PROTOCOL_MAGIC_0          0x4D // 'M'
#define MSCB_PROTOCOL_MAGIC_1          0x53 // 'S'
#define MSCB_PROTOCOL_VERSION          0x02
#define MSCB_MAX_PAYLOAD_SIZE          128
#define MSCB_UNASSIGNED_NODE_ID        0xFFFE
#define MSCB_EPHEMERAL_JOIN_MIN        0xFE00
#define MSCB_EPHEMERAL_JOIN_MAX        0xFEFD

inline bool mscbIsValidMac(const uint8_t mac[6], bool requireUnicast = true) {
  if (!mac) return false;
  bool anyNonZero = false;
  bool anyNonFF = false;
  for (uint8_t i = 0; i < 6; i++) {
    if (mac[i] != 0x00) anyNonZero = true;
    if (mac[i] != 0xFF) anyNonFF = true;
  }
  if (!anyNonZero || !anyNonFF) return false;
  if (requireUnicast && (mac[0] & 0x01) != 0) return false;
  return true;
}

inline bool mscbIsValidAssignedPoleId(uint16_t poleId) {
  return (poleId >= 0x0002 && poleId < MSCB_EPHEMERAL_JOIN_MIN);
}

inline uint16_t mscbMakeEphemeralJoinAddr(const uint8_t mac[6]) {
  if (!mac) return MSCB_UNASSIGNED_NODE_ID;
  uint16_t h = (uint16_t)(((uint16_t)mac[4] << 8) | mac[5]) ^
               (uint16_t)(((uint16_t)mac[2] << 8) | mac[3]);
  uint16_t span = (uint16_t)(MSCB_EPHEMERAL_JOIN_MAX - MSCB_EPHEMERAL_JOIN_MIN + 1);
  return (uint16_t)(MSCB_EPHEMERAL_JOIN_MIN + (h % span));
}

// =====================================================================
//  PACKET TYPE ENUMERATION
// =====================================================================
enum PacketType : uint8_t {
  // Discovery & Association
  PKT_BEACON_ANNOUNCE          = 0x01, // Gateway or Pole periodic presence beacon
  PKT_REGISTER_REQUEST         = 0x02, // New pole requests gateway enrollment
  PKT_REGISTER_RESPONSE        = 0x03, // Gateway assigns node ID and routing parameters
  PKT_HEARTBEAT_PING           = 0x04, // Keep-alive probe
  PKT_HEARTBEAT_ACK            = 0x05, // Keep-alive response with link metrics

  // Telemetry & Measurement
  PKT_TELEMETRY_POLE_SUMMARY   = 0x10, // Bus voltages + Feeder states + Total current
  PKT_TELEMETRY_FEEDER_DETAIL  = 0x11, // Extended 18-channel current measurement stream
  PKT_ALERT_OVERCURRENT        = 0x12, // Automatic trip alert from pole to gateway
  PKT_ALERT_VOLTAGE_FAULT      = 0x13, // Phase loss, under-voltage, over-voltage alert
  PKT_ALERT_REVOKED_REAPPEAR   = 0x14, // Dedicated Revoked Pole Reappearance Alert
  PKT_ALERT_CHILD_OFFLINE      = 0x15, // Reporting parent pole notifies Gateway of child offline (R3.2.2 P0)
  PKT_ALERT_CHILD_RECOVERED    = 0x16, // Reporting parent pole notifies Gateway of child recovered (R3.2.2 P0)

  // Command & Actuation
  PKT_CMD_FEEDER_SWITCH        = 0x20, // Gateway commands feeder relay ON/OFF
  PKT_CMD_FEEDER_ACK           = 0x21, // Pole acknowledges execution or rejection
  PKT_CMD_EMERGENCY_SHUTDOWN   = 0x22, // Global or individual emergency shutdown
  PKT_CMD_TIME_SYNC            = 0x23, // NTP epoch broadcast from gateway
  PKT_CMD_REVOKE_POLE          = 0x24, // Gateway sends authenticated revocation command to Pole
  PKT_CMD_REVOKE_ACK           = 0x25, // Pole acknowledges receipt of revocation command

  // Diagnostic & Maintenance
  PKT_DIAG_PING                = 0x30, // Link-budget round-trip measurement
  PKT_DIAG_PONG                = 0x31  // Response with RSSI and SNR
};

// =====================================================================
//  FIXED BINARY PACKET HEADER (23 BYTES - R3.2.2)
// =====================================================================
#pragma pack(push, 1)
struct MeshPacketHeader {
  uint8_t  magic[2];            // MSCB_PROTOCOL_MAGIC_0, MSCB_PROTOCOL_MAGIC_1
  uint8_t  version;             // MSCB_PROTOCOL_VERSION (0x02)
  uint8_t  type;                // PacketType
  uint8_t  priority;            // PacketPriority
  uint8_t  hopCount;            // Current hop count (incremented each forward)
  uint8_t  maxHops;             // Max allowed hops (e.g., 6)
  uint16_t seqNumber;           // Monotonically increasing sequence number
  uint16_t srcAddress;          // Originating node address (0x0001 = Gateway)
  uint16_t dstAddress;          // Final destination (0xFFFF = Broadcast)
  uint16_t nextHop;             // Immediate recipient address
  uint16_t previousHopAddress;  // Immediate transmitting node address (R3.2.2 P0)
  uint32_t hopAuthToken;        // Authenticated 32-bit hop verification token (R3.2.2 P0)
  uint16_t payloadLen;          // Payload length in bytes (excluding header and CRC)
};
#pragma pack(pop)

// =====================================================================
//  STRUCTURED WIRE PAYLOAD DEFINITIONS
// =====================================================================
#pragma pack(push, 1)
struct PktRegisterRequestPayload {
  uint8_t  macAddress[6];
  uint8_t  nodeType;             // e.g. 1 = FIELD_POLE
  uint16_t firmwareVersion;
  uint32_t sessionToken;
  uint16_t candidateParent1;
  uint16_t candidateParent2;
  int16_t  rssiToParent1;
  int8_t   snrToParent1;
  uint32_t nonce;
  uint8_t  authTag[16];
};

enum RegisterResponseCode : uint8_t {
  REG_RESP_APPROVED              = 0,
  REG_RESP_REJECTED_CAPACITY     = 1,
  REG_RESP_REJECTED_REVOKED      = 2,
  REG_RESP_REJECTED_UNAUTHORIZED = 3,
  REG_RESP_PENDING_APPROVAL      = 4
};

struct PktRegisterResponsePayload {
  uint8_t  macAddress[6];
  uint8_t  status;               // RegisterResponseCode
  uint16_t assignedPoleId;
  uint16_t assignedParent;
  uint16_t heartbeatIntervalSec;
  uint32_t networkKeyEpoch;
  uint32_t sessionToken;         // Correlated registration session token (R3.2.2 P0)
  uint32_t nonce;                // Correlated request nonce (R3.2.2 P0)
  uint8_t  authTag[16];          // Cryptographic authentication tag (R3.2.2 P0)
};

struct PktHeartbeatPingPayload {
  uint16_t poleId;
  uint16_t primaryParent;
  uint16_t backupParent;
  uint8_t  rank;
  uint8_t  healthState;
  uint8_t  feederStateMask;      // bits 0..5
  uint8_t  auxFeedbackMask;      // bits 0..5 contactor aux
  uint8_t  mcbStatusMask;        // bits 0..5 mcb aux
  uint8_t  auxFeedbackValid;     // 1 = Valid physical contactor feedback, 0 = NOT_CONNECTED / UNKNOWN
  uint8_t  mcbStatusValid;       // 1 = Valid MCB feedback, 0 = NOT_CONNECTED / UNKNOWN
  uint32_t uptimeSec;
  float    busVoltageV;          // R-phase bus voltage or avg
  float    totalCurrentA;        // aggregate current
  uint8_t  macAddress[6];        // Hardware MAC for identity & handshake binding (R3.2.2)
  uint32_t sessionToken;         // Active registration session token (R3.2.2)
  uint32_t seqNonce;             // Fresh monotonic heartbeat nonce (R3.2.2)
  uint8_t  authTag[16];          // Cryptographic authentication tag (R3.2.2)
};

enum HeartbeatAckStatus : uint8_t {
  HB_ACK_OK                   = 0,
  HB_ACK_CONFIG_OUTDATED      = 1,
  HB_ACK_RE_REGISTER_REQUIRED = 2,
  HB_ACK_REVOKED_LOCKED       = 3,
  HB_ACK_UNKNOWN_NODE         = 4
};

struct PktHeartbeatAckPayload {
  uint16_t poleId;
  uint32_t gatewayTimeEpoch;
  uint16_t configVersion;
  uint8_t  ackStatus;            // HeartbeatAckStatus
};

struct PktRevokeCmdPayload {
  uint16_t networkId;          // MSCB_CMD_NETWORK_MAGIC (0x4D53)
  uint16_t gatewayNodeId;      // MSCB_GATEWAY_NODE_ID (0x0001)
  uint16_t targetPoleId;       // Destination Pole ID
  uint8_t  macAddress[6];      // Hardware MAC of target pole
  uint32_t sequenceNumber;     // Monotonic sequence number
  uint32_t keyEpoch;           // Active key epoch
  uint32_t timestamp;          // Real UTC Unix Epoch from Gateway
  uint32_t expiryEpoch;        // Expiry timestamp
  char     txId[16];           // Transaction ID
  char     reason[32];         // Revocation reason
  uint8_t  hmacTag[32];        // 256-bit HMAC tag computed over the header fields above
};

#define MSCB_REVOKE_AUTH_BYTES (offsetof(PktRevokeCmdPayload, hmacTag))

struct PktRevokeAckPayload {
  uint8_t  macAddress[6];
  uint16_t poleId;
  char     txId[16];
  uint8_t  status; // 0 = SUCCESS_LOCKED, 1 = FAILED
  uint32_t timestamp;
  uint8_t  authTag[32]; // Authenticated ACK tag
};

struct PktRevokedReappearPayload {
  uint8_t  macAddress[6];
  uint16_t previousPoleId;
  uint32_t uptimeSec;
  uint32_t timestamp;
};

#define MSCB_TIME_SYNC_HEADER_BYTES  16

struct PktTimeSyncPayload {
  uint16_t networkId;       // 0x4D53 ("MS")
  uint16_t gatewayNodeId;   // 0x0001 (MSCB_GATEWAY_NODE_ID)
  uint32_t epochSec;        // Real UTC Unix Epoch from Gateway NTP
  uint32_t sequenceNumber;  // Monotonic sequence number
  uint32_t keyEpoch;        // Active key epoch
  uint8_t  hmacTag[32];     // 256-bit HMAC tag computed over the 16 bytes above
};

struct PktFeederCmdPayload {
  uint16_t targetPoleId;
  uint8_t  feederId;             // 1..6 or 0 for ALL
  uint8_t  commandAction;        // 0=OFF, 1=ON, 2=TOGGLE
  uint8_t  groupMask;            // bitmask for group control (bits 0..5)
  uint32_t sequenceNumber;
  uint32_t expiryEpochSec;
  uint8_t  hmacTag[32];          // 256-bit HMAC
};

struct PktFeederAckPayload {
  uint16_t poleId;
  uint8_t  feederId;
  uint32_t sequenceNumber;
  uint8_t  ackState;             // FeederCommandAckState
  uint8_t  currentRelayMask;     // Bitmask of physical relay coils
  uint8_t  currentAuxMask;       // Bitmask of contactor auxiliary contacts
  uint8_t  currentMcbMask;       // Bitmask of MCB auxiliary contacts
  uint8_t  auxFeedbackValid;     // 1 = Valid physical contactor feedback, 0 = NOT_CONNECTED / UNKNOWN
  uint8_t  mcbStatusValid;       // 1 = Valid MCB feedback, 0 = NOT_CONNECTED / UNKNOWN
};

struct PktBeaconAnnouncePayload {
  uint16_t senderId;
  uint8_t  rank;
  uint16_t primaryParent;
  uint8_t  capacitySlotsFree;
  uint32_t beaconTimestamp;
};

struct PktChildStatusEventPayload {
  uint16_t parentPoleId;      // Reporting parent pole
  uint16_t childPoleId;       // Downstream child pole ID
  uint8_t  eventType;         // 0 = CHILD_EVENT_OFFLINE, 1 = CHILD_EVENT_RECOVERED
  uint8_t  reason;            // 1 = TIMEOUT, 2 = PACKET_RECEIVED
  uint32_t eventTimestamp;    // Timestamp of event
  uint32_t eventSeqNumber;    // Monotonic event sequence number
};

/**
 * @brief Raw Wire Frame with Pre-allocated Payload Buffer
 */
struct MeshWireFrame {
  MeshPacketHeader header;
  uint8_t          payload[MSCB_MAX_PAYLOAD_SIZE];
  uint16_t         crc16;
};
#pragma pack(pop)

// =====================================================================
//  CRC-16 / CCITT-FALSE INTEGRITY VERIFICATION
// =====================================================================
class ProtocolCrc {
public:
  static uint16_t calculate(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
      crc ^= ((uint16_t)data[i]) << 8;
      for (uint8_t bit = 0; bit < 8; ++bit) {
        if (crc & 0x8000) {
          crc = (crc << 1) ^ 0x1021;
        } else {
          crc <<= 1;
        }
      }
    }
    return crc;
  }

  static bool verify(const uint8_t* data, size_t length, uint16_t expectedCrc) {
    return calculate(data, length) == expectedCrc;
  }
};

#endif // MSCB_PROTOCOL_H
