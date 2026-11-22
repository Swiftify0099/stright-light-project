/*
 * =====================================================================
 *  TEST_PROTOCOL.CPP — MSCB PROTOCOL 15-POINT COMPREHENSIVE TEST SUITE
 *  Tests: Serialization, AEAD Auth, Replay Protection, Bounds, Cross-Compat
 * =====================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../Sender/NetworkTypes.h"
#include "../Sender/Protocol.h"
#include "../Sender/CryptoManager.h"
#include "../Sender/ReplayProtection.h"
#include "../Sender/PacketCodec.h"

// Test status counters
static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
    } \
} while(0)

// Standard Test Keys (128-bit)
static const uint8_t TEST_KEY_GATEWAY[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

static const uint8_t TEST_KEY_NODE_2[16] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F
};

static const uint8_t TEST_KEY_WRONG[16] = {
    0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
    0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00
};

// ─────────────────────────────────────────────────────────────
//  TEST 1: PACKET SERIALIZATION
// ─────────────────────────────────────────────────────────────
void test_01_serialization() {
    printf("\n--- TEST 01: PACKET SERIALIZATION ---\n");
    cryptoMgr.init();
    replayProtector.init();

    uint8_t payload[] = "MSCB_RELAY_CMD_ON";
    uint8_t payloadLen = (uint8_t)strlen((char*)payload);
    uint8_t outBuf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t outLen = 0;

    bool ok = packetCodec.encodePacket(
        MSCB_MSG_COMMAND,
        MSCB_NODE_ID_GATEWAY,
        0x0002, // Node 2
        0x0002, // Next hop
        MSCB_DEFAULT_TTL,
        0,      // Hop count
        1001,   // Boot session
        1,      // Sequence
        MSCB_FLAG_ENCRYPTED | MSCB_FLAG_ACK_REQ,
        payload,
        payloadLen,
        TEST_KEY_NODE_2,
        outBuf,
        &outLen
    );

    TEST_ASSERT(ok == true, "encodePacket returns success");
    TEST_ASSERT(outLen == (size_t)(MSCB_HEADER_SIZE + payloadLen + MSCB_AUTH_TAG_SIZE + MSCB_RADIO_CRC_SIZE),
                "Output buffer length matches exact expected frame size");
}

// ─────────────────────────────────────────────────────────────
//  TEST 2: PACKET DESERIALIZATION
// ─────────────────────────────────────────────────────────────
void test_02_deserialization() {
    printf("\n--- TEST 02: PACKET DESERIALIZATION ---\n");
    replayProtector.init();

    uint8_t payload[] = "TELEMETRY_SAMPLE_DATA";
    uint8_t payloadLen = (uint8_t)strlen((char*)payload);
    uint8_t outBuf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t outLen = 0;

    packetCodec.encodePacket(
        MSCB_MSG_TELEMETRY,
        0x0002,
        MSCB_NODE_ID_GATEWAY,
        MSCB_NODE_ID_GATEWAY,
        MSCB_DEFAULT_TTL,
        1,
        2002,
        15,
        MSCB_FLAG_ENCRYPTED,
        payload,
        payloadLen,
        TEST_KEY_NODE_2,
        outBuf,
        &outLen
    );

    MscbParsedPacket parsed;
    MscbDecodeStatus status = packetCodec.decodePacket(
        outBuf, outLen, TEST_KEY_NODE_2, &parsed, -65, 8
    );

    TEST_ASSERT(status == MSCB_DECODE_OK, "decodePacket status is MSCB_DECODE_OK");
    TEST_ASSERT(parsed.protocol_version == MSCB_PROTOCOL_VERSION, "Parsed protocol version matches");
    TEST_ASSERT(parsed.network_id == MSCB_NETWORK_ID_DEFAULT, "Parsed network ID matches");
    TEST_ASSERT(parsed.msg_type == MSCB_MSG_TELEMETRY, "Parsed message type matches");
    TEST_ASSERT(parsed.src_id == 0x0002, "Parsed source node ID matches");
    TEST_ASSERT(parsed.dst_id == MSCB_NODE_ID_GATEWAY, "Parsed destination node ID matches");
    TEST_ASSERT(parsed.next_hop == MSCB_NODE_ID_GATEWAY, "Parsed next hop matches");
    TEST_ASSERT(parsed.hop_limit == MSCB_DEFAULT_TTL, "Parsed hop limit matches");
    TEST_ASSERT(parsed.hop_count == 1, "Parsed hop count matches");
    TEST_ASSERT(parsed.boot_session_id == 2002, "Parsed boot session matches");
    TEST_ASSERT(parsed.seq_num == 15, "Parsed sequence number matches");
    TEST_ASSERT(parsed.payload_len == payloadLen, "Parsed payload length matches");
    TEST_ASSERT(memcmp(parsed.payload, payload, payloadLen) == 0, "Decrypted payload content matches original");
    TEST_ASSERT(parsed.authenticated == true, "Authenticated flag is true");
}

// ─────────────────────────────────────────────────────────────
//  TEST 3: BYTE ORDERING (BIG-ENDIAN VERIFICATION)
// ─────────────────────────────────────────────────────────────
void test_03_byte_ordering() {
    printf("\n--- TEST 03: BYTE ORDERING ---\n");
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    packetCodec.encodePacket(
        MSCB_MSG_HEARTBEAT,
        0x1234, // src
        0x5678, // dst
        0x9ABC, // next_hop
        5, 0,
        0xA1B2C3D4, // session
        0x01020304, // seq
        MSCB_FLAG_NONE,
        NULL, 0, NULL, buf, &len
    );

    // Verify network byte order (Big-Endian) at exact offsets
    TEST_ASSERT(buf[MSCB_OFF_NETWORK_ID] == 0x4D && buf[MSCB_OFF_NETWORK_ID + 1] == 0x53,
                "Network ID serialized Big-Endian (0x4D, 0x53)");
    TEST_ASSERT(buf[MSCB_OFF_SRC_ID] == 0x12 && buf[MSCB_OFF_SRC_ID + 1] == 0x34,
                "Source ID 0x1234 serialized Big-Endian (0x12, 0x34)");
    TEST_ASSERT(buf[MSCB_OFF_DST_ID] == 0x56 && buf[MSCB_OFF_DST_ID + 1] == 0x78,
                "Destination ID 0x5678 serialized Big-Endian (0x56, 0x78)");
    TEST_ASSERT(buf[MSCB_OFF_NEXT_HOP] == 0x9A && buf[MSCB_OFF_NEXT_HOP + 1] == 0xBC,
                "Next Hop 0x9ABC serialized Big-Endian (0x9A, 0xBC)");
    TEST_ASSERT(buf[MSCB_OFF_BOOT_SESSION] == 0xA1 && buf[MSCB_OFF_BOOT_SESSION + 1] == 0xB2 &&
                buf[MSCB_OFF_BOOT_SESSION + 2] == 0xC3 && buf[MSCB_OFF_BOOT_SESSION + 3] == 0xD4,
                "Boot Session 0xA1B2C3D4 serialized Big-Endian (0xA1, 0xB2, 0xC3, 0xD4)");
    TEST_ASSERT(buf[MSCB_OFF_SEQ_NUM] == 0x01 && buf[MSCB_OFF_SEQ_NUM + 1] == 0x02 &&
                buf[MSCB_OFF_SEQ_NUM + 2] == 0x03 && buf[MSCB_OFF_SEQ_NUM + 3] == 0x04,
                "Sequence 0x01020304 serialized Big-Endian (0x01, 0x02, 0x03, 0x04)");
}

// ─────────────────────────────────────────────────────────────
//  TEST 4: PAYLOAD LENGTH VALIDATION
// ─────────────────────────────────────────────────────────────
void test_04_payload_length_validation() {
    printf("\n--- TEST 04: PAYLOAD LENGTH VALIDATION ---\n");
    replayProtector.init();

    uint8_t payload[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    packetCodec.encodePacket(
        MSCB_MSG_TELEMETRY, 0x0002, 0x0001, 0x0001, 5, 0,
        100, 1, MSCB_FLAG_NONE, payload, 10, NULL, buf, &len
    );

    // Tamper with payload length byte (offset 21)
    buf[MSCB_OFF_PAYLOAD_LEN] = 25; // mismatch declared vs actual
    // Recompute CRC so length check itself is tested
    size_t crcOff = len - MSCB_RADIO_CRC_SIZE;
    uint16_t newCrc = cryptoMgr.calculateCRC16(buf, crcOff);
    mscb_write_u16_be(&buf[crcOff], newCrc);

    MscbParsedPacket parsed;
    MscbDecodeStatus status = packetCodec.decodePacket(buf, len, NULL, &parsed);
    TEST_ASSERT(status == MSCB_DECODE_ERR_LENGTH_MISMATCH,
                "Decoder rejects packet with declared payload length mismatch");
}

// ─────────────────────────────────────────────────────────────
//  TEST 5: INVALID HEADER (VERSION & NETWORK ID)
// ─────────────────────────────────────────────────────────────
void test_05_invalid_header() {
    printf("\n--- TEST 05: INVALID HEADER VALIDATION ---\n");
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    packetCodec.encodePacket(
        MSCB_MSG_HEARTBEAT, 0x0002, 0x0001, 0x0001, 5, 0,
        100, 1, MSCB_FLAG_NONE, NULL, 0, NULL, buf, &len
    );

    // Corrupt version byte to 0x99
    uint8_t badVerBuf[MSCB_RADIO_FIFO_MAX_LEN];
    memcpy(badVerBuf, buf, len);
    badVerBuf[MSCB_OFF_VERSION] = 0x99;
    uint16_t newCrc = cryptoMgr.calculateCRC16(badVerBuf, len - MSCB_RADIO_CRC_SIZE);
    mscb_write_u16_be(&badVerBuf[len - MSCB_RADIO_CRC_SIZE], newCrc);

    MscbParsedPacket parsed;
    MscbDecodeStatus stVer = packetCodec.decodePacket(badVerBuf, len, NULL, &parsed);
    TEST_ASSERT(stVer == MSCB_DECODE_ERR_BAD_VERSION, "Decoder rejects unsupported protocol version");

    // Corrupt network ID
    uint8_t badNetBuf[MSCB_RADIO_FIFO_MAX_LEN];
    memcpy(badNetBuf, buf, len);
    mscb_write_u16_be(&badNetBuf[MSCB_OFF_NETWORK_ID], 0x1234);
    newCrc = cryptoMgr.calculateCRC16(badNetBuf, len - MSCB_RADIO_CRC_SIZE);
    mscb_write_u16_be(&badNetBuf[len - MSCB_RADIO_CRC_SIZE], newCrc);

    MscbDecodeStatus stNet = packetCodec.decodePacket(badNetBuf, len, NULL, &parsed);
    TEST_ASSERT(stNet == MSCB_DECODE_ERR_BAD_NETWORK, "Decoder rejects foreign network ID");
}

// ─────────────────────────────────────────────────────────────
//  TEST 6: CRC VALIDATION (RADIO BIT-FLIP INTEGRITY)
// ─────────────────────────────────────────────────────────────
void test_06_crc_validation() {
    printf("\n--- TEST 06: CRC VALIDATION ---\n");
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    packetCodec.encodePacket(
        MSCB_MSG_HEARTBEAT, 0x0002, 0x0001, 0x0001, 5, 0,
        100, 1, MSCB_FLAG_NONE, NULL, 0, NULL, buf, &len
    );

    // Flip single bit in header without updating CRC
    buf[MSCB_OFF_FLAGS] ^= 0x01;

    MscbParsedPacket parsed;
    MscbDecodeStatus status = packetCodec.decodePacket(buf, len, NULL, &parsed);
    TEST_ASSERT(status == MSCB_DECODE_ERR_CRC_FAILED,
                "Decoder detects single-bit transmission corruption via CRC16");
}

// ─────────────────────────────────────────────────────────────
//  TEST 7: AUTHENTICATION SUCCESS
// ─────────────────────────────────────────────────────────────
void test_07_auth_success() {
    printf("\n--- TEST 07: AUTHENTICATION SUCCESS ---\n");
    replayProtector.init();

    uint8_t payload[] = "SECRET_CONTROL_COMMAND_RELAY_3_ON";
    uint8_t payloadLen = (uint8_t)strlen((char*)payload);
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    packetCodec.encodePacket(
        MSCB_MSG_COMMAND, 0x0001, 0x0002, 0x0002, 5, 0,
        500, 1, MSCB_FLAG_ENCRYPTED, payload, payloadLen, TEST_KEY_NODE_2,
        buf, &len
    );

    MscbParsedPacket parsed;
    MscbDecodeStatus status = packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed);
    TEST_ASSERT(status == MSCB_DECODE_OK, "Authentication succeeds with matching key");
    TEST_ASSERT(parsed.authenticated == true, "Packet flagged as successfully authenticated");
    TEST_ASSERT(memcmp(parsed.payload, payload, payloadLen) == 0, "Plaintext exactly matches");
}

// ─────────────────────────────────────────────────────────────
//  TEST 8: AUTHENTICATION FAILURE (WRONG KEY)
// ─────────────────────────────────────────────────────────────
void test_08_auth_failure() {
    printf("\n--- TEST 08: AUTHENTICATION FAILURE ---\n");
    replayProtector.init();

    uint8_t payload[] = "CRITICAL_PAYLOAD";
    uint8_t payloadLen = (uint8_t)strlen((char*)payload);
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    // Encrypted with Node 2 Key
    packetCodec.encodePacket(
        MSCB_MSG_COMMAND, 0x0001, 0x0002, 0x0002, 5, 0,
        500, 1, MSCB_FLAG_ENCRYPTED, payload, payloadLen, TEST_KEY_NODE_2,
        buf, &len
    );

    // Attempt decode with WRONG Key
    MscbParsedPacket parsed;
    MscbDecodeStatus status = packetCodec.decodePacket(buf, len, TEST_KEY_WRONG, &parsed);
    TEST_ASSERT(status == MSCB_DECODE_ERR_AUTH_FAILED, "Decoder rejects packet decrypted with wrong key");
    TEST_ASSERT(parsed.authenticated == false, "Packet is NOT authenticated");
    TEST_ASSERT(parsed.payload[0] == 0, "Plaintext buffer is securely zeroized on auth failure");
}

// ─────────────────────────────────────────────────────────────
//  TEST 9: MODIFIED PACKET (CIPHERTEXT TAMPER DETECTION)
// ─────────────────────────────────────────────────────────────
void test_09_modified_packet() {
    printf("\n--- TEST 9: MODIFIED PACKET / TAMPER DETECTION ---\n");
    replayProtector.init();

    uint8_t payload[] = "TAMPER_RESISTANCE_TEST";
    uint8_t payloadLen = (uint8_t)strlen((char*)payload);
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    packetCodec.encodePacket(
        MSCB_MSG_COMMAND, 0x0001, 0x0002, 0x0002, 5, 0,
        500, 1, MSCB_FLAG_ENCRYPTED, payload, payloadLen, TEST_KEY_NODE_2,
        buf, &len
    );

    // Tamper with 1 byte of encrypted ciphertext in transit
    buf[MSCB_OFF_PAYLOAD_START + 2] ^= 0x40;

    // Recompute CRC so radio check passes, testing AEAD cryptographic tag integrity
    size_t crcOff = len - MSCB_RADIO_CRC_SIZE;
    uint16_t newCrc = cryptoMgr.calculateCRC16(buf, crcOff);
    mscb_write_u16_be(&buf[crcOff], newCrc);

    MscbParsedPacket parsed;
    MscbDecodeStatus status = packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed);
    TEST_ASSERT(status == MSCB_DECODE_ERR_AUTH_FAILED,
                "AEAD tag detects payload tampering even if CRC is manipulated");
}

// ─────────────────────────────────────────────────────────────
//  TEST 10: REPLAY PACKET
// ─────────────────────────────────────────────────────────────
void test_10_replay_packet() {
    printf("\n--- TEST 10: REPLAY PACKET MITIGATION ---\n");
    replayProtector.init();

    uint8_t payload[] = "TOGGLE_RELAY";
    uint8_t payloadLen = (uint8_t)strlen((char*)payload);
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    packetCodec.encodePacket(
        MSCB_MSG_COMMAND, 0x0001, 0x0002, 0x0002, 5, 0,
        800, 10, MSCB_FLAG_ENCRYPTED, payload, payloadLen, TEST_KEY_NODE_2,
        buf, &len
    );

    // First arrival: Legitimate
    MscbParsedPacket parsed;
    MscbDecodeStatus st1 = packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed);
    TEST_ASSERT(st1 == MSCB_DECODE_OK, "First packet accepted successfully");

    // Second arrival: Adversary replaying recorded frame
    MscbDecodeStatus st2 = packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed);
    TEST_ASSERT(st2 == MSCB_DECODE_ERR_REPLAY_DETECTED, "Replayed packet immediately rejected by anti-replay window");
}

// ─────────────────────────────────────────────────────────────
//  TEST 11: DUPLICATE SEQUENCE IN SLIDING WINDOW
// ─────────────────────────────────────────────────────────────
void test_11_duplicate_sequence() {
    printf("\n--- TEST 11: DUPLICATE SEQUENCE IN SLIDING WINDOW ---\n");
    replayProtector.init();

    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;
    MscbParsedPacket parsed;

    // Send Seq 1
    packetCodec.encodePacket(MSCB_MSG_TELEMETRY, 0x0002, 1, 1, 5, 0, 900, 1, MSCB_FLAG_ENCRYPTED, NULL, 0, TEST_KEY_NODE_2, buf, &len);
    TEST_ASSERT(packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed) == MSCB_DECODE_OK, "Seq 1 accepted");

    // Send Seq 5 (window advances by 4)
    packetCodec.encodePacket(MSCB_MSG_TELEMETRY, 0x0002, 1, 1, 5, 0, 900, 5, MSCB_FLAG_ENCRYPTED, NULL, 0, TEST_KEY_NODE_2, buf, &len);
    TEST_ASSERT(packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed) == MSCB_DECODE_OK, "Seq 5 accepted (window moved)");

    // Send Seq 3 (out of order, within window of 64)
    packetCodec.encodePacket(MSCB_MSG_TELEMETRY, 0x0002, 1, 1, 5, 0, 900, 3, MSCB_FLAG_ENCRYPTED, NULL, 0, TEST_KEY_NODE_2, buf, &len);
    TEST_ASSERT(packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed) == MSCB_DECODE_OK, "Seq 3 accepted (valid out-of-order in window)");

    // Re-send Seq 3 (duplicate out-of-order packet)
    TEST_ASSERT(packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed) == MSCB_DECODE_ERR_REPLAY_DETECTED,
                "Duplicate Seq 3 rejected by window bitmask");
}

// ─────────────────────────────────────────────────────────────
//  TEST 12: REBOOT SESSION HANDLING
// ─────────────────────────────────────────────────────────────
void test_12_reboot_session_handling() {
    printf("\n--- TEST 12: REBOOT SESSION HANDLING ---\n");
    replayProtector.init();

    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;
    MscbParsedPacket parsed;

    // Session 1: Sequence reaches 50
    packetCodec.encodePacket(MSCB_MSG_TELEMETRY, 0x0002, 1, 1, 5, 0, 100, 50, MSCB_FLAG_ENCRYPTED, NULL, 0, TEST_KEY_NODE_2, buf, &len);
    TEST_ASSERT(packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed) == MSCB_DECODE_OK, "Session 100, Seq 50 accepted");

    // Node reboots: New Session 101, Sequence resets to 1!
    packetCodec.encodePacket(MSCB_MSG_TELEMETRY, 0x0002, 1, 1, 5, 0, 101, 1, MSCB_FLAG_ENCRYPTED, NULL, 0, TEST_KEY_NODE_2, buf, &len);
    TEST_ASSERT(packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed) == MSCB_DECODE_OK,
                "Reboot Session 101 accepted with reset Seq 1");

    // Old Session 100 packet arrives late
    packetCodec.encodePacket(MSCB_MSG_TELEMETRY, 0x0002, 1, 1, 5, 0, 100, 60, MSCB_FLAG_ENCRYPTED, NULL, 0, TEST_KEY_NODE_2, buf, &len);
    TEST_ASSERT(packetCodec.decodePacket(buf, len, TEST_KEY_NODE_2, &parsed) == MSCB_DECODE_ERR_REPLAY_DETECTED,
                "Old Session 100 packet rejected after newer Session 101 established");
}

// ─────────────────────────────────────────────────────────────
//  TEST 13: INVALID MESSAGE TYPE
// ─────────────────────────────────────────────────────────────
void test_13_invalid_message_type() {
    printf("\n--- TEST 13: INVALID MESSAGE TYPE ---\n");
    uint8_t buf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t len = 0;

    packetCodec.encodePacket(
        0x55, // Invalid opcode
        0x0002, 0x0001, 0x0001, 5, 0, 100, 1, MSCB_FLAG_NONE, NULL, 0, NULL, buf, &len
    );

    MscbParsedPacket parsed;
    MscbDecodeStatus status = packetCodec.decodePacket(buf, len, NULL, &parsed);
    TEST_ASSERT(status == MSCB_DECODE_ERR_BAD_MSG_TYPE, "Decoder rejects undefined message opcode");
}

// ─────────────────────────────────────────────────────────────
//  TEST 14: OVERSIZED PACKET REJECTION
// ─────────────────────────────────────────────────────────────
void test_14_oversized_packet() {
    printf("\n--- TEST 14: OVERSIZED PACKET REJECTION ---\n");
    uint8_t bigPayload[230];
    memset(bigPayload, 0xAA, sizeof(bigPayload));
    uint8_t buf[300];
    size_t len = 0;

    // Attempt encode with payload exceeding max allowable (215 bytes)
    bool ok = packetCodec.encodePacket(
        MSCB_MSG_TELEMETRY, 0x0002, 1, 1, 5, 0, 100, 1,
        MSCB_FLAG_ENCRYPTED, bigPayload, 220, TEST_KEY_NODE_2, buf, &len
    );
    TEST_ASSERT(ok == false, "Encoder rejects payload exceeding 215 bytes (LoRa 255-byte FIFO limit)");

    // Attempt decode with buffer > 255 bytes
    MscbParsedPacket parsed;
    MscbDecodeStatus status = packetCodec.decodePacket(buf, 260, TEST_KEY_NODE_2, &parsed);
    TEST_ASSERT(status == MSCB_DECODE_ERR_SIZE_OVERFLOW, "Decoder rejects frames exceeding 255 bytes");
}

// ─────────────────────────────────────────────────────────────
//  TEST 15: CROSS-COMPATIBILITY BETWEEN GATEWAY AND NODE
// ─────────────────────────────────────────────────────────────
void test_15_cross_compatibility() {
    printf("\n--- TEST 15: CROSS-COMPATIBILITY (GATEWAY <-> NODE) ---\n");
    replayProtector.init();

    // 1. Gateway encodes Command for Pole Node 2
    MscbCommandPayload cmd;
    cmd.command_id = 42;
    cmd.command_code = MSCB_CMD_RELAY_ON;
    cmd.feeder_mask = 0x07; // Feeders 1, 2, 3
    cmd.duration_sec = 3600; // 1 hour
    cmd.param = 0;

    uint8_t gwToNodeBuf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t gwToNodeLen = 0;

    bool encGw = packetCodec.encodePacket(
        MSCB_MSG_COMMAND,
        MSCB_NODE_ID_GATEWAY,
        0x0002,
        0x0002,
        MSCB_DEFAULT_TTL,
        0,
        1001,
        1,
        MSCB_FLAG_ENCRYPTED | MSCB_FLAG_ACK_REQ,
        (const uint8_t*)&cmd,
        sizeof(cmd),
        TEST_KEY_NODE_2,
        gwToNodeBuf,
        &gwToNodeLen
    );
    TEST_ASSERT(encGw == true, "Gateway successfully encodes command with Node 2 key");

    // Intermediate Node forwards packet (Mutable update)
    bool fwdOk = packetCodec.updateForwarding(gwToNodeBuf, gwToNodeLen, 0x0003);
    TEST_ASSERT(fwdOk == true, "Mesh router successfully decrements TTL and updates Next Hop");
    TEST_ASSERT(gwToNodeBuf[MSCB_OFF_HOP_LIMIT] == (MSCB_DEFAULT_TTL - 1), "TTL decremented by 1");
    TEST_ASSERT(gwToNodeBuf[MSCB_OFF_HOP_COUNT] == 1, "Hop count incremented to 1");

    // Node 2 receives and decodes Gateway Command
    MscbParsedPacket nodeRx;
    MscbDecodeStatus decNode = packetCodec.decodePacket(
        gwToNodeBuf, gwToNodeLen, TEST_KEY_NODE_2, &nodeRx
    );
    TEST_ASSERT(decNode == MSCB_DECODE_OK, "Node 2 successfully decodes and authenticates forwarded command");
    TEST_ASSERT(nodeRx.authenticated == true, "Node 2 confirms AEAD authentication tag");

    MscbCommandPayload *pRxCmd = (MscbCommandPayload*)nodeRx.payload;
    TEST_ASSERT(pRxCmd->command_id == 42, "Received command ID matches");
    TEST_ASSERT(pRxCmd->command_code == MSCB_CMD_RELAY_ON, "Received command code matches");
    TEST_ASSERT(pRxCmd->feeder_mask == 0x07, "Received feeder mask matches");
    TEST_ASSERT(pRxCmd->duration_sec == 3600, "Received duration matches");

    // 2. Node 2 prepares 83-byte full electrical telemetry payload
    MscbPoleTelemetryPayload telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.pole_id = 0x0002;
    telemetry.timestamp = 1718000000;
    telemetry.measurement_seq = 100;
    telemetry.voltage_r_dV = 2305; // 230.5 V
    telemetry.voltage_y_dV = 2312; // 231.2 V
    telemetry.voltage_b_dV = 2298; // 229.8 V
    telemetry.voltage_validity = MSCB_PHASE_R_VALID | MSCB_PHASE_Y_VALID | MSCB_PHASE_B_VALID;
    telemetry.pole_fault_flags = MSCB_POLE_FAULT_NONE;

    for (int f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
        telemetry.feeders[f].feeder_id = f + 1;
        telemetry.feeders[f].current_r_cA = (uint16_t)(1050 + f * 100); // 10.50 A ...
        telemetry.feeders[f].current_y_cA = (uint16_t)(1120 + f * 100);
        telemetry.feeders[f].current_b_cA = (uint16_t)(1080 + f * 100);
        telemetry.feeders[f].current_validity = MSCB_PHASE_R_VALID | MSCB_PHASE_Y_VALID | MSCB_PHASE_B_VALID;
        telemetry.feeders[f].feeder_status = MSCB_FEEDER_STATUS_RELAY_ON | MSCB_FEEDER_STATUS_PROTECT;
        telemetry.feeders[f].mcb_status = MSCB_MCB_STATUS_R_OK | MSCB_MCB_STATUS_Y_OK | MSCB_MCB_STATUS_B_OK;
        telemetry.feeders[f].feeder_fault = MSCB_FEEDER_FAULT_NONE;
    }

    uint8_t nodeToGwBuf[MSCB_RADIO_FIFO_MAX_LEN];
    size_t nodeToGwLen = 0;

    bool encNode = packetCodec.encodePacket(
        MSCB_MSG_TELEMETRY,
        0x0002,
        MSCB_NODE_ID_GATEWAY,
        MSCB_NODE_ID_GATEWAY,
        MSCB_DEFAULT_TTL,
        0,
        2002,
        1,
        MSCB_FLAG_ENCRYPTED,
        (const uint8_t*)&telemetry,
        sizeof(telemetry), // 83 bytes
        TEST_KEY_NODE_2,
        nodeToGwBuf,
        &nodeToGwLen
    );
    TEST_ASSERT(encNode == true, "Node 2 successfully encodes 83-byte full electrical telemetry");
    TEST_ASSERT(nodeToGwLen == (size_t)(MSCB_HEADER_SIZE + 83 + MSCB_AUTH_TAG_SIZE + MSCB_RADIO_CRC_SIZE), // 123 bytes
                "Total over-the-air packet length is exactly 123 bytes (< 255 LoRa limit)");

    // Gateway receives and decodes Node 2 telemetry
    MscbParsedPacket gwRx;
    MscbDecodeStatus decGw = packetCodec.decodePacket(
        nodeToGwBuf, nodeToGwLen, TEST_KEY_NODE_2, &gwRx
    );
    TEST_ASSERT(decGw == MSCB_DECODE_OK, "Gateway successfully decodes Node 2 telemetry");
    TEST_ASSERT(gwRx.authenticated == true, "Gateway verifies AEAD authentication");

    MscbPoleTelemetryPayload *pRxTelem = (MscbPoleTelemetryPayload*)gwRx.payload;
    TEST_ASSERT(pRxTelem->pole_id == 0x0002, "Telemetry Pole ID matches");
    TEST_ASSERT(pRxTelem->voltage_r_dV == 2305, "Phase R Voltage matches 230.5 V");
    TEST_ASSERT(pRxTelem->voltage_y_dV == 2312, "Phase Y Voltage matches 231.2 V");
    TEST_ASSERT(pRxTelem->voltage_b_dV == 2298, "Phase B Voltage matches 229.8 V");
    TEST_ASSERT(pRxTelem->feeders[0].current_r_cA == 1050, "Feeder 1 Phase R Current matches 10.50 A");
    TEST_ASSERT(pRxTelem->feeders[5].current_b_cA == 1580, "Feeder 6 Phase B Current matches 15.80 A");
    TEST_ASSERT(pRxTelem->feeders[0].mcb_status == (MSCB_MCB_STATUS_R_OK | MSCB_MCB_STATUS_Y_OK | MSCB_MCB_STATUS_B_OK),
                "All 3 MCB phase statuses match");
}

int main() {
    printf("=====================================================================\n");
    printf(" MSCB SECURE PROTOCOL 15-POINT COMPREHENSIVE VERIFICATION SUITE\n");
    printf("=====================================================================\n");

    test_01_serialization();
    test_02_deserialization();
    test_03_byte_ordering();
    test_04_payload_length_validation();
    test_05_invalid_header();
    test_06_crc_validation();
    test_07_auth_success();
    test_08_auth_failure();
    test_09_modified_packet();
    test_10_replay_packet();
    test_11_duplicate_sequence();
    test_12_reboot_session_handling();
    test_13_invalid_message_type();
    test_14_oversized_packet();
    test_15_cross_compatibility();

    printf("\n=====================================================================\n");
    printf(" TEST RESULTS SUMMARY: %d / %d Tests Passed (%.1f%%)\n",
           g_tests_passed, g_tests_run, (float)g_tests_passed * 100.0f / (float)g_tests_run);
    printf("=====================================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
