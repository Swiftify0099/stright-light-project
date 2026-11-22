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
  nvsBank0(NULL), nvsBank1(NULL), transmitterStorageReady(false) {
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

  uint32_t highestCeiling = 0;
  uint32_t highestGen = 0;
  uint32_t activeEpoch = 1;

  if (bank0Valid && bank1Valid) {
    if (nvsBank0->generation >= nvsBank1->generation) {
      highestCeiling = nvsBank0->persistentCeiling;
      highestGen = nvsBank0->generation;
      activeEpoch = nvsBank0->keyEpoch;
    } else {
      highestCeiling = nvsBank1->persistentCeiling;
      highestGen = nvsBank1->generation;
      activeEpoch = nvsBank1->keyEpoch;
    }
  } else if (bank0Valid) {
    highestCeiling = nvsBank0->persistentCeiling;
    highestGen = nvsBank0->generation;
    activeEpoch = nvsBank0->keyEpoch;
  } else if (bank1Valid) {
    highestCeiling = nvsBank1->persistentCeiling;
    highestGen = nvsBank1->generation;
    activeEpoch = nvsBank1->keyEpoch;
  } else {
    // Both invalid / fresh blank storage
    highestCeiling = 0;
    highestGen = 0;
    activeEpoch = 1;
  }

  // Pre-allocate next lease block
  activeCeiling = highestCeiling + NVS_COUNTER_BLOCK_SIZE;
  activeKeyEpoch = activeEpoch;
  currentLocalSeq = highestCeiling; // Starts from last ceiling

  // Write new lease to the alternate bank
  PersistentNonceRecord nextRec;
  nextRec.magic = 0x4E4F4E43;
  nextRec.generation = highestGen + 1;
  nextRec.persistentCeiling = activeCeiling;
  nextRec.keyEpoch = activeKeyEpoch;
  nextRec.crc32 = calculateCrc32((const uint8_t*)&nextRec, payloadLen);

  if (highestGen % 2 == 0) {
    *nvsBank0 = nextRec;
  } else {
    *nvsBank1 = nextRec;
  }

  transmitterStorageReady = true;
  return true;
}

bool ReplayProtection::commitCeilingToNvs(uint32_t newCeiling) {
  if (!nvsBank0 || !nvsBank1) return false;
  size_t payloadLen = sizeof(PersistentNonceRecord) - sizeof(uint32_t);

  uint32_t currentGen = (nvsBank0->generation > nvsBank1->generation) ?
                         nvsBank0->generation : nvsBank1->generation;

  PersistentNonceRecord nextRec;
  nextRec.magic = 0x4E4F4E43;
  nextRec.generation = currentGen + 1;
  nextRec.persistentCeiling = newCeiling;
  nextRec.keyEpoch = activeKeyEpoch;
  nextRec.crc32 = calculateCrc32((const uint8_t*)&nextRec, payloadLen);

  if (nextRec.generation % 2 == 0) {
    *nvsBank0 = nextRec;
  } else {
    *nvsBank1 = nextRec;
  }
  return true;
}

uint32_t ReplayProtection::getNextMonotonicSeq() {
  if (!transmitterStorageReady) {
    return currentLocalSeq++;
  }

  // If we have exhausted the current leased ceiling, pre-commit next block
  if (currentLocalSeq >= activeCeiling) {
    activeCeiling += NVS_COUNTER_BLOCK_SIZE;
    commitCeilingToNvs(activeCeiling);
  }

  return currentLocalSeq++;
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
