/*
 * =====================================================================
 *  RADIOTYPES.H — DATA STRUCTURES, ENUMERATIONS & PACKET FRAMING
 *  Point-to-Point SX1278 LoRa Protocol
 * =====================================================================
 */

#ifndef RADIO_TYPES_H
#define RADIO_TYPES_H

#include <Arduino.h>
#include "RadioConfig.h"

// ─────────────────────────────────────────────────────────────
//  MESSAGE TYPES (DIAGNOSTIC PROTOCOL)
// ─────────────────────────────────────────────────────────────
enum RadioMsgType : uint8_t {
  MSG_NONE         = 0x00,
  MSG_RADIO_HELLO  = 0x01,  // Broadcast / P2P presence ping
  MSG_RADIO_ACK    = 0x02,  // Transaction acknowledgement
  MSG_RADIO_TEST   = 0x03,  // Diagnostic payload packet
  MSG_RADIO_STATUS = 0x04   // Telemetry / metric query or reply
};

static inline const char* getMsgTypeName(uint8_t type) {
  switch (type) {
    case MSG_RADIO_HELLO:  return "RADIO_HELLO";
    case MSG_RADIO_ACK:    return "RADIO_ACK";
    case MSG_RADIO_TEST:   return "RADIO_TEST";
    case MSG_RADIO_STATUS: return "RADIO_STATUS";
    default:               return "UNKNOWN";
  }
}

// ─────────────────────────────────────────────────────────────
//  PACKET PRIORITY LEVELS
// ─────────────────────────────────────────────────────────────
enum PacketPriority : uint8_t {
  PRIO_LOW    = 0,  // Periodic beacons, routine telemetry
  PRIO_MEDIUM = 1,  // Diagnostic test commands
  PRIO_HIGH   = 2   // ACKs, urgent safety notifications
};

// ─────────────────────────────────────────────────────────────
//  RADIO STATE MACHINE STATES
// ─────────────────────────────────────────────────────────────
enum RadioState : uint8_t {
  RADIO_STATE_UNINITIALIZED = 0,
  RADIO_STATE_STANDBY,
  RADIO_STATE_RX_LISTENING,
  RADIO_STATE_CARRIER_SENSE,
  RADIO_STATE_TX_ACTIVE,
  RADIO_STATE_WAIT_ACK,
  RADIO_STATE_RETRY_BACKOFF,
  RADIO_STATE_ERROR
};

static inline const char* getRadioStateName(RadioState state) {
  switch (state) {
    case RADIO_STATE_UNINITIALIZED: return "UNINIT";
    case RADIO_STATE_STANDBY:       return "STANDBY";
    case RADIO_STATE_RX_LISTENING:  return "RX_LISTEN";
    case RADIO_STATE_CARRIER_SENSE: return "CARRIER_SENSE";
    case RADIO_STATE_TX_ACTIVE:     return "TX_ACTIVE";
    case RADIO_STATE_WAIT_ACK:      return "WAIT_ACK";
    case RADIO_STATE_RETRY_BACKOFF: return "RETRY_BACKOFF";
    case RADIO_STATE_ERROR:         return "ERROR";
    default:                        return "UNKNOWN";
  }
}

// ─────────────────────────────────────────────────────────────
//  RADIO ERROR CODES
// ─────────────────────────────────────────────────────────────
enum RadioError : uint8_t {
  RADIO_ERR_NONE = 0,
  RADIO_ERR_SPI_FAIL,
  RADIO_ERR_INVALID_VERSION,
  RADIO_ERR_REGULATORY_VIOLATION,
  RADIO_ERR_TX_TIMEOUT,
  RADIO_ERR_RX_CRC_FAIL,
  RADIO_ERR_QUEUE_FULL,
  RADIO_ERR_CHANNEL_BUSY,
  RADIO_ERR_DUTY_CYCLE_EXCEEDED,
  RADIO_ERR_HARDWARE_FAULT,
  RADIO_ERR_PACKET_TOO_LARGE,
  RADIO_ERR_INVALID_PACKET,
  RADIO_ERR_NO_ACK
};

static inline const char* getRadioErrorString(RadioError err) {
  switch (err) {
    case RADIO_ERR_NONE:                 return "RADIO_OK";
    case RADIO_ERR_SPI_FAIL:             return "SPI_BUS_FAILURE";
    case RADIO_ERR_INVALID_VERSION:      return "INVALID_RADIO_SILICON_VERSION";
    case RADIO_ERR_REGULATORY_VIOLATION: return "WPC_REGULATORY_VIOLATION";
    case RADIO_ERR_TX_TIMEOUT:           return "TX_OPERATION_TIMEOUT";
    case RADIO_ERR_RX_CRC_FAIL:          return "RX_PAYLOAD_CRC_ERROR";
    case RADIO_ERR_QUEUE_FULL:           return "TX_QUEUE_CAPACITY_FULL";
    case RADIO_ERR_CHANNEL_BUSY:         return "LBT_CHANNEL_BUSY_BACKOFF_EXHAUSTED";
    case RADIO_ERR_DUTY_CYCLE_EXCEEDED:  return "WPC_10_PERCENT_DUTY_CYCLE_EXCEEDED";
    case RADIO_ERR_HARDWARE_FAULT:       return "RADIO_HARDWARE_LOCKUP";
    case RADIO_ERR_PACKET_TOO_LARGE:     return "PACKET_EXCEEDS_FIFO_LIMIT";
    case RADIO_ERR_INVALID_PACKET:       return "MALFORMED_FRAME_RECEIVED";
    case RADIO_ERR_NO_ACK:               return "PEER_ACK_TIMEOUT";
    default:                             return "UNKNOWN_ERROR";
  }
}

// ─────────────────────────────────────────────────────────────
//  STRUCTURED PACKET DEFINITION
//  Zero heap allocation, strictly bounded character arrays.
// ─────────────────────────────────────────────────────────────
#define PACKET_MAGIC_BYTE_1   0xAA
#define PACKET_MAGIC_BYTE_2   0x55
#define NODE_ID_MAX_LEN       12

struct RadioPacket_t {
  uint8_t        msgType;
  char           srcId[NODE_ID_MAX_LEN];
  char           dstId[NODE_ID_MAX_LEN];
  uint16_t       seq;
  PacketPriority priority;
  uint8_t        payloadLen;
  uint8_t        payload[RADIO_MAX_PAYLOAD_SIZE];
  uint16_t       checksum;

  // Metadata populated on RX
  int16_t        rssi;
  float          snr;
  uint32_t       timestamp;

  // Internal dispatch tracking
  uint8_t        retries;
  uint32_t       expiryMs;
};

// ─────────────────────────────────────────────────────────────
//  CRC-16-CCITT CALCULATION (Polynomial: 0x1021, Initial: 0xFFFF)
// ─────────────────────────────────────────────────────────────
static inline uint16_t calculateCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)data[i] << 8);
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// ─────────────────────────────────────────────────────────────
//  PACKET SERIALIZATION & DESERIALIZATION
// ─────────────────────────────────────────────────────────────
// Frame Format:
// [0..1]   Magic Bytes (0xAA, 0x55)
// [2]      MsgType
// [3..14]  SrcId (12 bytes, null-padded)
// [15..26] DstId (12 bytes, null-padded)
// [27..28] Seq (uint16_t, MSB first)
// [29]     Priority
// [30]     PayloadLen (N <= 64)
// [31..31+N-1] Payload bytes
// [31+N..32+N] Checksum (CRC-16-CCITT, MSB first)
#define PACKET_HEADER_SIZE    31
#define PACKET_FOOTER_SIZE    2

static inline size_t serializePacket(const RadioPacket_t &pkt, uint8_t *outBuf, size_t maxBufLen) {
  size_t totalLen = PACKET_HEADER_SIZE + pkt.payloadLen + PACKET_FOOTER_SIZE;
  if (totalLen > maxBufLen || pkt.payloadLen > RADIO_MAX_PAYLOAD_SIZE) {
    return 0;
  }

  outBuf[0] = PACKET_MAGIC_BYTE_1;
  outBuf[1] = PACKET_MAGIC_BYTE_2;
  outBuf[2] = pkt.msgType;

  memset(&outBuf[3], 0, NODE_ID_MAX_LEN);
  strncpy((char*)&outBuf[3], pkt.srcId, NODE_ID_MAX_LEN - 1);

  memset(&outBuf[15], 0, NODE_ID_MAX_LEN);
  strncpy((char*)&outBuf[15], pkt.dstId, NODE_ID_MAX_LEN - 1);

  outBuf[27] = (uint8_t)(pkt.seq >> 8);
  outBuf[28] = (uint8_t)(pkt.seq & 0xFF);
  outBuf[29] = (uint8_t)pkt.priority;
  outBuf[30] = pkt.payloadLen;

  if (pkt.payloadLen > 0) {
    memcpy(&outBuf[31], pkt.payload, pkt.payloadLen);
  }

  uint16_t crc = calculateCrc16(&outBuf[2], PACKET_HEADER_SIZE - 2 + pkt.payloadLen);
  size_t crcOffset = 31 + pkt.payloadLen;
  outBuf[crcOffset]     = (uint8_t)(crc >> 8);
  outBuf[crcOffset + 1] = (uint8_t)(crc & 0xFF);

  return totalLen;
}

static inline bool deserializePacket(const uint8_t *inBuf, size_t len, int16_t rssi, float snr, RadioPacket_t &outPkt) {
  if (len < (PACKET_HEADER_SIZE + PACKET_FOOTER_SIZE)) {
    return false;
  }
  if (inBuf[0] != PACKET_MAGIC_BYTE_1 || inBuf[1] != PACKET_MAGIC_BYTE_2) {
    return false;
  }

  uint8_t payloadLen = inBuf[30];
  if (payloadLen > RADIO_MAX_PAYLOAD_SIZE) {
    return false;
  }
  if (len != (size_t)(PACKET_HEADER_SIZE + payloadLen + PACKET_FOOTER_SIZE)) {
    return false;
  }

  // Verify CRC
  size_t crcOffset = 31 + payloadLen;
  uint16_t receivedCrc = ((uint16_t)inBuf[crcOffset] << 8) | inBuf[crcOffset + 1];
  uint16_t calculatedCrc = calculateCrc16(&inBuf[2], PACKET_HEADER_SIZE - 2 + payloadLen);
  if (receivedCrc != calculatedCrc) {
    return false;
  }

  outPkt.msgType = inBuf[2];
  memset(outPkt.srcId, 0, NODE_ID_MAX_LEN);
  memcpy(outPkt.srcId, &inBuf[3], NODE_ID_MAX_LEN - 1);
  outPkt.srcId[NODE_ID_MAX_LEN - 1] = '\0';

  memset(outPkt.dstId, 0, NODE_ID_MAX_LEN);
  memcpy(outPkt.dstId, &inBuf[15], NODE_ID_MAX_LEN - 1);
  outPkt.dstId[NODE_ID_MAX_LEN - 1] = '\0';

  outPkt.seq        = ((uint16_t)inBuf[27] << 8) | inBuf[28];
  outPkt.priority   = (PacketPriority)inBuf[29];
  outPkt.payloadLen = payloadLen;

  if (payloadLen > 0) {
    memcpy(outPkt.payload, &inBuf[31], payloadLen);
  }
  outPkt.checksum   = receivedCrc;
  outPkt.rssi       = rssi;
  outPkt.snr        = snr;
  outPkt.timestamp  = millis();
  outPkt.retries    = 0;
  outPkt.expiryMs   = 0;

  return true;
}

// ─────────────────────────────────────────────────────────────
//  RADIO QUALITY MONITORING METRICS
// ─────────────────────────────────────────────────────────────
struct RadioMetrics_t {
  uint32_t rxPacketCount;
  uint32_t txPacketCount;
  uint32_t txFailedCount;
  uint32_t ackSuccessCount;
  uint32_t ackTimeoutCount;
  int16_t  lastRssi;
  float    avgRssi;
  float    lastSnr;
  float    avgSnr;
  uint8_t  lastPacketLen;
  uint16_t radioRestartCount;
  uint32_t packetErrorCount;
  uint32_t totalAirtimeMs;
  uint32_t windowAirtimeMs;
  uint32_t windowStartMs;

  void reset() {
    rxPacketCount     = 0;
    txPacketCount     = 0;
    txFailedCount     = 0;
    ackSuccessCount   = 0;
    ackTimeoutCount   = 0;
    lastRssi          = -120;
    avgRssi           = -120.0f;
    lastSnr           = 0.0f;
    avgSnr            = 0.0f;
    lastPacketLen     = 0;
    radioRestartCount = 0;
    packetErrorCount  = 0;
    totalAirtimeMs    = 0;
    windowAirtimeMs   = 0;
    windowStartMs     = millis();
  }

  float getAckSuccessRate() const {
    uint32_t total = ackSuccessCount + ackTimeoutCount;
    if (total == 0) return 0.0f;
    return ((float)ackSuccessCount / (float)total) * 100.0f;
  }

  float getDutyCyclePercent() const {
    uint32_t elapsed = millis() - windowStartMs;
    if (elapsed == 0) return 0.0f;
    return ((float)windowAirtimeMs / (float)elapsed) * 100.0f;
  }

  void updateDutyCycle(uint32_t airtimeMs) {
    totalAirtimeMs += airtimeMs;
    uint32_t now = millis();
    if ((now - windowStartMs) >= DUTY_CYCLE_WINDOW_MS) {
      windowStartMs = now;
      windowAirtimeMs = airtimeMs;
    } else {
      windowAirtimeMs += airtimeMs;
    }
  }

  void recordRx(int16_t rssi, float snr, uint8_t len) {
    rxPacketCount++;
    lastRssi = rssi;
    lastSnr = snr;
    lastPacketLen = len;
    if (rxPacketCount == 1) {
      avgRssi = (float)rssi;
      avgSnr = snr;
    } else {
      // Exponential moving average (alpha = 0.2)
      avgRssi = (avgRssi * 0.8f) + ((float)rssi * 0.2f);
      avgSnr  = (avgSnr * 0.8f)  + (snr * 0.2f);
    }
  }

  void printSummary(Stream &out) const {
    out.println(F("═══════════════════════════════════════════════════════════"));
    out.println(F("              SX1278 RADIO QUALITY METRICS                "));
    out.println(F("═══════════════════════════════════════════════════════════"));
    out.printf("  RX Packets Received     : %lu\n", (unsigned long)rxPacketCount);
    out.printf("  TX Packets Sent         : %lu\n", (unsigned long)txPacketCount);
    out.printf("  TX Failures             : %lu\n", (unsigned long)txFailedCount);
    out.printf("  ACK Success Count       : %lu\n", (unsigned long)ackSuccessCount);
    out.printf("  ACK Timeout Count       : %lu\n", (unsigned long)ackTimeoutCount);
    out.printf("  ACK Success Rate        : %.1f %%\n", getAckSuccessRate());
    out.printf("  Last RSSI / Avg RSSI    : %d dBm / %.1f dBm\n", lastRssi, avgRssi);
    out.printf("  Last SNR / Avg SNR      : %.1f dB / %.1f dB\n", lastSnr, avgSnr);
    out.printf("  Last Packet Length      : %u bytes\n", lastPacketLen);
    out.printf("  Radio Restart Count     : %u\n", radioRestartCount);
    out.printf("  Packet Error Count      : %lu\n", (unsigned long)packetErrorCount);
    out.printf("  Total Airtime           : %lu ms\n", (unsigned long)totalAirtimeMs);
    out.printf("  Current Duty Cycle      : %.2f %% (WPC Limit: <= 10.0%%)\n", getDutyCyclePercent());
    out.println(F("═══════════════════════════════════════════════════════════"));
  }
};

#endif // RADIO_TYPES_H
