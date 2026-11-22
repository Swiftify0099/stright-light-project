/*
 * =====================================================================
 *  MESHMANAGER.H — GATEWAY NODE & ROUTE TOPOLOGY MANAGER (V10)
 *  Gateway-Rooted DAG Coordinator & Multi-Hop Route Engine
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

  // Node database access
  int  getNodeCount() const;
  int  getNodeCountByType(const String &type) const;
  NodeRecord* getNode(int index);
  NodeRecord* getNodeByTypeIndex(const String &type, int filteredIndex);
  NodeRecord* findNode(const String &id);
  int  findNodeIndex(const String &id);
  int  addOrUpdateNode(const String &hwId, const String &logicalId, const String &type, int rssi, int snr);
  
  String generateNextLogicalId(const String &type);

  // Mesh Route Resolution & Updates
  String findRouteNextHop(const String &dst);
  void   updateRouteTable(const String &dst, const String &nextHop, int hops, int cost, int rssi, int snr = 0, const String &routePath = "");
  void   updateRouteTable(const String &dst, const String &nextHop, int hops, int rssi); // Backward compatibility
  void   expireRoutes();

  // Mesh Event Handlers
  void   onNeighborHealthReport(const String &reportingNode, const String &data, int rssi);
  void   onRouteUpdate(const String &reportingNode, const String &data, int rssi, int snr);
  void   onRouteRecovered(const String &reportingNode, const String &data, int rssi, int snr);

  // Pending Registrations
  PendingRegistration* getPendingReg(int index);
  int  findPendingRegIndex(const String &hwId);
  int  addPendingReg(const String &hwId, int rssi);
  void clearPendingReg(int index);

  // ID Counters
  int  getNextStreetLightId() const;
  int  getNextFarmerId() const;
  void setNextStreetLightId(int id);
  void setNextFarmerId(int id);

  // Route & Neighbor Diagnostics
  GwRouteEntry* getRoute(int index);
  int  getRouteCount() const;
  NeighborSummary* getNeighborSummary(int index);
  int  getNeighborSummaryCount() const;

  // Route cleanup and migration (Prompt 12)
  void removeRoute(const String &destination);
  void removeRoutesThrough(const String &nextHop);
  void updateNodeParent(const String &childId, const String &primary, const String &backup, int hops);
  void markNodeDeregistered(const String &id);

  // Clean Telegram Integration APIs (Prompt 10 Section 12)
  int      getTotalRegisteredNodes() const;
  int      getOnlineNodeCount() const;
  int      getOfflineNodeCount() const;
  int      getOrphanNodeCount() const;
  float    getAverageHopCount() const;
  uint32_t getRouteEventCount() const { return totalRouteEvents; }
  uint32_t getRouteRecoveryCount() const { return totalRouteRecoveries; }
  bool     getNodeRoutingInfo(const String &id, String &primaryParent, String &backupParent,
                              int &hops, int &cost, int &rssi, int &snr,
                              unsigned long &lastUpdate, bool &isOrphan);

private:
  NodeRecord nodes[MAX_NODES];
  int nodeCount;

  GwRouteEntry gwRoutes[MAX_ROUTES];
  int gwRouteCount;

  NeighborSummary nbSummaries[MAX_NODES * 2];
  uint8_t nbSummaryCount;

  PendingRegistration pendingRegs[MAX_PENDING_REG];
  int nextStreetLightId;
  int nextFarmerId;

  // Global Mesh Topology Statistics
  uint32_t totalRouteEvents;
  uint32_t totalRouteRecoveries;
};

extern MeshManager meshMgr;

#endif // MESH_MANAGER_H
