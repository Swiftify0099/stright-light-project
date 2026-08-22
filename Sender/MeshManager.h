/*
 * =====================================================================
 *  MESHMANAGER.H — GATEWAY NODE & ROUTE TOPOLOGY MANAGER
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

  int  getNodeCount() const;
  int  getNodeCountByType(const String &type) const;
  NodeRecord* getNode(int index);
  NodeRecord* getNodeByTypeIndex(const String &type, int filteredIndex);
  NodeRecord* findNode(const String &id);
  int  findNodeIndex(const String &id);
  int  addOrUpdateNode(const String &hwId, const String &logicalId, const String &type, int rssi, int snr);
  
  String generateNextLogicalId(const String &type);

  String findRouteNextHop(const String &dst);
  void updateRouteTable(const String &dst, const String &nextHop, int hops, int rssi);
  void onNeighborHealthReport(const String &reportingNode, const String &data, int rssi);

  PendingRegistration* getPendingReg(int index);
  int  findPendingRegIndex(const String &hwId);
  int  addPendingReg(const String &hwId, int rssi);
  void clearPendingReg(int index);

  int  getNextStreetLightId() const;
  int  getNextFarmerId() const;
  void setNextStreetLightId(int id);
  void setNextFarmerId(int id);

  GwRouteEntry* getRoute(int index);
  int  getRouteCount() const;

  NeighborSummary* getNeighborSummary(int index);
  int  getNeighborSummaryCount() const;

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
};

extern MeshManager meshMgr;

#endif // MESH_MANAGER_H
