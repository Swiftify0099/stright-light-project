/*
 * =====================================================================
 *  MESHMANAGER.CPP — GATEWAY NODE & ROUTE TOPOLOGY MANAGER
 * =====================================================================
 */

#include "MeshManager.h"

MeshManager meshMgr;

MeshManager::MeshManager() : nodeCount(0), gwRouteCount(0), nbSummaryCount(0), nextStreetLightId(1), nextFarmerId(1) {
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    pendingRegs[i].valid = false;
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
    nodes[i].hopCount = 1;
    nodes[i].routePath = "DIRECT";
    nodes[i].connectionMode = "DIRECT";
    nodes[i].cmdGeneration = 0;
    nodes[i].neighborCount = 0;
    nodes[i].neighborData = "";

    // Init feeder sub-records
    for (int f = 0; f < MAX_FEEDERS; f++) {
      nodes[i].feeders[f].feederId = f + 1;
      nodes[i].feeders[f].name = String(FEEDER_DEFAULT_NAMES[f]);
      nodes[i].feeders[f].onlineState = "ONLINE";
      nodes[i].feeders[f].relayState = "OFF";
      nodes[i].feeders[f].voltage1 = 0.0f;
      nodes[i].feeders[f].voltage2 = 0.0f;
      nodes[i].feeders[f].voltage1Valid = false;
      nodes[i].feeders[f].voltage2Valid = false;
      nodes[i].feeders[f].voltage1State = "DISCONNECTED";
      nodes[i].feeders[f].voltage2State = "DISCONNECTED";
      nodes[i].feeders[f].lastVoltageUpdate = 0;
      nodes[i].feeders[f].lastHeartbeat = millis();
      nodes[i].feeders[f].loraState = "ONLINE";
      nodes[i].feeders[f].faultState = "OK";
      nodes[i].feeders[f].mcbR = true;
      nodes[i].feeders[f].mcbY = true;
      nodes[i].feeders[f].mcbB = true;
      nodes[i].feeders[f].contactor = false;
      nodes[i].feeders[f].protectionEnabled = true;
    }

    Serial.printf("[GW][DB] Node added: %s (Type: %s, HW: %s)\n",
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
  if (idx >= 0 && nodes[idx].primaryNextHop.length() > 0) {
    return nodes[idx].primaryNextHop;
  }
  return BROADCAST_ID;
}

void MeshManager::updateRouteTable(const String &dst, const String &nextHop, int hops, int rssi) {
  if (dst.length() == 0 || dst == GATEWAY_ID || dst == BROADCAST_ID) return;

  for (int i = 0; i < gwRouteCount; i++) {
    if (gwRoutes[i].destination == dst) {
      if (hops <= gwRoutes[i].hopCount || !gwRoutes[i].valid) {
        gwRoutes[i].primaryNextHop = nextHop;
        gwRoutes[i].hopCount = hops;
        gwRoutes[i].rssi = rssi;
        gwRoutes[i].valid = true;
        gwRoutes[i].lastSuccess = millis();
      }
      return;
    }
  }

  if (gwRouteCount < MAX_ROUTES) {
    gwRoutes[gwRouteCount].destination = dst;
    gwRoutes[gwRouteCount].primaryNextHop = nextHop;
    gwRoutes[gwRouteCount].backupNextHop = BROADCAST_ID;
    gwRoutes[gwRouteCount].hopCount = hops;
    gwRoutes[gwRouteCount].rssi = rssi;
    gwRoutes[gwRouteCount].valid = true;
    gwRoutes[gwRouteCount].lastSuccess = millis();
    gwRouteCount++;
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

  // Format: CHK_ID|uptime|count|nodeId,resp,rssi,snr,age|...
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
