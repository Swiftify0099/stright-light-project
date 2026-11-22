/*
 * =====================================================================
 *  PACKETCODEC.H — MSCB BINARY SERIALIZATION, ENCRYPTION & PARSER
 *  Municipal Smart Control Box (MSCB) Smart Pole System
 *  ESP32-S3 Gateway & ESP32 Secondary Nodes (SX1278 LoRa)
 * =====================================================================
 */

#ifndef MSCB_PACKET_CODEC_H
#define MSCB_PACKET_CODEC_H

#include "Protocol.h"
#include "CryptoManager.h"
#include "ReplayProtection.h"

typedef enum {
    MSCB_DECODE_OK = 0,
    MSCB_DECODE_ERR_SIZE_UNDERFLOW,
    MSCB_DECODE_ERR_SIZE_OVERFLOW,
    MSCB_DECODE_ERR_CRC_FAILED,
    MSCB_DECODE_ERR_BAD_VERSION,
    MSCB_DECODE_ERR_BAD_NETWORK,
    MSCB_DECODE_ERR_BAD_MSG_TYPE,
    MSCB_DECODE_ERR_LENGTH_MISMATCH,
    MSCB_DECODE_ERR_REPLAY_DETECTED,
    MSCB_DECODE_ERR_AUTH_FAILED
} MscbDecodeStatus;

class PacketCodec {
public:
    PacketCodec();

    // Encode a high-level message into a secure binary frame
    bool encodePacket(uint8_t msg_type,
                      uint16_t src_id,
                      uint16_t dst_id,
                      uint16_t next_hop,
                      uint8_t hop_limit,
                      uint8_t hop_count,
                      uint32_t boot_session_id,
                      uint32_t seq_num,
                      uint8_t flags,
                      const uint8_t *payload,
                      uint8_t payload_len,
                      const uint8_t *aesKey,
                      uint8_t *out_buf,
                      size_t *out_len);

    // Decode, validate, verify CRC, authenticate AEAD tag, and decrypt frame
    MscbDecodeStatus decodePacket(const uint8_t *in_buf,
                                  size_t in_len,
                                  const uint8_t *aesKey,
                                  MscbParsedPacket *out_pkt,
                                  int16_t rssi = 0,
                                  int8_t snr = 0,
                                  bool enforceReplayCheck = true);

    // Mutable forwarding update (updates Next Hop, decrements TTL, recomputes CRC16)
    // Preserves end-to-end AEAD ciphertext and authentication tag
    bool updateForwarding(uint8_t *frame_buf,
                          size_t frame_len,
                          uint16_t new_next_hop);
};

extern PacketCodec packetCodec;

#endif // MSCB_PACKET_CODEC_H
