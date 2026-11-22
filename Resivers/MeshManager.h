/*
 * =====================================================================
 *  MESHMANAGER.H — FIELD POLE MESH NEIGHBOR & ROUTE MANAGER (V10)
 *  Two-Parent DAG Mesh Routing with Automatic Failure Detection & Orphan Recovery
 * =====================================================================
 */

#ifndef MESH_MANAGER_H
#define MESH_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

class MeshManager {
public:
  MeshManager();
  void init();

  // Lifecycle & Periodic Tasks
  void processPeriodicTasks();

  // Neighbor Management & Link Quality
  void addOrUpdateNeighbor(const String &id, int rssi, int snr, uint8_t rank = INFINITY_RANK, const String &parent = "", uint16_t cost = 9999);
  void recordTxResult(const String &target, bool success);
  void expireNeighbors();
  int  getNeighborCount() const;
  NeighborEntry* getNeighbor(int index);
  NeighborEntry* findNeighbor(const String &id);
  uint8_t  computeLinkQuality(int rssi, int snr, uint8_t pdr);
  uint16_t computeLinkCost(uint8_t lq);

  // DAG & Two-Parent Management
  void evaluateAndSelectParents();
  bool isCandidateLoopFree(const NeighborEntry &candidate) const;
  bool isPathDiverse(const NeighborEntry &candidate, const ParentInfo &primary) const;
  void handlePrimaryParentFailure();
  void switchToBackupParent(const String &reason);
  void enterOrphanState();
  void exitOrphanState(const String &newPrimaryId, uint8_t newRank, uint16_t newCost);

  // Orphan Recovery State Machine
  void processOrphanRecovery();
  void onRouteReply(const String &src, uint8_t rank, uint16_t cost, const String &parent, int rssi, int snr);

  // Routing Table & Next-Hop Resolution
  String findNextHop(const String &dst);
  void   addOrUpdateRoute(const String &dst, const String &nextHop, int hops, uint16_t cost, int rssi, int snr = 0);
  void   expireRoutes();
  PoleRouteEntry* getRoute(int index);
  int    getRouteCount() const;

  // Multi-hop Forwarding Verification
  bool   shouldForward(const ParsedPacket &pkt, String &outNextHop);
  void   recordForwardedPacket();

  // Revocation & Cleanup (Prompt 12)
  void   removeNeighbor(const String &id);
  void   removeRoute(const String &dst);
  void   purgeRevokedNode(const String &revokedNodeId);

  // Routing Advertisements & Control Packets
  void   onBeaconReceived(const String &src, const String &data, int rssi, int snr);
  void   sendBeacon();
  void   sendRouteRequest();
  void   sendRouteReply(const String &orphanDst);
  void   sendRouteUpdate(const String &reason);
  void   sendRouteRecovered();

  // Health Checks (Prompt 09)
  void   startNeighborHealthCheck(const String &origin, const String &checkId);
  void   checkHealthCheckTimeout();
  void   onNeighborPong(const String &src);
  void   sendHealthReport();

  // Getters for Telemetry & Diagnostics
  uint8_t        getRank() const { return myRank; }
  uint16_t       getPathCost() const { return myPathCost; }
  MeshNodeState  getState() const { return nodeState; }
  String         getStateString() const;
  String         getPrimaryParentId() const { return primaryParent.valid ? primaryParent.nodeId : "NONE"; }
  String         getBackupParentId() const { return backupParent.valid ? backupParent.nodeId : "NONE"; }
  ParentInfo*    getPrimaryParent() { return &primaryParent; }
  ParentInfo*    getBackupParent() { return &backupParent; }
  uint32_t       getForwardedCount() const { return packetsForwarded; }
  uint32_t       getParentSwitchCount() const { return parentSwitchCount; }
  uint32_t       getOrphanEventCount() const { return orphanEventCount; }
  bool           isOrphan() const { return nodeState == MESH_STATE_ORPHAN; }

private:
  MeshNodeState  nodeState;
  uint8_t        myRank;
  uint16_t       myPathCost;
  uint16_t       routeVersion;

  ParentInfo     primaryParent;
  ParentInfo     backupParent;

  NeighborEntry  neighbors[MAX_NEIGHBORS];
  int            neighborCount;

  PoleRouteEntry poleRoutes[MAX_ROUTES_POLE];
  int            poleRouteCount;

  // Orphan state variables
  unsigned long  orphanStartTime;
  unsigned long  nextOrphanRreqMs;
  uint8_t        orphanAttempts;

  // Timing & Hysteresis
  unsigned long  lastParentSwitchMs;
  unsigned long  lastBeaconSentMs;
  unsigned long  lastRouteExpiryMs;
  unsigned long  lastParentHealthCheckMs;

  // Health check variables (Prompt 09)
  bool           healthCheckActive;
  String         currentHealthCheckId;
  String         healthCheckOrigin;
  unsigned long  healthCheckStartMs;
  int            nbPingsSent;
  int            nbPongsReceived;
  bool           nbResponded[MAX_NEIGHBORS];

  // Telemetry counters
  uint32_t       packetsForwarded;
  uint32_t       packetsDroppedTtl;
  uint32_t       packetsDroppedLoop;
  uint32_t       parentSwitchCount;
  uint32_t       orphanEventCount;
};

extern MeshManager meshMgr;

#endif // MESH_MANAGER_H
