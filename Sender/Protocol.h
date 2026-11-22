/*
 * =====================================================================
 *  PROTOCOL.H — MSCB BINARY PACKET SPECIFICATION & CODEC HELPERS
 *  Municipal Smart Control Box (MSCB) Smart Pole System
 *  ESP32-S3 Gateway & ESP32 Secondary Nodes (SX1278 LoRa)
 * =====================================================================
 */

#ifndef MSCB_PROTOCOL_H
#define MSCB_PROTOCOL_H

#include "NetworkTypes.h"
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────────────
//  EXACT PACKET FIELD OFFSETS & WIDTHS (TOTAL HEADER = 22 BYTES)
// ─────────────────────────────────────────────────────────────
#define MSCB_OFF_VERSION         0   // 1 byte  - Protocol Version
#define MSCB_OFF_NETWORK_ID      1   // 2 bytes - Network Identifier (Big-Endian)
#define MSCB_OFF_FLAGS           3   // 1 byte  - Control Flags
#define MSCB_OFF_MSG_TYPE        4   // 1 byte  - Message Type
#define MSCB_OFF_SRC_ID          5   // 2 bytes - Source Node ID (Big-Endian)
#define MSCB_OFF_DST_ID          7   // 2 bytes - Destination Node ID (Big-Endian)
#define MSCB_OFF_NEXT_HOP        9   // 2 bytes - Next Hop Node ID (Big-Endian) [MUTABLE]
#define MSCB_OFF_HOP_LIMIT       11  // 1 byte  - TTL / Hop Limit [MUTABLE]
#define MSCB_OFF_HOP_COUNT       12  // 1 byte  - Hops Elapsed [MUTABLE]
#define MSCB_OFF_BOOT_SESSION    13  // 4 bytes - Boot Session ID (Big-Endian)
#define MSCB_OFF_SEQ_NUM         17  // 4 bytes - Monotonic Sequence Number (Big-Endian)
#define MSCB_OFF_PAYLOAD_LEN     21  // 1 byte  - Payload Byte Length (0 .. 215)
#define MSCB_OFF_PAYLOAD_START   22  // Payload begins at byte 22

// Size of Immutable End-to-End Header authenticated via AEAD AAD
#define MSCB_AAD_SIZE            18

// ─────────────────────────────────────────────────────────────
//  PARSED PACKET STRUCTURE (DECODED IN MEMORY)
// ─────────────────────────────────────────────────────────────
typedef struct {
    uint8_t  protocol_version;
    uint16_t network_id;
    uint8_t  flags;
    uint8_t  msg_type;
    uint16_t src_id;
    uint16_t dst_id;
    uint16_t next_hop;
    uint8_t  hop_limit;
    uint8_t  hop_count;
    uint32_t boot_session_id;
    uint32_t seq_num;
    uint8_t  payload_len;
    uint8_t  payload[MSCB_MAX_PAYLOAD_LEN];
    uint8_t  auth_tag[MSCB_AUTH_TAG_SIZE];
    uint16_t crc16;
    int16_t  rssi;
    int8_t   snr;
    bool     authenticated;
} MscbParsedPacket;

// ─────────────────────────────────────────────────────────────
//  ENDIANNESS SERIALIZATION HELPERS (BIG-ENDIAN / NETWORK ORDER)
// ─────────────────────────────────────────────────────────────
static inline void mscb_write_u16_be(uint8_t *buf, uint16_t val) {
    buf[0] = (uint8_t)((val >> 8) & 0xFF);
    buf[1] = (uint8_t)(val & 0xFF);
}

static inline uint16_t mscb_read_u16_be(const uint8_t *buf) {
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

static inline void mscb_write_u32_be(uint8_t *buf, uint32_t val) {
    buf[0] = (uint8_t)((val >> 24) & 0xFF);
    buf[1] = (uint8_t)((val >> 16) & 0xFF);
    buf[2] = (uint8_t)((val >> 8) & 0xFF);
    buf[3] = (uint8_t)(val & 0xFF);
}

static inline uint32_t mscb_read_u32_be(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  |
           (uint32_t)buf[3];
}

// ─────────────────────────────────────────────────────────────
//  CONSTRUCT IMMUTABLE AAD BUFFER FOR AEAD AUTHENTICATION
//  (Extracts the 18 immutable end-to-end bytes from header)
// ─────────────────────────────────────────────────────────────
static inline void mscb_build_aad(uint8_t version, uint16_t network_id, uint8_t flags,
                                  uint8_t msg_type, uint16_t src_id, uint16_t dst_id,
                                  uint32_t boot_session_id, uint32_t seq_num,
                                  uint8_t payload_len, uint8_t *out_aad) {
    out_aad[0] = version;
    mscb_write_u16_be(&out_aad[1], network_id);
    out_aad[3] = flags;
    out_aad[4] = msg_type;
    mscb_write_u16_be(&out_aad[5], src_id);
    mscb_write_u16_be(&out_aad[7], dst_id);
    mscb_write_u32_be(&out_aad[9], boot_session_id);
    mscb_write_u32_be(&out_aad[13], seq_num);
    out_aad[17] = payload_len;
}

// ─────────────────────────────────────────────────────────────
//  CONSTRUCT 13-BYTE RFC 3610 NONCE
//  (src_id [2] + boot_session_id [4] + seq_num [4] + msg_type [1] + domain [2])
// ─────────────────────────────────────────────────────────────
static inline void mscb_build_nonce(uint16_t src_id, uint32_t boot_session_id,
                                    uint32_t seq_num, uint8_t msg_type, uint8_t *out_nonce) {
    mscb_write_u16_be(&out_nonce[0], src_id);
    mscb_write_u32_be(&out_nonce[2], boot_session_id);
    mscb_write_u32_be(&out_nonce[6], seq_num);
    out_nonce[10] = msg_type;
    out_nonce[11] = 0x00; // Domain separator
    out_nonce[12] = 0x00;
}

#ifdef __cplusplus
}
#endif

#endif // MSCB_PROTOCOL_H
