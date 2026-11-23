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
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../MSCBShared/CommandSecurity.h"

#define GW_MESH_MAX_HOPS             MSCB_DEFAULT_MAX_HOPS // 6
#define GW_DUP_CACHE_SIZE            32

struct SeqCacheEntry {
  uint16_t srcAddress;
  uint16_t seqNumber;
  uint32_t timestampMs;
};

class GwMeshCoordinator {
public:
  GwMeshCoordinator();

  bool begin();
  void service(); // Periodic beacon ticker & packet servicing

  // Downlink Transmission
  bool sendDownlinkPacket(uint16_t targetPoleId, uint8_t packetType, const uint8_t *payload, uint16_t payloadLen, uint8_t priority = PRIO_NORMAL);
  bool sendFeederCommand(uint16_t poleId, uint8_t feederId, uint8_t action, uint8_t groupMask, uint32_t &outSeqNumber);

  // Packet Processing
  bool processIncomingWireFrame(const MeshWireFrame &frame, int16_t rssi, int8_t snr);

  // Statistics
  uint32_t getPacketsForwarded() const { return packetsForwarded; }
  uint32_t getDuplicatesSuppressed() const { return duplicatesSuppressed; }
  uint32_t getHopExceededCount() const { return hopExceededCount; }

  // Feeder ACK tracking
  bool isFeederAckPending() const { return feederAckPending; }
  uint32_t getPendingAckSeq() const { return pendingAckSeq; }
  uint8_t getLastAckState() const { return lastAckState; }
  uint16_t getLastAckPole() const { return lastAckPole; }
  void resetFeederAckTracking();

private:
  bool isDuplicate(uint16_t src, uint16_t seq);
  void addDuplicate(uint16_t src, uint16_t seq);
  void broadcastBeacon();

  uint16_t       seqCounter;
  uint32_t       lastBeaconMs;
  uint32_t       packetsForwarded;
  uint32_t       duplicatesSuppressed;
  uint32_t       hopExceededCount;

  SeqCacheEntry  dupCache[GW_DUP_CACHE_SIZE];
  uint8_t        dupCacheIdx;

  // Feeder ACK synchronization
  bool           feederAckPending;
  uint32_t       pendingAckSeq;
  uint16_t       pendingAckPole;
  uint8_t        pendingAckFeeder;
  uint32_t       pendingAckStartMs;
  uint8_t        lastAckState;
  uint16_t       lastAckPole;
};

extern GwMeshCoordinator gwMesh;

#endif // GW_MESH_COORDINATOR_H
