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
#define MSCB_PROTOCOL_VERSION          0x01
#define MSCB_MAX_PAYLOAD_SIZE          128

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

  // Command & Actuation
  PKT_CMD_FEEDER_SWITCH        = 0x20, // Gateway commands feeder relay ON/OFF
  PKT_CMD_FEEDER_ACK           = 0x21, // Pole acknowledges execution or rejection
  PKT_CMD_EMERGENCY_SHUTDOWN   = 0x22, // Global or individual emergency shutdown
  PKT_CMD_TIME_SYNC            = 0x23, // NTP epoch broadcast from gateway

  // Diagnostic & Maintenance
  PKT_DIAG_PING                = 0x30, // Link-budget round-trip measurement
  PKT_DIAG_PONG                = 0x31  // Response with RSSI and SNR
};

// =====================================================================
//  FIXED BINARY PACKET HEADER (16 BYTES)
// =====================================================================
#pragma pack(push, 1)
struct MeshPacketHeader {
  uint8_t  magic[2];     // MSCB_PROTOCOL_MAGIC_0, MSCB_PROTOCOL_MAGIC_1
  uint8_t  version;      // MSCB_PROTOCOL_VERSION
  uint8_t  type;         // PacketType
  uint8_t  priority;     // PacketPriority
  uint8_t  hopCount;     // Current hop count (incremented each forward)
  uint8_t  maxHops;      // Max allowed hops (e.g., 6)
  uint16_t seqNumber;    // Monotonically increasing sequence number
  uint16_t srcAddress;   // Originating node address (0x0001 = Gateway)
  uint16_t dstAddress;   // Final destination (0xFFFF = Broadcast)
  uint16_t nextHop;      // Immediate recipient address
  uint16_t payloadLen;   // Payload length in bytes (excluding header and CRC)
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
