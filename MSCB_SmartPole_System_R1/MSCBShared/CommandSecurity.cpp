#include "CommandSecurity.h"
#include <string.h>

#if defined(ARDUINO) && defined(ESP32)
  #include <mbedtls/md.h>
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
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, key, keyLen);
  mbedtls_md_hmac_update(&ctx, data, dataLen);
  mbedtls_md_hmac_finish(&ctx, outHmac);
  mbedtls_md_free(&ctx);
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

  // 5. Absolute Expiry Timestamp Check (Never millis()!)
  if (currentEpochSec > envelope.expiryEpochSec) {
    return AUTH_ERR_COMMAND_EXPIRED;
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
