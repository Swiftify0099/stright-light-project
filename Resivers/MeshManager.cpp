/*
 * =====================================================================
 *  MESHMANAGER.CPP — FIELD POLE MESH NEIGHBOR & ROUTE MANAGER
 * =====================================================================
 */

#include "MeshManager.h"
#include "LoRaManager.h"
#include "StorageManager.h"

MeshManager meshMgr;

MeshManager::MeshManager() :
  neighborCount(0), poleRouteCount(0), healthCheckActive(false),
  currentHealthCheckId(""), healthCheckOrigin(""), healthCheckStartMs(0),
  nbPingsSent(0), nbPongsReceived(0)
{}

void MeshManager::init() {}

void MeshManager::addOrUpdateNeighbor(const String &id, int rssi, int snr) {
  String myHw = storageMgr.getHwNodeId();
  String myLog = storageMgr.getNodeId();
  if (id.length() == 0 || id == myHw || (myLog.length() > 0 && id == myLog)) return;

  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].nodeId == id) {
      neighbors[i].rssi = rssi;
      neighbors[i].snr  = snr;
      neighbors[i].lastSeen = millis();
      neighbors[i].active = true;
      return;
    }
  }

  if (neighborCount < MAX_NEIGHBORS) {
    neighbors[neighborCount].nodeId = id;
    neighbors[neighborCount].rssi = rssi;
    neighbors[neighborCount].snr  = snr;
    neighbors[neighborCount].lastSeen = millis();
    neighbors[neighborCount].active = true;
    neighbors[neighborCount].healthReplied = false;
    neighborCount++;
  }
}

void MeshManager::expireNeighbors() {
  unsigned long now = millis();
  for (int i = 0; i < neighborCount; i++) {
    if (now - neighbors[i].lastSeen >= NEIGHBOR_TIMEOUT_MS) {
      neighbors[i].active = false;
    }
  }
}

int MeshManager::getNeighborCount() const { return neighborCount; }

NeighborEntry* MeshManager::getNeighbor(int index) {
  if (index >= 0 && index < neighborCount) return &neighbors[index];
  return nullptr;
}

String MeshManager::findNextHop(const String &dst) {
  if (dst == BROADCAST_ID || dst == "ALL") return BROADCAST_ID;
  if (dst == GATEWAY_ID) return GATEWAY_ID;

  for (int i = 0; i < poleRouteCount; i++) {
    if (poleRoutes[i].destination == dst && poleRoutes[i].valid) {
      return poleRoutes[i].nextHop;
    }
  }
  return dst;
}

void MeshManager::addOrUpdateRoute(const String &dst, const String &nextHop, int hops, int rssi) {
  String myHw = storageMgr.getHwNodeId();
  String myLog = storageMgr.getNodeId();
  if (dst.length() == 0 || dst == myHw || (myLog.length() > 0 && dst == myLog)) return;

  for (int i = 0; i < poleRouteCount; i++) {
    if (poleRoutes[i].destination == dst) {
      if (hops <= poleRoutes[i].hopCount || !poleRoutes[i].valid) {
        poleRoutes[i].nextHop = nextHop;
        poleRoutes[i].hopCount = hops;
        poleRoutes[i].rssi = rssi;
        poleRoutes[i].valid = true;
        poleRoutes[i].lastSeen = millis();
      }
      return;
    }
  }

  if (poleRouteCount < MAX_ROUTES_POLE) {
    poleRoutes[poleRouteCount].destination = dst;
    poleRoutes[poleRouteCount].nextHop = nextHop;
    poleRoutes[poleRouteCount].hopCount = hops;
    poleRoutes[poleRouteCount].rssi = rssi;
    poleRoutes[poleRouteCount].valid = true;
    poleRoutes[poleRouteCount].lastSeen = millis();
    poleRouteCount++;
  }
}

void MeshManager::sendBeacon() {
  char bData[64];
  snprintf(bData, sizeof(bData), "HW_ID=%s|REG=%d",
           storageMgr.getHwNodeId().c_str(), storageMgr.isRegistered() ? 1 : 0);
  loraMgr.txMesh("BEACON", BROADCAST_ID, BROADCAST_ID, 1, loraMgr.nextSeq(), String(bData));
}

void MeshManager::startNeighborHealthCheck(const String &origin, const String &checkId) {
  Serial.printf("[POLE][MESH] Health check requested by %s (CheckID: %s)\n", origin.c_str(), checkId.c_str());
  healthCheckOrigin = origin;
  currentHealthCheckId = checkId;
  healthCheckActive = true;
  healthCheckStartMs = millis();
  nbPingsSent = 0;
  nbPongsReceived = 0;

  for (int i = 0; i < MAX_NEIGHBORS; i++) nbResponded[i] = false;

  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].active) {
      loraMgr.txMesh("NB_PING", neighbors[i].nodeId, neighbors[i].nodeId, 1, loraMgr.nextSeq(), currentHealthCheckId);
      nbPingsSent++;
      delay(20);
    }
  }

  if (nbPingsSent == 0) {
    healthCheckActive = false;
    sendHealthReport();
  }
}

void MeshManager::onNeighborPong(const String &src) {
  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].nodeId == src) {
      nbResponded[i] = true;
      nbPongsReceived++;
      break;
    }
  }
}

void MeshManager::checkHealthCheckTimeout() {
  if (!healthCheckActive) return;

  unsigned long now = millis();
  bool timeout = (now - healthCheckStartMs > HEALTH_CHECK_TIMEOUT_MS);
  bool allDone = (nbPongsReceived >= nbPingsSent && nbPingsSent > 0);

  if (timeout || allDone) {
    healthCheckActive = false;
    sendHealthReport();
  }
}

void MeshManager::sendHealthReport() {
  String rep = String(currentHealthCheckId) + "|" + String(millis() / 1000) + "|" + String(neighborCount);
  unsigned long now = millis();

  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].active) {
      rep += "|" + neighbors[i].nodeId + "," + (nbResponded[i] ? "1" : "0") + "," +
             String(neighbors[i].rssi) + "," + String(neighbors[i].snr) + "," +
             String((now - neighbors[i].lastSeen) / 1000);
    }
  }

  loraMgr.txMesh("NBHEALTHREPORT", GATEWAY_ID, findNextHop(GATEWAY_ID), DEFAULT_TTL, loraMgr.nextSeq(), rep);
  Serial.printf("[POLE][NEIGHBOR] Health report sent: %s\n", rep.c_str());
}
