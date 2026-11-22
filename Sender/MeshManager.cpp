/*
 * =====================================================================
 *  MESHMANAGER.CPP — GATEWAY NODE & ROUTE TOPOLOGY MANAGER (V10)
 *  Gateway-Rooted DAG Coordinator & Multi-Hop Route Engine
 * =====================================================================
 */

#include "MeshManager.h"

MeshManager meshMgr;

MeshManager::MeshManager() :
  nodeCount(0), gwRouteCount(0), nbSummaryCount(0),
  nextStreetLightId(1), nextFarmerId(1),
  totalRouteEvents(0), totalRouteRecoveries(0)
{
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    pendingRegs[i].valid = false;
  }
  for (int i = 0; i < MAX_ROUTES; i++) {
    gwRoutes[i] = {};
  }
}

void MeshManager::init() {
  for (int n = 0; n < MAX_NODES; n++) {
    for (int f = 0; f < MAX_FEEDERS; f++) {
      nodes[n].feeders[f].feederId = f + 1;
      nodes[n].feeders[f].name = String(FEEDER_DEFAULT_NAMES[f]);
      nodes[n].feeders[f].onlineState = "OFFLINE";
      nodes[n].feeders[f].relayState = "OFF";
      nodes[n].feeders[f].voltage1 = 0.0f;
      nodes[n].feeders[f].voltage2 = 0.0f;
      nodes[n].feeders[f].voltage1Valid = false;
      nodes[n].feeders[f].voltage2Valid = false;
      nodes[n].feeders[f].voltage1State = "DISCONNECTED";
      nodes[n].feeders[f].voltage2State = "DISCONNECTED";
      nodes[n].feeders[f].lastVoltageUpdate = 0;
      nodes[n].feeders[f].lastHeartbeat = 0;
      nodes[n].feeders[f].loraState = "OFFLINE";
      nodes[n].feeders[f].faultState = "OK";
      nodes[n].feeders[f].mcbR = true;
      nodes[n].feeders[f].mcbY = true;
      nodes[n].feeders[f].mcbB = true;
      nodes[n].feeders[f].contactor = false;
      nodes[n].feeders[f].protectionEnabled = true;
    }
  }
  Serial.println(F("[GW][MESH] Gateway Root Routing Subsystem Initialized (DAG Root: Rank 0)"));
}

int MeshManager::getNodeCount() const {
  return nodeCount;
}

int MeshManager::getNodeCountByType(const String &type) const {
  int count = 0;
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].type == type) count++;
  }
  return count;
}

NodeRecord* MeshManager::getNode(int index) {
  if (index >= 0 && index < nodeCount) {
    return &nodes[index];
  }
  return nullptr;
}

NodeRecord* MeshManager::getNodeByTypeIndex(const String &type, int filteredIndex) {
  int cur = 0;
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].type == type) {
      if (cur == filteredIndex) return &nodes[i];
      cur++;
    }
  }
  return nullptr;
}

NodeRecord* MeshManager::findNode(const String &id) {
  int idx = findNodeIndex(id);
  if (idx >= 0) return &nodes[idx];
  return nullptr;
}

int MeshManager::findNodeIndex(const String &id) {
  if (id.length() == 0) return -1;
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].logicalId == id || nodes[i].hwId == id) {
      return i;
    }
  }
  return -1;
}

String MeshManager::generateNextLogicalId(const String &type) {
  char buf[16];
  if (type == "FARMER") {
    snprintf(buf, sizeof(buf), "FR-%04d", nextFarmerId++);
  } else {
    snprintf(buf, sizeof(buf), "SL-%04d", nextStreetLightId++);
  }
  return String(buf);
}

int MeshManager::addOrUpdateNode(const String &hwId, const String &logicalId, const String &type, int rssi, int snr) {
  int idx = findNodeIndex(hwId);
  if (idx >= 0) {
    nodes[idx].rssi = rssi;
    nodes[idx].snr  = snr;
    nodes[idx].lastSeen = millis();
    nodes[idx].onlineState = "ONLINE";
    if (logicalId.length() > 0) nodes[idx].logicalId = logicalId;
    if (type.length() > 0) nodes[idx].type = type;
    return idx;
  }

  if (nodeCount < MAX_NODES) {
    int i = nodeCount++;
    nodes[i] = {};
    nodes[i].hwId = hwId;
    nodes[i].type = (type.length() > 0) ? type : "STREET";
    nodes[i].logicalId = (logicalId.length() > 0) ? logicalId : hwId;
    
    if (nodes[i].type == "FARMER") {
      nodes[i].name = "Farm Pump " + nodes[i].logicalId;
      nodes[i].location = "Farm Station";
    } else {
      nodes[i].name = "Pole " + nodes[i].logicalId;
      nodes[i].location = "Field Area";
    }

    nodes[i].registered = false;
    nodes[i].onlineState = "ONLINE";
    nodes[i].rssi = rssi;
    nodes[i].snr  = snr;
    nodes[i].lightState = "OFF";
    nodes[i].voltage = 0.0f;
    nodes[i].voltageState = "DISCONNECTED";
    nodes[i].voltageUpdatedAt = 0;
    nodes[i].current = 0.0f;
    nodes[i].power = 0.0f;
    nodes[i].totalKwh = 0.0f;
    nodes[i].fault = "OK";
    nodes[i].prevFault = "OK";
    nodes[i].firstSeen = millis();
    nodes[i].lastSeen = millis();
    nodes[i].primaryNextHop = BROADCAST_ID;
    nodes[i].backupNextHop = BROADCAST_ID;
    nodes[i].hopCount = 1;
    nodes[i].routeCost = 10;
    nodes[i].rank = 1;
    nodes[i].routePath = "Gateway -> " + nodes[i].logicalId;
    nodes[i].connectionMode = "DIRECT";
    nodes[i].orphan = false;
    nodes[i].recoveryEvents = 0;
    nodes[i].lastRouteUpdate = millis();
    nodes[i].cmdGeneration = 0;
    nodes[i].neighborCount = 0;
    nodes[i].neighborData = "";

    // Init feeder sub-records
    for (int f = 0; f < MAX_FEEDERS; f++) {
      nodes[i].feeders[f].feederId = f + 1;
      nodes[i].feeders[f].name = String(FEEDER_DEFAULT_NAMES[f]);
      nodes[i].feeders[f].onlineState = "OFFLINE";
      nodes[i].feeders[f].relayState = "OFF";
      nodes[i].feeders[f].voltage1 = 0.0f;
      nodes[i].feeders[f].voltage2 = 0.0f;
      nodes[i].feeders[f].voltage1Valid = false;
      nodes[i].feeders[f].voltage2Valid = false;
      nodes[i].feeders[f].voltage1State = "DISCONNECTED";
      nodes[i].feeders[f].voltage2State = "DISCONNECTED";
      nodes[i].feeders[f].lastVoltageUpdate = 0;
      nodes[i].feeders[f].lastHeartbeat = 0;
      nodes[i].feeders[f].loraState = "OFFLINE";
      nodes[i].feeders[f].faultState = "OK";
      nodes[i].feeders[f].mcbR = true;
      nodes[i].feeders[f].mcbY = true;
      nodes[i].feeders[f].mcbB = true;
      nodes[i].feeders[f].contactor = false;
      nodes[i].feeders[f].protectionEnabled = true;
    }

    Serial.printf("[GW][DB] Node registered in DB: %s (Type: %s, HW: %s)\n",
                  nodes[i].logicalId.c_str(), nodes[i].type.c_str(), hwId.c_str());
    return i;
  }
  return -1;
}

String MeshManager::findRouteNextHop(const String &dst) {
  if (dst == BROADCAST_ID || dst == "ALL") return BROADCAST_ID;

  for (int i = 0; i < gwRouteCount; i++) {
    if (gwRoutes[i].destination == dst && gwRoutes[i].valid) {
      return gwRoutes[i].primaryNextHop;
    }
  }

  int idx = findNodeIndex(dst);
  if (idx >= 0 && nodes[idx].primaryNextHop.length() > 0 && nodes[idx].primaryNextHop != BROADCAST_ID) {
    return nodes[idx].primaryNextHop;
  }
  return BROADCAST_ID;
}

void MeshManager::updateRouteTable(const String &dst, const String &nextHop, int hops, int cost, int rssi, int snr, const String &routePath) {
  if (dst.length() == 0 || dst == GATEWAY_ID || dst == BROADCAST_ID) return;

  unsigned long now = millis();
  for (int i = 0; i < gwRouteCount; i++) {
    if (gwRoutes[i].destination == dst) {
      if (hops <= gwRoutes[i].hopCount || !gwRoutes[i].valid || (cost > 0 && cost < gwRoutes[i].routeCost)) {
        gwRoutes[i].primaryNextHop = nextHop;
        gwRoutes[i].hopCount = hops;
        if (cost > 0) gwRoutes[i].routeCost = cost;
        gwRoutes[i].rssi = rssi;
        gwRoutes[i].snr = snr;
        gwRoutes[i].valid = true;
        gwRoutes[i].lastSuccess = now;
        gwRoutes[i].lastUpdate = now;
        if (routePath.length() > 0) gwRoutes[i].routePath = routePath;
      }
      // Update NodeRecord as well
      int nIdx = findNodeIndex(dst);
      if (nIdx >= 0) {
        nodes[nIdx].primaryNextHop = nextHop;
        nodes[nIdx].hopCount = hops;
        if (cost > 0) nodes[nIdx].routeCost = cost;
        nodes[nIdx].lastRouteUpdate = now;
        nodes[nIdx].orphan = false;
        nodes[nIdx].connectionMode = (hops == 1) ? "DIRECT" : "MESH";
      }
      return;
    }
  }

  if (gwRouteCount < MAX_ROUTES) {
    int idx = gwRouteCount++;
    gwRoutes[idx].destination = dst;
    gwRoutes[idx].primaryNextHop = nextHop;
    gwRoutes[idx].backupNextHop = BROADCAST_ID;
    gwRoutes[idx].hopCount = hops;
    gwRoutes[idx].routeCost = (cost > 0) ? cost : (hops * 25);
    gwRoutes[idx].rssi = rssi;
    gwRoutes[idx].snr = snr;
    gwRoutes[idx].valid = true;
    gwRoutes[idx].lastSuccess = now;
    gwRoutes[idx].lastUpdate = now;
    gwRoutes[idx].routePath = (routePath.length() > 0) ? routePath : ("Gateway -> " + nextHop + " -> " + dst);

    int nIdx = findNodeIndex(dst);
    if (nIdx >= 0) {
      nodes[nIdx].primaryNextHop = nextHop;
      nodes[nIdx].hopCount = hops;
      nodes[nIdx].routeCost = gwRoutes[idx].routeCost;
      nodes[nIdx].lastRouteUpdate = now;
      nodes[nIdx].orphan = false;
      nodes[nIdx].connectionMode = (hops == 1) ? "DIRECT" : "MESH";
    }
  }
}

void MeshManager::updateRouteTable(const String &dst, const String &nextHop, int hops, int rssi) {
  updateRouteTable(dst, nextHop, hops, hops * 25, rssi, 0, "");
}

void MeshManager::expireRoutes() {
  unsigned long now = millis();
  for (int i = 0; i < gwRouteCount; i++) {
    if (gwRoutes[i].valid && (now - gwRoutes[i].lastSuccess >= ROUTE_TIMEOUT_MS)) {
      gwRoutes[i].valid = false;
      Serial.printf("[GW][MESH] ⚠️ Route to %s expired (> %lu ms without success)\n",
                    gwRoutes[i].destination.c_str(), ROUTE_TIMEOUT_MS);
    }
  }
}

void MeshManager::onRouteUpdate(const String &reportingNode, const String &data, int rssi, int snr) {
  totalRouteEvents++;
  Serial.printf("[GW][MESH] ROUTE_UPDATE from %s: %s (RSSI: %d dBm)\n",
                reportingNode.c_str(), data.c_str(), rssi);

  String pri = "";
  String bak = "";
  int rk = 1;
  uint16_t cst = 0;
  String reason = "";

  int ip = data.indexOf("PRIMARY=");
  if (ip >= 0) {
    int ep = data.indexOf('|', ip);
    pri = data.substring(ip + 8, ep > 0 ? ep : data.length());
  }
  int ib = data.indexOf("BACKUP=");
  if (ib >= 0) {
    int eb = data.indexOf('|', ib);
    bak = data.substring(ib + 7, eb > 0 ? eb : data.length());
  }
  int ir = data.indexOf("RANK=");
  if (ir >= 0) {
    int er = data.indexOf('|', ir);
    rk = data.substring(ir + 5, er > 0 ? er : data.length()).toInt();
  }
  int ic = data.indexOf("COST=");
  if (ic >= 0) {
    int ec = data.indexOf('|', ic);
    cst = (uint16_t)data.substring(ic + 5, ec > 0 ? ec : data.length()).toInt();
  }
  int irz = data.indexOf("REASON=");
  if (irz >= 0) {
    int erz = data.indexOf('|', irz);
    reason = data.substring(irz + 7, erz > 0 ? erz : data.length());
  }

  int idx = findNodeIndex(reportingNode);
  if (idx >= 0) {
    if (pri.length() > 0) nodes[idx].primaryNextHop = pri;
    if (bak.length() > 0) nodes[idx].backupNextHop = bak;
    nodes[idx].rank = rk;
    nodes[idx].hopCount = rk;
    nodes[idx].routeCost = cst;
    nodes[idx].lastRouteUpdate = millis();
    nodes[idx].orphan = false;
  }

  // Update Gateway routing table next hop
  updateRouteTable(reportingNode, pri.length() > 0 ? pri : reportingNode, rk, cst, rssi, snr);
}

void MeshManager::onRouteRecovered(const String &reportingNode, const String &data, int rssi, int snr) {
  totalRouteRecoveries++;
  Serial.printf("[GW][MESH] 🎉 ROUTE_RECOVERED from %s: %s\n", reportingNode.c_str(), data.c_str());

  onRouteUpdate(reportingNode, data, rssi, snr);

  int idx = findNodeIndex(reportingNode);
  if (idx >= 0) {
    nodes[idx].orphan = false;
    nodes[idx].recoveryEvents++;
    nodes[idx].onlineState = "ONLINE";
  }
}

void MeshManager::onNeighborHealthReport(const String &reportingNode, const String &data, int rssi) {
  Serial.printf("[GW][MESH] NB_HEALTH from %s: %s\n", reportingNode.c_str(), data.c_str());

  int nodeIdx = findNodeIndex(reportingNode);
  if (nodeIdx >= 0) {
    nodes[nodeIdx].rssi = rssi;
    nodes[nodeIdx].lastSeen = millis();
    nodes[nodeIdx].onlineState = "ONLINE";
    updateRouteTable(reportingNode, reportingNode, 1, rssi);
  }

  int pipeCount = 0;
  int start = 0;
  
  for (int i = 0; i <= (int)data.length(); i++) {
    if (i == (int)data.length() || data[i] == '|') {
      if (pipeCount >= 3) {
        String nbData = data.substring(start, i);
        int comma1 = nbData.indexOf(',');
        int comma2 = nbData.indexOf(',', comma1 + 1);
        int comma3 = nbData.indexOf(',', comma2 + 1);
        int comma4 = nbData.indexOf(',', comma3 + 1);
        
        if (comma1 > 0 && comma2 > 0) {
          String nbId = nbData.substring(0, comma1);
          bool resp = (nbData.substring(comma1 + 1, comma2) == "1");
          int nbRssi = (comma3 > 0) ? nbData.substring(comma2 + 1, comma3).toInt() : 0;
          int nbSnr = (comma4 > 0) ? nbData.substring(comma3 + 1, comma4).toInt() : 0;
          
          if (nbSummaryCount < MAX_NODES * 2) {
            nbSummaries[nbSummaryCount].reportingNode = reportingNode;
            nbSummaries[nbSummaryCount].neighborId = nbId;
            nbSummaries[nbSummaryCount].rssi = nbRssi;
            nbSummaries[nbSummaryCount].snr = nbSnr;
            nbSummaries[nbSummaryCount].responded = resp;
            nbSummaries[nbSummaryCount].lastSeen = millis();
            nbSummaries[nbSummaryCount].valid = true;
            nbSummaryCount++;
          } else {
            nbSummaryCount = 0;
          }
          
          if (resp) {
            updateRouteTable(nbId, reportingNode, 2, nbRssi);
          }
        }
      }
      pipeCount++;
      start = i + 1;
    }
  }
}

PendingRegistration* MeshManager::getPendingReg(int index) {
  if (index >= 0 && index < MAX_PENDING_REG) {
    return &pendingRegs[index];
  }
  return nullptr;
}

int MeshManager::findPendingRegIndex(const String &hwId) {
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    if (pendingRegs[i].valid && pendingRegs[i].hwId == hwId) {
      return i;
    }
  }
  return -1;
}

int MeshManager::addPendingReg(const String &hwId, int rssi) {
  int existing = findPendingRegIndex(hwId);
  if (existing >= 0) {
    pendingRegs[existing].detectedAt = millis();
    pendingRegs[existing].rssi = rssi;
    return existing;
  }
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    if (!pendingRegs[i].valid) {
      pendingRegs[i].valid = true;
      pendingRegs[i].hwId = hwId;
      pendingRegs[i].rssi = rssi;
      pendingRegs[i].detectedAt = millis();
      pendingRegs[i].state = REG_DETECTED;
      pendingRegs[i].type = "STREET";
      pendingRegs[i].name = "";
      pendingRegs[i].location = "";
      return i;
    }
  }
  return -1;
}

void MeshManager::clearPendingReg(int index) {
  if (index >= 0 && index < MAX_PENDING_REG) {
    pendingRegs[index].valid = false;
  }
}

int MeshManager::getNextStreetLightId() const { return nextStreetLightId; }
int MeshManager::getNextFarmerId() const { return nextFarmerId; }
void MeshManager::setNextStreetLightId(int id) { nextStreetLightId = id; }
void MeshManager::setNextFarmerId(int id) { nextFarmerId = id; }

GwRouteEntry* MeshManager::getRoute(int index) {
  if (index >= 0 && index < gwRouteCount) return &gwRoutes[index];
  return nullptr;
}

int MeshManager::getRouteCount() const { return gwRouteCount; }

NeighborSummary* MeshManager::getNeighborSummary(int index) {
  if (index >= 0 && index < nbSummaryCount) return &nbSummaries[index];
  return nullptr;
}

int MeshManager::getNeighborSummaryCount() const { return nbSummaryCount; }

// ─────────────────────────────────────────────────────────────
//  CLEAN TELEGRAM INTEGRATION APIS (PROMPT 10 SECTION 12)
// ─────────────────────────────────────────────────────────────
int MeshManager::getTotalRegisteredNodes() const {
  int count = 0;
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].registered) count++;
  }
  return count;
}

int MeshManager::getOnlineNodeCount() const {
  int count = 0;
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].registered && nodes[i].onlineState == "ONLINE") count++;
  }
  return count;
}

int MeshManager::getOfflineNodeCount() const {
  int count = 0;
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].registered && nodes[i].onlineState != "ONLINE") count++;
  }
  return count;
}

int MeshManager::getOrphanNodeCount() const {
  int count = 0;
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].registered && nodes[i].orphan) count++;
  }
  return count;
}

float MeshManager::getAverageHopCount() const {
  int sum = 0;
  int validCount = 0;
  for (int i = 0; i < gwRouteCount; i++) {
    if (gwRoutes[i].valid) {
      sum += gwRoutes[i].hopCount;
      validCount++;
    }
  }
  return (validCount > 0) ? ((float)sum / validCount) : 1.0f;
}

bool MeshManager::getNodeRoutingInfo(const String &id, String &primaryParent, String &backupParent,
                                     int &hops, int &cost, int &rssi, int &snr,
                                     unsigned long &lastUpdate, bool &isOrphan) {
  int idx = findNodeIndex(id);
  if (idx >= 0) {
    primaryParent = nodes[idx].primaryNextHop;
    backupParent = nodes[idx].backupNextHop;
    hops = nodes[idx].hopCount;
    cost = nodes[idx].routeCost;
    rssi = nodes[idx].rssi;
    snr = nodes[idx].snr;
    lastUpdate = nodes[idx].lastRouteUpdate;
    isOrphan = nodes[idx].orphan;
    return true;
  }
  return false;
}
