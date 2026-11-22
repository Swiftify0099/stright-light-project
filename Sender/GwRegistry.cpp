#include "GwRegistry.h"
#include <stdio.h>

GwRegistry gwRegistry;

GwRegistry::GwRegistry() :
  nodeCount(0), pendingCount(0), revokedCount(0), nonceHead(0),
  masterKeyLen(0), nextPoleIndex(1), nextFeederIndex(1)
{
  for (int i = 0; i < NONCE_CACHE_SIZE; i++) {
    nonceCache[i].hwId[0] = '\0';
    nonceCache[i].nonce = 0;
    nonceCache[i].bootSessionId = 0;
    nonceCache[i].timestamp = 0;
  }
}

void GwRegistry::init(const uint8_t *commissioningKey, size_t keyLen) {
  if (commissioningKey && keyLen > 0) {
    masterKeyLen = (keyLen > 32) ? 32 : keyLen;
    memcpy(masterKey, commissioningKey, masterKeyLen);
  } else {
    // Default system master provisioning key
    const char *defaultMaster = "MSCB_MASTER_ROOT_KEY_2026_SEC";
    masterKeyLen = strlen(defaultMaster);
    memcpy(masterKey, defaultMaster, masterKeyLen);
  }

  storage.init();
  storage.loadRevocationList(revokedList, revokedCount);
  storage.loadRegistry(nodes, nodeCount, MSCB_MAX_REGISTRY_NODES);

  // Synchronize index counters to highest registered number
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (strncmp(nodes[i].logicalId, "POLE-", 5) == 0) {
      int idNum = atoi(nodes[i].logicalId + 5);
      if (idNum >= nextPoleIndex) nextPoleIndex = idNum + 1;
    } else if (strncmp(nodes[i].logicalId, "FDR-", 4) == 0) {
      int idNum = atoi(nodes[i].logicalId + 4);
      if (idNum >= nextFeederIndex) nextFeederIndex = idNum + 1;
    }
  }

#ifdef ARDUINO
  Serial.printf("[GW_REGISTRY] Initialized. Loaded %u nodes, %u revoked from NVS.\n",
                nodeCount, revokedCount);
#endif
}

bool GwRegistry::deriveDeviceKey(const char *hwId, uint8_t outKey[32]) {
  if (!hwId || strlen(hwId) == 0) return false;
  char tagHex[17];
  // Keyed derivation: Device key is cryptographic product of Master Key and Hardware ID
  bool ok = mscbComputeAuthTag(masterKey, masterKeyLen,
                               (const uint8_t*)hwId, strlen(hwId),
                               tagHex);
  if (!ok) return false;
  // Expand tag into 32-byte key material
  for (int i = 0; i < 32; i++) {
    outKey[i] = (uint8_t)(tagHex[i % 16] ^ (i * 0x5A));
  }
  return true;
}

bool GwRegistry::isNonceReplayed(const char *hwId, uint32_t nonce, uint32_t bootSessionId) {
  for (int i = 0; i < NONCE_CACHE_SIZE; i++) {
    if (strcmp(nonceCache[i].hwId, hwId) == 0 &&
        nonceCache[i].nonce == nonce &&
        nonceCache[i].bootSessionId == bootSessionId) {
      return true;
    }
  }
  return false;
}

void GwRegistry::recordNonce(const char *hwId, uint32_t nonce, uint32_t bootSessionId) {
  strncpy(nonceCache[nonceHead].hwId, hwId, 16);
  nonceCache[nonceHead].hwId[16] = '\0';
  nonceCache[nonceHead].nonce = nonce;
  nonceCache[nonceHead].bootSessionId = bootSessionId;
  nonceCache[nonceHead].timestamp = 0; // Host or millis()
  nonceHead = (nonceHead + 1) % NONCE_CACHE_SIZE;
}

bool GwRegistry::isDeviceRevoked(const char *hwId) const {
  for (uint8_t i = 0; i < revokedCount; i++) {
    if (strcmp(revokedList[i], hwId) == 0) {
      return true;
    }
  }
  return false;
}

uint8_t GwRegistry::getNodeChildLoad(const char *nodeLogicalId) const {
  if (!nodeLogicalId || strlen(nodeLogicalId) == 0) return 0;
  uint8_t load = 0;
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (strcmp(nodes[i].primaryParent, nodeLogicalId) == 0) {
      load++;
    }
  }
  return load;
}

bool GwRegistry::evaluateAndAssignParents(const char *priCand, const char *bkpCand,
                                         char outPri[16], char outBkp[16]) {
  outPri[0] = '\0';
  outBkp[0] = '\0';

  bool priValid = false;
  if (strcmp(priCand, "GATEWAY") == 0) {
    strncpy(outPri, "GATEWAY", 15);
    outPri[15] = '\0';
    priValid = true;
  } else {
    int idx = findNodeByLogicalId(priCand);
    if (idx >= 0 && nodes[idx].regState == REG_STATE_ACTIVE && !isDeviceRevoked(nodes[idx].hwId)) {
      strncpy(outPri, priCand, 15);
      outPri[15] = '\0';
      priValid = true;
    }
  }

  // Backup parent candidate evaluation
  if (bkpCand && strlen(bkpCand) > 0 && strcmp(bkpCand, "NONE") != 0 && strcmp(bkpCand, outPri) != 0) {
    if (strcmp(bkpCand, "GATEWAY") == 0) {
      strncpy(outBkp, "GATEWAY", 15);
      outBkp[15] = '\0';
    } else {
      int bIdx = findNodeByLogicalId(bkpCand);
      if (bIdx >= 0 && nodes[bIdx].regState == REG_STATE_ACTIVE && !isDeviceRevoked(nodes[bIdx].hwId)) {
        strncpy(outBkp, bkpCand, 15);
        outBkp[15] = '\0';
      } else {
        strncpy(outBkp, "NONE", 15);
      }
    }
  } else {
    strncpy(outBkp, "NONE", 15);
  }

  // If primary candidate was invalid, attempt fallback to Gateway direct
  if (!priValid) {
    strncpy(outPri, "GATEWAY", 15);
    outPri[15] = '\0';
  }
  return true;
}

uint8_t GwRegistry::processJoinRequest(const char *hwId, uint8_t nodeType, uint16_t fwVersion,
                                      uint32_t bootSessionId, const char *priCandidate,
                                      const char *bkpCandidate, uint32_t nodeNonce,
                                      const char *authTag, int16_t rssi, int8_t snr,
                                      char outAssignedLogicalId[16], char outPriParent[16],
                                      char outBkpParent[16]) {
  if (outAssignedLogicalId) outAssignedLogicalId[0] = '\0';
  if (outPriParent) outPriParent[0] = '\0';
  if (outBkpParent) outBkpParent[0] = '\0';

  // 1. Permanent Revocation Check
  if (isDeviceRevoked(hwId)) {
    return REJECT_DEVICE_REVOKED;
  }

  // 2. Replay Protection
  if (isNonceReplayed(hwId, nodeNonce, bootSessionId)) {
    return REJECT_REPLAY_DETECTED;
  }

  // 3. Authenticate JOIN_REQUEST with Cryptographic Identity
  uint8_t devKey[32];
  if (!deriveDeviceKey(hwId, devKey)) {
    return REJECT_AUTH_FAIL;
  }

  char signPayload[128];
  snprintf(signPayload, sizeof(signPayload), "%s|%u|%u|%u|%s|%s|%u|%04X",
           hwId, nodeType, fwVersion, bootSessionId,
           priCandidate, bkpCandidate, nodeNonce, MSCB_DEFAULT_NETWORK_ID);

  char expectedTag[17];
  mscbComputeAuthTag(devKey, 32, (const uint8_t*)signPayload, strlen(signPayload), expectedTag);

  if (strcmp(authTag, expectedTag) != 0) {
    return REJECT_AUTH_FAIL;
  }

  // Auth succeeded; record nonce to prevent subsequent replays
  recordNonce(hwId, nodeNonce, bootSessionId);

  // 4. Check for Existing Active Node (Reboot Recovery Scenario)
  int existingIdx = findNodeByHwId(hwId);
  if (existingIdx >= 0) {
    MscbNodeRecord &node = nodes[existingIdx];
    if (node.regState == REG_STATE_ACTIVE || node.regState == REG_STATE_APPROVED) {
      node.bootSessionId = bootSessionId;
      node.lastSeenTime  = 0;
      node.lastRssi      = rssi;
      node.lastSnr       = snr;
      node.regState      = REG_STATE_ACTIVE;
      storage.saveNodeRecord(existingIdx, node);

      if (outAssignedLogicalId) strncpy(outAssignedLogicalId, node.logicalId, 15);
      if (outPriParent) strncpy(outPriParent, node.primaryParent, 15);
      if (outBkpParent) strncpy(outBkpParent, node.backupParent, 15);
      return REJECT_NONE;
    }
  }

  // 5. Registry Capacity Limit Check (Bounded to 50 nodes)
  if (nodeCount >= MSCB_MAX_REGISTRY_NODES) {
    return REJECT_REGISTRY_FULL;
  }

  // 6. Parent Candidate Feasibility Check
  char finalPri[16], finalBkp[16];
  evaluateAndAssignParents(priCandidate, bkpCandidate, finalPri, finalBkp);
  if (outPriParent) strncpy(outPriParent, finalPri, 15);
  if (outBkpParent) strncpy(outBkpParent, finalBkp, 15);

  // 7. Enqueue into Pending Approval Queue
  int pendingIdx = findPendingByHwId(hwId);
  if (pendingIdx < 0) {
    if (pendingCount >= MSCB_MAX_PENDING_APPROVALS) {
      pendingIdx = 0; // Replace oldest if full
    } else {
      pendingIdx = pendingCount++;
    }
  }

  MscbPendingRegistration &p = pending[pendingIdx];
  p.active = true;
  strncpy(p.hwId, hwId, 16);
  p.hwId[16] = '\0';
  p.nodeType = nodeType;
  p.firmwareVersion = fwVersion;
  p.bootSessionId = bootSessionId;
  strncpy(p.priCandidate, finalPri, 15);
  strncpy(p.bkpCandidate, finalBkp, 15);
  p.rssi = rssi;
  p.snr = snr;
  p.nodeNonce = nodeNonce;
  strncpy(p.authTag, authTag, 16);
  p.authTag[16] = '\0';
  p.requestTime = 0;
  p.state = REG_STATE_PENDING_APPROVAL;

  // Generate suggested Logical ID
  generateUniqueLogicalId(nodeType, p.suggestedLogicalId);
  snprintf(p.suggestedName, sizeof(p.suggestedName), "Smart Pole %s", p.suggestedLogicalId + 5);
  strncpy(p.suggestedArea, "Zone 1", 31);
  strncpy(p.suggestedInstallInfo, "Pending Admin Commissioning", 47);

  if (outAssignedLogicalId) strncpy(outAssignedLogicalId, p.suggestedLogicalId, 15);
  return REJECT_NONE;
}

bool GwRegistry::generateUniqueLogicalId(uint8_t nodeType, char outId[16]) {
  const char *prefix = (nodeType == DEV_TYPE_FEEDER) ? "FDR" : "POLE";
  uint16_t &idxRef = (nodeType == DEV_TYPE_FEEDER) ? nextFeederIndex : nextPoleIndex;

  for (int attempts = 0; attempts < 1000; attempts++) {
    char candidate[16];
    snprintf(candidate, sizeof(candidate), "%s-%04u", prefix, idxRef++);
    if (findNodeByLogicalId(candidate) < 0) {
      strncpy(outId, candidate, 15);
      outId[15] = '\0';
      return true;
    }
  }
  return false;
}

bool GwRegistry::approveNode(const char *hwId, const char *logicalId, const char *friendlyName,
                            const char *area, const char *installInfo, const char *primaryParent,
                            const char *backupParent, const MscbFeederRecord *feederConfigs) {
  if (!hwId || !logicalId) return false;

  // 1. Enforce 50 Node Capacity Limit
  int existingIdx = findNodeByHwId(hwId);
  if (existingIdx < 0 && nodeCount >= MSCB_MAX_REGISTRY_NODES) {
    return false;
  }

  // 2. Prevent Duplicate Hardware Identity & Duplicate Logical IDs
  if (existingIdx >= 0 && strcmp(nodes[existingIdx].logicalId, logicalId) != 0) {
    // Hardware ID already registered under another logical ID
    return false;
  }

  int idMatch = findNodeByLogicalId(logicalId);
  if (idMatch >= 0 && (existingIdx < 0 || idMatch != existingIdx)) {
    return false;
  }

  // 3. Parent Validation (Never assign unregistered or revoked node as Parent)
  if (strcmp(primaryParent, "GATEWAY") != 0) {
    int pIdx = findNodeByLogicalId(primaryParent);
    if (pIdx < 0 || nodes[pIdx].regState != REG_STATE_ACTIVE || isDeviceRevoked(nodes[pIdx].hwId)) {
      return false;
    }
  }
  if (backupParent && strlen(backupParent) > 0 && strcmp(backupParent, "NONE") != 0 && strcmp(backupParent, "GATEWAY") != 0) {
    int bIdx = findNodeByLogicalId(backupParent);
    if (bIdx < 0 || nodes[bIdx].regState != REG_STATE_ACTIVE || isDeviceRevoked(nodes[bIdx].hwId)) {
      backupParent = "NONE";
    }
  }

  // 4. Fetch details from pending if available
  uint16_t fwVer = 11;
  uint32_t bootSession = 1;
  uint8_t nType = DEV_TYPE_POLE;
  int pIdx = findPendingByHwId(hwId);
  if (pIdx >= 0) {
    fwVer = pending[pIdx].firmwareVersion;
    bootSession = pending[pIdx].bootSessionId;
    nType = pending[pIdx].nodeType;
  }

  // 5. Build Registry Record
  uint8_t slot = (existingIdx >= 0) ? (uint8_t)existingIdx : nodeCount;
  MscbNodeRecord &rec = nodes[slot];
  memset(&rec, 0, sizeof(MscbNodeRecord));

  strncpy(rec.logicalId, logicalId, 15);
  strncpy(rec.hwId, hwId, 16);
  rec.nodeType = nType;
  strncpy(rec.friendlyName, friendlyName ? friendlyName : logicalId, 31);
  strncpy(rec.area, area ? area : "Default Area", 31);
  strncpy(rec.installInfo, installInfo ? installInfo : "Commissioned", 47);
  rec.registrationTime = 0; // Host/time(nullptr)
  rec.regState = REG_STATE_APPROVED;
  strncpy(rec.primaryParent, primaryParent, 15);
  strncpy(rec.backupParent, (backupParent && strlen(backupParent) > 0) ? backupParent : "NONE", 15);
  rec.firmwareVersion = fwVer;
  rec.bootSessionId = bootSession;
  rec.lastSeenTime = 0;
  rec.configVersion = 1;
  rec.feederCount = MSCB_FEEDERS_PER_POLE;

  // 6. Initialize 6 Feeder Channel Records
  for (uint8_t f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
    if (feederConfigs) {
      rec.feeders[f] = feederConfigs[f];
      rec.feeders[f].feederId = f + 1;
    } else {
      rec.feeders[f].feederId = f + 1;
      snprintf(rec.feeders[f].name, sizeof(rec.feeders[f].name), "Feeder %u", f + 1);
      rec.feeders[f].enabled = true;
      rec.feeders[f].currentRating = 16.0f;
      rec.feeders[f].relayState = false; // Do not activate relays during registration
      rec.feeders[f].mcbR = true;
      rec.feeders[f].mcbY = true;
      rec.feeders[f].mcbB = true;
      rec.feeders[f].contactor = false;
    }
  }

  // 7. Persist to NVS with CRC validation
  bool saveOk = storage.saveNodeRecord(slot, rec);
  if (!saveOk) return false;

  if (existingIdx < 0) {
    nodeCount++;
  }

  // 8. Remove from Pending Queue
  if (pIdx >= 0) {
    pending[pIdx].active = false;
  }
  return true;
}

bool GwRegistry::rejectNode(const char *hwId, uint8_t reason) {
  int pIdx = findPendingByHwId(hwId);
  if (pIdx >= 0) {
    pending[pIdx].active = false;
    pending[pIdx].state = REG_STATE_REJECTED;
    return true;
  }
  return false;
}

bool GwRegistry::revokeNode(const char *hwId, const char *reason) {
  if (!hwId) return false;

  // Add to revocation blacklist
  if (!isDeviceRevoked(hwId)) {
    if (revokedCount < 16) {
      strncpy(revokedList[revokedCount], hwId, 16);
      revokedList[revokedCount][16] = '\0';
      revokedCount++;
      storage.saveRevocationList(revokedList, revokedCount);
    }
  }

  // Update existing record state if present
  int idx = findNodeByHwId(hwId);
  if (idx >= 0) {
    nodes[idx].regState = REG_STATE_REVOKED;
    storage.saveNodeRecord(idx, nodes[idx]);
  }

  // Remove from pending
  int pIdx = findPendingByHwId(hwId);
  if (pIdx >= 0) {
    pending[pIdx].active = false;
  }
  return true;
}

bool GwRegistry::confirmNodeActivation(const char *logicalId, uint32_t sessionToken) {
  int idx = findNodeByLogicalId(logicalId);
  if (idx < 0) return false;

  nodes[idx].regState = REG_STATE_ACTIVE;
  nodes[idx].lastSeenTime = 0; // millis()
  storage.saveNodeRecord(idx, nodes[idx]);
  return true;
}

int GwRegistry::findNodeByHwId(const char *hwId) const {
  if (!hwId) return -1;
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (strcmp(nodes[i].hwId, hwId) == 0) return i;
  }
  return -1;
}

int GwRegistry::findNodeByLogicalId(const char *logicalId) const {
  if (!logicalId) return -1;
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (strcmp(nodes[i].logicalId, logicalId) == 0) return i;
  }
  return -1;
}

MscbNodeRecord* GwRegistry::getNode(uint8_t index) {
  if (index < nodeCount) return &nodes[index];
  return nullptr;
}

uint8_t GwRegistry::getNodeCount() const { return nodeCount; }

bool GwRegistry::isRegistryFull() const { return (nodeCount >= MSCB_MAX_REGISTRY_NODES); }

uint8_t GwRegistry::getPendingCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < pendingCount; i++) {
    if (pending[i].active) count++;
  }
  return count;
}

MscbPendingRegistration* GwRegistry::getPending(uint8_t index) {
  uint8_t cur = 0;
  for (uint8_t i = 0; i < pendingCount; i++) {
    if (pending[i].active) {
      if (cur == index) return &pending[i];
      cur++;
    }
  }
  return nullptr;
}

int GwRegistry::findPendingByHwId(const char *hwId) const {
  if (!hwId) return -1;
  for (uint8_t i = 0; i < pendingCount; i++) {
    if (pending[i].active && strcmp(pending[i].hwId, hwId) == 0) {
      return i;
    }
  }
  return -1;
}

void GwRegistry::purgeExpiredPending(uint32_t currentMillis) {
  // Purge any requests pending longer than 5 minutes
  for (uint8_t i = 0; i < pendingCount; i++) {
    if (pending[i].active && (currentMillis - pending[i].requestTime > MSCB_REG_APPROVAL_TIMEOUT_MS)) {
      pending[i].active = false;
      pending[i].state = REG_STATE_REJECTED;
    }
  }
}

uint8_t GwRegistry::getRevokedCount() const { return revokedCount; }

const char* GwRegistry::getRevokedHwId(uint8_t index) const {
  if (index < revokedCount) return revokedList[index];
  return nullptr;
}

void GwRegistry::updateNodeLastSeen(const char *idOrHw, int16_t rssi, int8_t snr) {
  int idx = findNodeByLogicalId(idOrHw);
  if (idx < 0) idx = findNodeByHwId(idOrHw);
  if (idx >= 0) {
    nodes[idx].lastSeenTime = 0; // or millis()
    nodes[idx].lastRssi = rssi;
    nodes[idx].lastSnr = snr;
    if (nodes[idx].regState == REG_STATE_OFFLINE) {
      nodes[idx].regState = REG_STATE_ACTIVE;
    }
  }
}

void GwRegistry::checkNodeTimeouts(uint32_t currentMillis, uint32_t timeoutMs) {
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (nodes[i].regState == REG_STATE_ACTIVE) {
      if (currentMillis - nodes[i].lastSeenTime > timeoutMs && nodes[i].lastSeenTime != 0) {
        nodes[i].regState = REG_STATE_OFFLINE;
      }
    }
  }
}
