/**
 * @file GwMeshCoordinator.h
 * @brief MSCB Smart Pole Gateway - Root Mesh Coordinator & Multi-Hop Router
 * @version 1.0.0
 * 
 * Enforces:
 * - Gateway as Root (Rank 0, Address 0x0001).
 * - Multi-hop packet forwarding and hop count enforcement (<= 6 hops).
 * - Duplicate packet suppression using sequence caching.
 * - Monotonically increasing rank loop prevention.
 * - Downlink routing via primary / backup parent paths.
 */

#ifndef GW_MESH_COORDINATOR_H
#define GW_MESH_COORDINATOR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "GwConfig.h"
#include "GwRadioDriver.h"
#include "GwNodeRegistry.h"
#if __has_include("../MSCBShared/CommandSecurity.h")
  #include "../MSCBShared/Protocol.h"
  #include "../MSCBShared/NetworkTypes.h"
  #include "../MSCBShared/CommandSecurity.h"
#elif __has_include(<CommandSecurity.h>)
  #include <Protocol.h>
  #include <NetworkTypes.h>
  #include <CommandSecurity.h>
#else
  #include "Protocol.h"
  #include "NetworkTypes.h"
  #include "CommandSecurity.h"
#endif

#define GW_MESH_MAX_HOPS             MSCB_DEFAULT_MAX_HOPS // 6
#define GW_DUP_CACHE_SIZE            32
#define GW_MAX_TRACKED_COMMANDS      16
#define GW_REG_RESP_RETRY_INTERVAL_MS 5000UL
#define GW_REG_RESP_MAX_RETRIES      5

struct SeqCacheEntry {
  uint16_t srcAddress;
  uint16_t seqNumber;
  uint32_t timestampMs;
};

struct GwTrackedCommand {
  bool     inUse;
  uint32_t seqNumber;
  uint16_t poleId;
  uint8_t  feederId;             // 1..6 for single feeder, 0x00 for ALL, 0xF0 for group
  uint8_t  action;               // 0=OFF, 1=ON
  uint8_t  groupMask;
  uint8_t  expectedMask;         // Bitmask of expected feeders (0x3F for all 6, (1<<(f-1)) for single)
  uint8_t  receivedAckMask;      // Bitmask of feeders that returned an ACK
  uint8_t  terminalAckMask;      // Bitmask of feeders with terminal ACK (COMPLETED/FAULT/LOCKED)
  uint8_t  outputConfirmedMask;
  uint8_t  auxConfirmedMask;
  uint8_t  mcbFaultMask;
  uint8_t  rejectedMask;
  int64_t  adminChatId;
  uint32_t sentMs;
  uint32_t timeoutMs;
  uint8_t  lastAckState;
  uint8_t  currentRelayMask;
  uint8_t  currentAuxMask;
  uint8_t  currentMcbMask;
  bool     completed;            // True ONLY when ALL expected feeders have terminal response or timed out
  bool     partialFailure;       // True if some feeders failed or timed out
  bool     timedOut;             // True if overall timeout was reached
  bool     notified;
};

class GwMeshCoordinator {
public:
  GwMeshCoordinator();

  bool begin();
  void service(); // Periodic beacon ticker & packet servicing

  // Downlink Transmission
  bool sendDownlinkPacket(uint16_t targetPoleId, uint8_t packetType, const uint8_t *payload, uint16_t payloadLen, uint8_t priority = PRIO_NORMAL);
  bool sendFeederCommand(uint16_t poleId, uint8_t feederId, uint8_t action, uint8_t groupMask, uint32_t &outSeqNumber, int64_t adminChatId = 0);
  bool sendRegisterResponse(const uint8_t mac[6], uint16_t assignedPoleId, uint16_t assignedParent, uint8_t status);
  bool queueRejectionResponse(const uint8_t mac[6], uint8_t status, uint32_t sessionToken = 0, uint32_t nonce = 0, uint16_t firstHop = 0);
  void serviceRegistrationRetries();
  bool sendRevokeCommand(uint16_t poleId, const uint8_t mac[6], uint32_t transactionId, const char* reason);

  // Packet Processing
  bool processIncomingWireFrame(const MeshWireFrame &frame, int16_t rssi, int8_t snr);

  // Statistics
  uint32_t getPacketsForwarded() const { return packetsForwarded; }
  uint32_t getDuplicatesSuppressed() const { return duplicatesSuppressed; }
  uint32_t getHopExceededCount() const { return hopExceededCount; }

  // Feeder ACK tracking (Non-blocking)
  bool isFeederAckPending() const { return feederAckPending; }
  uint32_t getPendingAckSeq() const { return pendingAckSeq; }
  uint8_t getLastAckState() const { return lastAckState; }
  uint16_t getLastAckPole() const { return lastAckPole; }
  void resetFeederAckTracking();

  // Multi-command Non-blocking Tracking
  bool pollCompletedCommand(GwTrackedCommand &outCmd);

private:
  bool isDuplicate(uint16_t src, uint16_t seq);
  void addDuplicate(uint16_t src, uint16_t seq);
  void broadcastBeacon();

  uint16_t          seqCounter;
  uint32_t          lastBeaconMs;
  uint32_t          packetsForwarded;
  uint32_t          duplicatesSuppressed;
  uint32_t          hopExceededCount;

  SeqCacheEntry     dupCache[GW_DUP_CACHE_SIZE];
  uint8_t           dupCacheIdx;

  // Feeder ACK synchronization
  bool              feederAckPending;
  uint32_t          pendingAckSeq;
  uint16_t          pendingAckPole;
  uint8_t           pendingAckFeeder;
  uint32_t          pendingAckStartMs;
  uint8_t           lastAckState;
  uint16_t          lastAckPole;

  GwTrackedCommand  trackedCommands[GW_MAX_TRACKED_COMMANDS];

  static const uint8_t GW_MAX_PENDING_REJECTIONS = 8;
  struct PendingRejectionRetry {
    bool     inUse;
    uint8_t  mac[6];
    uint8_t  status;
    uint32_t sessionToken;
    uint32_t nonce;
    uint16_t firstHop;
    uint8_t  attempts;
    uint32_t lastAttemptMs;
  };
  PendingRejectionRetry pendingRejections[GW_MAX_PENDING_REJECTIONS];
};

extern GwMeshCoordinator gwMesh;

#endif // GW_MESH_COORDINATOR_H
