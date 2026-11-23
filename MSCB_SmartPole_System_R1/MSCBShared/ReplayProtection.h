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
 * 3. AES-GCM NONCE SAFETY & ROLLBACK BOUNDARY:
 *    - Nonces are constructed as [NodeID (2B) | KeyEpoch (2B) | MonotonicCounter (8B)].
 *    - Guarantees zero nonce reuse under the same cryptographic key.
 *    - CRITICAL SECURITY PROOF (SNAPSHOT RESTORE BOUNDARY):
 *      Restoring both NVS banks, key epoch, and key from an old SPI flash snapshot
 *      CANNOT be detected by local microcontroller storage alone, because all local
 *      fields (magic, CRC, commit states, ceilings) appear perfectly valid and consistent.
 *      Therefore, local storage alone CANNOT prevent nonce reuse upon flash restore.
 *      To prevent catastrophic nonce reuse, the system mandates:
 *      (a) External Monotonic Authority: The receiver (Gateway) anti-replay sliding window
 *          tracks max_seq and strictly rejects rolled-back sequences (REPLAY_ERR_TOO_OLD);
 *          OR hardware-enforced one-way eFuse counters (e.g. ESP32 anti-rollback eFuses).
 *      (b) Mandatory New Key Provisioning: Any node restored from backup or re-flashed
 *          must undergo reprovisionStorage() with an incremented KeyEpoch and fresh AES key.
 *      (c) Counter Overflow Fail-Closed: If counter reaches saturation, transmission halts.
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

// Explicit durable 2-Phase Commit states for redundant NVS banks
enum NonceCommitState : uint32_t {
  NONCE_STATE_PREPARED  = 0x50524550, // "PREP" (Phase 1: Prepared in primary bank)
  NONCE_STATE_COMMITTED = 0x434F4D4D  // "COMM" (Phase 2: Durably committed in both banks)
};

// Storage record for persistent counter leasing (redundant dual-bank durable commit)
#pragma pack(push, 1)
struct PersistentNonceRecord {
  uint32_t magic;                    // 0x4E4F4E43 ("NONC")
  uint32_t generation;               // Monotonic write generation counter
  uint32_t persistentCeiling;        // Leased ceiling committed to NVS
  uint32_t keyEpoch;                 // Active cryptographic key epoch
  uint32_t commitState;              // NonceCommitState: COMMITTED (0x434F4D4D) or PREPARED (0x50524550)
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
  bool reprovisionStorage(uint32_t newKeyEpoch, PersistentNonceRecord *flashBank0, PersistentNonceRecord *flashBank1);
  uint32_t getNextMonotonicSeq();
  uint32_t getCurrentCeiling() const { return activeCeiling; }
  uint32_t getCurrentLocalSeq() const { return currentLocalSeq; }
  bool isTransmitterStorageReady() const { return transmitterStorageReady; }
  bool requiresKeyReprovisioning() const { return reprovisionRequired; }

  // NIST SP 800-38D Deterministic 96-bit AES-GCM Nonce Construction
  static void constructGcmNonce96(uint16_t senderNodeId, uint16_t keyEpoch, uint64_t monotonicSeq, uint8_t outNonce[12]);

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
  bool     reprovisionRequired;
};

extern ReplayProtection replayProtector;

#endif // MSCB_REPLAY_PROTECTION_H
