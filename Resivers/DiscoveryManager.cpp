/*
 * =====================================================================
 *  DISCOVERYMANAGER.CPP — FIELD POLE AUTOMATIC NODE DISCOVERY & LINK QUALITY
 *  Smart Street Light / Farmer Power Distribution Mesh System
 *  Platform : ESP32 + SX1278 RA-02 433 MHz
 *  Standards: Bounded Table, EWMA, Airtime Budgeting, Path Diversity
 * =====================================================================
 */

#include "DiscoveryManager.h"
#include "LoRaManager.h"
#include "StorageManager.h"
#include "FeederManager.h"
#include "VoltageSensor.h"

DiscoveryManager discoveryMgr;

DiscoveryManager::DiscoveryManager() :
  neighborCount(0), revokedCount(0),
  myNetworkRank(255), myForwardingCapacity(5),
  tNextBeacon(0), tLastBeaconSent(0), tLastLqReportSent(0),
  tAirtimeWindowStart(0), airtimeUsedInWindow(0), discoveryFailures(0),
  seenCacheHead(0), hysteresisHoldCount(0)
{
  memset(myUplinkParentId, 0, sizeof(myUplinkParentId));
  memset(lastPrimaryId, 0, sizeof(lastPrimaryId));
  for (int i = 0; i < 8; i++) {
    memset(seenCache[i].nodeId, 0, sizeof(seenCache[i].nodeId));
    seenCache[i].authTag = 0;
    seenCache[i].seenMs = 0;
  }
}

void DiscoveryManager::init(uint8_t initialRank) {
  myNetworkRank = initialRank;
  neighborCount = 0;
  revokedCount = 0;
  airtimeUsedInWindow = 0;
  tAirtimeWindowStart = millis();

  // Initialize random seed from hardware noise
  randomSeed(esp_random());

  // Set initial jittered beacon timer
  scheduleNextBeacon();

  Serial.printf("[DISCOVERY] Initialized. MyRank=%u, Cap=%u, BoundedTableSize=%d\n",
                myNetworkRank, myForwardingCapacity, MAX_NEIGHBORS_TABLE);
}

void DiscoveryManager::update() {
  unsigned long now = millis();

  // 1. Maintain Rolling 1-Hour Airtime Window
  if (now - tAirtimeWindowStart >= AIRTIME_WINDOW_MS) {
    tAirtimeWindowStart = now;
    airtimeUsedInWindow = 0; // Reset rolling budget window
  }

  // 2. Neighbor Table Expiration Engine
  expireNeighborEntries();

  // 3. Periodic Autonomous Beacon Dispatch
  if (now >= tNextBeacon) {
    // Check duty cycle budget before transmitting
    uint32_t beaconAirtime = calculateLoRaAirtimeMs(32);
    if (canTransmit(beaconAirtime)) {
      if (!hasValidPrimaryCandidate() || myNetworkRank == 255) {
        // Unparented or seeking: transmit Discovery Beacon
        sendDiscoveryBeacon();
      } else {
        // Parented: transmit periodic Neighbor Advertisement
        sendNeighborAdvertisement();
      }
    } else {
      discoveryFailures++;
      Serial.printf("[DISCOVERY] ⚠️ Beacon throttled by 1%% Airtime Duty Cycle! (Used: %lu ms / %lu ms)\n",
                    (unsigned long)airtimeUsedInWindow, (unsigned long)AIRTIME_MAX_BUDGET_MS);
    }
    scheduleNextBeacon();
  }

  // 4. Periodic Link Quality Report to Gateway (Every 60s if registered and parented)
  if (storageMgr.isRegistered() && hasValidPrimaryCandidate()) {
    if (now - tLastLqReportSent >= 60000UL) {
      tLastLqReportSent = now;
      uint32_t reportAirtime = calculateLoRaAirtimeMs(48);
      if (canTransmit(reportAirtime)) {
        sendLinkQualityReport();
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  PACKET PROCESSING & INGESTION
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::processDiscoveryPacket(const String &type, const String &src, const String &dst,
                                             const String &data, int16_t rssi, int8_t snr) {
  String myHw = storageMgr.getHwNodeId();
  String myLog = storageMgr.getNodeId();

  // Ignore loopback packets from self
  if (src == myHw || (myLog.length() > 0 && src == myLog)) return;

  // Process based on discovery packet type
  if (type == PKT_TYPE_DISC_BEACON) {
    DiscoveryPayload payload;
    if (payload.deserialize(data)) {
      // Validate Network ID
      if (payload.networkId != MESH_NETWORK_ID) {
        Serial.printf("[DISCOVERY] Rejected beacon from %s: Wrong NetID 0x%04X\n", src.c_str(), payload.networkId);
        return;
      }

      int idx = findOrAllocateNeighbor(payload.nodeId);
      if (idx >= 0) {
        updateNeighborRecord(idx, payload, rssi, snr);
        evaluateParentCandidates();
      }

      // If we are a registered node with capacity, send reciprocal response so seeker discovers us
      if (storageMgr.isRegistered() && myForwardingCapacity > 0 && myNetworkRank < 255) {
        // Add random backoff between 50ms and 300ms to avoid reply storms
        delay(random(50, 300));
        sendNeighborResponse(payload.nodeId, rssi, snr);
      }
    }
  }
  else if (type == PKT_TYPE_NEIGH_ADV) {
    DiscoveryPayload payload;
    if (payload.deserialize(data)) {
      if (payload.networkId != MESH_NETWORK_ID) return;

      // Duplicate suppression check
      if (isDuplicateAdvertisement(payload.nodeId, payload.authTag)) {
        // Just record that we saw a packet to keep statistics updated
        onPacketReceived(src, rssi, snr);
        return;
      }
      recordAdvertisementSeen(payload.nodeId, payload.authTag);

      int idx = findOrAllocateNeighbor(payload.nodeId);
      if (idx >= 0) {
        updateNeighborRecord(idx, payload, rssi, snr);
        evaluateParentCandidates();
      }
    }
  }
  else if (type == PKT_TYPE_NEIGH_RSP) {
    // Directed response with reciprocal link quality
    DiscoveryPayload payload;
    if (payload.deserialize(data)) {
      if (payload.networkId != MESH_NETWORK_ID) return;

      int idx = findOrAllocateNeighbor(payload.nodeId);
      if (idx >= 0) {
        updateNeighborRecord(idx, payload, rssi, snr);
        evaluateParentCandidates();
        Serial.printf("[DISCOVERY] Received reciprocal link response from %s (RSSI: %d, SNR: %d)\n",
                      payload.nodeId, rssi, snr);
      }
    }
  }
}

void DiscoveryManager::onPacketReceived(const String &src, int16_t rssi, int8_t snr) {
  for (int i = 0; i < neighborCount; i++) {
    if (strcmp(neighborTable[i].nodeId, src.c_str()) == 0) {
      NeighborTableEntry &e = neighborTable[i];
      e.lastSeenMs = millis();
      e.availability = AVAIL_ONLINE;
      e.consecutiveMisses = 0;
      e.consecutiveSuccess++;
      e.rxPackets++;

      // EWMA update
      float alpha = 0.20f;
      e.rssi = rssi;
      e.snr = snr;
      e.avgRssi = (alpha * rssi) + ((1.0f - alpha) * e.avgRssi);
      e.avgSnr  = (alpha * snr)  + ((1.0f - alpha) * e.avgSnr);

      // Stability update
      float diff = (float)rssi - e.avgRssi;
      e.rssiVariance = (alpha * (diff * diff)) + ((1.0f - alpha) * e.rssiVariance);

      // PDR
      uint16_t total = e.rxPackets + e.failedPackets;
      e.pdr = (total > 0) ? ((float)e.rxPackets / (float)total) : 1.0f;

      e.linkQualityScore = calculateLinkQualityScore(e);
      return;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  INTERNAL NEIGHBOR TABLE ALLOCATION & MAINTENANCE
// ─────────────────────────────────────────────────────────────
int DiscoveryManager::findOrAllocateNeighbor(const char* nodeId) {
  if (nodeId == nullptr || nodeId[0] == '\0') return -1;

  // 1. Search existing entries
  for (int i = 0; i < neighborCount; i++) {
    if (strcmp(neighborTable[i].nodeId, nodeId) == 0) {
      return i;
    }
  }

  // 2. Allocate in free slot if available
  if (neighborCount < MAX_NEIGHBORS_TABLE) {
    int idx = neighborCount++;
    neighborTable[idx] = NeighborTableEntry();
    strncpy(neighborTable[idx].nodeId, nodeId, sizeof(neighborTable[idx].nodeId) - 1);
    neighborTable[idx].firstSeenMs = millis();
    neighborTable[idx].occupied = true;
    return idx;
  }

  // 3. Table is full: find lowest-quality DEAD or STALE candidate to evict
  int evictIdx = -1;
  uint32_t oldestSeen = 0xFFFFFFFF;

  for (int i = 0; i < MAX_NEIGHBORS_TABLE; i++) {
    if (neighborTable[i].availability == AVAIL_OFFLINE &&
        (millis() - neighborTable[i].lastSeenMs) >= NEIGHBOR_EVICTION_MS) {
      if (neighborTable[i].lastSeenMs < oldestSeen) {
        oldestSeen = neighborTable[i].lastSeenMs;
        evictIdx = i;
      }
    }
  }

  if (evictIdx >= 0) {
    Serial.printf("[DISCOVERY] Evicting dead neighbor %s to accommodate new neighbor %s\n",
                  neighborTable[evictIdx].nodeId, nodeId);
    neighborTable[evictIdx] = NeighborTableEntry();
    strncpy(neighborTable[evictIdx].nodeId, nodeId, sizeof(neighborTable[evictIdx].nodeId) - 1);
    neighborTable[evictIdx].firstSeenMs = millis();
    neighborTable[evictIdx].occupied = true;
    return evictIdx;
  }

  // Bounded table limit reached; cannot allocate without exceeding bounded memory
  Serial.printf("[DISCOVERY] ⚠️ Neighbor table FULL (%d entries)! Cannot insert %s\n",
                MAX_NEIGHBORS_TABLE, nodeId);
  return -1;
}

void DiscoveryManager::updateNeighborRecord(int index, const DiscoveryPayload &payload, int16_t rssi, int8_t snr) {
  if (index < 0 || index >= MAX_NEIGHBORS_TABLE) return;

  NeighborTableEntry &e = neighborTable[index];
  unsigned long now = millis();

  // Basic attributes
  e.networkId = payload.networkId;
  e.lastSeenMs = now;
  e.availability = AVAIL_ONLINE;
  e.capabilities = payload.capabilities;
  e.firmwareVersion = payload.firmwareVersion;
  e.nodeHealth = payload.nodeHealth;
  e.networkRank = payload.networkRank;
  e.advertisedCapacity = payload.availableCapacity;
  strncpy(e.uplinkParentId, payload.uplinkParentId, sizeof(e.uplinkParentId) - 1);

  // Trust verification
  if (isNodeRevoked(payload.nodeId)) {
    e.trustStatus = TRUST_REVOKED;
  } else if (payload.trustStatus == TRUST_REGISTERED) {
    // Verify authentication tag
    uint32_t expectedTag = DiscoveryPayload::computeAuthTag(payload.nodeId, payload.networkId, payload.networkRank, MESH_AUTH_SECRET);
    if (payload.authTag == expectedTag) {
      e.trustStatus = TRUST_REGISTERED;
    } else {
      e.trustStatus = TRUST_UNTRUSTED;
      Serial.printf("[DISCOVERY] ⚠️ Node %s claimed REGISTERED but failed AuthTag! (Got: 0x%08lX, Exp: 0x%08lX)\n",
                    payload.nodeId, (unsigned long)payload.authTag, (unsigned long)expectedTag);
    }
  } else {
    e.trustStatus = TRUST_UNREGISTERED;
  }

  // RSSI / SNR EWMA Filter (alpha = 0.20)
  float alpha = 0.20f;
  if (e.rxPackets == 0) {
    e.avgRssi = (float)rssi;
    e.avgSnr  = (float)snr;
    e.rssiVariance = 0.0f;
  } else {
    float diff = (float)rssi - e.avgRssi;
    e.rssiVariance = (alpha * (diff * diff)) + ((1.0f - alpha) * e.rssiVariance);
    e.avgRssi = (alpha * rssi) + ((1.0f - alpha) * e.avgRssi);
    e.avgSnr  = (alpha * snr)  + ((1.0f - alpha) * e.avgSnr);
  }

  e.rssi = rssi;
  e.snr  = snr;
  e.rxPackets++;
  e.consecutiveSuccess++;
  e.consecutiveMisses = 0;

  // Packet Delivery Ratio (PDR)
  uint16_t total = e.rxPackets + e.failedPackets;
  e.pdr = (total > 0) ? ((float)e.rxPackets / (float)total) : 1.0f;

  // Link Stability calculation (0 to 100%)
  // Standard deviation indicator from variance
  float varScore = 100.0f - (e.rssiVariance * 2.5f);
  if (varScore < 0.0f) varScore = 0.0f;
  if (varScore > 100.0f) varScore = 100.0f;

  float successBonus = (e.consecutiveSuccess > 5) ? 20.0f : (e.consecutiveSuccess * 4.0f);
  float stabTotal = (varScore * 0.8f) + successBonus;
  e.linkStability = (stabTotal > 100.0f) ? 100 : (uint8_t)stabTotal;

  // Calculate composite Link Quality Score
  e.linkQualityScore = calculateLinkQualityScore(e);

  Serial.printf("[DISCOVERY] Updated %s | RSSI: %d (Avg: %.1f) | SNR: %d (Avg: %.1f) | Score: %u/1000 | Trust: %u | Rank: %u\n",
                e.nodeId, e.rssi, e.avgRssi, e.snr, e.avgSnr, e.linkQualityScore, e.trustStatus, e.networkRank);
}

void DiscoveryManager::expireNeighborEntries() {
  unsigned long now = millis();

  for (int i = 0; i < neighborCount; i++) {
    NeighborTableEntry &e = neighborTable[i];
    unsigned long elapsed = now - e.lastSeenMs;

    if (elapsed >= NEIGHBOR_DEAD_TIMEOUT_MS) {
      if (e.availability != AVAIL_OFFLINE) {
        e.availability = AVAIL_OFFLINE;
        e.linkQualityScore = 0;
        e.isPrimaryCandidate = false;
        e.isBackupCandidate = false;
        Serial.printf("[DISCOVERY] Neighbor %s transitioned to OFFLINE (No packets for %lu s)\n",
                      e.nodeId, elapsed / 1000);
        evaluateParentCandidates();
      }
    } else if (elapsed >= NEIGHBOR_TIMEOUT_MS) {
      if (e.availability == AVAIL_ONLINE) {
        e.availability = AVAIL_STALE;
        e.failedPackets++;
        e.consecutiveMisses++;
        e.consecutiveSuccess = 0;
        uint16_t total = e.rxPackets + e.failedPackets;
        e.pdr = (total > 0) ? ((float)e.rxPackets / (float)total) : 0.0f;
        e.linkQualityScore = calculateLinkQualityScore(e);
        Serial.printf("[DISCOVERY] Neighbor %s transitioned to STALE (Elapsed: %lu s)\n",
                      e.nodeId, elapsed / 1000);
        evaluateParentCandidates();
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  LINK QUALITY SCORING ALGORITHM
// ─────────────────────────────────────────────────────────────
uint16_t DiscoveryManager::calculateLinkQualityScore(NeighborTableEntry &e) {
  // Disqualifications: Revoked, untrusted, dead, or zero capacity nodes score 0
  if (e.trustStatus != TRUST_REGISTERED) return 0;
  if (e.availability == AVAIL_OFFLINE) return 0;
  if (e.advertisedCapacity == 0) return 0;

  // 1. RSSI Score (0 to 100): Range -128 dBm to -65 dBm
  float sRssi = 0.0f;
  if (e.avgRssi >= -65.0f) sRssi = 100.0f;
  else if (e.avgRssi <= -128.0f) sRssi = 0.0f;
  else sRssi = ((e.avgRssi - (-128.0f)) / ((-65.0f) - (-128.0f))) * 100.0f;

  // 2. SNR Score (0 to 100): Range -15 dB to +10 dB
  float sSnr = 0.0f;
  if (e.avgSnr >= 10.0f) sSnr = 100.0f;
  else if (e.avgSnr <= -15.0f) sSnr = 0.0f;
  else sSnr = ((e.avgSnr - (-15.0f)) / (10.0f - (-15.0f))) * 100.0f;

  // 3. PDR Score (0 to 100)
  float sPdr = e.pdr * 100.0f;
  if (sPdr < 0.0f) sPdr = 0.0f;
  if (sPdr > 100.0f) sPdr = 100.0f;

  // 4. Stability Score (0 to 100)
  float sStab = (float)e.linkStability;

  // 5. Available Capacity Score (0 to 100)
  float sCap = (e.advertisedCapacity >= 5) ? 100.0f : (e.advertisedCapacity * 20.0f);

  // 6. Network Rank / Hop Score (0 to 100)
  float sRank = 0.0f;
  if (e.networkRank == 0) sRank = 100.0f; // Gateway direct
  else if (e.networkRank >= 6) sRank = 10.0f;
  else sRank = 100.0f - (e.networkRank * 15.0f);

  // Weighted sum (weights default sum to 10.0)
  float rawScore = (sPdr  * weights.wPdr) +
                   (sSnr  * weights.wSnr) +
                   (sRssi * weights.wRssi) +
                   (sStab * weights.wStability) +
                   (sCap  * weights.wCapacity) +
                   (sRank * weights.wRank);

  // Scale rawScore from 0-1000
  // Health modifier: scale down if node reports internal faults
  if (e.nodeHealth < 75) {
    rawScore *= ((float)e.nodeHealth / 100.0f);
  }

  // Stale availability penalty
  if (e.availability == AVAIL_STALE) {
    rawScore *= 0.5f;
  }

  if (rawScore < 0.0f) rawScore = 0.0f;
  if (rawScore > 1000.0f) rawScore = 1000.0f;

  return (uint16_t)rawScore;
}

// ─────────────────────────────────────────────────────────────
//  PARENT CANDIDATE DISCOVERY & SELECTION
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::evaluateParentCandidates() {
  int bestPrimaryIdx = -1;
  uint16_t bestPrimaryScore = 0;

  int bestBackupIdx = -1;
  uint16_t bestBackupScore = 0;

  // Clear previous candidate tags
  for (int i = 0; i < neighborCount; i++) {
    neighborTable[i].isPrimaryCandidate = false;
    neighborTable[i].isBackupCandidate = false;
  }

  // Step 1: Find best primary parent candidate
  for (int i = 0; i < neighborCount; i++) {
    NeighborTableEntry &e = neighborTable[i];

    if (e.availability != AVAIL_ONLINE) continue;
    if (e.trustStatus != TRUST_REGISTERED) continue;
    if (e.advertisedCapacity == 0) continue;
    if (myNetworkRank != 255 && e.networkRank >= myNetworkRank) continue; // Must be strictly closer to Gateway!
    if (e.linkQualityScore < 250) continue; // Minimum baseline quality threshold

    if (e.linkQualityScore > bestPrimaryScore) {
      bestPrimaryScore = e.linkQualityScore;
      bestPrimaryIdx = i;
    }
  }

  // Apply Hysteresis to prevent route flapping
  if (bestPrimaryIdx >= 0) {
    // If we had a previous primary, only switch if new candidate exceeds old by at least 80 points (8%)
    if (lastPrimaryId[0] != '\0' && strcmp(lastPrimaryId, neighborTable[bestPrimaryIdx].nodeId) != 0) {
      NeighborTableEntry* oldPrimary = findNeighbor(lastPrimaryId);
      if (oldPrimary != nullptr && oldPrimary->availability == AVAIL_ONLINE && oldPrimary->linkQualityScore > 0) {
        if (bestPrimaryScore < oldPrimary->linkQualityScore + 80) {
          // Keep old primary under hysteresis
          bestPrimaryIdx = -1;
          for (int i = 0; i < neighborCount; i++) {
            if (strcmp(neighborTable[i].nodeId, lastPrimaryId) == 0) {
              bestPrimaryIdx = i;
              bestPrimaryScore = neighborTable[i].linkQualityScore;
              break;
            }
          }
        }
      }
    }
  }

  if (bestPrimaryIdx >= 0) {
    neighborTable[bestPrimaryIdx].isPrimaryCandidate = true;
    strncpy(lastPrimaryId, neighborTable[bestPrimaryIdx].nodeId, sizeof(lastPrimaryId) - 1);
    const char* primaryParentUplink = neighborTable[bestPrimaryIdx].uplinkParentId;

    // Step 2: Find best backup parent candidate with PATH DIVERSITY
    for (int i = 0; i < neighborCount; i++) {
      if (i == bestPrimaryIdx) continue;
      NeighborTableEntry &e = neighborTable[i];

      if (e.availability != AVAIL_ONLINE) continue;
      if (e.trustStatus != TRUST_REGISTERED) continue;
      if (e.advertisedCapacity == 0) continue;
      if (myNetworkRank != 255 && e.networkRank >= myNetworkRank) continue;
      if (e.linkQualityScore < 200) continue;

      // PATH DIVERSITY (Fate Sharing Avoidance):
      // A backup parent should ideally have a DIFFERENT uplink path toward Gateway
      bool sharesSameUplink = (strcmp(primaryParentUplink, "GATEWAY") != 0 &&
                               primaryParentUplink[0] != '\0' &&
                               strcmp(primaryParentUplink, e.uplinkParentId) == 0);

      uint16_t effectiveScore = e.linkQualityScore;
      if (sharesSameUplink) {
        // Penalize fate-sharing candidate by 150 points to prefer diverse path
        effectiveScore = (effectiveScore > 150) ? (effectiveScore - 150) : 50;
      }

      if (effectiveScore > bestBackupScore) {
        bestBackupScore = effectiveScore;
        bestBackupIdx = i;
      }
    }

    if (bestBackupIdx >= 0) {
      neighborTable[bestBackupIdx].isBackupCandidate = true;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  TRANSMISSION & BEACON SCHEDULING
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::sendDiscoveryBeacon() {
  DiscoveryPayload p;
  String myLog = storageMgr.getNodeId();
  String myHw  = storageMgr.getHwNodeId();
  String idToSend = (myLog.length() > 0) ? myLog : myHw;

  strncpy(p.nodeId, idToSend.c_str(), sizeof(p.nodeId) - 1);
  p.networkId = MESH_NETWORK_ID;
  p.trustStatus = storageMgr.isRegistered() ? TRUST_REGISTERED : TRUST_UNREGISTERED;
  p.capabilities = CAP_ROUTER | CAP_MAINS_POWERED | CAP_SENSOR_NODE | CAP_PARENT_ELIGIBLE;
  if (feederMgr.isMainRelayOn()) p.capabilities |= CAP_FEEDER_CTRL;
  p.firmwareVersion = FW_VERSION_INT;
  p.nodeHealth = 100;
  p.networkRank = myNetworkRank;
  p.availableCapacity = myForwardingCapacity;
  strncpy(p.uplinkParentId, (myUplinkParentId[0] != '\0') ? myUplinkParentId : "NONE", sizeof(p.uplinkParentId) - 1);
  p.authTag = DiscoveryPayload::computeAuthTag(p.nodeId, p.networkId, p.networkRank, MESH_AUTH_SECRET);

  String payloadStr = p.serialize();
  uint32_t airtime = calculateLoRaAirtimeMs(payloadStr.length());

  loraMgr.txMesh(PKT_TYPE_DISC_BEACON, BROADCAST_ID, BROADCAST_ID, 1, loraMgr.nextSeq(), payloadStr);
  recordAirtimeUsage(airtime);
  tLastBeaconSent = millis();

  Serial.printf("[DISCOVERY] Broadcast DISC_BEACON (Airtime: %lu ms)\n", (unsigned long)airtime);
}

void DiscoveryManager::sendNeighborAdvertisement(const char* targetDst) {
  DiscoveryPayload p;
  String myLog = storageMgr.getNodeId();
  String myHw  = storageMgr.getHwNodeId();
  String idToSend = (myLog.length() > 0) ? myLog : myHw;

  strncpy(p.nodeId, idToSend.c_str(), sizeof(p.nodeId) - 1);
  p.networkId = MESH_NETWORK_ID;
  p.trustStatus = storageMgr.isRegistered() ? TRUST_REGISTERED : TRUST_UNREGISTERED;
  p.capabilities = CAP_ROUTER | CAP_MAINS_POWERED | CAP_SENSOR_NODE | CAP_PARENT_ELIGIBLE;
  p.firmwareVersion = FW_VERSION_INT;
  p.nodeHealth = 100;
  p.networkRank = myNetworkRank;
  p.availableCapacity = myForwardingCapacity;
  strncpy(p.uplinkParentId, (myUplinkParentId[0] != '\0') ? myUplinkParentId : "NONE", sizeof(p.uplinkParentId) - 1);
  p.authTag = DiscoveryPayload::computeAuthTag(p.nodeId, p.networkId, p.networkRank, MESH_AUTH_SECRET);

  String payloadStr = p.serialize();
  uint32_t airtime = calculateLoRaAirtimeMs(payloadStr.length());

  loraMgr.txMesh(PKT_TYPE_NEIGH_ADV, targetDst, targetDst, 1, loraMgr.nextSeq(), payloadStr);
  recordAirtimeUsage(airtime);
  tLastBeaconSent = millis();

  Serial.printf("[DISCOVERY] Sent NEIGH_ADV to %s (Airtime: %lu ms)\n", targetDst, (unsigned long)airtime);
}

void DiscoveryManager::sendNeighborResponse(const char* targetNode, int16_t rxRssi, int8_t rxSnr) {
  DiscoveryPayload p;
  String myLog = storageMgr.getNodeId();
  String myHw  = storageMgr.getHwNodeId();
  String idToSend = (myLog.length() > 0) ? myLog : myHw;

  strncpy(p.nodeId, idToSend.c_str(), sizeof(p.nodeId) - 1);
  p.networkId = MESH_NETWORK_ID;
  p.trustStatus = storageMgr.isRegistered() ? TRUST_REGISTERED : TRUST_UNREGISTERED;
  p.capabilities = CAP_ROUTER | CAP_MAINS_POWERED | CAP_SENSOR_NODE | CAP_PARENT_ELIGIBLE;
  p.firmwareVersion = FW_VERSION_INT;
  p.nodeHealth = 100;
  p.networkRank = myNetworkRank;
  p.availableCapacity = myForwardingCapacity;
  strncpy(p.uplinkParentId, (myUplinkParentId[0] != '\0') ? myUplinkParentId : "NONE", sizeof(p.uplinkParentId) - 1);
  p.authTag = DiscoveryPayload::computeAuthTag(p.nodeId, p.networkId, p.networkRank, MESH_AUTH_SECRET);

  String payloadStr = p.serialize() + "|R_RSSI=" + String(rxRssi) + "|R_SNR=" + String(rxSnr);
  uint32_t airtime = calculateLoRaAirtimeMs(payloadStr.length());

  loraMgr.txMesh(PKT_TYPE_NEIGH_RSP, targetNode, targetNode, 1, loraMgr.nextSeq(), payloadStr);
  recordAirtimeUsage(airtime);

  Serial.printf("[DISCOVERY] Sent NEIGH_RSP to %s (Reciprocal RSSI: %d, SNR: %d)\n", targetNode, rxRssi, rxSnr);
}

void DiscoveryManager::sendLinkQualityReport() {
  // Build aggregated diagnostic string to Gateway
  char buf[128];
  const NeighborTableEntry* primary = getPrimaryParentCandidate();
  const NeighborTableEntry* backup  = getBackupParentCandidate();

  snprintf(buf, sizeof(buf), "COUNT=%d|PRI=%s,SC=%u|BCK=%s,SC=%u|AIR=%lu,DC=%.2f",
           neighborCount,
           primary ? primary->nodeId : "NONE", primary ? primary->linkQualityScore : 0,
           backup  ? backup->nodeId  : "NONE", backup  ? backup->linkQualityScore  : 0,
           (unsigned long)airtimeUsedInWindow, getDutyCyclePercent());

  String nh = primary ? primary->nodeId : GATEWAY_ID;
  uint32_t airtime = calculateLoRaAirtimeMs(strlen(buf));
  loraMgr.txMesh(PKT_TYPE_LQ_REPORT, GATEWAY_ID, nh, DEFAULT_TTL, loraMgr.nextSeq(), String(buf));
  recordAirtimeUsage(airtime);
}

// ─────────────────────────────────────────────────────────────
//  AIRTIME BUDGETING & DUTY CYCLE CONTROLLER
// ─────────────────────────────────────────────────────────────
bool DiscoveryManager::canTransmit(uint32_t airtimeMs) const {
  unsigned long now = millis();
  if (now - tLastBeaconSent < MIN_PACKET_INTERVAL_MS) {
    return false; // Physical inter-packet spacing
  }
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

// ─────────────────────────────────────────────────────────────
//  DUPLICATE SUPPRESSION
// ─────────────────────────────────────────────────────────────
bool DiscoveryManager::isDuplicateAdvertisement(const char* nodeId, uint32_t authTag) {
  unsigned long now = millis();
  for (int i = 0; i < 8; i++) {
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
  seenCacheHead = (seenCacheHead + 1) % 8;
}

// ─────────────────────────────────────────────────────────────
//  BEACON SCHEDULER & JITTER (Trickle / Backoff)
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::scheduleNextBeacon() {
  unsigned long baseInterval;

  if (!hasValidPrimaryCandidate() || myNetworkRank == 255) {
    // Unparented: bootstrap fast discovery (15s to 30s)
    baseInterval = (neighborCount == 0) ? DISCOVERY_BOOT_INTERVAL_MS : DISCOVERY_MIN_INTERVAL_MS;
  } else {
    // Steady state: long interval (3 minutes)
    baseInterval = DISCOVERY_MAX_INTERVAL_MS;
  }

  unsigned long jittered = calculateJitteredInterval(baseInterval);
  tNextBeacon = millis() + jittered;
}

unsigned long DiscoveryManager::calculateJitteredInterval(unsigned long baseInterval) {
  // Apply +/- 30% randomization to prevent synchronized beacon storms
  long maxJitter = (long)(baseInterval * DISCOVERY_JITTER_PCT / 100);
  long jitter = random(-maxJitter, maxJitter + 1);
  long result = (long)baseInterval + jitter;
  return (result > 5000) ? (unsigned long)result : 5000UL;
}

// ─────────────────────────────────────────────────────────────
//  REVOCATION MANAGEMENT
// ─────────────────────────────────────────────────────────────
void DiscoveryManager::revokeNode(const char* nodeId) {
  if (nodeId == nullptr || nodeId[0] == '\0') return;
  if (isNodeRevoked(nodeId)) return;

  if (revokedCount < 4) {
    strncpy(revokedNodes[revokedCount++], nodeId, 15);
    revokedNodes[revokedCount - 1][15] = '\0';
  }

  // Update existing table entry if present
  NeighborTableEntry* n = findNeighbor(nodeId);
  if (n) {
    n->trustStatus = TRUST_REVOKED;
    n->linkQualityScore = 0;
    n->isPrimaryCandidate = false;
    n->isBackupCandidate = false;
  }
  evaluateParentCandidates();
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
//  QUERIES & DIAGNOSTICS
// ─────────────────────────────────────────────────────────────
int DiscoveryManager::getNeighborCount() const {
  return neighborCount;
}

int DiscoveryManager::getValidNeighborCount() const {
  int count = 0;
  for (int i = 0; i < neighborCount; i++) {
    if (neighborTable[i].occupied && neighborTable[i].availability != AVAIL_OFFLINE) {
      count++;
    }
  }
  return count;
}

NeighborTableEntry* DiscoveryManager::getNeighbor(int index) {
  if (index >= 0 && index < neighborCount) {
    return &neighborTable[index];
  }
  return nullptr;
}

NeighborTableEntry* DiscoveryManager::findNeighbor(const char* nodeId) {
  if (nodeId == nullptr) return nullptr;
  for (int i = 0; i < neighborCount; i++) {
    if (strcmp(neighborTable[i].nodeId, nodeId) == 0) {
      return &neighborTable[i];
    }
  }
  return nullptr;
}

const NeighborTableEntry* DiscoveryManager::getPrimaryParentCandidate() const {
  for (int i = 0; i < neighborCount; i++) {
    if (neighborTable[i].isPrimaryCandidate) return &neighborTable[i];
  }
  return nullptr;
}

const NeighborTableEntry* DiscoveryManager::getBackupParentCandidate() const {
  for (int i = 0; i < neighborCount; i++) {
    if (neighborTable[i].isBackupCandidate) return &neighborTable[i];
  }
  return nullptr;
}

bool DiscoveryManager::hasValidPrimaryCandidate() const {
  return (getPrimaryParentCandidate() != nullptr);
}

void DiscoveryManager::setWeights(const LinkQualityWeights &w) {
  weights = w;
}

const LinkQualityWeights& DiscoveryManager::getWeights() const {
  return weights;
}

void DiscoveryManager::setNetworkRank(uint8_t rank) {
  myNetworkRank = rank;
}

uint8_t DiscoveryManager::getNetworkRank() const {
  return myNetworkRank;
}

void DiscoveryManager::setForwardingCapacity(uint8_t capacity) {
  myForwardingCapacity = capacity;
}

uint8_t DiscoveryManager::getForwardingCapacity() const {
  return myForwardingCapacity;
}

void DiscoveryManager::setUplinkParent(const char* parentId) {
  if (parentId) {
    strncpy(myUplinkParentId, parentId, sizeof(myUplinkParentId) - 1);
  } else {
    memset(myUplinkParentId, 0, sizeof(myUplinkParentId));
  }
}

const char* DiscoveryManager::getUplinkParent() const {
  return myUplinkParentId;
}

void DiscoveryManager::getDiagnostics(DiscoveryDiagnostics &diag) const {
  diag.totalDiscovered = neighborCount;
  diag.authenticatedNeighbors = 0;
  diag.unregisteredDevices = 0;
  diag.revokedDevices = 0;
  diag.unstableCandidates = 0;
  diag.bestRssi = -128;
  diag.worstRssi = 0;
  diag.bestSnr = -30;
  diag.worstSnr = 30;
  diag.discoveryFailures = discoveryFailures;
  diag.airtimeUsedMs = airtimeUsedInWindow;
  diag.dutyCyclePercent = getDutyCyclePercent();
  diag.lastAdvSentMs = tLastBeaconSent;

  memset(diag.primaryCandidateId, 0, sizeof(diag.primaryCandidateId));
  memset(diag.backupCandidateId, 0, sizeof(diag.backupCandidateId));
  diag.primaryCandidateScore = 0;
  diag.backupCandidateScore = 0;

  for (int i = 0; i < neighborCount; i++) {
    const NeighborTableEntry &e = neighborTable[i];
    if (e.trustStatus == TRUST_REGISTERED) diag.authenticatedNeighbors++;
    else if (e.trustStatus == TRUST_UNREGISTERED) diag.unregisteredDevices++;
    else if (e.trustStatus == TRUST_REVOKED) diag.revokedDevices++;

    if (e.linkStability < 50 || e.pdr < 0.70f) diag.unstableCandidates++;

    if (e.rssi > diag.bestRssi) diag.bestRssi = e.rssi;
    if (e.rssi < diag.worstRssi || diag.worstRssi == 0) diag.worstRssi = e.rssi;
    if (e.snr > diag.bestSnr) diag.bestSnr = e.snr;
    if (e.snr < diag.worstSnr) diag.worstSnr = e.snr;

    if (e.isPrimaryCandidate) {
      strncpy(diag.primaryCandidateId, e.nodeId, 15);
      diag.primaryCandidateScore = e.linkQualityScore;
    }
    if (e.isBackupCandidate) {
      strncpy(diag.backupCandidateId, e.nodeId, 15);
      diag.backupCandidateScore = e.linkQualityScore;
    }
  }
}

String DiscoveryManager::buildDiagnosticSummary() const {
  DiscoveryDiagnostics d;
  getDiagnostics(d);

  char buf[256];
  snprintf(buf, sizeof(buf),
           "[DISC DIAG] Nodes=%u (Auth=%u, Unreg=%u, Rev=%u, Unst=%u) | BestRSSI=%d, BestSNR=%d | Prim=%s (%u) | Bck=%s (%u) | Air=%lu ms (%.2f%%)",
           d.totalDiscovered, d.authenticatedNeighbors, d.unregisteredDevices, d.revokedDevices, d.unstableCandidates,
           d.bestRssi, d.bestSnr,
           (d.primaryCandidateId[0] != '\0') ? d.primaryCandidateId : "NONE", d.primaryCandidateScore,
           (d.backupCandidateId[0] != '\0')  ? d.backupCandidateId  : "NONE", d.backupCandidateScore,
           (unsigned long)d.airtimeUsedMs, d.dutyCyclePercent);
  return String(buf);
}

String DiscoveryManager::buildNeighborTableDump() const {
  String out = "=== NEIGHBOR TABLE (Total: " + String(neighborCount) + ") ===\n";
  out += "ID          RSSI  AvgR  SNR  AvgS  PDR   Stab  Trust  Cap  Rank  Score  Avail    Role\n";
  out += "------------------------------------------------------------------------------------\n";

  for (int i = 0; i < neighborCount; i++) {
    const NeighborTableEntry &e = neighborTable[i];
    char line[120];
    const char* trustStr = (e.trustStatus == TRUST_REGISTERED) ? "REG" :
                           (e.trustStatus == TRUST_UNREGISTERED) ? "UNR" :
                           (e.trustStatus == TRUST_REVOKED) ? "REV" : "UNT";
    const char* availStr = (e.availability == AVAIL_ONLINE) ? "ONLINE" :
                           (e.availability == AVAIL_STALE)  ? "STALE"  : "OFFLINE";
    const char* roleStr  = e.isPrimaryCandidate ? "PRIMARY" :
                           e.isBackupCandidate  ? "BACKUP"  : "CANDIDATE";

    snprintf(line, sizeof(line), "%-11s %4d %5.1f %4d %5.1f %5.1f%% %3u%%  %-5s %3u  %4u   %4u  %-7s  %s\n",
             e.nodeId, e.rssi, e.avgRssi, e.snr, e.avgSnr, e.pdr * 100.0f,
             e.linkStability, trustStr, e.advertisedCapacity, e.networkRank,
             e.linkQualityScore, availStr, roleStr);
    out += line;
  }
  return out;
}
