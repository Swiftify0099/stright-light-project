/**
 * @file GwNodeRegistry.cpp
 * @brief MSCB Smart Pole Gateway - Persistent Pole Registry & Revocation Blacklist Implementation
 * @version 1.0.0
 */

#include "GwNodeRegistry.h"
#include <string.h>
#include <stdio.h>

GwNodeRegistry gwRegistry;

static inline uint32_t calculateRegistryCrc(uint8_t activeCnt, const RegisteredPole* pPoles,
                                           uint8_t revokedCnt, const PermanentRevocationRecord* pRevoked,
                                           uint8_t pendingCnt, const PendingRegistration* pPending) {
  uint32_t crc = 0xFFFFFFFFUL;
  auto updateCrc = [&crc](const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
      crc ^= data[i];
      for (uint8_t j = 0; j < 8; j++) {
        if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320UL;
        else crc >>= 1;
      }
    }
  };

  updateCrc(&activeCnt, sizeof(activeCnt));
  if (activeCnt > 0 && pPoles) {
    updateCrc((const uint8_t*)pPoles, activeCnt * sizeof(RegisteredPole));
  }
  updateCrc(&revokedCnt, sizeof(revokedCnt));
  if (revokedCnt > 0 && pRevoked) {
    updateCrc((const uint8_t*)pRevoked, revokedCnt * sizeof(PermanentRevocationRecord));
  }
  updateCrc(&pendingCnt, sizeof(pendingCnt));
  if (pendingCnt > 0 && pPending) {
    updateCrc((const uint8_t*)pPending, pendingCnt * sizeof(PendingRegistration));
  }
  return ~crc;
}

GwNodeRegistry::GwNodeRegistry() :
  activeCount(0), pendingCount(0), revokedCount(0),
  reappearedAlertPending(false)
{
  memset(poles, 0, sizeof(poles));
  memset(pending, 0, sizeof(pending));
  memset(revoked, 0, sizeof(revoked));
  memset(reappearedAlertBuf, 0, sizeof(reappearedAlertBuf));
}

bool GwNodeRegistry::begin() {
  return loadRegistry();
}

bool GwNodeRegistry::isMacRevoked(const uint8_t mac[6]) const {
  if (!mac) return false;
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  for (uint8_t i = 0; i < revokedCount; i++) {
    if (strncmp(revoked[i].hwId, macHex, 12) == 0) {
      return true;
    }
  }
  return false;
}

const PermanentRevocationRecord* GwNodeRegistry::findRevokedByMac(const uint8_t mac[6]) const {
  if (!mac) return NULL;
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  for (uint8_t i = 0; i < revokedCount; i++) {
    if (strncmp(revoked[i].hwId, macHex, 12) == 0) {
      return &revoked[i];
    }
  }
  return NULL;
}

const PermanentRevocationRecord* GwNodeRegistry::findRevokedByPoleId(uint16_t poleId) const {
  char pidStr[16];
  snprintf(pidStr, sizeof(pidStr), "POLE_%02d", poleId);
  for (uint8_t i = 0; i < revokedCount; i++) {
    if (strcmp(revoked[i].logicalId, pidStr) == 0) {
      return &revoked[i];
    }
  }
  return NULL;
}

const PermanentRevocationRecord* GwNodeRegistry::getRevokedByIndex(uint8_t index) const {
  if (index >= revokedCount) return NULL;
  return &revoked[index];
}

void GwNodeRegistry::setReappearedRevokedAlert(const uint8_t mac[6], const char* prevLogicalId, uint16_t prevPoleId) {
  reappearedAlertPending = true;
  snprintf(reappearedAlertBuf, sizeof(reappearedAlertBuf),
           "🚨 *REVOKED DEVICE REAPPEARED*\n"
           "MAC: `%02X:%02X:%02X:%02X:%02X:%02X`\n"
           "Previous ID: %s (0x%04X)\n"
           "Status: PERMANENTLY REVOKED (Relays locked OFF)\n"
           "Action: Device rejected from network.\n"
           "To re-authorize: /restore_%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
           prevLogicalId ? prevLogicalId : "UNKNOWN",
           prevPoleId,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

RegisteredPole* GwNodeRegistry::findPoleById(uint16_t poleId) {
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].poleId == poleId && poles[i].status == POLE_STATUS_ACTIVE) {
      return &poles[i];
    }
  }
  return NULL;
}

RegisteredPole* GwNodeRegistry::findPoleByMac(const uint8_t mac[6]) {
  if (!mac) return NULL;
  for (uint8_t i = 0; i < activeCount; i++) {
    if (memcmp(poles[i].mac, mac, 6) == 0 && poles[i].status == POLE_STATUS_ACTIVE) {
      return &poles[i];
    }
  }
  return NULL;
}

const RegisteredPole* GwNodeRegistry::getPoleByIndex(uint8_t index) const {
  if (index >= activeCount) return NULL;
  return &poles[index];
}

const PendingRegistration* GwNodeRegistry::getPendingByIndex(uint8_t index) const {
  if (index >= pendingCount) return NULL;
  return &pending[index];
}

int GwNodeRegistry::findPendingIndex(const uint8_t mac[6]) const {
  if (!mac) return -1;
  for (uint8_t i = 0; i < pendingCount; i++) {
    if (memcmp(pending[i].mac, mac, 6) == 0) {
      return (int)i;
    }
  }
  return -1;
}

uint16_t GwNodeRegistry::allocateNextPoleId() {
  for (uint16_t candidate = GW_POLE_ID_START; candidate < (GW_POLE_ID_START + GW_MAX_MANAGED_POLES); candidate++) {
    bool inUse = false;
    for (uint8_t i = 0; i < activeCount; i++) {
      if (poles[i].poleId == candidate) {
        inUse = true;
        break;
      }
    }
    if (!inUse) {
      return candidate;
    }
  }
  return 0xFFFF; // Capacity full
}

uint8_t GwNodeRegistry::handleJoinRequest(const PktRegisterRequestPayload &req,
                                          int16_t rssi, int8_t snr,
                                          PktRegisterResponsePayload &outResp) {
  memcpy(outResp.macAddress, req.macAddress, 6);
  outResp.assignedPoleId = 0x0000;
  outResp.assignedParent = MSCB_GATEWAY_NODE_ID;
  outResp.heartbeatIntervalSec = 30;
  outResp.networkKeyEpoch = 1;

  // 1. Check Permanent Revocation Blacklist
  if (isMacRevoked(req.macAddress)) {
    const PermanentRevocationRecord* rev = findRevokedByMac(req.macAddress);
    setReappearedRevokedAlert(req.macAddress, rev ? rev->logicalId : "UNKNOWN", 0);
    Serial.printf("[SECURITY] 🚨 Blocked JOIN_REQ from Revoked Pole MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                  req.macAddress[0], req.macAddress[1], req.macAddress[2],
                  req.macAddress[3], req.macAddress[4], req.macAddress[5]);
    outResp.status = REG_RESP_REJECTED_REVOKED;
    return REG_RESP_REJECTED_REVOKED;
  }

  // 2. Check if already actively registered (e.g. node rebooted)
  RegisteredPole* existing = findPoleByMac(req.macAddress);
  if (existing) {
    existing->sessionToken = req.sessionToken;
    existing->firmwareVersion = req.firmwareVersion;
    existing->lastRssi = rssi;
    existing->lastSnr = snr;
    existing->lastSeenTimestamp = millis() / 1000;

    outResp.status = REG_RESP_APPROVED;
    outResp.assignedPoleId = existing->poleId;
    outResp.assignedParent = existing->primaryParent ? existing->primaryParent : MSCB_GATEWAY_NODE_ID;
    return REG_RESP_APPROVED;
  }

  // 3. Enforce 50-Pole Capacity Limit
  if (activeCount >= GW_MAX_MANAGED_POLES) {
    outResp.status = REG_RESP_REJECTED_CAPACITY;
    return REG_RESP_REJECTED_CAPACITY;
  }

  // 4. Check / Enqueue in Pending Queue
  int pendingIdx = findPendingIndex(req.macAddress);
  if (pendingIdx >= 0) {
    // Update existing pending record
    pending[pendingIdx].requestedParent = req.candidateParent1;
    pending[pendingIdx].firmwareVersion = req.firmwareVersion;
    pending[pendingIdx].sessionToken = req.sessionToken;
    pending[pendingIdx].rssi = rssi;
    pending[pendingIdx].snr = snr;
    pending[pendingIdx].requestTimestamp = millis() / 1000;
  } else if (pendingCount < GW_MAX_PENDING_REGISTRATIONS) {
    // Add new pending record
    PendingRegistration &p = pending[pendingCount++];
    memcpy(p.mac, req.macAddress, 6);
    p.requestedParent = req.candidateParent1;
    p.firmwareVersion = req.firmwareVersion;
    p.sessionToken = req.sessionToken;
    p.rssi = rssi;
    p.snr = snr;
    p.requestTimestamp = millis() / 1000;
    p.notifiedAdmin = false;
    saveRegistry(); // Persist pending queue across reboots
  } else {
    // Pending queue is full: return capacity rejection
    outResp.status = REG_RESP_REJECTED_CAPACITY;
    return REG_RESP_REJECTED_CAPACITY;
  }

  outResp.status = REG_RESP_PENDING_APPROVAL;
  return REG_RESP_PENDING_APPROVAL;
}

bool GwNodeRegistry::approvePending(const uint8_t mac[6], uint16_t &outAssignedId) {
  int pIdx = findPendingIndex(mac);
  if (pIdx < 0) return false;
  if (activeCount >= GW_MAX_MANAGED_POLES) return false;

  uint16_t newId = allocateNextPoleId();
  if (newId == 0xFFFF) return false;

  // Staged backup for rollback
  RegisteredPole polesBackup[GW_MAX_MANAGED_POLES];
  uint8_t activeCountBackup = activeCount;
  PendingRegistration pendingBackup[GW_MAX_PENDING_REGISTRATIONS];
  uint8_t pendingCountBackup = pendingCount;
  memcpy(polesBackup, poles, sizeof(poles));
  memcpy(pendingBackup, pending, sizeof(pending));

  RegisteredPole &newPole = poles[activeCount++];
  memcpy(newPole.mac, mac, 6);
  newPole.poleId = newId;
  snprintf(newPole.stringId, sizeof(newPole.stringId), "POLE_%02d", newId);
  newPole.status = POLE_STATUS_ACTIVE;
  newPole.firmwareVersion = pending[pIdx].firmwareVersion;
  newPole.primaryParent = pending[pIdx].requestedParent ? pending[pIdx].requestedParent : MSCB_GATEWAY_NODE_ID;
  newPole.backupParent = MSCB_GATEWAY_NODE_ID;
  newPole.rank = 1;
  newPole.sessionToken = pending[pIdx].sessionToken;
  newPole.registeredTimestamp = millis() / 1000;
  newPole.lastSeenTimestamp = newPole.registeredTimestamp;
  newPole.lastRssi = pending[pIdx].rssi;
  newPole.lastSnr = pending[pIdx].snr;
  newPole.feederStateMask = 0;
  newPole.auxFeedbackMask = 0;
  newPole.mcbStatusMask = 0;
  newPole.lastBusVoltage = 0.0f;
  newPole.lastCurrent = 0.0f;

  outAssignedId = newId;

  // Remove from pending
  for (uint8_t i = pIdx; i < pendingCount - 1; i++) {
    pending[i] = pending[i + 1];
  }
  pendingCount--;

  if (!saveRegistry()) {
    // Rollback on write failure
    memcpy(poles, polesBackup, sizeof(poles));
    activeCount = activeCountBackup;
    memcpy(pending, pendingBackup, sizeof(pending));
    pendingCount = pendingCountBackup;
    return false;
  }
  return true;
}

bool GwNodeRegistry::rejectPending(const uint8_t mac[6]) {
  int pIdx = findPendingIndex(mac);
  if (pIdx < 0) return false;

  // Remove from pending
  for (uint8_t i = pIdx; i < pendingCount - 1; i++) {
    pending[i] = pending[i + 1];
  }
  pendingCount--;
  saveRegistry();
  return true;
}

PoleDeleteResult GwNodeRegistry::deleteAndRevokePoleEx(uint16_t poleId, const char* adminId, const char* reason) {
  int targetIdx = -1;
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].poleId == poleId && poles[i].status == POLE_STATUS_ACTIVE) {
      targetIdx = i;
      break;
    }
  }
  if (targetIdx < 0) return DELETE_ERR_POLE_NOT_FOUND;

  // Verify whether MAC is already revoked
  if (isMacRevoked(poles[targetIdx].mac)) {
    return DELETE_ERR_ALREADY_REVOKED;
  }

  // Verify revocation capacity
  if (revokedCount >= MSCB_MAX_REVOCATION_RECORDS) {
    Serial.println(F("[REGISTRY] ❌ Revocation list full! Rejecting pole deletion safely."));
    return DELETE_ERR_REVOCATION_FULL;
  }

  // Backup state for rollback
  RegisteredPole polesBackup[GW_MAX_MANAGED_POLES];
  uint8_t activeCountBackup = activeCount;
  PermanentRevocationRecord revokedBackup[MSCB_MAX_REVOCATION_RECORDS];
  uint8_t revokedCountBackup = revokedCount;
  PendingRegistration pendingBackup[GW_MAX_PENDING_REGISTRATIONS];
  uint8_t pendingCountBackup = pendingCount;
  memcpy(polesBackup, poles, sizeof(poles));
  memcpy(revokedBackup, revoked, sizeof(revoked));
  memcpy(pendingBackup, pending, sizeof(pending));

  // 1. Stage permanent revocation record with integrity CRC
  PermanentRevocationRecord &r = revoked[revokedCount++];
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
           poles[targetIdx].mac[0], poles[targetIdx].mac[1], poles[targetIdx].mac[2],
           poles[targetIdx].mac[3], poles[targetIdx].mac[4], poles[targetIdx].mac[5]);
  strncpy(r.hwId, macHex, sizeof(r.hwId) - 1);
  r.hwId[sizeof(r.hwId) - 1] = '\0';
  strncpy(r.logicalId, poles[targetIdx].stringId, sizeof(r.logicalId) - 1);
  r.logicalId[sizeof(r.logicalId) - 1] = '\0';
  snprintf(r.txId, sizeof(r.txId), "TX_REV_%04X_%lu", poleId, (unsigned long)(millis() / 1000));
  strncpy(r.reason, reason ? reason : "ADMIN_REVOKED", sizeof(r.reason) - 1);
  r.reason[sizeof(r.reason) - 1] = '\0';
  strncpy(r.adminId, adminId ? adminId : "TELEGRAM_ADMIN", sizeof(r.adminId) - 1);
  r.adminId[sizeof(r.adminId) - 1] = '\0';
  r.timestamp = millis() / 1000;
  r.crc32 = calculateRevocationCrc(r);

  // 2. Stage removal from active poles array
  for (uint8_t i = targetIdx; i < activeCount - 1; i++) {
    poles[i] = poles[i + 1];
  }
  activeCount--;

  // 5. Child Route Migration: If any pole had deleted pole as primary/backup parent, re-route to Gateway
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].primaryParent == poleId) {
      poles[i].primaryParent = MSCB_GATEWAY_NODE_ID;
      poles[i].rank = 1;
    }
    if (poles[i].backupParent == poleId) {
      poles[i].backupParent = MSCB_GATEWAY_NODE_ID;
    }
  }

  // 4. Atomic Two-Phase Commit Persistence
  if (!saveRegistry()) {
    Serial.println(F("[REGISTRY] ❌ saveRegistry() failed! Rolling back delete transaction."));
    // ROLLBACK!
    memcpy(poles, polesBackup, sizeof(poles));
    activeCount = activeCountBackup;
    memcpy(revoked, revokedBackup, sizeof(revoked));
    revokedCount = revokedCountBackup;
    memcpy(pending, pendingBackup, sizeof(pending));
    pendingCount = pendingCountBackup;
    return DELETE_ERR_REGISTRY_SAVE_FAILED;
  }

  Serial.printf("[REGISTRY] ✅ Pole 0x%04X (MAC %s) revoked and deleted successfully.\n", poleId, macHex);
  return DELETE_SUCCESS;
}

bool GwNodeRegistry::deleteAndRevokePole(uint16_t poleId, const char* adminId, const char* reason) {
  return (deleteAndRevokePoleEx(poleId, adminId, reason) == DELETE_SUCCESS);
}

bool GwNodeRegistry::unrevokePole(const uint8_t mac[6], const char* adminId, const char* reason) {
  if (!mac) return false;
  (void)adminId;
  (void)reason;
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  int targetIdx = -1;
  for (uint8_t i = 0; i < revokedCount; i++) {
    if (strncmp(revoked[i].hwId, macHex, 12) == 0) {
      targetIdx = i;
      break;
    }
  }
  if (targetIdx < 0) return false;

  // Backup for rollback
  PermanentRevocationRecord revokedBackup[MSCB_MAX_REVOCATION_RECORDS];
  uint8_t revokedCountBackup = revokedCount;
  memcpy(revokedBackup, revoked, sizeof(revoked));

  // Remove entry from revoked list
  for (uint8_t i = targetIdx; i < revokedCount - 1; i++) {
    revoked[i] = revoked[i + 1];
  }
  revokedCount--;

  if (!saveRegistry()) {
    // Rollback
    memcpy(revoked, revokedBackup, sizeof(revoked));
    revokedCount = revokedCountBackup;
    return false;
  }

  Serial.printf("[REGISTRY] ✅ MAC %s successfully un-revoked / re-authorized by %s.\n",
                macHex, adminId ? adminId : "ADMIN");
  return true;
}

void GwNodeRegistry::updateNodeTelemetry(uint16_t poleId, uint16_t priParent, uint16_t bkpParent,
                                         uint8_t rank, uint8_t feederMask, uint8_t auxMask, uint8_t mcbMask,
                                         float busVolt, float current, int16_t rssi, int8_t snr) {
  RegisteredPole* pole = findPoleById(poleId);
  if (!pole) return;

  pole->lastSeenTimestamp = millis() / 1000;
  if (priParent != 0) pole->primaryParent = priParent;
  if (bkpParent != 0) pole->backupParent = bkpParent;
  if (rank != 0) pole->rank = rank;
  pole->feederStateMask = feederMask;
  pole->auxFeedbackMask = auxMask;
  pole->mcbStatusMask = mcbMask;
  pole->lastBusVoltage = busVolt;
  pole->lastCurrent = current;
  pole->lastRssi = rssi;
  pole->lastSnr = snr;
}

bool GwNodeRegistry::saveRegistry() {
#if defined(ARDUINO)
  if (!prefs.begin("mscb_reg", false)) {
    Serial.println(F("[REGISTRY] ❌ Preferences.begin('mscb_reg', false) FAILED!"));
    return false;
  }

  uint8_t schemaVer = MSCB_CURRENT_REG_SCHEMA; // V2
  if (prefs.putUChar("schema_ver", schemaVer) != sizeof(schemaVer)) { prefs.end(); return false; }
  if (prefs.putUChar("active_cnt", activeCount) != sizeof(activeCount)) { prefs.end(); return false; }
  if (prefs.putUChar("revoked_cnt", revokedCount) != sizeof(revokedCount)) { prefs.end(); return false; }
  if (prefs.putUChar("pending_cnt", pendingCount) != sizeof(pendingCount)) { prefs.end(); return false; }

  if (activeCount > 0) {
    size_t expected = activeCount * sizeof(RegisteredPole);
    if (prefs.putBytes("poles", poles, expected) != expected) {
      prefs.end();
      return false;
    }
  }
  if (revokedCount > 0) {
    size_t expected = revokedCount * sizeof(PermanentRevocationRecord);
    if (prefs.putBytes("revoked", revoked, expected) != expected) {
      prefs.end();
      return false;
    }
  }
  if (pendingCount > 0) {
    size_t expected = pendingCount * sizeof(PendingRegistration);
    if (prefs.putBytes("pending", pending, expected) != expected) {
      prefs.end();
      return false;
    }
  }

  uint32_t crc = calculateRegistryCrc(activeCount, poles, revokedCount, revoked, pendingCount, pending);
  if (prefs.putUInt("reg_crc", crc) != sizeof(crc)) {
    prefs.end();
    return false;
  }

  prefs.end();
#endif
  return true;
}

bool GwNodeRegistry::loadRegistry() {
#if defined(ARDUINO)
  if (!prefs.begin("mscb_reg", true)) {
    return false;
  }

  uint8_t schemaVer = prefs.getUChar("schema_ver", MSCB_REG_SCHEMA_V1);
  activeCount = prefs.getUChar("active_cnt", 0);
  revokedCount = prefs.getUChar("revoked_cnt", 0);
  pendingCount = (schemaVer >= MSCB_REG_SCHEMA_V2) ? prefs.getUChar("pending_cnt", 0) : 0;

  if (activeCount > GW_MAX_MANAGED_POLES) activeCount = 0;
  if (revokedCount > MSCB_MAX_REVOCATION_RECORDS) revokedCount = 0;
  if (pendingCount > GW_MAX_PENDING_REGISTRATIONS) pendingCount = 0;

  if (activeCount > 0) {
    size_t expected = activeCount * sizeof(RegisteredPole);
    size_t read = prefs.getBytes("poles", poles, expected);
    if (read != expected) {
      activeCount = 0;
    }
  }
  if (revokedCount > 0) {
    size_t expected = revokedCount * sizeof(PermanentRevocationRecord);
    size_t read = prefs.getBytes("revoked", revoked, expected);
    if (read != expected) {
      revokedCount = 0;
    }
  }
  if (pendingCount > 0) {
    size_t expected = pendingCount * sizeof(PendingRegistration);
    size_t read = prefs.getBytes("pending", pending, expected);
    if (read != expected) {
      pendingCount = 0;
    }
  }

  if (schemaVer >= MSCB_REG_SCHEMA_V2) {
    uint32_t storedCrc = prefs.getUInt("reg_crc", 0);
    uint32_t computedCrc = calculateRegistryCrc(activeCount, poles, revokedCount, revoked, pendingCount, pending);
    if (storedCrc != computedCrc) {
      Serial.printf("[REGISTRY] ⚠️ Registry CRC mismatch! Stored: 0x%08X, Computed: 0x%08X\n", storedCrc, computedCrc);
      activeCount = 0;
      revokedCount = 0;
      pendingCount = 0;
      prefs.end();
      return false;
    }
  }

  prefs.end();
#endif
  return true;
}
