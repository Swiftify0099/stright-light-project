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
