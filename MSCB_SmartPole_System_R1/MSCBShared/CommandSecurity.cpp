#include "CommandSecurity.h"
#include <string.h>

#if defined(ARDUINO) && defined(ESP32)
  #if __has_include(<mbedtls/sha256.h>)
    #include <mbedtls/md.h>
    #define MSCB_USE_LEGACY_MBEDTLS 1
  #elif __has_include(<psa/crypto.h>)
    #include <psa/crypto.h>
    #define MSCB_USE_PSA_CRYPTO 1
  #endif
#endif

CommandSecurity cmdSecurity;

CommandSecurity::CommandSecurity() : activeKeyEpoch(1), keyProvisioned(false) {
  memset(provisionedKey, 0, sizeof(provisionedKey));
}

bool CommandSecurity::provisionKey(const uint8_t *keyBytes, size_t keyLen, uint32_t epoch) {
  if (!keyBytes || keyLen != MSCB_KEY_LEN_BYTES || epoch == 0) {
    return false;
  }
  memcpy(provisionedKey, keyBytes, MSCB_KEY_LEN_BYTES);
  activeKeyEpoch = epoch;
  keyProvisioned = true;
  return true;
}

void CommandSecurity::revokeKey() {
  memset(provisionedKey, 0, sizeof(provisionedKey));
  keyProvisioned = false;
  activeKeyEpoch = 0;
}

bool CommandSecurity::constantTimeCompare(const uint8_t *a, const uint8_t *b, size_t len) {
  if (!a || !b) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) {
    diff |= (a[i] ^ b[i]);
  }
  return diff == 0;
}

void CommandSecurity::computeHmacSha256(
    const uint8_t *key, size_t keyLen,
    const uint8_t *data, size_t dataLen,
    uint8_t outHmac[MSCB_HMAC_TAG_BYTES]) {
  if (!key || !data || !outHmac) return;

#if defined(ARDUINO) && defined(ESP32)
#if defined(MSCB_USE_PSA_CRYPTO)
  psa_crypto_init();
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
  psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
  psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);

  mbedtls_svc_key_id_t key_id = 0;
  psa_status_t status = psa_import_key(&attributes, key, keyLen, &key_id);
  if (status == PSA_SUCCESS) {
    size_t mac_length = 0;
    psa_mac_compute(key_id, PSA_ALG_HMAC(PSA_ALG_SHA_256), data, dataLen, outHmac, MSCB_HMAC_TAG_BYTES, &mac_length);
    psa_destroy_key(key_id);
  }
#elif defined(MSCB_USE_LEGACY_MBEDTLS)
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, key, keyLen);
  mbedtls_md_hmac_update(&ctx, data, dataLen);
  mbedtls_md_hmac_finish(&ctx, outHmac);
  mbedtls_md_free(&ctx);
#endif
#else
  // Original compact keyed integrity tag for host simulation / unit test runner
  uint32_t state[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
  };
  for (size_t i = 0; i < keyLen; ++i) {
    state[i % 8] = (state[i % 8] ^ key[i]) * 0x01000193 + 0x55;
  }
  for (size_t i = 0; i < dataLen; ++i) {
    state[i % 8] = (state[(i + 1) % 8] ^ data[i]) * 0x01000193 + 0xAA;
  }
  memcpy(outHmac, state, MSCB_HMAC_TAG_BYTES);
#endif
}

bool CommandSecurity::createCommand(
    uint16_t srcAddr,
    uint16_t dstAddr,
    uint8_t  feederId,
    uint8_t  cmd,
    uint8_t  cmdParam,
    uint32_t expiryEpochSec,
    AuthenticatedCommandEnvelope &outEnvelope) {
  // Fail-closed rule: refuse creation if key is not provisioned
  if (!keyProvisioned) {
    return false;
  }

  uint32_t seq = replayProtector.getNextMonotonicSeq();
  if (seq == 0 && !replayProtector.isTransmitterStorageReady()) {
    return false;
  }

  outEnvelope.networkId = MSCB_CMD_NETWORK_MAGIC;
  outEnvelope.sourceAddress = srcAddr;
  outEnvelope.destAddress = dstAddr;
  outEnvelope.feederId = feederId;
  outEnvelope.command = cmd;
  outEnvelope.commandParam = cmdParam;
  outEnvelope.sequenceNumber = seq;
  outEnvelope.expiryEpochSec = expiryEpochSec;
  outEnvelope.keyEpoch = activeKeyEpoch;

  // Compute HMAC over the 19 header bytes
  computeHmacSha256(
    provisionedKey, MSCB_KEY_LEN_BYTES,
    reinterpret_cast<const uint8_t*>(&outEnvelope), MSCB_ENVELOPE_HEADER_BYTES,
    outEnvelope.hmacTag
  );
  return true;
}

CommandAuthResult CommandSecurity::verifyCommand(
    const AuthenticatedCommandEnvelope &envelope,
    uint16_t localNodeId,
    uint32_t currentEpochSec) {
  // 1. Strict Fail-Closed Check: provisioned key required
  if (!keyProvisioned) {
    return AUTH_ERR_NO_PROVISIONED_KEY;
  }

  // 2. Network ID
  if (envelope.networkId != MSCB_CMD_NETWORK_MAGIC) {
    return AUTH_ERR_NETWORK_ID_MISMATCH;
  }

  // 3. Destination Addressing
  if (envelope.destAddress != localNodeId && envelope.destAddress != MSCB_BROADCAST_NODE_ID) {
    return AUTH_ERR_DEST_MISMATCH;
  }

  // 4. Key Epoch Alignment
  if (envelope.keyEpoch != activeKeyEpoch) {
    return AUTH_ERR_KEY_EPOCH_MISMATCH;
  }

  // 5. Action Authorization & Absolute Expiry Check
  // Policy: ON commands strictly require synchronized UTC epoch (currentEpochSec >= 1700000000UL)
  // Safe de-energizing (OFF: commandParam == 0, or emergency shutdown) is permitted when unsynced,
  // protected by key provision check, valid HMAC tag, destination address, and monotonic anti-replay sequence!
  bool isEnergizingOn = (envelope.command == PKT_CMD_FEEDER_SWITCH && envelope.commandParam != 0);

  if (isEnergizingOn) {
    if (currentEpochSec < 1700000000UL) {
      return AUTH_ERR_TIME_UNSYNCED;
    }
    if (currentEpochSec > envelope.expiryEpochSec) {
      return AUTH_ERR_COMMAND_EXPIRED;
    }
  } else {
    // De-energizing OFF / Safe Shutdown:
    // If clock is synchronized, verify that command is not expired
    if (currentEpochSec >= 1700000000UL && currentEpochSec > envelope.expiryEpochSec) {
      return AUTH_ERR_COMMAND_EXPIRED;
    }
  }

  // 6. Sliding Window Anti-Replay Check
  ReplayCheckResult rRes = replayProtector.checkPacket(
    envelope.sourceAddress, 0, envelope.keyEpoch, envelope.sequenceNumber
  );
  if (rRes != REPLAY_OK_NEW_PACKET) {
    return AUTH_ERR_REPLAY_DETECTED;
  }

  // 7. HMAC-SHA256 Authenticity & Integrity Check
  uint8_t expectedTag[MSCB_HMAC_TAG_BYTES];
  computeHmacSha256(
    provisionedKey, MSCB_KEY_LEN_BYTES,
    reinterpret_cast<const uint8_t*>(&envelope), MSCB_ENVELOPE_HEADER_BYTES,
    expectedTag
  );

  if (!constantTimeCompare(expectedTag, envelope.hmacTag, MSCB_HMAC_TAG_BYTES)) {
    return AUTH_ERR_HMAC_INVALID;
  }

  // 8. Commit Monotonic Sequence on Verification
  replayProtector.commitPacket(
    envelope.sourceAddress, 0, envelope.keyEpoch, envelope.sequenceNumber
  );

  return AUTH_SUCCESS;
}

bool CommandSecurity::createTimeSyncPacket(uint32_t epochSec, PktTimeSyncPayload &outPkt) {
  if (!keyProvisioned) return false;
  if (epochSec < 1700000000UL) return false;

  outPkt.networkId = MSCB_CMD_NETWORK_MAGIC;
  outPkt.gatewayNodeId = MSCB_GATEWAY_NODE_ID;
  outPkt.epochSec = epochSec;
  outPkt.sequenceNumber = replayProtector.getNextMonotonicSeq();
  outPkt.keyEpoch = activeKeyEpoch;

  computeHmacSha256(
    provisionedKey, MSCB_KEY_LEN_BYTES,
    reinterpret_cast<const uint8_t*>(&outPkt), MSCB_TIME_SYNC_HEADER_BYTES,
    outPkt.hmacTag
  );
  return true;
}

bool CommandSecurity::verifyTimeSyncPacket(const PktTimeSyncPayload &pkt) {
  if (!keyProvisioned) return false;
  if (pkt.networkId != MSCB_CMD_NETWORK_MAGIC) return false;
  if (pkt.gatewayNodeId != MSCB_GATEWAY_NODE_ID) return false;
  if (pkt.keyEpoch != activeKeyEpoch) return false;
  if (pkt.epochSec < 1700000000UL) return false;

  uint8_t expectedTag[MSCB_HMAC_TAG_BYTES];
  computeHmacSha256(
    provisionedKey, MSCB_KEY_LEN_BYTES,
    reinterpret_cast<const uint8_t*>(&pkt), MSCB_TIME_SYNC_HEADER_BYTES,
    expectedTag
  );

  return constantTimeCompare(expectedTag, pkt.hmacTag, MSCB_HMAC_TAG_BYTES);
}

bool CommandSecurity::createRevokeCommand(
    uint16_t targetPoleId,
    const uint8_t mac[6],
    const char* txId,
    const char* reason,
    uint32_t currentEpochSec,
    uint32_t expiryEpochSec,
    PktRevokeCmdPayload &outPkt) {
  if (!keyProvisioned) return false;

  outPkt.networkId = MSCB_CMD_NETWORK_MAGIC;
  outPkt.gatewayNodeId = MSCB_GATEWAY_NODE_ID;
  outPkt.targetPoleId = targetPoleId;
  if (mac) {
    memcpy(outPkt.macAddress, mac, 6);
  } else {
    memset(outPkt.macAddress, 0, 6);
  }
  outPkt.sequenceNumber = replayProtector.getNextMonotonicSeq();
  outPkt.keyEpoch = activeKeyEpoch;
  outPkt.timestamp = currentEpochSec;
  outPkt.expiryEpoch = expiryEpochSec;
  memset(outPkt.txId, 0, sizeof(outPkt.txId));
  if (txId) {
    strncpy(outPkt.txId, txId, sizeof(outPkt.txId) - 1);
  }
  memset(outPkt.reason, 0, sizeof(outPkt.reason));
  if (reason) {
    strncpy(outPkt.reason, reason, sizeof(outPkt.reason) - 1);
  }

  computeHmacSha256(
    provisionedKey, MSCB_KEY_LEN_BYTES,
    reinterpret_cast<const uint8_t*>(&outPkt), MSCB_REVOKE_AUTH_BYTES,
    outPkt.hmacTag
  );
  return true;
}

CommandAuthResult CommandSecurity::verifyRevokeCommand(
    const PktRevokeCmdPayload &pkt,
    uint16_t localNodeId,
    const uint8_t localMac[6],
    uint32_t currentEpochSec) {
  // 1. Fail-closed check
  if (!keyProvisioned) {
    return AUTH_ERR_NO_PROVISIONED_KEY;
  }

  // 2. Network ID check
  if (pkt.networkId != MSCB_CMD_NETWORK_MAGIC) {
    return AUTH_ERR_NETWORK_ID_MISMATCH;
  }

  // 3. Destination addressing check (Pole ID or MAC matching this node)
  bool idMatch = (pkt.targetPoleId == localNodeId || pkt.targetPoleId == MSCB_BROADCAST_NODE_ID);
  bool macMatch = (localMac != nullptr && memcmp(pkt.macAddress, localMac, 6) == 0);
  if (!idMatch && !macMatch) {
    return AUTH_ERR_DEST_MISMATCH;
  }

  // 4. Key Epoch Alignment
  if (pkt.keyEpoch != activeKeyEpoch) {
    return AUTH_ERR_KEY_EPOCH_MISMATCH;
  }

  // 5. Expiry Check (if clock is synchronized)
  if (currentEpochSec >= 1700000000UL && pkt.expiryEpoch > 0 && currentEpochSec > pkt.expiryEpoch) {
    return AUTH_ERR_COMMAND_EXPIRED;
  }

  // 6. Sliding Window Anti-Replay Check
  ReplayCheckResult rRes = replayProtector.checkPacket(
    pkt.gatewayNodeId, 0, pkt.keyEpoch, pkt.sequenceNumber
  );
  if (rRes != REPLAY_OK_NEW_PACKET) {
    return AUTH_ERR_REPLAY_DETECTED;
  }

  // 7. HMAC Authenticity Check (Reject all-zeros or missing auth tag!)
  bool allZero = true;
  for (size_t i = 0; i < MSCB_HMAC_TAG_BYTES; i++) {
    if (pkt.hmacTag[i] != 0) {
      allZero = false;
      break;
    }
  }
  if (allZero) {
    return AUTH_ERR_HMAC_INVALID;
  }

  uint8_t expectedTag[MSCB_HMAC_TAG_BYTES];
  computeHmacSha256(
    provisionedKey, MSCB_KEY_LEN_BYTES,
    reinterpret_cast<const uint8_t*>(&pkt), MSCB_REVOKE_AUTH_BYTES,
    expectedTag
  );

  if (!constantTimeCompare(expectedTag, pkt.hmacTag, MSCB_HMAC_TAG_BYTES)) {
    return AUTH_ERR_HMAC_INVALID;
  }

  // 8. Commit Monotonic Sequence on Verification
  replayProtector.commitPacket(
    pkt.gatewayNodeId, 0, pkt.keyEpoch, pkt.sequenceNumber
  );

  return AUTH_SUCCESS;
}

void CommandSecurity::getKeyFingerprint(char* outBuf, size_t maxLen) const {
  if (!outBuf || maxLen == 0) return;
  if (!keyProvisioned) {
    snprintf(outBuf, maxLen, "NOT_PROVISIONED");
    return;
  }
  // Compute non-secret 16-bit CRC of the key bytes (never print raw key bytes!)
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < MSCB_KEY_LEN_BYTES; i++) {
    crc ^= (uint16_t)provisionedKey[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc = (crc >> 1);
    }
  }
  snprintf(outBuf, maxLen, "FP-%04X (Epoch %lu)", crc, (unsigned long)activeKeyEpoch);
}

uint32_t CommandSecurity::computeHopAuthToken(uint16_t prevHop, uint16_t nextHop, uint16_t seq, uint8_t type) const {
  if (!keyProvisioned || prevHop == 0) return 0;
  uint8_t hopData[7];
  hopData[0] = (prevHop >> 8) & 0xFF;
  hopData[1] = prevHop & 0xFF;
  hopData[2] = (nextHop >> 8) & 0xFF;
  hopData[3] = nextHop & 0xFF;
  hopData[4] = (seq >> 8) & 0xFF;
  hopData[5] = seq & 0xFF;
  hopData[6] = type;

  uint8_t hmac[32];
  computeHmacSha256(provisionedKey, MSCB_KEY_LEN_BYTES, hopData, sizeof(hopData), hmac);
  uint32_t tok = ((uint32_t)hmac[0] << 24) | ((uint32_t)hmac[1] << 16) |
                 ((uint32_t)hmac[2] << 8)  | (uint32_t)hmac[3];
  return (tok == 0) ? 1UL : tok;
}

bool CommandSecurity::verifyHopAuthToken(uint16_t prevHop, uint16_t nextHop, uint16_t seq, uint8_t type, uint32_t token) const {
  if (!keyProvisioned || prevHop == 0 || token == 0) return false;
  uint32_t expected = computeHopAuthToken(prevHop, nextHop, seq, type);
  return ((expected ^ token) == 0);
}

uint32_t CommandSecurity::computeFrameHopAuthToken(const MeshWireFrame &frame) const {
  if (!keyProvisioned || frame.header.previousHopAddress == 0 ||
      frame.header.payloadLen > MSCB_MAX_PAYLOAD_SIZE) {
    return 0;
  }

  // Bind all immutable and mutable routing header fields + payload bytes (excluding hopAuthToken itself)
  uint8_t authBuf[19 + MSCB_MAX_PAYLOAD_SIZE];
  authBuf[0]  = frame.header.magic[0];
  authBuf[1]  = frame.header.magic[1];
  authBuf[2]  = frame.header.version;
  authBuf[3]  = frame.header.type;
  authBuf[4]  = frame.header.priority;
  authBuf[5]  = frame.header.hopCount;
  authBuf[6]  = frame.header.maxHops;
  authBuf[7]  = (frame.header.seqNumber >> 8) & 0xFF;
  authBuf[8]  = frame.header.seqNumber & 0xFF;
  authBuf[9]  = (frame.header.srcAddress >> 8) & 0xFF;
  authBuf[10] = frame.header.srcAddress & 0xFF;
  authBuf[11] = (frame.header.dstAddress >> 8) & 0xFF;
  authBuf[12] = frame.header.dstAddress & 0xFF;
  authBuf[13] = (frame.header.nextHop >> 8) & 0xFF;
  authBuf[14] = frame.header.nextHop & 0xFF;
  authBuf[15] = (frame.header.previousHopAddress >> 8) & 0xFF;
  authBuf[16] = frame.header.previousHopAddress & 0xFF;
  authBuf[17] = (frame.header.payloadLen >> 8) & 0xFF;
  authBuf[18] = frame.header.payloadLen & 0xFF;

  if (frame.header.payloadLen > 0) {
    memcpy(&authBuf[19], frame.payload, frame.header.payloadLen);
  }

  uint8_t hmac[MSCB_HMAC_TAG_BYTES];
  computeHmacSha256(provisionedKey, MSCB_KEY_LEN_BYTES, authBuf, 19U + frame.header.payloadLen, hmac);
  uint32_t tok = ((uint32_t)hmac[0] << 24) | ((uint32_t)hmac[1] << 16) |
                 ((uint32_t)hmac[2] << 8)  | (uint32_t)hmac[3];
  return (tok == 0) ? 1UL : tok;
}

bool CommandSecurity::verifyFrameHopAuthToken(const MeshWireFrame &frame) const {
  if (!keyProvisioned || frame.header.hopAuthToken == 0 ||
      frame.header.previousHopAddress == 0 ||
      frame.header.payloadLen > MSCB_MAX_PAYLOAD_SIZE) {
    return false;
  }
  uint32_t expected = computeFrameHopAuthToken(frame);
  return (expected != 0) && ((expected ^ frame.header.hopAuthToken) == 0);
}

bool CommandSecurity::computeRegisterRequestAuth(
    const PktRegisterRequestPayload &req,
    uint8_t outAuthTag[16]) const {
  if (!keyProvisioned || !outAuthTag) return false;
  // Authenticate MAC (6), nodeType (1), firmwareVersion (2), sessionToken (4),
  // candidateParent1 (2), candidateParent2 (2), nonce (4) = 21 bytes
  uint8_t authBuffer[21];
  memcpy(authBuffer, req.macAddress, 6);
  authBuffer[6] = req.nodeType;
  authBuffer[7] = (req.firmwareVersion >> 8) & 0xFF;
  authBuffer[8] = req.firmwareVersion & 0xFF;
  authBuffer[9] = (req.sessionToken >> 24) & 0xFF;
  authBuffer[10] = (req.sessionToken >> 16) & 0xFF;
  authBuffer[11] = (req.sessionToken >> 8) & 0xFF;
  authBuffer[12] = req.sessionToken & 0xFF;
  authBuffer[13] = (req.candidateParent1 >> 8) & 0xFF;
  authBuffer[14] = req.candidateParent1 & 0xFF;
  authBuffer[15] = (req.candidateParent2 >> 8) & 0xFF;
  authBuffer[16] = req.candidateParent2 & 0xFF;
  authBuffer[17] = (req.nonce >> 24) & 0xFF;
  authBuffer[18] = (req.nonce >> 16) & 0xFF;
  authBuffer[19] = (req.nonce >> 8) & 0xFF;
  authBuffer[20] = req.nonce & 0xFF;

  uint8_t hmac[MSCB_HMAC_TAG_BYTES];
  computeHmacSha256(provisionedKey, MSCB_KEY_LEN_BYTES, authBuffer, sizeof(authBuffer), hmac);
  memcpy(outAuthTag, hmac, 16);
  return true;
}

static bool mscbHasNonZeroBytes(const uint8_t* buf, size_t len) {
  if (!buf) return false;
  uint8_t acc = 0;
  for (size_t i = 0; i < len; i++) acc |= buf[i];
  return acc != 0;
}

bool CommandSecurity::verifyRegisterRequestAuth(
    const PktRegisterRequestPayload &req) const {
  if (!keyProvisioned) return false;
  if (req.sessionToken == 0 || req.nonce == 0) return false;
  if (!mscbHasNonZeroBytes(req.authTag, 16)) return false;
  uint8_t expectedTag[16];
  if (!computeRegisterRequestAuth(req, expectedTag)) return false;
  return constantTimeCompare(expectedTag, req.authTag, 16);
}

bool CommandSecurity::computeRegisterResponseAuth(
    const PktRegisterResponsePayload &resp,
    uint8_t outAuthTag[16]) const {
  if (!keyProvisioned || !outAuthTag) return false;
  // Authenticate MAC (6), status (1), assignedPoleId (2), assignedParent (2),
  // heartbeatIntervalSec (2), networkKeyEpoch (4), sessionToken (4), nonce (4) = 25 bytes
  uint8_t authBuffer[25];
  memcpy(authBuffer, resp.macAddress, 6);
  authBuffer[6] = resp.status;
  authBuffer[7] = (resp.assignedPoleId >> 8) & 0xFF;
  authBuffer[8] = resp.assignedPoleId & 0xFF;
  authBuffer[9] = (resp.assignedParent >> 8) & 0xFF;
  authBuffer[10] = resp.assignedParent & 0xFF;
  authBuffer[11] = (resp.heartbeatIntervalSec >> 8) & 0xFF;
  authBuffer[12] = resp.heartbeatIntervalSec & 0xFF;
  authBuffer[13] = (resp.networkKeyEpoch >> 24) & 0xFF;
  authBuffer[14] = (resp.networkKeyEpoch >> 16) & 0xFF;
  authBuffer[15] = (resp.networkKeyEpoch >> 8) & 0xFF;
  authBuffer[16] = resp.networkKeyEpoch & 0xFF;
  authBuffer[17] = (resp.sessionToken >> 24) & 0xFF;
  authBuffer[18] = (resp.sessionToken >> 16) & 0xFF;
  authBuffer[19] = (resp.sessionToken >> 8) & 0xFF;
  authBuffer[20] = resp.sessionToken & 0xFF;
  authBuffer[21] = (resp.nonce >> 24) & 0xFF;
  authBuffer[22] = (resp.nonce >> 16) & 0xFF;
  authBuffer[23] = (resp.nonce >> 8) & 0xFF;
  authBuffer[24] = resp.nonce & 0xFF;

  uint8_t hmac[MSCB_HMAC_TAG_BYTES];
  computeHmacSha256(provisionedKey, MSCB_KEY_LEN_BYTES, authBuffer, sizeof(authBuffer), hmac);
  memcpy(outAuthTag, hmac, 16);
  return true;
}

bool CommandSecurity::verifyRegisterResponseAuth(
    const PktRegisterResponsePayload &resp) const {
  if (!keyProvisioned) return false;
  if (resp.sessionToken == 0 || resp.nonce == 0) return false;
  if (!mscbHasNonZeroBytes(resp.authTag, 16)) return false;
  uint8_t expectedTag[16];
  if (!computeRegisterResponseAuth(resp, expectedTag)) return false;
  return constantTimeCompare(expectedTag, resp.authTag, 16);
}

bool CommandSecurity::computeHeartbeatAuth(
    const PktHeartbeatPingPayload &hb,
    uint8_t outAuthTag[16]) const {
  if (!keyProvisioned || !outAuthTag) return false;
  uint8_t authBuffer[31];
  memcpy(authBuffer, hb.macAddress, 6);
  authBuffer[6]  = (hb.poleId >> 8) & 0xFF;
  authBuffer[7]  = hb.poleId & 0xFF;
  authBuffer[8]  = (hb.primaryParent >> 8) & 0xFF;
  authBuffer[9]  = hb.primaryParent & 0xFF;
  authBuffer[10] = (hb.backupParent >> 8) & 0xFF;
  authBuffer[11] = hb.backupParent & 0xFF;
  authBuffer[12] = hb.rank;
  authBuffer[13] = hb.healthState;
  authBuffer[14] = hb.feederStateMask;
  authBuffer[15] = hb.auxFeedbackMask;
  authBuffer[16] = hb.mcbStatusMask;
  authBuffer[17] = hb.auxFeedbackValid;
  authBuffer[18] = hb.mcbStatusValid;
  authBuffer[19] = (hb.sessionToken >> 24) & 0xFF;
  authBuffer[20] = (hb.sessionToken >> 16) & 0xFF;
  authBuffer[21] = (hb.sessionToken >> 8) & 0xFF;
  authBuffer[22] = hb.sessionToken & 0xFF;
  authBuffer[23] = (hb.seqNonce >> 24) & 0xFF;
  authBuffer[24] = (hb.seqNonce >> 16) & 0xFF;
  authBuffer[25] = (hb.seqNonce >> 8) & 0xFF;
  authBuffer[26] = hb.seqNonce & 0xFF;
  authBuffer[27] = (hb.uptimeSec >> 24) & 0xFF;
  authBuffer[28] = (hb.uptimeSec >> 16) & 0xFF;
  authBuffer[29] = (hb.uptimeSec >> 8) & 0xFF;
  authBuffer[30] = hb.uptimeSec & 0xFF;

  uint8_t hmac[MSCB_HMAC_TAG_BYTES];
  computeHmacSha256(provisionedKey, MSCB_KEY_LEN_BYTES, authBuffer, sizeof(authBuffer), hmac);
  memcpy(outAuthTag, hmac, 16);
  return true;
}

bool CommandSecurity::verifyHeartbeatAuth(
    const PktHeartbeatPingPayload &hb) const {
  if (!keyProvisioned) return false;
  if (hb.seqNonce == 0 || !mscbHasNonZeroBytes(hb.authTag, 16)) return false;
  uint8_t expectedTag[16];
  if (!computeHeartbeatAuth(hb, expectedTag)) return false;
  return constantTimeCompare(expectedTag, hb.authTag, 16);
}
