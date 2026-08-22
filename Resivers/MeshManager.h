/*
 * =====================================================================
 *  MESHMANAGER.H — FIELD POLE MESH NEIGHBOR & ROUTE MANAGER
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

  // Neighbor management
  void addOrUpdateNeighbor(const String &id, int rssi, int snr);
  void expireNeighbors();
  int  getNeighborCount() const;
  NeighborEntry* getNeighbor(int index);

  // Routing
  String findNextHop(const String &dst);
  void   addOrUpdateRoute(const String &dst, const String &nextHop, int hops, int rssi);

  // Health checks
  void startNeighborHealthCheck(const String &origin, const String &checkId);
  void checkHealthCheckTimeout();
  void onNeighborPong(const String &src);
  void sendHealthReport();
  void sendBeacon();

private:
  NeighborEntry neighbors[MAX_NEIGHBORS];
  int neighborCount;

  PoleRouteEntry poleRoutes[MAX_ROUTES_POLE];
  int poleRouteCount;

  bool   healthCheckActive;
  String currentHealthCheckId;
  String healthCheckOrigin;
  unsigned long healthCheckStartMs;
  int    nbPingsSent;
  int    nbPongsReceived;
  bool   nbResponded[MAX_NEIGHBORS];
};

extern MeshManager meshMgr;

#endif // MESH_MANAGER_H
