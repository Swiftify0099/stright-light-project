/**
 * @file PoleMeshRouter.h
 * @brief MSCB Smart Pole Node - Multi-Hop Mesh Router & Dynamic Parent Selection
 * @version 1.0.0
 * 
 * Enforces:
 * - Neighbor discovery & real link quality evaluation (RSSI, SNR, packet delivery).
 * - Monotonically increasing rank loop prevention (Rank = Parent Rank + 1).
 * - Primary Parent + Independent Backup Parent architecture.
 * - Automatic failover to backup parent upon link loss.
 * - Orphan state detection and autonomous route recovery.
 * - Multi-hop packet forwarding with duplicate suppression and hop count <= 6 limit.
 */

#ifndef POLE_MESH_ROUTER_H
#define POLE_MESH_ROUTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "PoleConfig.h"
#include "PoleBoardPins.h"
#include "PoleRadioDriver.h"
#include "PoleStorageManager.h"
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"

#define MESH_MAX_NEIGHBORS           12
#define MESH_DUP_CACHE_SIZE          32
#define MESH_MAX_HOPS                MSCB_DEFAULT_MAX_HOPS // 6
#define MESH_PARENT_TIMEOUT_MS       90000UL // 90 seconds without parent packet = link loss

enum NodeRoutingState : uint8_t {
  ROUTING_UNINITIALIZED = 0,
  ROUTING_SEARCHING     = 1, // Listening for beacons / soliciting parents
  ROUTING_CONNECTED     = 2, // Primary parent active
  ROUTING_FAILOVER      = 3, // Switched to backup parent
  ROUTING_ORPHAN        = 4  // Both parents lost; searching for route
};

inline const char* routingStateToString(NodeRoutingState st) {
  switch (st) {
    case ROUTING_UNINITIALIZED: return "UNINITIALIZED";
    case ROUTING_SEARCHING:     return "SEARCHING_PARENTS";
    case ROUTING_CONNECTED:     return "CONNECTED_PRIMARY";
    case ROUTING_FAILOVER:      return "FAILOVER_BACKUP";
    case ROUTING_ORPHAN:        return "ORPHAN_RECOVERY";
    default:                    return "UNKNOWN";
  }
}

struct MeshNeighbor {
  uint16_t nodeAddress;
  uint8_t  rank;
  uint16_t primaryParent;
  int16_t  lastRssi;
  int8_t   lastSnr;
  uint32_t lastSeenMs;
  uint16_t packetsReceived;
  int16_t  linkScore; // Calculated metric
};

struct DupCacheEntry {
  uint16_t srcAddress;
  uint16_t seqNumber;
  uint32_t timestampMs;
};

class PoleMeshRouter {
public:
  PoleMeshRouter();

  bool begin();
  void service(); // Periodic state machine ticker

  // Packet Processing
  bool handleReceivedWireFrame(const MeshWireFrame &frame, int16_t rssi, int8_t snr,
                               MeshWireFrame &outActionFrame, bool &outSendAction);

  // Packet Transmission Uplink (towards Gateway)
  bool sendUplinkPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen, uint8_t priority = PRIO_NORMAL);

  // Parent Selection & Failover
  void updateNeighborsFromBeacon(uint16_t sender, uint8_t rank, uint16_t priParent, int16_t rssi, int8_t snr);
  bool selectBestParents();
  void triggerFailover();
  void declareOrphan();

  // Status & Metrics
  NodeRoutingState getState() const { return state; }
  uint16_t getPrimaryParent() const { return primaryParent; }
  uint16_t getBackupParent() const { return backupParent; }
  uint8_t  getMyRank() const { return myRank; }
  uint8_t  getNeighborCount() const { return neighborCount; }
  const MeshNeighbor* getNeighbor(uint8_t idx) const;

  uint32_t getPacketsForwarded() const { return packetsForwarded; }
  uint32_t getPacketsDropped() const { return packetsDropped; }

private:
  bool isDuplicate(uint16_t src, uint16_t seq);
  void addDuplicate(uint16_t src, uint16_t seq);
  int  findNeighborIndex(uint16_t nodeAddr) const;
  int16_t calculateLinkScore(int16_t rssi, int8_t snr, uint8_t rank) const;

  NodeRoutingState state;
  uint16_t         myNodeId;
  uint16_t         primaryParent;
  uint16_t         backupParent;
  uint8_t          myRank;
  uint16_t         seqCounter;

  uint32_t         lastParentPacketMs;
  uint32_t         lastBeaconMs;
  uint32_t         packetsForwarded;
  uint32_t         packetsDropped;

  MeshNeighbor     neighbors[MESH_MAX_NEIGHBORS];
  uint8_t          neighborCount;

  DupCacheEntry    dupCache[MESH_DUP_CACHE_SIZE];
  uint8_t          dupCacheIdx;
};

extern PoleMeshRouter poleMesh;

#endif // POLE_MESH_ROUTER_H
