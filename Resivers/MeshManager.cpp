/*
 * =====================================================================
 *  MESHMANAGER.CPP — FIELD POLE MESH NEIGHBOR & ROUTE MANAGER (V10)
 *  Two-Parent DAG Mesh Routing with Automatic Failure Detection & Orphan Recovery
 * =====================================================================
 */

#include "MeshManager.h"
#include "LoRaManager.h"
#include "StorageManager.h"

MeshManager meshMgr;

MeshManager::MeshManager() :
  nodeState(MESH_STATE_INIT), myRank(INFINITY_RANK), myPathCost(9999), routeVersion(1),
  neighborCount(0), poleRouteCount(0), orphanStartTime(0), nextOrphanRreqMs(0), orphanAttempts(0),
  lastParentSwitchMs(0), lastBeaconSentMs(0), lastRouteExpiryMs(0), lastParentHealthCheckMs(0),
  healthCheckActive(false), currentHealthCheckId(""), healthCheckOrigin(""), healthCheckStartMs(0),
  nbPingsSent(0), nbPongsReceived(0),
  packetsForwarded(0), packetsDroppedTtl(0), packetsDroppedLoop(0),
  parentSwitchCount(0), orphanEventCount(0)
{
  primaryParent.valid = false;
  primaryParent.nodeId = "NONE";
  primaryParent.rank = INFINITY_RANK;
  primaryParent.cost = 9999;
  primaryParent.upstreamParent = "";
  primaryParent.consecutiveFailures = 0;
  primaryParent.lastSeen = 0;

  backupParent.valid = false;
  backupParent.nodeId = "NONE";
  backupParent.rank = INFINITY_RANK;
  backupParent.cost = 9999;
  backupParent.upstreamParent = "";
  backupParent.consecutiveFailures = 0;
  backupParent.lastSeen = 0;

  for (int i = 0; i < MAX_NEIGHBORS; i++) {
    nbResponded[i] = false;
    neighbors[i] = {};
  }
  for (int i = 0; i < MAX_ROUTES_POLE; i++) {
    poleRoutes[i] = {};
  }
}

void MeshManager::init() {
  nodeState = MESH_STATE_INIT;
  myRank = INFINITY_RANK;
  myPathCost = 9999;
  primaryParent.valid = false;
  backupParent.valid = false;
  unsigned long now = millis();
  lastBeaconSentMs = now;
  lastParentSwitchMs = now;
  lastRouteExpiryMs = now;
  lastParentHealthCheckMs = now;
  Serial.println(F("[POLE][MESH] Mesh Routing Subsystem Initialized (DAG Mode)"));
}

String MeshManager::getStateString() const {
  switch (nodeState) {
    case MESH_STATE_NORMAL:   return "NORMAL";
    case MESH_STATE_DEGRADED: return "DEGRADED";
    case MESH_STATE_ORPHAN:   return "ORPHAN";
    default:                  return "INIT";
  }
}

uint8_t MeshManager::computeLinkQuality(int rssi, int snr, uint8_t pdr) {
  int rScore = constrain((rssi + 120) * 100 / 60, 0, 100);
  int sScore = constrain((snr + 15) * 100 / 25, 0, 100);
  int quality = (rScore * 30 + sScore * 30 + (int)pdr * 40) / 100;
  return (uint8_t)constrain(quality, 0, 100);
}

uint16_t MeshManager::computeLinkCost(uint8_t lq) {
  int cost = ((100 - (int)lq) * 100) / 100 + 10;
  return (uint16_t)constrain(cost, 10, 150);
}

void MeshManager::addOrUpdateNeighbor(const String &id, int rssi, int snr, uint8_t rank, const String &parent, uint16_t cost) {
  String myHw = storageMgr.getHwNodeId();
  String myLog = storageMgr.getNodeId();
  if (id.length() == 0 || id == myHw || (myLog.length() > 0 && id == myLog) || id == BROADCAST_ID) return;

  unsigned long now = millis();
  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].nodeId == id) {
      neighbors[i].rssi = rssi;
      neighbors[i].snr  = snr;
      neighbors[i].lastSeen = now;
      neighbors[i].active = true;
      if (rank != INFINITY_RANK) neighbors[i].rank = rank;
      if (parent.length() > 0)   neighbors[i].primaryParent = parent;
      if (cost != 9999)          neighbors[i].routeCost = cost;
      neighbors[i].linkQuality = computeLinkQuality(rssi, snr, neighbors[i].pdr);
      return;
    }
  }

  if (neighborCount < MAX_NEIGHBORS) {
    int idx = neighborCount++;
    neighbors[idx].nodeId = id;
    neighbors[idx].rssi = rssi;
    neighbors[idx].snr  = snr;
    neighbors[idx].lastSeen = now;
    neighbors[idx].active = true;
    neighbors[idx].healthReplied = false;
    neighbors[idx].rank = rank;
    neighbors[idx].primaryParent = parent;
    neighbors[idx].routeCost = cost;
    neighbors[idx].pdr = 85; // Initial optimistic PDR estimate
    neighbors[idx].txSuccessCount = 1;
    neighbors[idx].txTotalCount = 1;
    neighbors[idx].consecutiveFailures = 0;
    neighbors[idx].linkQuality = computeLinkQuality(rssi, snr, 85);
  }
}

NeighborEntry* MeshManager::findNeighbor(const String &id) {
  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].nodeId == id) return &neighbors[i];
  }
  return nullptr;
}

void MeshManager::recordTxResult(const String &target, bool success) {
  NeighborEntry *nb = findNeighbor(target);
  if (nb) {
    nb->txTotalCount++;
    if (success) {
      nb->txSuccessCount++;
      nb->consecutiveFailures = 0;
    } else {
      nb->consecutiveFailures++;
    }
    if (nb->txTotalCount > 0) {
      nb->pdr = (uint8_t)((nb->txSuccessCount * 100) / nb->txTotalCount);
    }
    nb->linkQuality = computeLinkQuality(nb->rssi, nb->snr, nb->pdr);
  }

  if (primaryParent.valid && primaryParent.nodeId == target) {
    if (success) {
      primaryParent.consecutiveFailures = 0;
    } else {
      primaryParent.consecutiveFailures++;
      Serial.printf("[POLE][DAG] Primary Parent %s TX failure count: %d/%d\n",
                    target.c_str(), primaryParent.consecutiveFailures, MAX_PARENT_FAILURES);
      if (primaryParent.consecutiveFailures >= MAX_PARENT_FAILURES) {
        handlePrimaryParentFailure();
      }
    }
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

bool MeshManager::isCandidateLoopFree(const NeighborEntry &candidate) const {
  String myHw = storageMgr.getHwNodeId();
  String myLog = storageMgr.getNodeId();

  // 1. Never select self
  if (candidate.nodeId == myHw || (myLog.length() > 0 && candidate.nodeId == myLog)) {
    return false;
  }
  // 2. Cannot select orphan node
  if (candidate.rank >= INFINITY_RANK) {
    return false;
  }
  // 3. Candidate must not list this node as its primary parent (direct loop prevention)
  if (candidate.primaryParent == myHw || (myLog.length() > 0 && candidate.primaryParent == myLog)) {
    return false;
  }
  // 4. Strict Rank Monotonicity: Parent rank must be strictly lower than node's rank
  if (myRank != INFINITY_RANK && candidate.rank >= myRank && candidate.primaryParent.length() > 0) {
    return false;
  }
  return true;
}

bool MeshManager::isPathDiverse(const NeighborEntry &candidate, const ParentInfo &primary) const {
  if (!primary.valid) return true;
  if (candidate.nodeId == primary.nodeId) return false;
  // Route diversity: Backup should not depend on the exact same upstream parent
  if (primary.upstreamParent.length() > 0 && candidate.primaryParent == primary.upstreamParent) {
    return false;
  }
  return true;
}

void MeshManager::evaluateAndSelectParents() {
  int bestIdx = -1;
  uint16_t bestCost = 65000;

  int backupIdx = -1;
  uint16_t backupCost = 65000;

  unsigned long now = millis();

  for (int i = 0; i < neighborCount; i++) {
    if (!neighbors[i].active || (now - neighbors[i].lastSeen >= NEIGHBOR_TIMEOUT_MS)) {
      continue;
    }
    if (!isCandidateLoopFree(neighbors[i])) {
      continue;
    }

    uint16_t linkCost = computeLinkCost(neighbors[i].linkQuality);
    uint16_t totalCandidateCost = neighbors[i].routeCost + linkCost + 15;

    if (totalCandidateCost < bestCost) {
      if (bestIdx >= 0 && isPathDiverse(neighbors[bestIdx], primaryParent)) {
        backupIdx = bestIdx;
        backupCost = bestCost;
      }
      bestIdx = i;
      bestCost = totalCandidateCost;
    } else if (totalCandidateCost < backupCost) {
      if (bestIdx >= 0 && isPathDiverse(neighbors[i], primaryParent)) {
        backupIdx = i;
        backupCost = totalCandidateCost;
      }
    }
  }

  if (bestIdx >= 0) {
    NeighborEntry &best = neighbors[bestIdx];

    bool shouldSwitch = false;
    if (!primaryParent.valid || nodeState == MESH_STATE_ORPHAN) {
      shouldSwitch = true;
    } else if (primaryParent.nodeId == best.nodeId) {
      // Same parent: update cost, rank and freshness
      primaryParent.rank = best.rank;
      primaryParent.cost = bestCost;
      primaryParent.upstreamParent = best.primaryParent;
      primaryParent.rssi = best.rssi;
      primaryParent.snr = best.snr;
      primaryParent.lastSeen = best.lastSeen;
      myRank = best.rank + 1;
      myPathCost = bestCost;
    } else {
      // Different candidate: require hysteresis margin and minimum stability window
      if ((int)primaryParent.cost - (int)bestCost >= ROUTE_HYSTERESIS_MARGIN &&
          (now - lastParentSwitchMs >= ROUTE_STABILITY_TIME_MS)) {
        shouldSwitch = true;
      }
    }

    if (shouldSwitch) {
      String oldParent = primaryParent.valid ? primaryParent.nodeId : "NONE";
      primaryParent.nodeId = best.nodeId;
      primaryParent.rank = best.rank;
      primaryParent.cost = bestCost;
      primaryParent.upstreamParent = best.primaryParent;
      primaryParent.rssi = best.rssi;
      primaryParent.snr = best.snr;
      primaryParent.consecutiveFailures = 0;
      primaryParent.lastSeen = best.lastSeen;
      primaryParent.valid = true;

      myRank = best.rank + 1;
      myPathCost = bestCost;
      nodeState = MESH_STATE_NORMAL;
      lastParentSwitchMs = now;
      parentSwitchCount++;

      Serial.printf("[POLE][DAG] Primary Parent set: %s (Rank: %d, Cost: %u, Prev: %s)\n",
                    best.nodeId.c_str(), myRank, myPathCost, oldParent.c_str());

      if (storageMgr.isRegistered()) {
        sendRouteUpdate("PARENT_SELECTED");
      }
    }

    // Select or update Backup Parent
    if (backupIdx >= 0 && backupIdx != bestIdx) {
      NeighborEntry &bak = neighbors[backupIdx];
      backupParent.nodeId = bak.nodeId;
      backupParent.rank = bak.rank;
      backupParent.cost = backupCost;
      backupParent.upstreamParent = bak.primaryParent;
      backupParent.rssi = bak.rssi;
      backupParent.snr = bak.snr;
      backupParent.consecutiveFailures = 0;
      backupParent.lastSeen = bak.lastSeen;
      backupParent.valid = true;
    } else {
      backupParent.valid = false;
    }
  } else if (nodeState != MESH_STATE_ORPHAN) {
    enterOrphanState();
  }
}

void MeshManager::handlePrimaryParentFailure() {
  Serial.printf("[POLE][DAG] ⚠️ Primary Parent %s FAILED!\n", primaryParent.nodeId.c_str());
  primaryParent.valid = false;

  unsigned long now = millis();
  if (backupParent.valid && (now - backupParent.lastSeen < NEIGHBOR_TIMEOUT_MS)) {
    switchToBackupParent("PRIMARY_FAILED");
  } else {
    enterOrphanState();
  }
}

void MeshManager::switchToBackupParent(const String &reason) {
  Serial.printf("[POLE][DAG] 🔄 Switching to Backup Parent: %s (Reason: %s)\n",
                backupParent.nodeId.c_str(), reason.c_str());

  primaryParent = backupParent;
  primaryParent.consecutiveFailures = 0;
  primaryParent.valid = true;
  backupParent.valid = false;

  myRank = primaryParent.rank + 1;
  myPathCost = primaryParent.cost;
  lastParentSwitchMs = millis();
  parentSwitchCount++;

  // ELECTRICAL SAFETY: Local relays and MCB protection are untouched and maintained!

  if (storageMgr.isRegistered()) {
    sendRouteUpdate(reason);
  }

  evaluateAndSelectParents();
}

void MeshManager::enterOrphanState() {
  if (nodeState == MESH_STATE_ORPHAN) return;

  Serial.println(F("[POLE][DAG] 🚨 ENTERING ORPHAN STATE: Lost all valid routes to Gateway!"));
  nodeState = MESH_STATE_ORPHAN;
  myRank = INFINITY_RANK;
  myPathCost = 9999;
  primaryParent.valid = false;
  backupParent.valid = false;
  orphanStartTime = millis();
  orphanAttempts = 0;
  nextOrphanRreqMs = millis();
  orphanEventCount++;

  // ELECTRICAL SAFETY: Relay state and autonomous local schedules are 100% preserved!
}

void MeshManager::exitOrphanState(const String &newPrimaryId, uint8_t newRank, uint16_t newCost) {
  nodeState = MESH_STATE_NORMAL;
  myRank = newRank;
  myPathCost = newCost;
  Serial.printf("[POLE][ORPHAN] 🎉 Recovered from orphan state! Primary: %s (Rank: %d, Cost: %u)\n",
                newPrimaryId.c_str(), myRank, myPathCost);
  sendRouteRecovered();
}

void MeshManager::processOrphanRecovery() {
  if (nodeState != MESH_STATE_ORPHAN) return;

  unsigned long now = millis();
  if (now >= nextOrphanRreqMs) {
    orphanAttempts++;
    sendRouteRequest();

    unsigned long backoff = ORPHAN_RREQ_BASE_INTERVAL * (1 << min((int)orphanAttempts - 1, 3));
    if (backoff > ORPHAN_RREQ_MAX_INTERVAL) backoff = ORPHAN_RREQ_MAX_INTERVAL;
    backoff += random(0, 1000); // Jitter to prevent channel collision
    nextOrphanRreqMs = now + backoff;

    Serial.printf("[POLE][ORPHAN] RREQ Broadcast sent (Attempt %d, next in %lu ms)\n",
                  orphanAttempts, backoff);
  }
}

void MeshManager::onRouteReply(const String &src, uint8_t rank, uint16_t cost, const String &parent, int rssi, int snr) {
  Serial.printf("[POLE][ORPHAN] RREP from %s (Rank: %d, Cost: %u, Parent: %s, RSSI: %d)\n",
                src.c_str(), rank, cost, parent.c_str(), rssi);

  addOrUpdateNeighbor(src, rssi, snr, rank, parent, cost);

  if (nodeState == MESH_STATE_ORPHAN && rank < INFINITY_RANK) {
    evaluateAndSelectParents();
    if (primaryParent.valid && nodeState == MESH_STATE_NORMAL) {
      sendRouteRecovered();
    }
  }
}

String MeshManager::findNextHop(const String &dst) {
  if (dst == BROADCAST_ID || dst == "ALL") return BROADCAST_ID;
  if (dst == GATEWAY_ID) {
    if (primaryParent.valid) return primaryParent.nodeId;
    if (backupParent.valid)  return backupParent.nodeId;
    return BROADCAST_ID;
  }

  // Look up downstream routing table
  for (int i = 0; i < poleRouteCount; i++) {
    if (poleRoutes[i].destination == dst && poleRoutes[i].valid) {
      return poleRoutes[i].nextHop;
    }
  }

  // Check if dst is a direct neighbor
  NeighborEntry *nb = findNeighbor(dst);
  if (nb && nb->active) return dst;

  return dst;
}

void MeshManager::addOrUpdateRoute(const String &dst, const String &nextHop, int hops, uint16_t cost, int rssi, int snr) {
  String myHw = storageMgr.getHwNodeId();
  String myLog = storageMgr.getNodeId();
  if (dst.length() == 0 || dst == myHw || (myLog.length() > 0 && dst == myLog) || dst == GATEWAY_ID) return;

  unsigned long now = millis();
  for (int i = 0; i < poleRouteCount; i++) {
    if (poleRoutes[i].destination == dst) {
      if (hops <= poleRoutes[i].hopCount || !poleRoutes[i].valid) {
        poleRoutes[i].nextHop = nextHop;
        poleRoutes[i].hopCount = hops;
        poleRoutes[i].routeCost = cost;
        poleRoutes[i].rssi = rssi;
        poleRoutes[i].snr = snr;
        poleRoutes[i].valid = true;
        poleRoutes[i].lastSeen = now;
      }
      return;
    }
  }

  if (poleRouteCount < MAX_ROUTES_POLE) {
    int idx = poleRouteCount++;
    poleRoutes[idx].destination = dst;
    poleRoutes[idx].nextHop = nextHop;
    poleRoutes[idx].hopCount = hops;
    poleRoutes[idx].routeCost = cost;
    poleRoutes[idx].rssi = rssi;
    poleRoutes[idx].snr = snr;
    poleRoutes[idx].valid = true;
    poleRoutes[idx].lastSeen = now;
    poleRoutes[idx].routeVersion = routeVersion;
  }
}

void MeshManager::expireRoutes() {
  unsigned long now = millis();
  for (int i = 0; i < poleRouteCount; i++) {
    if (now - poleRoutes[i].lastSeen >= ROUTE_TIMEOUT_MS) {
      poleRoutes[i].valid = false;
    }
  }
}

PoleRouteEntry* MeshManager::getRoute(int index) {
  if (index >= 0 && index < poleRouteCount) return &poleRoutes[index];
  return nullptr;
}

int MeshManager::getRouteCount() const { return poleRouteCount; }

bool MeshManager::shouldForward(const ParsedPacket &pkt, String &outNextHop) {
  String myHw = storageMgr.getHwNodeId();
  String myLog = storageMgr.getNodeId();

  // 1. If broadcast, forward if TTL > 1 and not originated by me
  if (pkt.dst == BROADCAST_ID || pkt.dst == "ALL") {
    outNextHop = BROADCAST_ID;
    return (pkt.ttl > 1 && pkt.src != myLog && pkt.src != myHw);
  }

  // 2. Check if addressed to this node as intermediate router
  bool isRouter = (myLog.length() > 0 && pkt.nextHop == myLog) ||
                  (myHw.length() > 0 && pkt.nextHop == myHw);
  if (!isRouter) return false;

  // 3. TTL check
  if (pkt.ttl <= 1) {
    packetsDroppedTtl++;
    Serial.printf("[POLE][FWD] ❌ Dropped: TTL expired (Src: %s, Dst: %s)\n",
                  pkt.src.c_str(), pkt.dst.c_str());
    return false;
  }

  // 4. Resolve next hop
  outNextHop = findNextHop(pkt.dst);

  // 5. Loop prevention
  if (outNextHop == pkt.src || outNextHop == pkt.sender) {
    packetsDroppedLoop++;
    Serial.printf("[POLE][FWD] ❌ Dropped: Loop detected (NextHop %s equals sender/source)\n",
                  outNextHop.c_str());
    return false;
  }

  if (pkt.dst == GATEWAY_ID && nodeState == MESH_STATE_ORPHAN) {
    Serial.println(F("[POLE][FWD] ❌ Cannot forward uplink: Node is orphaned!"));
    return false;
  }

  return true;
}

void MeshManager::recordForwardedPacket() {
  packetsForwarded++;
}

void MeshManager::onBeaconReceived(const String &src, const String &data, int rssi, int snr) {
  // Beacon format: RANK=r|PARENT=p|COST=c|VER=v|CHILDREN=k
  uint8_t rank = INFINITY_RANK;
  String parent = "";
  uint16_t cost = 9999;

  int idxRank = data.indexOf("RANK=");
  if (idxRank >= 0) {
    int endRank = data.indexOf('|', idxRank);
    rank = (uint8_t)data.substring(idxRank + 5, endRank > 0 ? endRank : data.length()).toInt();
  }

  int idxParent = data.indexOf("PARENT=");
  if (idxParent >= 0) {
    int endParent = data.indexOf('|', idxParent);
    parent = data.substring(idxParent + 7, endParent > 0 ? endParent : data.length());
  }

  int idxCost = data.indexOf("COST=");
  if (idxCost >= 0) {
    int endCost = data.indexOf('|', idxCost);
    cost = (uint16_t)data.substring(idxCost + 5, endCost > 0 ? endCost : data.length()).toInt();
  }

  addOrUpdateNeighbor(src, rssi, snr, rank, parent, cost);

  // Re-evaluate parents based on new topological information
  evaluateAndSelectParents();
}

void MeshManager::sendBeacon() {
  char buf[96];
  snprintf(buf, sizeof(buf), "RANK=%d|PARENT=%s|COST=%u|VER=%u|CHILDREN=%d",
           myRank, primaryParent.valid ? primaryParent.nodeId.c_str() : "NONE",
           myPathCost, routeVersion, poleRouteCount);

  loraMgr.txMesh("BEACON", BROADCAST_ID, BROADCAST_ID, 1, loraMgr.nextSeq(), String(buf));
  lastBeaconSentMs = millis();
}

void MeshManager::sendRouteRequest() {
  char buf[64];
  snprintf(buf, sizeof(buf), "REQ_PARENT|ORPHAN=1|ATTEMPT=%d", orphanAttempts);
  loraMgr.txMesh("RREQ", BROADCAST_ID, BROADCAST_ID, 1, loraMgr.nextSeq(), String(buf));
}

void MeshManager::sendRouteReply(const String &orphanDst) {
  if (nodeState == MESH_STATE_ORPHAN || myRank >= INFINITY_RANK) return;

  char buf[96];
  snprintf(buf, sizeof(buf), "RANK=%d|PARENT=%s|COST=%u|CHILDREN=%d",
           myRank, primaryParent.valid ? primaryParent.nodeId.c_str() : "GATEWAY",
           myPathCost, poleRouteCount);

  loraMgr.txMesh("RREP", orphanDst, orphanDst, 1, loraMgr.nextSeq(), String(buf));
  Serial.printf("[POLE][MESH] Sent RREP to orphan %s (MyRank: %d, Cost: %u)\n",
                orphanDst.c_str(), myRank, myPathCost);
}

void MeshManager::sendRouteUpdate(const String &reason) {
  String mySrc = storageMgr.getNodeId().length() > 0 ? storageMgr.getNodeId() : storageMgr.getHwNodeId();
  char buf[96];
  snprintf(buf, sizeof(buf), "NODE=%s|PRIMARY=%s|BACKUP=%s|RANK=%d|COST=%u|REASON=%s",
           mySrc.c_str(),
           primaryParent.valid ? primaryParent.nodeId.c_str() : "NONE",
           backupParent.valid ? backupParent.nodeId.c_str() : "NONE",
           myRank, myPathCost, reason.c_str());

  String nh = findNextHop(GATEWAY_ID);
  loraMgr.txMesh("ROUTE_UPDATE", GATEWAY_ID, nh, DEFAULT_TTL, loraMgr.nextSeq(), String(buf));
}

void MeshManager::sendRouteRecovered() {
  String mySrc = storageMgr.getNodeId().length() > 0 ? storageMgr.getNodeId() : storageMgr.getHwNodeId();
  char buf[96];
  snprintf(buf, sizeof(buf), "NODE=%s|PRIMARY=%s|BACKUP=%s|RANK=%d|COST=%u|STATUS=RECOVERED",
           mySrc.c_str(),
           primaryParent.valid ? primaryParent.nodeId.c_str() : "NONE",
           backupParent.valid ? backupParent.nodeId.c_str() : "NONE",
           myRank, myPathCost);

  String nh = findNextHop(GATEWAY_ID);
  loraMgr.txMesh("ROUTE_RECOVERED", GATEWAY_ID, nh, DEFAULT_TTL, loraMgr.nextSeq(), String(buf));
}

void MeshManager::processPeriodicTasks() {
  unsigned long now = millis();

  // 1. Periodic Beacon Broadcast (Every 30s)
  if (now - lastBeaconSentMs >= BEACON_INTERVAL_MS) {
    expireNeighbors();
    sendBeacon();
  }

  // 2. Periodic Downstream Route Expiry (Every 60s)
  if (now - lastRouteExpiryMs >= 60000UL) {
    lastRouteExpiryMs = now;
    expireRoutes();
  }

  // 3. Parent Freshness Monitoring (Every 10s)
  if (now - lastParentHealthCheckMs >= 10000UL) {
    lastParentHealthCheckMs = now;
    if (primaryParent.valid && (now - primaryParent.lastSeen >= PARENT_TIMEOUT_MS)) {
      Serial.printf("[POLE][DAG] Primary Parent %s timed out (> %lu ms)!\n",
                    primaryParent.nodeId.c_str(), PARENT_TIMEOUT_MS);
      handlePrimaryParentFailure();
    }
  }

  // 4. Orphan Recovery State Machine
  if (nodeState == MESH_STATE_ORPHAN) {
    processOrphanRecovery();
  }
}

// ─────────────────────────────────────────────────────────────
//  HEALTH CHECK METHODS (PROMPT 09)
// ─────────────────────────────────────────────────────────────
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
      recordTxResult(src, true);
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

void MeshManager::removeNeighbor(const String &id) {
  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].nodeId == id) {
      for (int j = i; j < neighborCount - 1; j++) {
        neighbors[j] = neighbors[j + 1];
      }
      neighborCount--;
      i--;
    }
  }
}

void MeshManager::removeRoute(const String &dst) {
  for (int i = 0; i < poleRouteCount; i++) {
    if (poleRoutes[i].destination == dst || poleRoutes[i].nextHop == dst) {
      poleRoutes[i].valid = false;
      for (int j = i; j < poleRouteCount - 1; j++) {
        poleRoutes[j] = poleRoutes[j + 1];
      }
      poleRouteCount--;
      i--;
    }
  }
}

void MeshManager::purgeRevokedNode(const String &revokedNodeId) {
  removeNeighbor(revokedNodeId);
  removeRoute(revokedNodeId);
  if (primaryParent.valid && primaryParent.nodeId == revokedNodeId) {
    Serial.printf("[POLE][DAG] ⚠️ Primary Parent %s was REVOKED! Switching to backup parent.\n", revokedNodeId.c_str());
    switchToBackupParent("PARENT_REVOKED");
  } else if (backupParent.valid && backupParent.nodeId == revokedNodeId) {
    Serial.printf("[POLE][DAG] ⚠️ Backup Parent %s was REVOKED! Invalidating backup.\n", revokedNodeId.c_str());
    backupParent.valid = false;
    backupParent.nodeId = "NONE";
  }
}
