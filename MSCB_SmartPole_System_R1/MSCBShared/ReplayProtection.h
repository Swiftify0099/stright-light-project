/**
 * @file ReplayProtection.h
 * @brief MSCB Smart Pole System - Anti-Replay, Persistent Nonce Lease & Session Management
 * @version 1.1.0
 * 
 * REMEDIATED SECURITY ARCHITECTURE:
 * 1. PERSISTENT MONOTONIC COUNTER RESERVATION (NVS LEASE):
 *    - To protect against brownouts, power cuts, and flash wear, sequence counters
 *      are leased in persistent blocks of NVS_COUNTER_BLOCK_SIZE (1,000 counts).
 *    - On boot, the next ceiling is committed to NVS before any radio transmission.
 *    - Eliminates volatile RAM resets and guarantees strictly monotonic nonces for AES-GCM.
 * 
 * 2. RANDOM BOOT SESSION TOKEN HANDLING:
 *    - bootSessionId is an unforgeable 32-bit random session token (TRNG).
 *    - ELIMINATED invalid numeric ordering (< and >) on random session IDs.
 *    - Session transitions require explicit authenticated handshake / JOIN protocol.
 *    - Sliding window (64-bit mask) tracks sequence order strictly within an authenticated session.
 * 
 * 3. AES-GCM NONCE SAFETY:
 *    - Nonces are constructed as [NodeID (2B) | PersistentCeiling (4B) | IntraBlockSeq (4B) | Reserved (2B)].
 *    - Guarantees zero nonce reuse under the same cryptographic key.
 */

#ifndef MSCB_REPLAY_PROTECTION_H
#define MSCB_REPLAY_PROTECTION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#define MSCB_MAX_TRACKED_NODES       52
#define MSCB_REPLAY_WINDOW_BITS      64
#define NVS_COUNTER_BLOCK_SIZE       1000UL

// Storage record for persistent counter leasing (ping-pong double buffered)
#pragma pack(push, 1)
struct PersistentNonceRecord {
  uint32_t magic;                    // 0x4E4F4E43 ("NONC")
  uint32_t generation;               // Monotonic write generation
  uint32_t persistentCeiling;        // Leased ceiling committed to NVS
  uint32_t keyEpoch;                 // Active key epoch
  uint32_t crc32;                    // Record CRC-32 integrity
};
#pragma pack(pop)

// Node replay tracking state on Receiver (Gateway or Pole)
struct MscbNodeReplayState {
  uint16_t node_id;
  bool     initialized;
  uint32_t current_session_id;       // Current authenticated session token
  uint32_t key_epoch;                // Current active key epoch
  uint32_t max_seq;                  // Highest sequence number seen in this session
  uint64_t window_mask;              // 64-bit sliding window of received packets
  uint32_t total_replays_detected;
};

// Replay Verification Outcome
enum ReplayCheckResult : uint8_t {
  REPLAY_OK_NEW_PACKET            = 0,
  REPLAY_ERR_DUPLICATE_PACKET     = 1,
  REPLAY_ERR_TOO_OLD_WINDOW_EXCEEDED = 2,
  REPLAY_ERR_STALE_SESSION        = 3,
  REPLAY_ERR_UNKNOWN_NODE         = 4,
  REPLAY_ERR_KEY_EPOCH_MISMATCH   = 5
};

class ReplayProtection {
public:
  ReplayProtection();

  void init();
  void reset();

  // Transmitter: Persistent Monotonic Counter Leasing
  bool initTransmitterStorage(PersistentNonceRecord *flashBank0, PersistentNonceRecord *flashBank1);
  uint32_t getNextMonotonicSeq();
  uint32_t getCurrentCeiling() const { return activeCeiling; }
  uint32_t getCurrentLocalSeq() const { return currentLocalSeq; }

  // Receiver: Anti-Replay Sliding Window & Session Management
  ReplayCheckResult checkPacket(uint16_t nodeId, uint32_t sessionId, uint32_t keyEpoch, uint32_t seq);
  bool commitPacket(uint16_t nodeId, uint32_t sessionId, uint32_t keyEpoch, uint32_t seq);
  void registerNewSession(uint16_t nodeId, uint32_t newSessionId, uint32_t keyEpoch, uint32_t startSeq);
  void handleKeyRotation(uint16_t nodeId, uint32_t newKeyEpoch);

  const MscbNodeReplayState* getNodeState(uint16_t nodeId) const;

  // Power brownout / corruption test hooks
  void simulatePowerLoss();
  void simulateNvsCorruption(bool bank0, bool bank1);
  void simulateFactoryReset();

private:
  int findOrAllocateIndex(uint16_t nodeId);
  uint32_t calculateCrc32(const uint8_t *data, size_t len) const;
  bool commitCeilingToNvs(uint32_t newCeiling);

  MscbNodeReplayState table[MSCB_MAX_TRACKED_NODES];

  // Transmitter state
  uint32_t currentLocalSeq;
  uint32_t activeCeiling;
  uint32_t activeKeyEpoch;
  PersistentNonceRecord *nvsBank0;
  PersistentNonceRecord *nvsBank1;
  bool     transmitterStorageReady;
};

extern ReplayProtection replayProtector;

#endif // MSCB_REPLAY_PROTECTION_H
