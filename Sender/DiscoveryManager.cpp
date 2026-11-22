/*
 * =====================================================================
 *  DISCOVERYMANAGER.CPP — GATEWAY AUTOMATIC NODE DISCOVERY & LINK QUALITY
 *  Smart Street Light / Farmer Power Distribution Mesh System
 *  Platform : ESP32 + SX1278 RA-02 433 MHz + WiFi + Telegram
 *  Role     : Root Rank 0 Coordinator, Topology Aggregator, Airtime Tracker
 * =====================================================================
 */

#include "DiscoveryManager.h"
#include "LoRaManager.h"
#include "MeshManager.h"
#include "StorageManager.h"

DiscoveryManager discoveryMgr;

DiscoveryManager::DiscoveryManager() :
  directNeighborCount(0), revokedCount(0),
  tNextRootAdv(0), tLastAdvSent(0),
  tAirtimeWindowStart(0), airtimeUsedInWindow(0), discoveryFailures(0),
  seenCacheHead(0)
{
  for (int i = 0; i < MAX_NODES; i++) {
    nodeLqRecords[i].valid = false;
  }
  for (int i = 0; i < 16; i++) {
    memset(seenCache[i].nodeId, 0, sizeof(seenCache[i].nodeId));
    seenCache[i].authTag = 0;
    seenCache[i].seenMs = 0;
  }
}

void DiscoveryManager::init() {
  directNeighborCount = 0;
  revokedCount = 0;
  airtimeUsedInWindow = 0;
  tAirtimeWindowStart = millis();

  randomSeed(esp_random());
  // Gateway broadcasts root advertisement every 120s with +/- 15s jitter
  tNextRootAdv = millis() + 5000UL; // First root advertisement 5s after boot

  Serial.println(F("[GW][DISCOVERY] Gateway Discovery Engine Initialized (Root Rank 0)"));
}

void DiscoveryManager::update() {
  unsigned long now = millis();

  // 1. Maintain Rolling 1-Hour Airtime Window
  if (now - tAirtimeWindowStart >= AIRTIME_WINDOW_MS) {
    tAirtimeWindowStart = now;
    airtimeUsedInWindow = 0;
  }

  // 2. Direct Neighbor Expiration
  expireDirectNeighbors();

  // 3. Periodic Root Advertisement Broadcast
  if (now >= tNextRootAdv) {
    uint32_t airtime = calculateLoRaAirtimeMs(32);
    if (canTransmit(airtime)) {
      sendRootAdvertisement();
    } else {
      discoveryFailures++;
    }
    // Schedule next root advertisement: 120s +/- 20s jitter
    tNextRootAdv = millis() + 120000UL + random(-20000, 20001);
  }
}

// ─────────────────────────────────────────────────────────────
//  PACKET PROCESSING & INGESTION
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::processDiscoveryPacket(const String &type, const String &src, const String &dst,
                                             const String &data, int16_t rssi, int8_t snr) {
  if (src == GATEWAY_ID) return;

  if (type == PKT_TYPE_DISC_BEACON) {
    DiscoveryPayload payload;
    if (payload.deserialize(data)) {
      if (payload.networkId != MESH_NETWORK_ID) return;

      int idx = findOrAllocateDirectNeighbor(payload.nodeId);
      if (idx >= 0) {
        updateDirectNeighborRecord(idx, payload, rssi, snr);
      }

      // Gateway is Root (Rank 0): reply immediately with reciprocal link quality
      delay(random(30, 150));
      sendNeighborResponse(payload.nodeId, rssi, snr);
    }
  }
  else if (type == PKT_TYPE_NEIGH_ADV) {
    DiscoveryPayload payload;
    if (payload.deserialize(data)) {
      if (payload.networkId != MESH_NETWORK_ID) return;

      if (isDuplicateAdvertisement(payload.nodeId, payload.authTag)) {
        onPacketReceived(src, rssi, snr);
        return;
      }
      recordAdvertisementSeen(payload.nodeId, payload.authTag);

      int idx = findOrAllocateDirectNeighbor(payload.nodeId);
      if (idx >= 0) {
        updateDirectNeighborRecord(idx, payload, rssi, snr);
      }
    }
  }
  else if (type == PKT_TYPE_LQ_REPORT) {
    // Format: COUNT=x|PRI=x,SC=x|BCK=x,SC=x|AIR=x,DC=x
    int pPri = data.indexOf("PRI=");
    int pBck = data.indexOf("BCK=");
    int pAir = data.indexOf("AIR=");
    int pDc  = data.indexOf("DC=");
    int pCnt = data.indexOf("COUNT=");

    String pri = "NONE";
    uint16_t priSc = 0;
    String bck = "NONE";
    uint16_t bckSc = 0;
    uint8_t nCnt = 0;
    uint32_t airMs = 0;
    float dcPct = 0.0f;

    if (pCnt >= 0) nCnt = (uint8_t)data.substring(pCnt + 6, data.indexOf('|', pCnt)).toInt();

    if (pPri >= 0) {
      int endPri = data.indexOf('|', pPri);
      String priStr = (endPri > 0) ? data.substring(pPri + 4, endPri) : data.substring(pPri + 4);
      int cIdx = priStr.indexOf(",SC=");
      if (cIdx > 0) {
        pri = priStr.substring(0, cIdx);
        priSc = (uint16_t)priStr.substring(cIdx + 4).toInt();
      } else {
        pri = priStr;
      }
    }

    if (pBck >= 0) {
      int endBck = data.indexOf('|', pBck);
      String bckStr = (endBck > 0) ? data.substring(pBck + 4, endBck) : data.substring(pBck + 4);
      int cIdx = bckStr.indexOf(",SC=");
      if (cIdx > 0) {
        bck = bckStr.substring(0, cIdx);
        bckSc = (uint16_t)bckStr.substring(cIdx + 4).toInt();
      } else {
        bck = bckStr;
      }
    }

    if (pAir >= 0) airMs = (uint32_t)data.substring(pAir + 4, data.indexOf(',', pAir)).toInt();
    if (pDc >= 0)  dcPct = data.substring(pDc + 3).toFloat();

    updateNodeLqRecord(src.c_str(), pri.c_str(), priSc, bck.c_str(), bckSc, nCnt, airMs, dcPct);

    Serial.printf("[GW][LQ] Node %s -> Primary: %s (Score %u) | Backup: %s (Score %u) | Neighbors: %u | Airtime: %lu ms\n",
                  src.c_str(), pri.c_str(), priSc, bck.c_str(), bckSc, nCnt, (unsigned long)airMs);
  }
}

void DiscoveryManager::onPacketReceived(const String &src, int16_t rssi, int8_t snr) {
  for (int i = 0; i < directNeighborCount; i++) {
    if (strcmp(directNeighbors[i].nodeId, src.c_str()) == 0) {
      NeighborTableEntry &e = directNeighbors[i];
      e.lastSeenMs = millis();
      e.availability = AVAIL_ONLINE;
      e.rxPackets++;
      float alpha = 0.20f;
      e.rssi = rssi;
      e.snr = snr;
      e.avgRssi = (alpha * rssi) + ((1.0f - alpha) * e.avgRssi);
      e.avgSnr  = (alpha * snr)  + ((1.0f - alpha) * e.avgSnr);
      return;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  DIRECT NEIGHBOR MANAGEMENT
// ─────────────────────────────────────────────────────────────
int DiscoveryManager::findOrAllocateDirectNeighbor(const char* nodeId) {
  if (nodeId == nullptr || nodeId[0] == '\0') return -1;

  for (int i = 0; i < directNeighborCount; i++) {
    if (strcmp(directNeighbors[i].nodeId, nodeId) == 0) {
      return i;
    }
  }

  if (directNeighborCount < MAX_GW_NEIGHBORS_TABLE) {
    int idx = directNeighborCount++;
    directNeighbors[idx] = NeighborTableEntry();
    strncpy(directNeighbors[idx].nodeId, nodeId, sizeof(directNeighbors[idx].nodeId) - 1);
    directNeighbors[idx].firstSeenMs = millis();
    directNeighbors[idx].occupied = true;
    return idx;
  }

  // Evict oldest dead neighbor if full
  int evictIdx = -1;
  uint32_t oldest = 0xFFFFFFFF;
  for (int i = 0; i < MAX_GW_NEIGHBORS_TABLE; i++) {
    if (directNeighbors[i].availability == AVAIL_OFFLINE) {
      if (directNeighbors[i].lastSeenMs < oldest) {
        oldest = directNeighbors[i].lastSeenMs;
        evictIdx = i;
      }
    }
  }

  if (evictIdx >= 0) {
    directNeighbors[evictIdx] = NeighborTableEntry();
    strncpy(directNeighbors[evictIdx].nodeId, nodeId, sizeof(directNeighbors[evictIdx].nodeId) - 1);
    directNeighbors[evictIdx].firstSeenMs = millis();
    directNeighbors[evictIdx].occupied = true;
    return evictIdx;
  }

  return -1;
}

void DiscoveryManager::updateDirectNeighborRecord(int index, const DiscoveryPayload &payload, int16_t rssi, int8_t snr) {
  if (index < 0 || index >= MAX_GW_NEIGHBORS_TABLE) return;

  NeighborTableEntry &e = directNeighbors[index];
  e.networkId = payload.networkId;
  e.lastSeenMs = millis();
  e.availability = AVAIL_ONLINE;
  e.capabilities = payload.capabilities;
  e.firmwareVersion = payload.firmwareVersion;
  e.nodeHealth = payload.nodeHealth;
  e.networkRank = payload.networkRank;
  e.advertisedCapacity = payload.availableCapacity;
  strncpy(e.uplinkParentId, payload.uplinkParentId, sizeof(e.uplinkParentId) - 1);

  if (isNodeRevoked(payload.nodeId)) {
    e.trustStatus = TRUST_REVOKED;
  } else if (payload.trustStatus == TRUST_REGISTERED) {
    uint32_t expectedTag = DiscoveryPayload::computeAuthTag(payload.nodeId, payload.networkId, payload.networkRank, MESH_AUTH_SECRET);
    e.trustStatus = (payload.authTag == expectedTag) ? TRUST_REGISTERED : TRUST_UNTRUSTED;
  } else {
    e.trustStatus = TRUST_UNREGISTERED;
  }

  float alpha = 0.20f;
  if (e.rxPackets == 0) {
    e.avgRssi = (float)rssi;
    e.avgSnr  = (float)snr;
  } else {
    e.avgRssi = (alpha * rssi) + ((1.0f - alpha) * e.avgRssi);
    e.avgSnr  = (alpha * snr)  + ((1.0f - alpha) * e.avgSnr);
  }

  e.rssi = rssi;
  e.snr  = snr;
  e.rxPackets++;

  // Score computation for Gateway perspective
  float sRssi = (e.avgRssi >= -65.0f) ? 100.0f : ((e.avgRssi <= -128.0f) ? 0.0f : ((e.avgRssi - (-128.0f)) / 63.0f * 100.0f));
  float sSnr  = (e.avgSnr >= 10.0f) ? 100.0f : ((e.avgSnr <= -15.0f) ? 0.0f : ((e.avgSnr - (-15.0f)) / 25.0f * 100.0f));
  e.linkQualityScore = (uint16_t)((sRssi * 5.0f) + (sSnr * 5.0f));
}

void DiscoveryManager::expireDirectNeighbors() {
  unsigned long now = millis();
  for (int i = 0; i < directNeighborCount; i++) {
    unsigned long elapsed = now - directNeighbors[i].lastSeenMs;
    if (elapsed >= NEIGHBOR_DEAD_TIMEOUT_MS) {
      directNeighbors[i].availability = AVAIL_OFFLINE;
    } else if (elapsed >= NEIGHBOR_TIMEOUT_MS) {
      directNeighbors[i].availability = AVAIL_STALE;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  TRANSMISSIONS
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::sendRootAdvertisement() {
  DiscoveryPayload p;
  strncpy(p.nodeId, GATEWAY_ID, sizeof(p.nodeId) - 1);
  p.networkId = MESH_NETWORK_ID;
  p.trustStatus = TRUST_REGISTERED;
  p.capabilities = CAP_GATEWAY | CAP_ROUTER | CAP_MAINS_POWERED | CAP_PARENT_ELIGIBLE;
  p.firmwareVersion = FW_VERSION_INT;
  p.nodeHealth = 100;
  p.networkRank = 0; // Root is Rank 0
  p.availableCapacity = 10; // High capacity root
  strncpy(p.uplinkParentId, "ROOT", sizeof(p.uplinkParentId) - 1);
  p.authTag = DiscoveryPayload::computeAuthTag(p.nodeId, p.networkId, p.networkRank, MESH_AUTH_SECRET);

  String payloadStr = p.serialize();
  uint32_t airtime = calculateLoRaAirtimeMs(payloadStr.length());

  loraMgr.dispatchLoRaPacket(PKT_TYPE_NEIGH_ADV, BROADCAST_ID, BROADCAST_ID, 1, loraMgr.getNextSeq(), payloadStr);
  recordAirtimeUsage(airtime);
  tLastAdvSent = millis();

  Serial.printf("[GW][DISCOVERY] Broadcast Root Advertisement (Rank 0, Airtime: %lu ms)\n", (unsigned long)airtime);
}

void DiscoveryManager::sendNeighborResponse(const char* targetNode, int16_t rxRssi, int8_t rxSnr) {
  DiscoveryPayload p;
  strncpy(p.nodeId, GATEWAY_ID, sizeof(p.nodeId) - 1);
  p.networkId = MESH_NETWORK_ID;
  p.trustStatus = TRUST_REGISTERED;
  p.capabilities = CAP_GATEWAY | CAP_ROUTER | CAP_MAINS_POWERED | CAP_PARENT_ELIGIBLE;
  p.firmwareVersion = FW_VERSION_INT;
  p.nodeHealth = 100;
  p.networkRank = 0;
  p.availableCapacity = 10;
  strncpy(p.uplinkParentId, "ROOT", sizeof(p.uplinkParentId) - 1);
  p.authTag = DiscoveryPayload::computeAuthTag(p.nodeId, p.networkId, p.networkRank, MESH_AUTH_SECRET);

  String payloadStr = p.serialize() + "|R_RSSI=" + String(rxRssi) + "|R_SNR=" + String(rxSnr);
  uint32_t airtime = calculateLoRaAirtimeMs(payloadStr.length());

  loraMgr.dispatchLoRaPacket(PKT_TYPE_NEIGH_RSP, targetNode, targetNode, 1, loraMgr.getNextSeq(), payloadStr);
  recordAirtimeUsage(airtime);
}

// ─────────────────────────────────────────────────────────────
//  TOPOLOGY RECORD AGGREGATION
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::updateNodeLqRecord(const char* nodeId, const char* primary, uint16_t priScore,
                                         const char* backup, uint16_t bckScore, uint8_t nbrCount,
                                         uint32_t airtimeMs, float dutyCycle) {
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodeLqRecords[i].valid && strcmp(nodeLqRecords[i].nodeId, nodeId) == 0) {
      strncpy(nodeLqRecords[i].primaryParent, primary, 15);
      nodeLqRecords[i].primaryScore = priScore;
      strncpy(nodeLqRecords[i].backupParent, backup, 15);
      nodeLqRecords[i].backupScore = bckScore;
      nodeLqRecords[i].neighborCount = nbrCount;
      nodeLqRecords[i].airtimeUsedMs = airtimeMs;
      nodeLqRecords[i].dutyCyclePercent = dutyCycle;
      nodeLqRecords[i].lastReportMs = millis();
      return;
    }
  }

  for (int i = 0; i < MAX_NODES; i++) {
    if (!nodeLqRecords[i].valid) {
      nodeLqRecords[i].valid = true;
      strncpy(nodeLqRecords[i].nodeId, nodeId, 15);
      nodeLqRecords[i].nodeId[15] = '\0';
      strncpy(nodeLqRecords[i].primaryParent, primary, 15);
      nodeLqRecords[i].primaryScore = priScore;
      strncpy(nodeLqRecords[i].backupParent, backup, 15);
      nodeLqRecords[i].backupScore = bckScore;
      nodeLqRecords[i].neighborCount = nbrCount;
      nodeLqRecords[i].airtimeUsedMs = airtimeMs;
      nodeLqRecords[i].dutyCyclePercent = dutyCycle;
      nodeLqRecords[i].lastReportMs = millis();
      return;
    }
  }
}

const NodeLqRecord* DiscoveryManager::getNodeLqRecord(const char* nodeId) const {
  if (nodeId == nullptr) return nullptr;
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodeLqRecords[i].valid && strcmp(nodeLqRecords[i].nodeId, nodeId) == 0) {
      return &nodeLqRecords[i];
    }
  }
  return nullptr;
}

// ─────────────────────────────────────────────────────────────
//  REVOCATION MANAGEMENT
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::revokeNode(const char* nodeId) {
  if (nodeId == nullptr || nodeId[0] == '\0') return;
  if (isNodeRevoked(nodeId)) return;

  if (revokedCount < 8) {
    strncpy(revokedNodes[revokedCount++], nodeId, 15);
    revokedNodes[revokedCount - 1][15] = '\0';
  }

  NeighborTableEntry* n = findDirectNeighbor(nodeId);
  if (n) {
    n->trustStatus = TRUST_REVOKED;
    n->linkQualityScore = 0;
  }
}

void DiscoveryManager::unrevokeNode(const char* nodeId) {
  for (int i = 0; i < revokedCount; i++) {
    if (strcmp(revokedNodes[i], nodeId) == 0) {
      for (int j = i; j < revokedCount - 1; j++) {
        strncpy(revokedNodes[j], revokedNodes[j + 1], 16);
      }
      revokedCount--;
      break;
    }
  }
}

bool DiscoveryManager::isNodeRevoked(const char* nodeId) const {
  for (int i = 0; i < revokedCount; i++) {
    if (strcmp(revokedNodes[i], nodeId) == 0) return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────
//  AIRTIME & DUPLICATE SUPPRESSION
// ─────────────────────────────────────────────────────────────
bool DiscoveryManager::canTransmit(uint32_t airtimeMs) const {
  unsigned long now = millis();
  if (now - tLastAdvSent < MIN_PACKET_INTERVAL_MS) return false;
  return (airtimeUsedInWindow + airtimeMs <= AIRTIME_MAX_BUDGET_MS);
}

void DiscoveryManager::recordAirtimeUsage(uint32_t airtimeMs) {
  airtimeUsedInWindow += airtimeMs;
}

uint32_t DiscoveryManager::getAirtimeUsedThisHour() const {
  return airtimeUsedInWindow;
}

float DiscoveryManager::getDutyCyclePercent() const {
  return ((float)airtimeUsedInWindow / (float)AIRTIME_WINDOW_MS) * 100.0f;
}

bool DiscoveryManager::isDuplicateAdvertisement(const char* nodeId, uint32_t authTag) {
  unsigned long now = millis();
  for (int i = 0; i < 16; i++) {
    if (strcmp(seenCache[i].nodeId, nodeId) == 0 && seenCache[i].authTag == authTag) {
      if (now - seenCache[i].seenMs < DUPLICATE_SUPPRESS_MS) {
        return true;
      }
    }
  }
  return false;
}

void DiscoveryManager::recordAdvertisementSeen(const char* nodeId, uint32_t authTag) {
  strncpy(seenCache[seenCacheHead].nodeId, nodeId, sizeof(seenCache[seenCacheHead].nodeId) - 1);
  seenCache[seenCacheHead].authTag = authTag;
  seenCache[seenCacheHead].seenMs = millis();
  seenCacheHead = (seenCacheHead + 1) % 16;
}

// ─────────────────────────────────────────────────────────────
//  DIAGNOSTICS & SUMMARY
// ─────────────────────────────────────────────────────────────
int DiscoveryManager::getDirectNeighborCount() const {
  return directNeighborCount;
}

NeighborTableEntry* DiscoveryManager::getDirectNeighbor(int index) {
  if (index >= 0 && index < directNeighborCount) {
    return &directNeighbors[index];
  }
  return nullptr;
}

NeighborTableEntry* DiscoveryManager::findDirectNeighbor(const char* nodeId) {
  if (nodeId == nullptr) return nullptr;
  for (int i = 0; i < directNeighborCount; i++) {
    if (strcmp(directNeighbors[i].nodeId, nodeId) == 0) {
      return &directNeighbors[i];
    }
  }
  return nullptr;
}

String DiscoveryManager::buildTelegramTopologySummary() const {
  String s = "📡 *MESH TOPOLOGY & LINK QUALITY*\n";
  s += "━━━━━━━━━━━━━━━━━━━━\n";
  s += "🛰 *Direct 1-Hop Neighbors:* " + String(directNeighborCount) + "\n";
  s += "⏱ *Airtime Used:* " + String(airtimeUsedInWindow) + " ms (" + String(getDutyCyclePercent(), 2) + "%)\n\n";

  for (int i = 0; i < directNeighborCount; i++) {
    const NeighborTableEntry &e = directNeighbors[i];
    const NodeLqRecord* lq = getNodeLqRecord(e.nodeId);

    s += "📍 *" + String(e.nodeId) + "*: " + String(e.rssi) + " dBm, SNR: " + String(e.snr) + " dB\n";
    if (lq && lq->valid) {
      s += "   ├─ Primary Parent: `" + String(lq->primaryParent) + "` (" + String(lq->primaryScore) + ")\n";
      s += "   └─ Backup Parent: `" + String(lq->backupParent) + "` (" + String(lq->backupScore) + ")\n";
    }
  }
  s += "━━━━━━━━━━━━━━━━━━━━\n";
  return s;
}
