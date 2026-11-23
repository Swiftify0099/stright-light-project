/**
 * @file ReplayProtection.cpp
 * @brief MSCB Smart Pole System - Anti-Replay & Persistent Nonce Lease Implementation
 * @version 1.1.0
 */

#include "ReplayProtection.h"
#include <string.h>

ReplayProtection replayProtector;

ReplayProtection::ReplayProtection() :
  currentLocalSeq(0), activeCeiling(0), activeKeyEpoch(1),
  nvsBank0(NULL), nvsBank1(NULL), transmitterStorageReady(false), reprovisionRequired(false) {
  reset();
}

void ReplayProtection::init() {
  reset();
}

void ReplayProtection::reset() {
  memset(table, 0, sizeof(table));
  currentLocalSeq = 0;
  activeCeiling = 0;
  activeKeyEpoch = 1;
  transmitterStorageReady = false;
  reprovisionRequired = false;
}

uint32_t ReplayProtection::calculateCrc32(const uint8_t *data, size_t len) const {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  return ~crc;
}

bool ReplayProtection::commitCeilingToNvs(uint32_t newCeiling) {
  if (!nvsBank0 || !nvsBank1) return false;
  size_t payloadLen = sizeof(PersistentNonceRecord) - sizeof(uint32_t);

  uint32_t currentGen = 0;
  if (nvsBank0->magic == 0x4E4F4E43 && nvsBank1->magic == 0x4E4F4E43) {
    currentGen = (nvsBank0->generation > nvsBank1->generation) ?
                  nvsBank0->generation : nvsBank1->generation;
  }

  // Phase 1: Write target ceiling to Bank 0 in PREPARED state
  PersistentNonceRecord prepRec;
  prepRec.magic = 0x4E4F4E43;
  prepRec.generation = currentGen + 1;
  prepRec.persistentCeiling = newCeiling;
  prepRec.keyEpoch = activeKeyEpoch;
  prepRec.commitState = NONCE_STATE_PREPARED;
  prepRec.crc32 = calculateCrc32((const uint8_t*)&prepRec, payloadLen);
  *nvsBank0 = prepRec;

  // Phase 2: Durably commit target ceiling to Bank 1 in COMMITTED state
  PersistentNonceRecord commRec1;
  commRec1.magic = 0x4E4F4E43;
  commRec1.generation = currentGen + 2;
  commRec1.persistentCeiling = newCeiling;
  commRec1.keyEpoch = activeKeyEpoch;
  commRec1.commitState = NONCE_STATE_COMMITTED;
  commRec1.crc32 = calculateCrc32((const uint8_t*)&commRec1, payloadLen);
  *nvsBank1 = commRec1;

  // Phase 3: Durably finalize commit in Bank 0 in COMMITTED state
  PersistentNonceRecord commRec0;
  commRec0.magic = 0x4E4F4E43;
  commRec0.generation = currentGen + 3;
  commRec0.persistentCeiling = newCeiling;
  commRec0.keyEpoch = activeKeyEpoch;
  commRec0.commitState = NONCE_STATE_COMMITTED;
  commRec0.crc32 = calculateCrc32((const uint8_t*)&commRec0, payloadLen);
  *nvsBank0 = commRec0;

  return true;
}

bool ReplayProtection::initTransmitterStorage(PersistentNonceRecord *flashBank0, PersistentNonceRecord *flashBank1) {
  if (!flashBank0 || !flashBank1) return false;
  nvsBank0 = flashBank0;
  nvsBank1 = flashBank1;

  bool bank0Valid = false;
  bool bank1Valid = false;
  size_t payloadLen = sizeof(PersistentNonceRecord) - sizeof(uint32_t);

  if (nvsBank0->magic == 0x4E4F4E43 &&
      nvsBank0->crc32 == calculateCrc32((const uint8_t*)nvsBank0, payloadLen)) {
    bank0Valid = true;
  }

  if (nvsBank1->magic == 0x4E4F4E43 &&
      nvsBank1->crc32 == calculateCrc32((const uint8_t*)nvsBank1, payloadLen)) {
    bank1Valid = true;
  }

  // STRICT FAIL-CLOSED RULE:
  // If EITHER bank is corrupt, missing, or has invalid CRC:
  // We CANNOT determine whether nonces from a newer ceiling were already issued!
  // Falling back to a single intact older bank could silently roll back counters.
  // Therefore: Fail Closed immediately!
  if (!bank0Valid || !bank1Valid) {
    transmitterStorageReady = false;
    reprovisionRequired = true;
    return false;
  }

  // Both records have valid CRC. Now verify dual durable commit status:
  // Both records MUST have commitState == NONCE_STATE_COMMITTED,
  // matching persistentCeiling, and matching keyEpoch.
  if (nvsBank0->commitState != NONCE_STATE_COMMITTED ||
      nvsBank1->commitState != NONCE_STATE_COMMITTED ||
      nvsBank0->persistentCeiling != nvsBank1->persistentCeiling ||
      nvsBank0->keyEpoch != nvsBank1->keyEpoch) {
    // Interrupted commit or ceiling mismatch: Latest ceiling is uncertain!
    // STRICT FAIL CLOSED!
    transmitterStorageReady = false;
    reprovisionRequired = true;
    return false;
  }

  uint32_t confirmedCeiling = nvsBank0->persistentCeiling;
  activeKeyEpoch = nvsBank0->keyEpoch;

  // Advance to next block
  activeCeiling = confirmedCeiling + NVS_COUNTER_BLOCK_SIZE;
  currentLocalSeq = confirmedCeiling;

  // Commit new leased ceiling to BOTH banks before issuing any sequence numbers!
  if (!commitCeilingToNvs(activeCeiling)) {
    transmitterStorageReady = false;
    reprovisionRequired = true;
    return false;
  }

  transmitterStorageReady = true;
  reprovisionRequired = false;
  return true;
}

bool ReplayProtection::reprovisionStorage(uint32_t newKeyEpoch, PersistentNonceRecord *flashBank0, PersistentNonceRecord *flashBank1) {
  if (!flashBank0 || !flashBank1) return false;
  nvsBank0 = flashBank0;
  nvsBank1 = flashBank1;

  activeKeyEpoch = newKeyEpoch;
  activeCeiling = NVS_COUNTER_BLOCK_SIZE;
  currentLocalSeq = 0;

  size_t payloadLen = sizeof(PersistentNonceRecord) - sizeof(uint32_t);

  // Phase 1: Bank 0 PREPARED
  PersistentNonceRecord prepRec;
  prepRec.magic = 0x4E4F4E43;
  prepRec.generation = 1;
  prepRec.persistentCeiling = activeCeiling;
  prepRec.keyEpoch = activeKeyEpoch;
  prepRec.commitState = NONCE_STATE_PREPARED;
  prepRec.crc32 = calculateCrc32((const uint8_t*)&prepRec, payloadLen);
  *nvsBank0 = prepRec;

  // Phase 2: Bank 1 COMMITTED
  PersistentNonceRecord commRec1;
  commRec1.magic = 0x4E4F4E43;
  commRec1.generation = 2;
  commRec1.persistentCeiling = activeCeiling;
  commRec1.keyEpoch = activeKeyEpoch;
  commRec1.commitState = NONCE_STATE_COMMITTED;
  commRec1.crc32 = calculateCrc32((const uint8_t*)&commRec1, payloadLen);
  *nvsBank1 = commRec1;

  // Phase 3: Bank 0 COMMITTED
  PersistentNonceRecord commRec0;
  commRec0.magic = 0x4E4F4E43;
  commRec0.generation = 3;
  commRec0.persistentCeiling = activeCeiling;
  commRec0.keyEpoch = activeKeyEpoch;
  commRec0.commitState = NONCE_STATE_COMMITTED;
  commRec0.crc32 = calculateCrc32((const uint8_t*)&commRec0, payloadLen);
  *nvsBank0 = commRec0;

  transmitterStorageReady = true;
  reprovisionRequired = false;
  return true;
}

uint32_t ReplayProtection::getNextMonotonicSeq() {
  if (!transmitterStorageReady || reprovisionRequired) {
    return 0; // Refuses to generate nonces when storage cannot guarantee uniqueness!
  }

  // Counter overflow protection:
  // If sequence number reaches saturation boundary, FAIL CLOSED immediately.
  // Never wrap around or reuse sequence numbers under the same key/epoch!
  if (currentLocalSeq >= 0xFFFFFFFFUL - NVS_COUNTER_BLOCK_SIZE) {
    transmitterStorageReady = false;
    reprovisionRequired = true;
    return 0;
  }

  // If we have exhausted the current leased ceiling, pre-commit next block
  if (currentLocalSeq >= activeCeiling) {
    if (activeCeiling > 0xFFFFFFFFUL - NVS_COUNTER_BLOCK_SIZE) {
      transmitterStorageReady = false;
      reprovisionRequired = true;
      return 0;
    }
    activeCeiling += NVS_COUNTER_BLOCK_SIZE;
    if (!commitCeilingToNvs(activeCeiling)) {
      transmitterStorageReady = false;
      reprovisionRequired = true;
      return 0;
    }
  }

  return currentLocalSeq++;
}

void ReplayProtection::constructGcmNonce96(uint16_t senderNodeId, uint16_t keyEpoch, uint64_t monotonicSeq, uint8_t outNonce[12]) {
  outNonce[0] = (uint8_t)(senderNodeId >> 8);
  outNonce[1] = (uint8_t)(senderNodeId & 0xFF);
  outNonce[2] = (uint8_t)(keyEpoch >> 8);
  outNonce[3] = (uint8_t)(keyEpoch & 0xFF);
  for (int i = 0; i < 8; ++i) {
    outNonce[4 + i] = (uint8_t)((monotonicSeq >> (56 - i * 8)) & 0xFF);
  }
}

void ReplayProtection::simulatePowerLoss() {
  // On power loss / brownout: volatile memory is lost,
  // but NVS banks retain the committed activeCeiling.
  // Re-initializing re-reads from NVS and jumps to next block.
  transmitterStorageReady = false;
  currentLocalSeq = 0;
  activeCeiling = 0;
}

void ReplayProtection::simulateNvsCorruption(bool bank0, bool bank1) {
  if (bank0 && nvsBank0) {
    nvsBank0->crc32 ^= 0xFFFFFFFF; // Corrupt CRC
  }
  if (bank1 && nvsBank1) {
    nvsBank1->crc32 ^= 0xFFFFFFFF; // Corrupt CRC
  }
}

void ReplayProtection::simulateFactoryReset() {
  if (nvsBank0) memset(nvsBank0, 0xFF, sizeof(PersistentNonceRecord));
  if (nvsBank1) memset(nvsBank1, 0xFF, sizeof(PersistentNonceRecord));
  reset();
}

int ReplayProtection::findOrAllocateIndex(uint16_t nodeId) {
  if (nodeId == 0x0000 || nodeId == 0xFFFF) return -1;
  if (nodeId < MSCB_MAX_TRACKED_NODES) {
    return (int)nodeId;
  }
  // Fallback linear scan
  int freeIdx = -1;
  for (int i = 0; i < MSCB_MAX_TRACKED_NODES; i++) {
    if (table[i].initialized && table[i].node_id == nodeId) return i;
    if (!table[i].initialized && freeIdx == -1) freeIdx = i;
  }
  return freeIdx;
}

const MscbNodeReplayState* ReplayProtection::getNodeState(uint16_t nodeId) const {
  if (nodeId < MSCB_MAX_TRACKED_NODES && table[nodeId].initialized) {
    return &table[nodeId];
  }
  for (int i = 0; i < MSCB_MAX_TRACKED_NODES; i++) {
    if (table[i].initialized && table[i].node_id == nodeId) return &table[i];
  }
  return NULL;
}

void ReplayProtection::registerNewSession(uint16_t nodeId, uint32_t newSessionId, uint32_t keyEpoch, uint32_t startSeq) {
  int idx = findOrAllocateIndex(nodeId);
  if (idx < 0) return;

  MscbNodeReplayState *entry = &table[idx];
  entry->node_id = nodeId;
  entry->initialized = true;
  entry->current_session_id = newSessionId;
  entry->key_epoch = keyEpoch;
  entry->max_seq = startSeq;
  entry->window_mask = 1ULL;
}

void ReplayProtection::handleKeyRotation(uint16_t nodeId, uint32_t newKeyEpoch) {
  int idx = findOrAllocateIndex(nodeId);
  if (idx < 0) return;

  MscbNodeReplayState *entry = &table[idx];
  if (entry->initialized) {
    entry->key_epoch = newKeyEpoch;
    entry->max_seq = 0;
    entry->window_mask = 0ULL;
  }
}

ReplayCheckResult ReplayProtection::checkPacket(uint16_t nodeId, uint32_t sessionId, uint32_t keyEpoch, uint32_t seq) {
  int idx = findOrAllocateIndex(nodeId);
  if (idx < 0) return REPLAY_ERR_UNKNOWN_NODE;

  MscbNodeReplayState *entry = &table[idx];
  if (!entry->initialized) {
    return REPLAY_OK_NEW_PACKET;
  }

  // 1. Key Epoch check
  if (keyEpoch < entry->key_epoch) {
    entry->total_replays_detected++;
    return REPLAY_ERR_KEY_EPOCH_MISMATCH;
  }

  // 2. Session Token check
  // NOTE: bootSessionId is an unforgeable random token; do NOT perform numeric < or > ordering!
  if (sessionId != entry->current_session_id) {
    // If packet comes with a different session ID without handshake registration:
    entry->total_replays_detected++;
    return REPLAY_ERR_STALE_SESSION;
  }

  // 3. Monotonic Sequence check within active session
  if (seq > entry->max_seq) {
    return REPLAY_OK_NEW_PACKET;
  }

  uint32_t diff = entry->max_seq - seq;
  if (diff >= MSCB_REPLAY_WINDOW_BITS) {
    entry->total_replays_detected++;
    return REPLAY_ERR_TOO_OLD_WINDOW_EXCEEDED;
  }

  if (entry->window_mask & (1ULL << diff)) {
    entry->total_replays_detected++;
    return REPLAY_ERR_DUPLICATE_PACKET;
  }

  // Out-of-order in-window new packet
  return REPLAY_OK_NEW_PACKET;
}

bool ReplayProtection::commitPacket(uint16_t nodeId, uint32_t sessionId, uint32_t keyEpoch, uint32_t seq) {
  int idx = findOrAllocateIndex(nodeId);
  if (idx < 0) return false;

  MscbNodeReplayState *entry = &table[idx];
  if (!entry->initialized) {
    entry->node_id = nodeId;
    entry->initialized = true;
    entry->current_session_id = sessionId;
    entry->key_epoch = keyEpoch;
    entry->max_seq = seq;
    entry->window_mask = 1ULL;
    return true;
  }

  if (seq > entry->max_seq) {
    uint32_t shift = seq - entry->max_seq;
    if (shift >= MSCB_REPLAY_WINDOW_BITS) {
      entry->window_mask = 1ULL;
    } else {
      entry->window_mask = (entry->window_mask << shift) | 1ULL;
    }
    entry->max_seq = seq;
  } else {
    uint32_t diff = entry->max_seq - seq;
    if (diff < MSCB_REPLAY_WINDOW_BITS) {
      entry->window_mask |= (1ULL << diff);
    }
  }

  return true;
}
