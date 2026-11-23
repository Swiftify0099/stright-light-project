/**
 * @file GwNodeRegistry.cpp
 * @brief MSCB Smart Pole Gateway - Persistent Pole Registry & Revocation Blacklist Implementation
 * @version 1.0.0
 */

#include "GwNodeRegistry.h"
#include <string.h>
#include <stdio.h>

GwNodeRegistry gwRegistry;

GwNodeRegistry::GwNodeRegistry() :
  activeCount(0), pendingCount(0), revokedCount(0)
{
  memset(poles, 0, sizeof(poles));
  memset(pending, 0, sizeof(pending));
  memset(revoked, 0, sizeof(revoked));
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
    saveRegistry();
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

  saveRegistry();
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

bool GwNodeRegistry::deleteAndRevokePole(uint16_t poleId, const char* adminId, const char* reason) {
  int targetIdx = -1;
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].poleId == poleId) {
      targetIdx = i;
      break;
    }
  }
  if (targetIdx < 0) return false;

  // 1. Strict Revocation Capacity Check:
  // Deletion MUST NEVER proceed if the permanent revocation list is full!
  if (revokedCount >= MSCB_MAX_REVOCATION_RECORDS) {
    #if defined(ARDUINO)
    Serial.println(F("[REGISTRY] ❌ Deletion aborted: Permanent revocation list is full!"));
    #endif
    return false;
  }

  // 2. Add hardware identity to permanent revocation list
  PermanentRevocationRecord &r = revoked[revokedCount];
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
           poles[targetIdx].mac[0], poles[targetIdx].mac[1], poles[targetIdx].mac[2],
           poles[targetIdx].mac[3], poles[targetIdx].mac[4], poles[targetIdx].mac[5]);
  strncpy(r.hwId, macHex, sizeof(r.hwId) - 1);
  r.hwId[sizeof(r.hwId) - 1] = '\0';
  strncpy(r.logicalId, poles[targetIdx].stringId, sizeof(r.logicalId) - 1);
  r.logicalId[sizeof(r.logicalId) - 1] = '\0';
  strncpy(r.reason, reason ? reason : "ADMIN_REVOKED", sizeof(r.reason) - 1);
  r.reason[sizeof(r.reason) - 1] = '\0';
  strncpy(r.adminId, adminId ? adminId : "TELEGRAM_ADMIN", sizeof(r.adminId) - 1);
  r.adminId[sizeof(r.adminId) - 1] = '\0';
  r.timestamp = millis() / 1000;
  revokedCount++;

  // 3. Persist revocation record in NVS BEFORE removing active registration
  if (!saveRegistry()) {
    revokedCount--; // Rollback on failure
    #if defined(ARDUINO)
    Serial.println(F("[REGISTRY] ❌ Storage persistence failed; aborting pole deletion."));
    #endif
    return false;
  }

  // 4. Remove from active poles array
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

  saveRegistry();
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
    return false;
  }
  prefs.putUChar("active_cnt", activeCount);
  prefs.putUChar("revoked_cnt", revokedCount);
  prefs.putUChar("pending_cnt", pendingCount);

  if (activeCount > 0) {
    prefs.putBytes("poles", poles, activeCount * sizeof(RegisteredPole));
  } else {
    prefs.remove("poles");
  }
  if (revokedCount > 0) {
    prefs.putBytes("revoked", revoked, revokedCount * sizeof(PermanentRevocationRecord));
  } else {
    prefs.remove("revoked");
  }
  if (pendingCount > 0) {
    prefs.putBytes("pending", pending, pendingCount * sizeof(PendingRegistration));
  } else {
    prefs.remove("pending");
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
  activeCount = prefs.getUChar("active_cnt", 0);
  revokedCount = prefs.getUChar("revoked_cnt", 0);
  pendingCount = prefs.getUChar("pending_cnt", 0);

  if (activeCount > GW_MAX_MANAGED_POLES) activeCount = 0;
  if (revokedCount > MSCB_MAX_REVOCATION_RECORDS) revokedCount = 0;
  if (pendingCount > GW_MAX_PENDING_REGISTRATIONS) pendingCount = 0;

  if (activeCount > 0) {
    prefs.getBytes("poles", poles, activeCount * sizeof(RegisteredPole));
  }
  if (revokedCount > 0) {
    prefs.getBytes("revoked", revoked, revokedCount * sizeof(PermanentRevocationRecord));
  }
  if (pendingCount > 0) {
    prefs.getBytes("pending", pending, pendingCount * sizeof(PendingRegistration));
  }
  prefs.end();
#endif
  return true;
}
