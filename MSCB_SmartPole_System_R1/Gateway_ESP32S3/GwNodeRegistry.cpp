/**
 * @file GwNodeRegistry.cpp
 * @brief MSCB Smart Pole Gateway - Persistent Pole Registry & Revocation
 * Blacklist Implementation
 * @version 1.0.0
 */

#include "GwNodeRegistry.h"
#include <stdio.h>
#include <string.h>

GwNodeRegistry gwRegistry;

static inline uint32_t
calculateRegistryCrc(uint8_t activeCnt, const RegisteredPole *pPoles,
                     uint8_t revokedCnt,
                     const PermanentRevocationRecord *pRevoked,
                     uint8_t pendingCnt, const PendingRegistration *pPending) {
  uint32_t crc = 0xFFFFFFFFUL;
  auto updateCrc = [&crc](const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
      crc ^= data[i];
      for (uint8_t j = 0; j < 8; j++) {
        if (crc & 1)
          crc = (crc >> 1) ^ 0xEDB88320UL;
        else
          crc >>= 1;
      }
    }
  };

  updateCrc(&activeCnt, sizeof(activeCnt));
  if (activeCnt > 0 && pPoles) {
    updateCrc((const uint8_t *)pPoles, activeCnt * sizeof(RegisteredPole));
  }
  updateCrc(&revokedCnt, sizeof(revokedCnt));
  if (revokedCnt > 0 && pRevoked) {
    updateCrc((const uint8_t *)pRevoked,
              revokedCnt * sizeof(PermanentRevocationRecord));
  }
  updateCrc(&pendingCnt, sizeof(pendingCnt));
  if (pendingCnt > 0 && pPending) {
    updateCrc((const uint8_t *)pPending,
              pendingCnt * sizeof(PendingRegistration));
  }
  return ~crc;
}

GwNodeRegistry::GwNodeRegistry()
    : activeCount(0), pendingCount(0), revokedCount(0),
      currentGeneration(0), activeSlot(0), simulateInterruptedWrite(false),
      revokedAlertHead(0), revokedAlertTail(0), revokedAlertCount(0),
      revokedIncidentCount(0) {
  memset(poles, 0, sizeof(poles));
  memset(pending, 0, sizeof(pending));
  memset(revoked, 0, sizeof(revoked));
  memset(revokedAlertQueue, 0, sizeof(revokedAlertQueue));
  memset(revokedIncidents, 0, sizeof(revokedIncidents));
}

bool GwNodeRegistry::begin() {
  bool regOk = loadRegistry();
  loadRevokedIncidents();
  return regOk;
}

bool GwNodeRegistry::isMacRevoked(const uint8_t mac[6]) const {
  if (!mac)
    return false;
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);

  for (uint8_t i = 0; i < revokedCount; i++) {
    if (strncmp(revoked[i].hwId, macHex, 12) == 0) {
      if (revoked[i].deliveryStatus != REVOKE_STATUS_UNREVOKED) {
        return true;
      }
    }
  }
  return false;
}

const PermanentRevocationRecord *
GwNodeRegistry::findRevokedByMac(const uint8_t mac[6]) const {
  if (!mac)
    return NULL;
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);

  for (uint8_t i = 0; i < revokedCount; i++) {
    if (strncmp(revoked[i].hwId, macHex, 12) == 0 &&
        revoked[i].deliveryStatus != REVOKE_STATUS_UNREVOKED) {
      return &revoked[i];
    }
  }
  return NULL;
}

const PermanentRevocationRecord *
GwNodeRegistry::findRevokedByPoleId(uint16_t poleId) const {
  char pidStr[16];
  snprintf(pidStr, sizeof(pidStr), "POLE_%02d", poleId);
  char pidStrAlt[16];
  snprintf(pidStrAlt, sizeof(pidStrAlt), "POLE-%04d", poleId);

  for (uint8_t i = 0; i < revokedCount; i++) {
    if (revoked[i].deliveryStatus != REVOKE_STATUS_UNREVOKED) {
      if (strcmp(revoked[i].logicalId, pidStr) == 0 ||
          strcmp(revoked[i].logicalId, pidStrAlt) == 0) {
        return &revoked[i];
      }
    }
  }
  return NULL;
}

const PermanentRevocationRecord *
GwNodeRegistry::getRevokedByIndex(uint8_t index) const {
  if (index >= revokedCount)
    return NULL;
  return &revoked[index];
}

#if !defined(ARDUINO)
struct SimRevokedIncidentStore {
  bool initialized;
  uint32_t magic;
  uint8_t count;
  RevokedIncidentTracker items[GW_MAX_REVOKED_INCIDENTS];
  uint32_t crc;
};
static SimRevokedIncidentStore s_simRevokedIncidents = {false, 0, 0, {}, 0};
#endif

bool GwNodeRegistry::saveRevokedIncidents() {
  uint32_t crc = mscbStorageCrc32((const uint8_t*)revokedIncidents,
                                   revokedIncidentCount * sizeof(RevokedIncidentTracker));
#if !defined(ARDUINO)
  s_simRevokedIncidents.initialized = true;
  s_simRevokedIncidents.magic = MSCB_SLOT_MAGIC;
  s_simRevokedIncidents.count = revokedIncidentCount;
  memcpy(s_simRevokedIncidents.items, revokedIncidents, sizeof(revokedIncidents));
  s_simRevokedIncidents.crc = crc;
  return true;
#else
  Preferences pInc;
  if (!pInc.begin("mscb_rev_inc", false)) return false;
  pInc.putUInt("magic", MSCB_SLOT_MAGIC);
  pInc.putUChar("cnt", revokedIncidentCount);
  if (revokedIncidentCount > 0) {
    pInc.putBytes("items", revokedIncidents, revokedIncidentCount * sizeof(RevokedIncidentTracker));
  }
  pInc.putUInt("crc", crc);
  pInc.end();
  return true;
#endif
}

bool GwNodeRegistry::loadRevokedIncidents() {
  memset(revokedIncidents, 0, sizeof(revokedIncidents));
  revokedIncidentCount = 0;
#if !defined(ARDUINO)
  if (!s_simRevokedIncidents.initialized || s_simRevokedIncidents.magic != MSCB_SLOT_MAGIC ||
      s_simRevokedIncidents.count > GW_MAX_REVOKED_INCIDENTS) {
    return false;
  }
  uint32_t expected = mscbStorageCrc32((const uint8_t*)s_simRevokedIncidents.items,
                                        s_simRevokedIncidents.count * sizeof(RevokedIncidentTracker));
  if (expected != s_simRevokedIncidents.crc) return false;
  revokedIncidentCount = s_simRevokedIncidents.count;
  memcpy(revokedIncidents, s_simRevokedIncidents.items, sizeof(revokedIncidents));
#else
  Preferences pInc;
  if (!pInc.begin("mscb_rev_inc", true)) return false;
  uint32_t magic = pInc.getUInt("magic", 0);
  uint8_t cnt = pInc.getUChar("cnt", 0);
  uint32_t storedCrc = pInc.getUInt("crc", 0);
  RevokedIncidentTracker temp[GW_MAX_REVOKED_INCIDENTS];
  memset(temp, 0, sizeof(temp));
  bool ok = (magic == MSCB_SLOT_MAGIC && cnt <= GW_MAX_REVOKED_INCIDENTS);
  if (ok && cnt > 0) {
    size_t sz = cnt * sizeof(RevokedIncidentTracker);
    ok = (pInc.getBytes("items", temp, sz) == sz);
  }
  pInc.end();
  if (!ok) return false;
  if (mscbStorageCrc32((const uint8_t*)temp, cnt * sizeof(RevokedIncidentTracker)) != storedCrc) {
    return false;
  }
  revokedIncidentCount = cnt;
  memcpy(revokedIncidents, temp, sizeof(temp));
#endif
  uint32_t bootNow = millis() / 1000UL;
  for (uint8_t i = 0; i < revokedIncidentCount; i++) {
    if (revokedIncidents[i].activeIncident && revokedIncidents[i].lastAlertEpoch > bootNow) {
      // Gateway rebooted while incident active: anchor post-reboot cooldown cleanly
      revokedIncidents[i].lastAlertEpoch = bootNow;
    }
  }
  return true;
}

void GwNodeRegistry::clearReappearedRevokedAlert() {
  if (revokedAlertCount > 0) {
    memset(&revokedAlertQueue[revokedAlertHead], 0, sizeof(GwRevokedAlertItem));
    revokedAlertHead = (revokedAlertHead + 1) % GW_REVOKED_ALERT_QUEUE_CAP;
    revokedAlertCount--;
  }
}

bool GwNodeRegistry::setReappearedRevokedAlert(const uint8_t mac[6],
                                               const char *prevLogicalId,
                                               uint16_t prevPoleId) {
  if (!mac) return false;
  uint32_t nowEpoch = millis() / 1000UL;

  // Search for an active incident for this MAC
  int foundIdx = -1;
  for (uint8_t i = 0; i < revokedIncidentCount; i++) {
    if (memcmp(revokedIncidents[i].mac, mac, 6) == 0) {
      foundIdx = (int)i;
      break;
    }
  }

  bool emitAlert = false;
  uint32_t totalOcc = 1;

  if (foundIdx >= 0) {
    // Incident already active: update diagnostic counters and timestamps
    revokedIncidents[foundIdx].lastSeenEpoch = nowEpoch;
    revokedIncidents[foundIdx].occurrences++;
    totalOcc = revokedIncidents[foundIdx].occurrences;
    if (prevPoleId != 0) revokedIncidents[foundIdx].poleId = prevPoleId;

    // Check rearm window: allow a new alert only after 3600 seconds of ongoing incident
    if (nowEpoch >= revokedIncidents[foundIdx].lastAlertEpoch + 3600UL) {
      revokedIncidents[foundIdx].lastAlertEpoch = nowEpoch;
      emitAlert = true;
    }
  } else {
    // New incident!
    if (revokedIncidentCount < GW_MAX_REVOKED_INCIDENTS) {
      foundIdx = revokedIncidentCount++;
    } else {
      // Evict oldest seen incident
      foundIdx = 0;
      for (uint8_t i = 1; i < revokedIncidentCount; i++) {
        if (revokedIncidents[i].lastSeenEpoch < revokedIncidents[foundIdx].lastSeenEpoch) {
          foundIdx = (int)i;
        }
      }
    }

    memcpy(revokedIncidents[foundIdx].mac, mac, 6);
    revokedIncidents[foundIdx].poleId = prevPoleId;
    revokedIncidents[foundIdx].firstSeenEpoch = nowEpoch;
    revokedIncidents[foundIdx].lastSeenEpoch = nowEpoch;
    revokedIncidents[foundIdx].lastAlertEpoch = nowEpoch;
    revokedIncidents[foundIdx].occurrences = 1;
    revokedIncidents[foundIdx].activeIncident = true;
    emitAlert = true;
  }

  saveRevokedIncidents();

  if (!emitAlert) {
    return false;
  }

  // Enqueue without overwriting unread alerts for OTHER MACs
  int queueSlot = -1;
  for (uint8_t k = 0; k < revokedAlertCount; k++) {
    uint8_t idx = (revokedAlertHead + k) % GW_REVOKED_ALERT_QUEUE_CAP;
    if (memcmp(revokedAlertQueue[idx].mac, mac, 6) == 0) {
      queueSlot = (int)idx;
      break;
    }
  }
  if (queueSlot < 0) {
    if (revokedAlertCount < GW_REVOKED_ALERT_QUEUE_CAP) {
      queueSlot = (int)revokedAlertTail;
      revokedAlertTail = (revokedAlertTail + 1) % GW_REVOKED_ALERT_QUEUE_CAP;
      revokedAlertCount++;
    } else {
      // Preserve unread queued alerts for distinct MACs
      return false;
    }
  }

  GwRevokedAlertItem &item = revokedAlertQueue[queueSlot];
  memcpy(item.mac, mac, 6);
  item.poleId = prevPoleId;
  item.occurrences = totalOcc;
  snprintf(item.text, sizeof(item.text),
           "🚨 *REVOKED DEVICE REAPPEARED*\n"
           "• MAC: `%02X:%02X:%02X:%02X:%02X:%02X`\n"
           "• Previous ID: %s (0x%04X)\n"
           "• Status: PERMANENTLY REVOKED (Relays locked OFF)\n"
           "• Total Occurrences: %u\n"
           "• Action: Device rejected from network.\n"
           "• To re-authorize: /restore_%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
           prevLogicalId ? prevLogicalId : "UNKNOWN", prevPoleId,
           (unsigned)totalOcc,
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return true;
}

void GwNodeRegistry::resetRevokedIncidentDeduplication(const uint8_t mac[6]) {
  if (!mac) {
    memset(revokedIncidents, 0, sizeof(revokedIncidents));
    revokedIncidentCount = 0;
    memset(revokedAlertQueue, 0, sizeof(revokedAlertQueue));
    revokedAlertHead = 0;
    revokedAlertTail = 0;
    revokedAlertCount = 0;
    saveRevokedIncidents();
    return;
  }
  for (uint8_t i = 0; i < revokedIncidentCount; i++) {
    if (memcmp(revokedIncidents[i].mac, mac, 6) == 0) {
      for (uint8_t j = i; j < revokedIncidentCount - 1; j++) {
        revokedIncidents[j] = revokedIncidents[j + 1];
      }
      revokedIncidentCount--;
      break;
    }
  }
  saveRevokedIncidents();
}

uint32_t GwNodeRegistry::getRevokedIncidentOccurrences(const uint8_t mac[6]) const {
  if (!mac) return 0;
  for (uint8_t i = 0; i < revokedIncidentCount; i++) {
    if (memcmp(revokedIncidents[i].mac, mac, 6) == 0) {
      return revokedIncidents[i].occurrences;
    }
  }
  return 0;
}

RegisteredPole *GwNodeRegistry::findPoleById(uint16_t poleId) {
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].poleId == poleId &&
        (poles[i].status == POLE_STATUS_ACTIVE || poles[i].status == POLE_STATUS_PENDING_ACK)) {
      return &poles[i];
    }
  }
  return NULL;
}

RegisteredPole *GwNodeRegistry::findPoleByMac(const uint8_t mac[6]) {
  if (!mac)
    return NULL;
  for (uint8_t i = 0; i < activeCount; i++) {
    if (memcmp(poles[i].mac, mac, 6) == 0 &&
        (poles[i].status == POLE_STATUS_ACTIVE || poles[i].status == POLE_STATUS_PENDING_ACK)) {
      return &poles[i];
    }
  }
  return NULL;
}

const RegisteredPole *GwNodeRegistry::getPoleByIndex(uint8_t index) const {
  if (index >= activeCount)
    return NULL;
  return &poles[index];
}

const PendingRegistration *
GwNodeRegistry::getPendingByIndex(uint8_t index) const {
  if (index >= pendingCount)
    return NULL;
  return &pending[index];
}

int GwNodeRegistry::findPendingIndex(const uint8_t mac[6]) const {
  if (!mac)
    return -1;
  for (uint8_t i = 0; i < pendingCount; i++) {
    if (memcmp(pending[i].mac, mac, 6) == 0) {
      return (int)i;
    }
  }
  return -1;
}

uint16_t GwNodeRegistry::allocateNextPoleId() {
  for (uint16_t candidate = GW_POLE_ID_START;
       candidate < (GW_POLE_ID_START + GW_MAX_MANAGED_POLES); candidate++) {
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
  memset(&outResp, 0, sizeof(outResp));
  memcpy(outResp.macAddress, req.macAddress, 6);
  outResp.assignedPoleId = 0x0000;
  outResp.assignedParent = MSCB_GATEWAY_NODE_ID;
  outResp.heartbeatIntervalSec = 30;
  outResp.networkKeyEpoch = cmdSecurity.getActiveKeyEpoch() ? cmdSecurity.getActiveKeyEpoch() : 1;
  outResp.sessionToken = req.sessionToken;
  outResp.nonce = req.nonce;

  // 0. Reject invalid MAC addresses (all-zero / all-FF)
  if (!mscbIsValidMac(req.macAddress, false)) {
    outResp.status = REG_RESP_REJECTED_UNAUTHORIZED;
    return REG_RESP_REJECTED_UNAUTHORIZED;
  }

  // 1. Check Permanent Revocation Blacklist (Immediate fail-closed before cryptographic auth)
  if (isMacRevoked(req.macAddress)) {
    const PermanentRevocationRecord *rev = findRevokedByMac(req.macAddress);
    setReappearedRevokedAlert(req.macAddress, rev ? rev->logicalId : "UNKNOWN",
                              0);
    Serial.printf("[SECURITY] 🚨 Blocked JOIN_REQ from Revoked Pole MAC "
                  "%02X:%02X:%02X:%02X:%02X:%02X\n",
                  req.macAddress[0], req.macAddress[1], req.macAddress[2],
                  req.macAddress[3], req.macAddress[4], req.macAddress[5]);
    outResp.status = REG_RESP_REJECTED_REVOKED;
    if (cmdSecurity.isKeyProvisioned()) {
      cmdSecurity.computeRegisterResponseAuth(outResp, outResp.authTag);
    }
    return REG_RESP_REJECTED_REVOKED;
  }

  // 0b. Enforce Fail-Closed Cryptographic Request Authentication when key provisioned
  if (cmdSecurity.isKeyProvisioned()) {
    if (req.sessionToken == 0 || req.nonce == 0 || !cmdSecurity.verifyRegisterRequestAuth(req)) {
      Serial.println(F("[SECURITY] ❌ Rejected unauthenticated JOIN_REQ (invalid session/nonce/authTag)"));
      outResp.status = REG_RESP_REJECTED_UNAUTHORIZED;
      if (outResp.sessionToken != 0 && outResp.nonce != 0) {
        cmdSecurity.computeRegisterResponseAuth(outResp, outResp.authTag);
      }
      return REG_RESP_REJECTED_UNAUTHORIZED;
    }
  }

  // 2. Check if already actively registered or waiting handshake (e.g. node rebooted or retrying response)
  RegisteredPole *existing = findPoleByMac(req.macAddress);
  if (existing) {
    existing->sessionToken = req.sessionToken;
    existing->lastReqNonce = req.nonce;
    existing->firmwareVersion = req.firmwareVersion;
    existing->lastRssi = rssi;
    existing->lastSnr = snr;
    existing->lastSeenTimestamp = millis() / 1000;
    saveRegistry();

    outResp.status = REG_RESP_APPROVED;
    outResp.assignedPoleId = existing->poleId;
    outResp.assignedParent = existing->primaryParent ? existing->primaryParent
                                                     : MSCB_GATEWAY_NODE_ID;
    if (cmdSecurity.isKeyProvisioned()) {
      cmdSecurity.computeRegisterResponseAuth(outResp, outResp.authTag);
    }
    return REG_RESP_APPROVED;
  }

  // 3. Enforce 50-Pole Capacity Limit
  if (activeCount >= GW_MAX_MANAGED_POLES) {
    outResp.status = REG_RESP_REJECTED_CAPACITY;
    if (cmdSecurity.isKeyProvisioned()) {
      cmdSecurity.computeRegisterResponseAuth(outResp, outResp.authTag);
    }
    return REG_RESP_REJECTED_CAPACITY;
  }

  // 4. Check / Enqueue in Pending Queue (never duplicate same MAC, never re-trigger notifiedAdmin!)
  int pendingIdx = findPendingIndex(req.macAddress);
  if (pendingIdx >= 0) {
    // Update existing pending record
    pending[pendingIdx].requestedParent = req.candidateParent1;
    pending[pendingIdx].firmwareVersion = req.firmwareVersion;
    pending[pendingIdx].sessionToken = req.sessionToken;
    pending[pendingIdx].lastReqNonce = req.nonce;
    pending[pendingIdx].rssi = rssi;
    pending[pendingIdx].snr = snr;
    pending[pendingIdx].requestTimestamp = millis() / 1000;
    saveRegistry();
  } else if (pendingCount < GW_MAX_PENDING_REGISTRATIONS) {
    // Add new pending record
    PendingRegistration &p = pending[pendingCount++];
    memset(&p, 0, sizeof(PendingRegistration));
    memcpy(p.mac, req.macAddress, 6);
    p.requestedParent = req.candidateParent1;
    p.firmwareVersion = req.firmwareVersion;
    p.sessionToken = req.sessionToken;
    p.lastReqNonce = req.nonce;
    p.firstHopAddress = MSCB_GATEWAY_NODE_ID;
    p.rssi = rssi;
    p.snr = snr;
    p.requestTimestamp = millis() / 1000;
    p.notifiedAdmin = false;
    saveRegistry(); // Persist pending queue across reboots
  } else {
    // Pending queue is full: return capacity rejection
    outResp.status = REG_RESP_REJECTED_CAPACITY;
    if (cmdSecurity.isKeyProvisioned()) {
      cmdSecurity.computeRegisterResponseAuth(outResp, outResp.authTag);
    }
    return REG_RESP_REJECTED_CAPACITY;
  }

  outResp.status = REG_RESP_PENDING_APPROVAL;
  if (cmdSecurity.isKeyProvisioned()) {
    cmdSecurity.computeRegisterResponseAuth(outResp, outResp.authTag);
  }
  return REG_RESP_PENDING_APPROVAL;
}

bool GwNodeRegistry::hasPendingRegistrationAlert() const {
  for (uint8_t i = 0; i < pendingCount; i++) {
    if (!pending[i].notifiedAdmin) {
      return true;
    }
  }
  return false;
}

const char* GwNodeRegistry::getNextPendingRegistrationAlertText(int &outPendingIndex) {
  outPendingIndex = -1;
  for (uint8_t i = 0; i < pendingCount; i++) {
    if (!pending[i].notifiedAdmin) {
      outPendingIndex = (int)i;
      static char buf[384];
      snprintf(buf, sizeof(buf),
               "🆕 *NEW POLE REGISTRATION REQUEST*\n"
               "• MAC: `%02X:%02X:%02X:%02X:%02X:%02X`\n"
               "• Firmware: v%u.%u\n"
               "• Signal: RSSI %d dBm | SNR %d dB\n"
               "• Requested Parent: 0x%04X\n"
               "• Status: PENDING APPROVAL\n\n"
               "ℹ️ _Open Registrations menu or use `/pending` to configure and approve._",
               pending[i].mac[0], pending[i].mac[1], pending[i].mac[2],
               pending[i].mac[3], pending[i].mac[4], pending[i].mac[5],
               pending[i].firmwareVersion / 10, pending[i].firmwareVersion % 10,
               pending[i].rssi, pending[i].snr,
               pending[i].requestedParent);
      return buf;
    }
  }
  return "";
}

void GwNodeRegistry::markPendingRegistrationNotified(int pendingIndex) {
  if (pendingIndex >= 0 && pendingIndex < pendingCount) {
    pending[pendingIndex].notifiedAdmin = true;
    saveRegistry();
  }
}

void GwNodeRegistry::setPendingSourceRoute(const uint8_t mac[6], uint32_t reqNonce, uint16_t firstHop, uint16_t srcAddr) {
  if (!mac) return;
  int pIdx = findPendingIndex(mac);
  if (pIdx >= 0) {
    pending[pIdx].lastReqNonce = reqNonce;
    pending[pIdx].firstHopAddress = firstHop;
    pending[pIdx].lastSrcAddress = srcAddr;
    saveRegistry();
  }
  RegisteredPole* rp = findPoleByMac(mac);
  if (rp) {
    rp->lastReqNonce = reqNonce;
    rp->firstHopAddress = firstHop;
  }
}

void GwNodeRegistry::recordRegResponseAttempt(const uint8_t mac[6], bool txSuccess) {
  RegisteredPole* rp = findPoleByMac(mac);
  if (!rp) return;
  rp->regResponseAttempts++;
  rp->lastRegResponseMs = millis();
  if (rp->status == POLE_STATUS_PENDING_ACK) {
    rp->regDeliveryState = txSuccess ? 1 : 2;
  }
}

bool GwNodeRegistry::hasHandshakeConfirmAlert() const {
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].status == POLE_STATUS_ACTIVE &&
        poles[i].regDeliveryState == 3 &&
        !poles[i].handshakeNotifiedAdmin) {
      return true;
    }
  }
  return false;
}

const char* GwNodeRegistry::getNextHandshakeConfirmAlertText(int &outPoleIndex) {
  outPoleIndex = -1;
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].status == POLE_STATUS_ACTIVE &&
        poles[i].regDeliveryState == 3 &&
        !poles[i].handshakeNotifiedAdmin) {
      outPoleIndex = (int)i;
      static char buf[384];
      snprintf(buf, sizeof(buf),
               "✅ *POLE REGISTRATION CONFIRMED ACTIVE*\n"
               "• Pole: *%s* (`POLE-%04d` / `0x%04X`)\n"
               "• MAC: `%02X:%02X:%02X:%02X:%02X:%02X`\n"
               "• Location: %s\n"
               "• Parent: `0x%04X` (Rank %u)\n"
               "• Status: First authenticated heartbeat received.",
               poles[i].displayName, poles[i].poleId, poles[i].poleId,
               poles[i].mac[0], poles[i].mac[1], poles[i].mac[2],
               poles[i].mac[3], poles[i].mac[4], poles[i].mac[5],
               poles[i].location,
               poles[i].primaryParent, poles[i].rank);
      return buf;
    }
  }
  return "";
}

void GwNodeRegistry::markHandshakeConfirmNotified(int poleIndex) {
  if (poleIndex >= 0 && poleIndex < activeCount) {
    poles[poleIndex].handshakeNotifiedAdmin = true;
    saveRegistry();
  }
}

bool GwNodeRegistry::approvePending(const uint8_t mac[6],
                                    uint16_t &outAssignedId,
                                    const char *displayName,
                                    const char *location, const char *adminId) {
  int pIdx = findPendingIndex(mac);
  if (pIdx < 0)
    return false;
  if (activeCount >= GW_MAX_MANAGED_POLES)
    return false;

  // Copy ALL pending data BEFORE mutating pending[] or poles[]
  PendingRegistration pendingCopy = pending[pIdx];

  // Determine Display Name and Location (Wizard draft or argument)
  const char *nameToUse = (displayName && strlen(displayName) > 0)
                              ? displayName
                              : pendingCopy.draftDisplayName;
  const char *locToUse = (location && strlen(location) > 0)
                             ? location
                             : pendingCopy.draftLocation;

  // Enforce mandatory non-empty Display Name and Installation Location!
  if (!nameToUse || strlen(nameToUse) == 0 || !locToUse ||
      strlen(locToUse) == 0) {
    Serial.println(F("[REGISTRY] ❌ Cannot approve pole: Mandatory Display "
                     "Name and Installation Location required!"));
    return false;
  }

  uint16_t newId = allocateNextPoleId();
  if (newId == 0xFFFF)
    return false;

  // Staged backup for rollback
  RegisteredPole polesBackup[GW_MAX_MANAGED_POLES];
  uint8_t activeCountBackup = activeCount;
  PendingRegistration pendingBackup[GW_MAX_PENDING_REGISTRATIONS];
  uint8_t pendingCountBackup = pendingCount;
  memcpy(polesBackup, poles, sizeof(poles));
  memcpy(pendingBackup, pending, sizeof(pending));

  RegisteredPole &newPole = poles[activeCount++];
  memset(&newPole, 0, sizeof(RegisteredPole));
  memcpy(newPole.mac, pendingCopy.mac, 6);
  newPole.poleId = newId;
  snprintf(newPole.stringId, sizeof(newPole.stringId), "POLE_%02d", newId);
  strncpy(newPole.displayName, nameToUse, sizeof(newPole.displayName) - 1);
  strncpy(newPole.location, locToUse, sizeof(newPole.location) - 1);
  if (strlen(pendingCopy.draftArea) > 0) {
    strncpy(newPole.area, pendingCopy.draftArea, sizeof(newPole.area) - 1);
  }
  if (strlen(pendingCopy.draftLandmark) > 0) {
    strncpy(newPole.landmark, pendingCopy.draftLandmark,
            sizeof(newPole.landmark) - 1);
  }
  newPole.latitude = pendingCopy.draftLatitude;
  newPole.longitude = pendingCopy.draftLongitude;
  newPole.hasGps = pendingCopy.draftHasGps;
  strncpy(newPole.adminId, adminId ? adminId : "TELEGRAM_ADMIN",
          sizeof(newPole.adminId) - 1);

  // Initialize Feeder Names
  for (uint8_t f = 0; f < 6; f++) {
    if (strlen(pendingCopy.draftFeederNames[f]) > 0) {
      strncpy(newPole.feederNames[f], pendingCopy.draftFeederNames[f],
              sizeof(newPole.feederNames[f]) - 1);
    } else {
      snprintf(newPole.feederNames[f], sizeof(newPole.feederNames[f]),
               "Feeder %u", f + 1);
    }
  }

  newPole.status = POLE_STATUS_PENDING_ACK; // Handshake pending until confirmed over mesh
  newPole.firmwareVersion = pendingCopy.firmwareVersion;
  uint16_t priParent = (pendingCopy.requestedParent != 0 &&
                        pendingCopy.requestedParent < MSCB_EPHEMERAL_JOIN_MIN &&
                        pendingCopy.requestedParent != newId)
                           ? pendingCopy.requestedParent
                           : MSCB_GATEWAY_NODE_ID;
  newPole.primaryParent = priParent;
  newPole.backupParent = MSCB_GATEWAY_NODE_ID;
  if (priParent == MSCB_GATEWAY_NODE_ID) {
    newPole.rank = 1;
  } else {
    RegisteredPole* par = findPoleById(priParent);
    newPole.rank = (par && par->rank > 0 && par->rank < 254) ? (uint8_t)(par->rank + 1) : 2;
  }
  newPole.sessionToken = pendingCopy.sessionToken;
  newPole.lastReqNonce = pendingCopy.lastReqNonce;
  newPole.firstHopAddress = pendingCopy.firstHopAddress ? pendingCopy.firstHopAddress : MSCB_GATEWAY_NODE_ID;
  newPole.regResponseAttempts = 0;
  newPole.lastRegResponseMs = 0;
  newPole.regDeliveryState = 0;
  newPole.handshakeNotifiedAdmin = false;
  newPole.lastHeartbeatSeq = 0;
  newPole.lastUptimeSec = 0;
  newPole.registeredTimestamp = millis() / 1000;
  newPole.lastSeenTimestamp = newPole.registeredTimestamp;
  newPole.lastRssi = pendingCopy.rssi;
  newPole.lastSnr = pendingCopy.snr;
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

bool GwNodeRegistry::updatePoleMetadata(uint16_t poleId,
                                        const char *displayName,
                                        const char *location, const char *area,
                                        const char *landmark) {
  RegisteredPole *p = findPoleById(poleId);
  if (!p)
    return false;
  if (displayName && strlen(displayName) > 0) {
    strncpy(p->displayName, displayName, sizeof(p->displayName) - 1);
  }
  if (location && strlen(location) > 0) {
    strncpy(p->location, location, sizeof(p->location) - 1);
  }
  if (area) {
    strncpy(p->area, area, sizeof(p->area) - 1);
  }
  if (landmark) {
    strncpy(p->landmark, landmark, sizeof(p->landmark) - 1);
  }
  return saveRegistry();
}

bool GwNodeRegistry::setPoleFeederName(uint16_t poleId, uint8_t feederId,
                                       const char *name) {
  if (feederId < 1 || feederId > 6 || !name)
    return false;
  RegisteredPole *p = findPoleById(poleId);
  if (!p)
    return false;
  strncpy(p->feederNames[feederId - 1], name,
          sizeof(p->feederNames[feederId - 1]) - 1);
  p->feederNames[feederId - 1][sizeof(p->feederNames[feederId - 1]) - 1] = '\0';
  return saveRegistry();
}

const char *GwNodeRegistry::getPoleFeederName(uint16_t poleId,
                                              uint8_t feederId) const {
  if (feederId < 1 || feederId > 6)
    return "Unknown Feeder";
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].poleId == poleId &&
        (poles[i].status == POLE_STATUS_ACTIVE ||
         poles[i].status == POLE_STATUS_PENDING_ACK)) {
      if (strlen(poles[i].feederNames[feederId - 1]) > 0) {
        return poles[i].feederNames[feederId - 1];
      }
      break;
    }
  }
  static char fallback[16];
  snprintf(fallback, sizeof(fallback), "Feeder %u", feederId);
  return fallback;
}

PendingRegistration *GwNodeRegistry::getPendingByMac(const uint8_t mac[6]) {
  int idx = findPendingIndex(mac);
  if (idx >= 0)
    return &pending[idx];
  return nullptr;
}

bool GwNodeRegistry::setPendingWizardName(const uint8_t mac[6],
                                          const char *name) {
  int idx = findPendingIndex(mac);
  if (idx < 0 || !name || strlen(name) == 0)
    return false;
  strncpy(pending[idx].draftDisplayName, name,
          sizeof(pending[idx].draftDisplayName) - 1);
  pending[idx].draftDisplayName[sizeof(pending[idx].draftDisplayName) - 1] =
      '\0';
  pending[idx].wizardStep = 2; // Next: Location
  return saveRegistry();
}

bool GwNodeRegistry::setPendingWizardLocation(const uint8_t mac[6],
                                              const char *loc) {
  int idx = findPendingIndex(mac);
  if (idx < 0 || !loc || strlen(loc) == 0)
    return false;
  strncpy(pending[idx].draftLocation, loc,
          sizeof(pending[idx].draftLocation) - 1);
  pending[idx].draftLocation[sizeof(pending[idx].draftLocation) - 1] = '\0';
  pending[idx].wizardStep = 3; // Next: Area / Landmark
  return saveRegistry();
}

bool GwNodeRegistry::setPendingWizardAreaLandmark(const uint8_t mac[6],
                                                  const char *area,
                                                  const char *landmark) {
  int idx = findPendingIndex(mac);
  if (idx < 0)
    return false;
  if (area) {
    strncpy(pending[idx].draftArea, area, sizeof(pending[idx].draftArea) - 1);
    pending[idx].draftArea[sizeof(pending[idx].draftArea) - 1] = '\0';
  }
  if (landmark) {
    strncpy(pending[idx].draftLandmark, landmark,
            sizeof(pending[idx].draftLandmark) - 1);
    pending[idx].draftLandmark[sizeof(pending[idx].draftLandmark) - 1] = '\0';
  }
  pending[idx].wizardStep = 4; // Ready for review & approval
  return saveRegistry();
}

bool GwNodeRegistry::setPendingWizardFeederName(const uint8_t mac[6],
                                                uint8_t feederId,
                                                const char *name) {
  int idx = findPendingIndex(mac);
  if (idx < 0 || feederId < 1 || feederId > 6 || !name)
    return false;
  strncpy(pending[idx].draftFeederNames[feederId - 1], name,
          sizeof(pending[idx].draftFeederNames[feederId - 1]) - 1);
  pending[idx].draftFeederNames
      [feederId - 1][sizeof(pending[idx].draftFeederNames[feederId - 1]) - 1] =
      '\0';
  return saveRegistry();
}

bool GwNodeRegistry::rejectPending(const uint8_t mac[6]) {
  int pIdx = findPendingIndex(mac);
  if (pIdx < 0)
    return false;

  // Remove from pending
  for (uint8_t i = pIdx; i < pendingCount - 1; i++) {
    pending[i] = pending[i + 1];
  }
  pendingCount--;
  saveRegistry();
  return true;
}

PoleDeleteResult GwNodeRegistry::deleteAndRevokePoleEx(uint16_t poleId,
                                                       const char *adminId,
                                                       const char *reason) {
  int targetIdx = -1;
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].poleId == poleId &&
        (poles[i].status == POLE_STATUS_ACTIVE ||
         poles[i].status == POLE_STATUS_PENDING_ACK)) {
      targetIdx = i;
      break;
    }
  }
  if (targetIdx < 0)
    return DELETE_ERR_POLE_NOT_FOUND;

  // Verify whether MAC is already revoked
  if (isMacRevoked(poles[targetIdx].mac)) {
    return DELETE_ERR_ALREADY_REVOKED;
  }

  // Verify revocation capacity
  if (revokedCount >= MSCB_MAX_REVOCATION_RECORDS) {
    Serial.println(F(
        "[REGISTRY] ❌ Revocation list full! Rejecting pole deletion safely."));
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

  // 1. Stage permanent revocation record with delivery pending
  PermanentRevocationRecord &r = revoked[revokedCount++];
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
           poles[targetIdx].mac[0], poles[targetIdx].mac[1],
           poles[targetIdx].mac[2], poles[targetIdx].mac[3],
           poles[targetIdx].mac[4], poles[targetIdx].mac[5]);
  strncpy(r.hwId, macHex, sizeof(r.hwId) - 1);
  r.hwId[sizeof(r.hwId) - 1] = '\0';
  strncpy(r.logicalId, poles[targetIdx].stringId, sizeof(r.logicalId) - 1);
  r.logicalId[sizeof(r.logicalId) - 1] = '\0';
  snprintf(r.txId, sizeof(r.txId), "TX_REV_%04X_%lu", poleId,
           (unsigned long)(millis() / 1000));
  strncpy(r.reason, reason ? reason : "ADMIN_REVOKED", sizeof(r.reason) - 1);
  r.reason[sizeof(r.reason) - 1] = '\0';
  strncpy(r.adminId, adminId ? adminId : "TELEGRAM_ADMIN",
          sizeof(r.adminId) - 1);
  r.adminId[sizeof(r.adminId) - 1] = '\0';
  r.timestamp = millis() / 1000;
  r.deliveryStatus = REVOKE_DELIVERY_PENDING;
  r.unrevokeAdmin[0] = '\0';
  r.unrevokeReason[0] = '\0';
  r.unrevokeTimestamp = 0;
  r.crc32 = calculateRevocationCrc(r);

  // 2. Mark pole status as REVOKED in registry (preserve route until delivery confirmed/acked)
  poles[targetIdx].status = POLE_STATUS_REVOKED;

  // 3. Child Route Migration: If any pole had deleted pole as primary/backup
  // parent, re-route to Gateway
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].primaryParent == poleId) {
      poles[i].primaryParent = MSCB_GATEWAY_NODE_ID;
      poles[i].rank = 1;
    }
    if (poles[i].backupParent == poleId) {
      poles[i].backupParent = MSCB_GATEWAY_NODE_ID;
    }
  }

  // 4. Two-Phase Commit Persistence
  if (!saveRegistry()) {
    Serial.println(F("[REGISTRY] ❌ saveRegistry() failed! Rolling back delete "
                     "transaction."));
    // ROLLBACK!
    memcpy(poles, polesBackup, sizeof(poles));
    activeCount = activeCountBackup;
    memcpy(revoked, revokedBackup, sizeof(revoked));
    revokedCount = revokedCountBackup;
    memcpy(pending, pendingBackup, sizeof(pending));
    pendingCount = pendingCountBackup;
    return DELETE_ERR_REGISTRY_SAVE_FAILED;
  }

  Serial.printf(
      "[REGISTRY] ✅ Pole 0x%04X (MAC %s) marked REVOKED (delivery pending).\n",
      poleId, macHex);
  return DELETE_SUCCESS;
}

bool GwNodeRegistry::deleteAndRevokePole(uint16_t poleId, const char *adminId,
                                         const char *reason) {
  return (deleteAndRevokePoleEx(poleId, adminId, reason) == DELETE_SUCCESS);
}

bool GwNodeRegistry::unrevokePole(const uint8_t mac[6], const char *adminId,
                                  const char *reason) {
  if (!mac)
    return false;
  char macHex[13];
  snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);

  int targetIdx = -1;
  for (uint8_t i = 0; i < revokedCount; i++) {
    if (strncmp(revoked[i].hwId, macHex, 12) == 0 &&
        revoked[i].deliveryStatus != REVOKE_STATUS_UNREVOKED) {
      targetIdx = i;
      break;
    }
  }
  if (targetIdx < 0)
    return false;

  // Backup for rollback
  PermanentRevocationRecord revokedBackup[MSCB_MAX_REVOCATION_RECORDS];
  uint8_t revokedCountBackup = revokedCount;
  memcpy(revokedBackup, revoked, sizeof(revoked));

  // Retain record in audit trail: mark un-revoked with admin, reason, timestamp
  revoked[targetIdx].deliveryStatus = REVOKE_STATUS_UNREVOKED;
  revoked[targetIdx].unrevokeTimestamp = millis() / 1000;
  strncpy(revoked[targetIdx].unrevokeAdmin, adminId ? adminId : "ADMIN",
          sizeof(revoked[targetIdx].unrevokeAdmin) - 1);
  revoked[targetIdx].unrevokeAdmin[sizeof(revoked[targetIdx].unrevokeAdmin) - 1] = '\0';
  strncpy(revoked[targetIdx].unrevokeReason, reason ? reason : "ADMIN_REAUTHORIZED",
          sizeof(revoked[targetIdx].unrevokeReason) - 1);
  revoked[targetIdx].unrevokeReason[sizeof(revoked[targetIdx].unrevokeReason) - 1] = '\0';
  revoked[targetIdx].crc32 = calculateRevocationCrc(revoked[targetIdx]);

  if (!saveRegistry()) {
    // Rollback
    memcpy(revoked, revokedBackup, sizeof(revoked));
    revokedCount = revokedCountBackup;
    return false;
  }

  Serial.printf(
      "[REGISTRY] ✅ MAC %s successfully un-revoked / re-authorized by %s.\n",
      macHex, adminId ? adminId : "ADMIN");
  return true;
}

void GwNodeRegistry::confirmPoleHandshake(uint16_t poleId) {
  for (uint8_t i = 0; i < activeCount; i++) {
    if (poles[i].poleId == poleId && poles[i].status == POLE_STATUS_PENDING_ACK) {
      poles[i].status = POLE_STATUS_ACTIVE;
      poles[i].regDeliveryState = 3; // HANDSHAKE_CONFIRMED
      poles[i].handshakeNotifiedAdmin = false;
      Serial.printf("[REGISTRY] 🤝 Pole 0x%04X handshake confirmed -> POLE_STATUS_ACTIVE\n", poleId);
      saveRegistry();
      break;
    }
  }
}

void GwNodeRegistry::confirmRevokeDelivery(const uint8_t mac[6], uint16_t poleId) {
  char macHex[13] = {0};
  if (mac) {
    snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  char polePrefix[16] = {0};
  if (poleId != 0) {
    snprintf(polePrefix, sizeof(polePrefix), "TX_REV_%04X", poleId);
  }

  // Update revocation delivery status to confirmed
  for (uint8_t i = 0; i < revokedCount; i++) {
    if ((mac && strncmp(revoked[i].hwId, macHex, 12) == 0) ||
        (poleId != 0 && strstr(revoked[i].txId, polePrefix) != nullptr)) {
      revoked[i].deliveryStatus = REVOKE_DELIVERY_CONFIRMED;
      revoked[i].crc32 = calculateRevocationCrc(revoked[i]);
      break;
    }
  }

  // Remove pole from poles array now that delivery is confirmed
  int targetIdx = -1;
  for (uint8_t i = 0; i < activeCount; i++) {
    if ((poleId != 0 && poles[i].poleId == poleId) ||
        (mac && memcmp(poles[i].mac, mac, 6) == 0)) {
      targetIdx = i;
      break;
    }
  }

  if (targetIdx >= 0) {
    for (uint8_t i = targetIdx; i < activeCount - 1; i++) {
      poles[i] = poles[i + 1];
    }
    activeCount--;
    Serial.printf("[REGISTRY] 🏁 Revocation delivery confirmed. Removed pole 0x%04X from active routing.\n", poleId);
    saveRegistry();
  }
}

void GwNodeRegistry::updateNodeTelemetry(uint16_t poleId, uint16_t priParent,
                                         uint16_t bkpParent, uint8_t rank,
                                         uint8_t feederMask, uint8_t auxMask,
                                         uint8_t mcbMask, float busVolt,
                                         float current, int16_t rssi,
                                         int8_t snr,
                                         uint8_t auxValid, uint8_t mcbValid) {
  RegisteredPole *pole = findPoleById(poleId);
  if (!pole)
    return;

  pole->lastSeenTimestamp = millis() / 1000;
  if (priParent != 0)
    pole->primaryParent = priParent;
  if (bkpParent != 0)
    pole->backupParent = bkpParent;
  if (rank != 0)
    pole->rank = rank;
  pole->feederStateMask = feederMask;
  pole->auxFeedbackMask = auxMask;
  pole->mcbStatusMask = mcbMask;
  pole->auxFeedbackValid = auxValid;
  pole->mcbStatusValid = mcbValid;
  pole->voltageValid = (busVolt > 50.0f) ? MEAS_VALID : MEAS_SENSOR_UNAVAILABLE;
  pole->currentValid = std::isnan(current) ? MEAS_SENSOR_UNAVAILABLE : (current > 0.0f ? MEAS_VALID : MEAS_SENSOR_UNAVAILABLE);
  pole->lastBusVoltage = busVolt;
  pole->lastCurrent = current;
  pole->lastRssi = rssi;
  pole->lastSnr = snr;
}

#if !defined(ARDUINO)
struct SimulatedSlot {
  RegistrySlotHeader header;
  RegisteredPole poles[GW_MAX_MANAGED_POLES];
  PermanentRevocationRecord revoked[MSCB_MAX_REVOCATION_RECORDS];
  PendingRegistration pending[GW_MAX_PENDING_REGISTRATIONS];
  bool valid;
};
static SimulatedSlot s_simSlotA;
static SimulatedSlot s_simSlotB;
static bool s_simInitDone = false;

static void initSimSlotsOnce() {
  if (!s_simInitDone) {
    memset(&s_simSlotA, 0, sizeof(s_simSlotA));
    memset(&s_simSlotB, 0, sizeof(s_simSlotB));
    s_simSlotA.valid = false;
    s_simSlotB.valid = false;
    s_simInitDone = true;
  }
}
#endif

void GwNodeRegistry::simulateCorruptSlot(uint8_t slotIdx) {
#if !defined(ARDUINO)
  initSimSlotsOnce();
  if (slotIdx == 0) {
    s_simSlotA.header.magic = 0xDEADBEEF;
    s_simSlotA.valid = false;
  } else {
    s_simSlotB.header.magic = 0xDEADBEEF;
    s_simSlotB.valid = false;
  }
#else
  const char *slotNs = (slotIdx == 0) ? "mscb_reg_a" : "mscb_reg_b";
  Preferences p;
  if (p.begin(slotNs, false)) {
    p.putUInt("magic", 0xDEADBEEF);
    p.end();
  }
#endif
}

void GwNodeRegistry::simulatePowerFailureMidWrite(bool enable) {
  simulateInterruptedWrite = enable;
}

void GwNodeRegistry::resetStorageToDefault() {
  activeCount = 0;
  revokedCount = 0;
  pendingCount = 0;
  currentGeneration = 0;
  activeSlot = 0;
  simulateInterruptedWrite = false;
  memset(revokedAlertQueue, 0, sizeof(revokedAlertQueue));
  revokedAlertHead = 0;
  revokedAlertTail = 0;
  revokedAlertCount = 0;
  memset(revokedIncidents, 0, sizeof(revokedIncidents));
  revokedIncidentCount = 0;
  memset(poles, 0, sizeof(poles));
  memset(pending, 0, sizeof(pending));
  memset(revoked, 0, sizeof(revoked));

#if !defined(ARDUINO)
  initSimSlotsOnce();
  s_simSlotA.valid = false;
  s_simSlotB.valid = false;
  memset(&s_simSlotA, 0, sizeof(s_simSlotA));
  memset(&s_simSlotB, 0, sizeof(s_simSlotB));
  memset(&s_simRevokedIncidents, 0, sizeof(s_simRevokedIncidents));
#else
  Preferences pA, pB, pInc;
  if (pA.begin("mscb_reg_a", false)) {
    pA.clear();
    pA.end();
  }
  if (pB.begin("mscb_reg_b", false)) {
    pB.clear();
    pB.end();
  }
  if (pInc.begin("mscb_rev_inc", false)) {
    pInc.clear();
    pInc.end();
  }
#endif
}

bool GwNodeRegistry::saveRegistry() {
  uint8_t targetSlot = 1 - activeSlot; // Write to alternate inactive slot
  uint32_t nextGen = currentGeneration + 1;

  uint32_t payloadCrc = calculateRegistryCrc(
      activeCount, poles, revokedCount, revoked, pendingCount, pending);

#if !defined(ARDUINO)
  initSimSlotsOnce();
  SimulatedSlot &dest = (targetSlot == 0) ? s_simSlotA : s_simSlotB;

  // 1. Invalidate slot before write
  dest.valid = false;
  dest.header.magic = 0;
  dest.header.generation = nextGen;
  dest.header.schemaVersion = MSCB_CURRENT_REG_SCHEMA;
  dest.header.activeCount = activeCount;
  dest.header.revokedCount = revokedCount;
  dest.header.pendingCount = pendingCount;
  dest.header.payloadCrc = payloadCrc;

  // 2. Write payload
  memcpy(dest.poles, poles, sizeof(poles));
  memcpy(dest.revoked, revoked, sizeof(revoked));
  memcpy(dest.pending, pending, sizeof(pending));

  // 3. Test hook: simulate power loss before final commit marker
  if (simulateInterruptedWrite) {
    Serial.println(F("[REGISTRY] ⚠️ Simulated power failure mid-write! Commit marker not set."));
    return false;
  }

  // 4. Atomic Commit
  dest.header.magic = MSCB_SLOT_MAGIC;
  dest.valid = true;
  activeSlot = targetSlot;
  currentGeneration = nextGen;
  return true;

#else
  const char *targetNs = (targetSlot == 0) ? "mscb_reg_a" : "mscb_reg_b";
  Preferences p;
  if (!p.begin(targetNs, false)) {
    Serial.printf("[REGISTRY] ❌ Preferences.begin('%s', false) FAILED!\n", targetNs);
    return false;
  }

  // 1. Clear magic marker to invalidate slot during write
  p.putUInt("magic", 0);

  // 2. Write header fields
  p.putUInt("generation", nextGen);
  p.putUChar("schema_ver", MSCB_CURRENT_REG_SCHEMA);
  p.putUChar("active_cnt", activeCount);
  p.putUChar("revoked_cnt", revokedCount);
  p.putUChar("pending_cnt", pendingCount);
  p.putUInt("reg_crc", payloadCrc);

  // 3. Write payload data
  if (activeCount > 0) {
    size_t sz = activeCount * sizeof(RegisteredPole);
    p.putBytes("poles", poles, sz);
  }
  if (revokedCount > 0) {
    size_t sz = revokedCount * sizeof(PermanentRevocationRecord);
    p.putBytes("revoked", revoked, sz);
  }
  if (pendingCount > 0) {
    size_t sz = pendingCount * sizeof(PendingRegistration);
    p.putBytes("pending", pending, sz);
  }

  // 4. Test hook for power loss
  if (simulateInterruptedWrite) {
    p.end();
    return false;
  }

  // 5. Atomic commit: write valid magic last
  p.putUInt("magic", MSCB_SLOT_MAGIC);
  p.end();

  activeSlot = targetSlot;
  currentGeneration = nextGen;
  return true;
#endif
}

bool GwNodeRegistry::loadRegistry() {
#if !defined(ARDUINO)
  initSimSlotsOnce();

  bool aValid = s_simSlotA.valid && (s_simSlotA.header.magic == MSCB_SLOT_MAGIC);
  if (aValid) {
    uint32_t calcCrcA = calculateRegistryCrc(
        s_simSlotA.header.activeCount, s_simSlotA.poles,
        s_simSlotA.header.revokedCount, s_simSlotA.revoked,
        s_simSlotA.header.pendingCount, s_simSlotA.pending);
    if (calcCrcA != s_simSlotA.header.payloadCrc) {
      aValid = false;
    }
  }

  bool bValid = s_simSlotB.valid && (s_simSlotB.header.magic == MSCB_SLOT_MAGIC);
  if (bValid) {
    uint32_t calcCrcB = calculateRegistryCrc(
        s_simSlotB.header.activeCount, s_simSlotB.poles,
        s_simSlotB.header.revokedCount, s_simSlotB.revoked,
        s_simSlotB.header.pendingCount, s_simSlotB.pending);
    if (calcCrcB != s_simSlotB.header.payloadCrc) {
      bValid = false;
    }
  }

  if (!aValid && !bValid) {
    Serial.println(F("[REGISTRY] No valid A/B slot found. Initializing empty."));
    activeCount = 0;
    revokedCount = 0;
    pendingCount = 0;
    currentGeneration = 0;
    activeSlot = 0;
    return false;
  }

  uint8_t selectedSlot = 0;
  if (aValid && bValid) {
    selectedSlot = (s_simSlotB.header.generation > s_simSlotA.header.generation) ? 1 : 0;
  } else if (bValid) {
    selectedSlot = 1;
  } else {
    selectedSlot = 0;
  }

  const SimulatedSlot &src = (selectedSlot == 0) ? s_simSlotA : s_simSlotB;
  activeCount = src.header.activeCount;
  revokedCount = src.header.revokedCount;
  pendingCount = src.header.pendingCount;
  currentGeneration = src.header.generation;
  activeSlot = selectedSlot;

  memcpy(poles, src.poles, sizeof(poles));
  memcpy(revoked, src.revoked, sizeof(revoked));
  memcpy(pending, src.pending, sizeof(pending));

  Serial.printf("[REGISTRY] ✅ Loaded Slot %c (Gen %u, Active: %u, Revoked: %u, Pending: %u)\n",
                selectedSlot == 0 ? 'A' : 'B', currentGeneration, activeCount, revokedCount, pendingCount);
  return true;

#else
  Preferences pA, pB;
  bool aOpen = pA.begin("mscb_reg_a", true);
  bool bOpen = pB.begin("mscb_reg_b", true);

  auto validateSlot = [](Preferences &p, RegisteredPole *tempPoles,
                         PermanentRevocationRecord *tempRev, PendingRegistration *tempPend,
                         uint8_t &outAct, uint8_t &outRev, uint8_t &outPend,
                         uint32_t &outGen) -> bool {
    uint32_t magic = p.getUInt("magic", 0);
    if (magic != MSCB_SLOT_MAGIC) return false;
    outGen = p.getUInt("generation", 0);
    outAct = p.getUChar("active_cnt", 0);
    outRev = p.getUChar("revoked_cnt", 0);
    outPend = p.getUChar("pending_cnt", 0);
    uint32_t storedCrc = p.getUInt("reg_crc", 0);

    if (outAct > GW_MAX_MANAGED_POLES || outRev > MSCB_MAX_REVOCATION_RECORDS ||
        outPend > GW_MAX_PENDING_REGISTRATIONS) return false;

    if (outAct > 0) {
      size_t expected = outAct * sizeof(RegisteredPole);
      if (p.getBytes("poles", tempPoles, expected) != expected) return false;
    }
    if (outRev > 0) {
      size_t expected = outRev * sizeof(PermanentRevocationRecord);
      if (p.getBytes("revoked", tempRev, expected) != expected) return false;
    }
    if (outPend > 0) {
      size_t expected = outPend * sizeof(PendingRegistration);
      if (p.getBytes("pending", tempPend, expected) != expected) return false;
    }

    uint32_t computed = calculateRegistryCrc(outAct, tempPoles, outRev, tempRev, outPend, tempPend);
    return (computed == storedCrc);
  };

  RegisteredPole tempPoles[GW_MAX_MANAGED_POLES];
  PermanentRevocationRecord tempRev[MSCB_MAX_REVOCATION_RECORDS];
  PendingRegistration tempPend[GW_MAX_PENDING_REGISTRATIONS];

  uint8_t actA = 0, revA = 0, pendA = 0;
  uint32_t genA = 0;
  bool aValid = aOpen && validateSlot(pA, tempPoles, tempRev, tempPend, actA, revA, pendA, genA);

  uint8_t actB = 0, revB = 0, pendB = 0;
  uint32_t genB = 0;
  bool bValid = bOpen && validateSlot(pB, tempPoles, tempRev, tempPend, actB, revB, pendB, genB);

  if (!aValid && !bValid) {
    if (aOpen) pA.end();
    if (bOpen) pB.end();
    activeCount = 0;
    revokedCount = 0;
    pendingCount = 0;
    currentGeneration = 0;
    activeSlot = 0;
    return false;
  }

  uint8_t selectedSlot = 0;
  if (aValid && bValid) {
    selectedSlot = (genB > genA) ? 1 : 0;
  } else if (bValid) {
    selectedSlot = 1;
  } else {
    selectedSlot = 0;
  }

  Preferences &selP = (selectedSlot == 0) ? pA : pB;
  activeCount = (selectedSlot == 0) ? actA : actB;
  revokedCount = (selectedSlot == 0) ? revA : revB;
  pendingCount = (selectedSlot == 0) ? pendA : pendB;
  currentGeneration = (selectedSlot == 0) ? genA : genB;
  activeSlot = selectedSlot;

  if (activeCount > 0) selP.getBytes("poles", poles, activeCount * sizeof(RegisteredPole));
  if (revokedCount > 0) selP.getBytes("revoked", revoked, revokedCount * sizeof(PermanentRevocationRecord));
  if (pendingCount > 0) selP.getBytes("pending", pending, pendingCount * sizeof(PendingRegistration));

  if (aOpen) pA.end();
  if (bOpen) pB.end();

  Serial.printf("[REGISTRY] ✅ Loaded Slot %c (Gen %u, Active: %u, Revoked: %u, Pending: %u)\n",
                selectedSlot == 0 ? 'A' : 'B', currentGeneration, activeCount, revokedCount, pendingCount);
  return true;
#endif
}
