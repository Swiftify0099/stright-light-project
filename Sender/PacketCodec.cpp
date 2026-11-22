/*
 * =====================================================================
 *  PACKETCODEC.CPP — MSCB BINARY SERIALIZATION & AUTHENTICATION CODEC
 *  Municipal Smart Control Box (MSCB) Smart Pole System
 * =====================================================================
 */

#include "PacketCodec.h"
#include <string.h>

PacketCodec packetCodec;

PacketCodec::PacketCodec() {}

bool PacketCodec::encodePacket(uint8_t msg_type,
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
                               size_t *out_len) {
    if (!out_buf || !out_len) return false;
    if (payload_len > MSCB_MAX_PAYLOAD_LEN) return false;
    if ((flags & MSCB_FLAG_ENCRYPTED) && !aesKey) return false;

    // 1. Write Header (22 bytes)
    out_buf[MSCB_OFF_VERSION] = MSCB_PROTOCOL_VERSION;
    mscb_write_u16_be(&out_buf[MSCB_OFF_NETWORK_ID], MSCB_NETWORK_ID_DEFAULT);
    out_buf[MSCB_OFF_FLAGS] = flags;
    out_buf[MSCB_OFF_MSG_TYPE] = msg_type;
    mscb_write_u16_be(&out_buf[MSCB_OFF_SRC_ID], src_id);
    mscb_write_u16_be(&out_buf[MSCB_OFF_DST_ID], dst_id);
    mscb_write_u16_be(&out_buf[MSCB_OFF_NEXT_HOP], next_hop);
    out_buf[MSCB_OFF_HOP_LIMIT] = hop_limit;
    out_buf[MSCB_OFF_HOP_COUNT] = hop_count;
    mscb_write_u32_be(&out_buf[MSCB_OFF_BOOT_SESSION], boot_session_id);
    mscb_write_u32_be(&out_buf[MSCB_OFF_SEQ_NUM], seq_num);
    out_buf[MSCB_OFF_PAYLOAD_LEN] = payload_len;

    uint8_t *pPayload = &out_buf[MSCB_OFF_PAYLOAD_START];
    uint8_t *pTag     = &out_buf[MSCB_OFF_PAYLOAD_START + payload_len];

    // 2. Encryption and Authentication
    if (flags & MSCB_FLAG_ENCRYPTED) {
        uint8_t aad[MSCB_AAD_SIZE];
        mscb_build_aad(MSCB_PROTOCOL_VERSION, MSCB_NETWORK_ID_DEFAULT, flags,
                       msg_type, src_id, dst_id, boot_session_id, seq_num,
                       payload_len, aad);

        uint8_t nonce[MSCB_CRYPTO_NONCE_SIZE];
        mscb_build_nonce(src_id, boot_session_id, seq_num, msg_type, nonce);

        bool encOk = cryptoMgr.encrypt(aesKey, nonce, aad, MSCB_AAD_SIZE,
                                       payload, payload_len, pPayload, pTag);
        if (!encOk) return false;
    } else {
        if (payload_len > 0 && payload) {
            memcpy(pPayload, payload, payload_len);
        }
        memset(pTag, 0, MSCB_AUTH_TAG_SIZE);
    }

    // 3. Radio Error Detection (CRC16-CCITT over entire packet before CRC)
    size_t dataLenToCrc = MSCB_HEADER_SIZE + payload_len + MSCB_AUTH_TAG_SIZE;
    uint16_t crc = cryptoMgr.calculateCRC16(out_buf, dataLenToCrc);
    mscb_write_u16_be(&out_buf[dataLenToCrc], crc);

    *out_len = dataLenToCrc + MSCB_RADIO_CRC_SIZE;
    return true;
}

MscbDecodeStatus PacketCodec::decodePacket(const uint8_t *in_buf,
                                           size_t in_len,
                                           const uint8_t *aesKey,
                                           MscbParsedPacket *out_pkt,
                                           int16_t rssi,
                                           int8_t snr,
                                           bool enforceReplayCheck) {
    if (!in_buf || !out_pkt) return MSCB_DECODE_ERR_SIZE_UNDERFLOW;

    // Step 1: Physical Size Bounds Check
    if (in_len < MSCB_MIN_PACKET_LEN) return MSCB_DECODE_ERR_SIZE_UNDERFLOW;
    if (in_len > MSCB_RADIO_FIFO_MAX_LEN) return MSCB_DECODE_ERR_SIZE_OVERFLOW;

    // Step 2: Radio Error Detection (CRC16)
    size_t crcOffset = in_len - MSCB_RADIO_CRC_SIZE;
    uint16_t expectedCrc = mscb_read_u16_be(&in_buf[crcOffset]);
    if (!cryptoMgr.verifyCRC16(in_buf, crcOffset, expectedCrc)) {
        return MSCB_DECODE_ERR_CRC_FAILED;
    }

    // Step 3: Header Protocol Version & Network ID Check
    uint8_t version = in_buf[MSCB_OFF_VERSION];
    if (version != MSCB_PROTOCOL_VERSION) {
        return MSCB_DECODE_ERR_BAD_VERSION;
    }

    uint16_t networkId = mscb_read_u16_be(&in_buf[MSCB_OFF_NETWORK_ID]);
    if (networkId != MSCB_NETWORK_ID_DEFAULT) {
        return MSCB_DECODE_ERR_BAD_NETWORK;
    }

    // Step 4: Message Type & Declared Length Validation
    uint8_t msgType = in_buf[MSCB_OFF_MSG_TYPE];
    if (msgType < MSCB_MSG_DISCOVERY_BEACON || msgType > MSCB_MSG_ERROR_RESPONSE) {
        return MSCB_DECODE_ERR_BAD_MSG_TYPE;
    }

    uint8_t payloadLen = in_buf[MSCB_OFF_PAYLOAD_LEN];
    size_t expectedLen = MSCB_HEADER_SIZE + payloadLen + MSCB_AUTH_TAG_SIZE + MSCB_RADIO_CRC_SIZE;
    if (in_len != expectedLen) {
        return MSCB_DECODE_ERR_LENGTH_MISMATCH;
    }

    // Step 5: Extract Header Fields
    out_pkt->protocol_version = version;
    out_pkt->network_id       = networkId;
    out_pkt->flags            = in_buf[MSCB_OFF_FLAGS];
    out_pkt->msg_type         = msgType;
    out_pkt->src_id           = mscb_read_u16_be(&in_buf[MSCB_OFF_SRC_ID]);
    out_pkt->dst_id           = mscb_read_u16_be(&in_buf[MSCB_OFF_DST_ID]);
    out_pkt->next_hop         = mscb_read_u16_be(&in_buf[MSCB_OFF_NEXT_HOP]);
    out_pkt->hop_limit        = in_buf[MSCB_OFF_HOP_LIMIT];
    out_pkt->hop_count        = in_buf[MSCB_OFF_HOP_COUNT];
    out_pkt->boot_session_id  = mscb_read_u32_be(&in_buf[MSCB_OFF_BOOT_SESSION]);
    out_pkt->seq_num          = mscb_read_u32_be(&in_buf[MSCB_OFF_SEQ_NUM]);
    out_pkt->payload_len      = payloadLen;
    out_pkt->crc16            = expectedCrc;
    out_pkt->rssi             = rssi;
    out_pkt->snr              = snr;
    out_pkt->authenticated    = false;

    const uint8_t *pCiphertext = &in_buf[MSCB_OFF_PAYLOAD_START];
    const uint8_t *pTag        = &in_buf[MSCB_OFF_PAYLOAD_START + payloadLen];
    memcpy(out_pkt->auth_tag, pTag, MSCB_AUTH_TAG_SIZE);

    // Step 6: Decryption & Authentication
    if (out_pkt->flags & MSCB_FLAG_ENCRYPTED) {
        if (!aesKey) return MSCB_DECODE_ERR_AUTH_FAILED;

        // Anti-replay pre-check
        if (enforceReplayCheck) {
            bool replayOk = replayProtector.checkReplay(out_pkt->src_id,
                                                        out_pkt->boot_session_id,
                                                        out_pkt->seq_num);
            if (!replayOk) {
                return MSCB_DECODE_ERR_REPLAY_DETECTED;
            }
        }

        uint8_t aad[MSCB_AAD_SIZE];
        mscb_build_aad(version, networkId, out_pkt->flags, msgType,
                       out_pkt->src_id, out_pkt->dst_id,
                       out_pkt->boot_session_id, out_pkt->seq_num,
                       payloadLen, aad);

        uint8_t nonce[MSCB_CRYPTO_NONCE_SIZE];
        mscb_build_nonce(out_pkt->src_id, out_pkt->boot_session_id,
                         out_pkt->seq_num, msgType, nonce);

        bool decOk = cryptoMgr.decrypt(aesKey, nonce, aad, MSCB_AAD_SIZE,
                                       pCiphertext, payloadLen, pTag,
                                       out_pkt->payload);
        if (!decOk) {
            return MSCB_DECODE_ERR_AUTH_FAILED;
        }

        // Commit sequence number to anti-replay sliding window only after AEAD passes
        if (enforceReplayCheck) {
            replayProtector.commit(out_pkt->src_id,
                                   out_pkt->boot_session_id,
                                   out_pkt->seq_num);
        }

        out_pkt->authenticated = true;
    } else {
        if (payloadLen > 0) {
            memcpy(out_pkt->payload, pCiphertext, payloadLen);
        }
        out_pkt->authenticated = false;
    }

    return MSCB_DECODE_OK;
}

bool PacketCodec::updateForwarding(uint8_t *frame_buf,
                                   size_t frame_len,
                                   uint16_t new_next_hop) {
    if (!frame_buf || frame_len < MSCB_MIN_PACKET_LEN) return false;

    uint8_t hopLimit = frame_buf[MSCB_OFF_HOP_LIMIT];
    if (hopLimit <= 1) {
        // TTL expired
        return false;
    }

    // Update mutable forwarding fields
    frame_buf[MSCB_OFF_HOP_LIMIT] = hopLimit - 1;
    frame_buf[MSCB_OFF_HOP_COUNT] = frame_buf[MSCB_OFF_HOP_COUNT] + 1;
    mscb_write_u16_be(&frame_buf[MSCB_OFF_NEXT_HOP], new_next_hop);

    // Recompute CRC16 over modified frame (excluding the 2-byte CRC itself)
    size_t crcOffset = frame_len - MSCB_RADIO_CRC_SIZE;
    uint16_t newCrc = cryptoMgr.calculateCRC16(frame_buf, crcOffset);
    mscb_write_u16_be(&frame_buf[crcOffset], newCrc);

    return true;
}
