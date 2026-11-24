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
#if __has_include("../MSCBShared/Protocol.h")
  #include "../MSCBShared/Protocol.h"
  #include "../MSCBShared/NetworkTypes.h"
#elif __has_include(<Protocol.h>)
  #include <Protocol.h>
  #include <NetworkTypes.h>
#else
  #include "Protocol.h"
  #include "NetworkTypes.h"
#endif

#define MESH_MAX_NEIGHBORS           12
#define MESH_DUP_CACHE_SIZE          32
#define MESH_MAX_HOPS                MSCB_DEFAULT_MAX_HOPS // 6
#define MESH_PARENT_TIMEOUT_MS       90000UL // 90 seconds without parent packet = link loss
#define MESH_MAX_TRACKED_CHILDREN    12
#define MESH_CHILD_SUSPECTED_MS      45000UL // 45 seconds without packet = suspected
#define MESH_CHILD_TIMEOUT_MS        90000UL // 90 seconds without packet = offline

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

enum ChildNodeState : uint8_t {
  CHILD_STATE_UNKNOWN   = 0,
  CHILD_STATE_ONLINE    = 1,
  CHILD_STATE_SUSPECTED = 2,
  CHILD_STATE_OFFLINE   = 3
};

inline const char* childNodeStateToString(ChildNodeState st) {
  switch (st) {
    case CHILD_STATE_ONLINE:    return "ONLINE";
    case CHILD_STATE_SUSPECTED: return "SUSPECTED";
    case CHILD_STATE_OFFLINE:   return "OFFLINE";
    default:                    return "UNKNOWN";
  }
}

struct TrackedChildNode {
  uint16_t       childPoleId;
  uint32_t       lastSeenMs;
  uint32_t       lastHeartbeatEpoch;
  uint32_t       lastSeqNumber;
  int16_t        lastRssi;
  int8_t         lastSnr;
  ChildNodeState state;
  uint16_t       missedPings;
  bool           reportedFault;
};

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

  // Child Node Tracking (Parent monitors children)
  void onChildPacketReceived(uint16_t childId, uint32_t seq, int16_t rssi = 0, int8_t snr = 0, uint8_t pktType = 0);
  uint8_t getTrackedChildCount() const { return childCount; }
  const TrackedChildNode* getTrackedChild(uint8_t idx) const;
  const TrackedChildNode* findTrackedChild(uint16_t childId) const;
  void checkChildTimeouts(uint32_t nowMs);

  // Gateway Reachability Verification
  bool isGatewayReachable() const {
    return (state == ROUTING_CONNECTED || state == ROUTING_FAILOVER) && 
           (primaryParent != 0) && (primaryParent != myNodeId) && (myRank < MESH_MAX_HOPS);
  }

  // Status & Metrics
  NodeRoutingState getState() const { return state; }
  uint16_t getPrimaryParent() const { return primaryParent; }
  uint16_t getBackupParent() const { return backupParent; }
  uint8_t  getMyRank() const { return myRank; }
  uint8_t  getNeighborCount() const { return neighborCount; }
  const MeshNeighbor* getNeighbor(uint8_t idx) const;

  uint32_t getPacketsForwarded() const { return packetsForwarded; }
  uint32_t getPacketsDropped() const { return packetsDropped; }
  uint32_t getPacketsDroppedSelfLoop() const { return packetsDroppedSelfLoop; }
  uint32_t getPacketsDroppedHopLimit() const { return packetsDroppedHopLimit; }
  uint32_t getPacketsDroppedMalformed() const { return packetsDroppedMalformed; }
  uint32_t getPacketsDroppedDuplicate() const { return packetsDroppedDuplicate; }

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
  uint32_t         packetsDroppedSelfLoop;
  uint32_t         packetsDroppedHopLimit;
  uint32_t         packetsDroppedMalformed;
  uint32_t         packetsDroppedDuplicate;

  MeshNeighbor     neighbors[MESH_MAX_NEIGHBORS];
  uint8_t          neighborCount;

  TrackedChildNode trackedChildren[MESH_MAX_TRACKED_CHILDREN];
  uint8_t          childCount;

  DupCacheEntry    dupCache[MESH_DUP_CACHE_SIZE];
  uint8_t          dupCacheIdx;
};

extern PoleMeshRouter poleMesh;

#endif // POLE_MESH_ROUTER_H
