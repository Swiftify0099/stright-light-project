/**
 * @file PoleRegistrationClient.cpp
 * @brief MSCB Smart Pole Node - End-to-End Discovery & Registration Client Implementation
 * @version 1.0.0
 */

#include "PoleRegistrationClient.h"
#include "PoleActuators.h"
#include "PoleRadioDriver.h"
#include "PoleHeartbeatSender.h"
#if __has_include("../MSCBShared/CommandSecurity.h")
  #include "../MSCBShared/CommandSecurity.h"
#elif __has_include(<CommandSecurity.h>)
  #include <CommandSecurity.h>
#else
  #include "CommandSecurity.h"
#endif
#include <string.h>
#include <stdio.h>

#if defined(ARDUINO) && defined(ESP32)
  #include <esp_mac.h>
#endif

PoleRegistrationClient poleRegClient;

PoleRegistrationClient::PoleRegistrationClient() :
  state(REG_CLIENT_UNREGISTERED),
  lastError(REG_ERR_NONE),
  sessionToken(0),
  reqNonceCounter(0),
  lastReqNonce(0),
  lastAcceptedRespNonce(0),
  lastAcceptedRespStatus(0xFF),
  lastRequestMs(0),
  lastDiscoveryMs(0),
  sessionStartMs(0),
  handshakeStartMs(0),
  hasPendingSession(false),
  retryIntervalMs(12000)
{
  memset(myMac, 0, sizeof(myMac));
}

void PoleRegistrationClient::readHardwareMac() {
#if defined(ARDUINO) && defined(ESP32)
  esp_read_mac(myMac, ESP_MAC_WIFI_STA);
#else
  myMac[0] = 0xAA; myMac[1] = 0xBB; myMac[2] = 0xCC;
  myMac[3] = 0xDD; myMac[4] = 0xEE; myMac[5] = 0x02;
#endif
}

uint32_t PoleRegistrationClient::generateSessionToken() {
  static uint32_t s_sessionSeq = 0;
  s_sessionSeq++;
  uint32_t macMix = ((uint32_t)myMac[2] << 24) | ((uint32_t)myMac[3] << 16) |
                    ((uint32_t)myMac[4] << 8) | (uint32_t)myMac[5];
  uint32_t tok = macMix ^ ((uint32_t)millis() * 2654435761UL) ^ (s_sessionSeq * 0x9E3779B1UL) ^ 0xA5A50001UL;
#if defined(ARDUINO) && defined(ESP32)
  tok ^= esp_random();
#endif
  if (tok == 0) tok = 0xA5A50001UL + s_sessionSeq;
  return tok;
}

void PoleRegistrationClient::clearRegistration() {
  state = REG_CLIENT_UNREGISTERED;
  lastError = REG_ERR_NONE;
  retryIntervalMs = 12000;
  lastAcceptedRespNonce = 0;
  lastAcceptedRespStatus = 0xFF;
  lastReqNonce = 0;
  sessionStartMs = 0;
  handshakeStartMs = 0;
  hasPendingSession = false;
  poleStorage.clearRegistration();
  sessionToken = generateSessionToken();
  poleMesh.resetToUnassignedIdentity(myMac);
}

bool PoleRegistrationClient::begin() {
  readHardwareMac();
  lastError = REG_ERR_NONE;
  lastAcceptedRespNonce = 0;
  lastAcceptedRespStatus = 0xFF;

  if (poleStorage.isRevokedLocked()) {
    state = REG_CLIENT_REVOKED_LOCKED;
    lastError = REG_ERR_REVOKED;
    sessionToken = (poleStorage.getSessionToken() != 0) ? poleStorage.getSessionToken() : generateSessionToken();
    Serial.println(F("[REG_CLIENT] ⛔ Node is in persistent REVOKED_LOCKED state. Relays locked OFF."));
    poleActuators.emergencyShutdownAll("BOOT_REVOCATION_LOCK");
    poleActuators.setRelaysLockedDisabled(true);
    poleMesh.resetToUnassignedIdentity(myMac);
  } else if (poleStorage.getRegState() == REG_CLIENT_REJECTED) {
    state = REG_CLIENT_REJECTED;
    lastError = REG_ERR_REJECTED;
    sessionToken = generateSessionToken();
    Serial.println(F("[REG_CLIENT] ⚠️ Node is in persistent REJECTED state. Waiting for admin."));
    poleMesh.resetToUnassignedIdentity(myMac);
  } else if (poleStorage.isRegistered() && mscbIsValidAssignedPoleId(poleStorage.getAssignedPoleId())) {
    state = REG_CLIENT_REGISTERED;
    sessionToken = (poleStorage.getSessionToken() != 0) ? poleStorage.getSessionToken() : generateSessionToken();
    hasPendingSession = false;
  } else if (poleStorage.getRegState() == REG_CLIENT_WAITING_FOR_HANDSHAKE &&
             mscbIsValidAssignedPoleId(poleStorage.getAssignedPoleId())) {
    state = REG_CLIENT_WAITING_FOR_HANDSHAKE;
    sessionToken = (poleStorage.getSessionToken() != 0) ? poleStorage.getSessionToken() : generateSessionToken();
    handshakeStartMs = millis();
    hasPendingSession = true;
    poleMesh.setNodeIdentity(poleStorage.getAssignedPoleId(), poleStorage.getPrimaryParent(), poleStorage.getNetworkRank());
    Serial.println(F("[REG_CLIENT] 🔄 Resuming WAITING_FOR_HANDSHAKE after reboot."));
  } else if (poleStorage.getRegState() == REG_CLIENT_PENDING_APPROVAL) {
    state = REG_CLIENT_PENDING_APPROVAL;
    sessionToken = (poleStorage.getSessionToken() != 0) ? poleStorage.getSessionToken() : generateSessionToken();
    retryIntervalMs = 60000;
    hasPendingSession = true;
    poleMesh.resetToUnassignedIdentity(myMac);
    Serial.println(F("[REG_CLIENT] ⏳ Resuming PENDING_ADMIN_APPROVAL with 60s status interval."));
  } else {
    state = REG_CLIENT_UNREGISTERED;
    sessionToken = generateSessionToken();
    hasPendingSession = false;
    poleMesh.resetToUnassignedIdentity(myMac);
  }
  return true;
}

bool PoleRegistrationClient::beginDiscovery() {
  if (state == REG_CLIENT_REGISTERED || state == REG_CLIENT_REVOKED_LOCKED ||
      state == REG_CLIENT_REJECTED) {
    return false;
  }
  if (!mscbIsValidMac(myMac, true)) {
    lastError = REG_ERR_INVALID_MAC;
    state = REG_CLIENT_REGISTRATION_ERROR;
    return false;
  }
  if (!poleRadio.isDetected()) {
    lastError = REG_ERR_RADIO_UNAVAILABLE;
    state = REG_CLIENT_REGISTRATION_ERROR;
    return false;
  }
  state = REG_CLIENT_DISCOVERING_PARENT;
  lastDiscoveryMs = millis();
  if (poleMesh.getNeighborCount() > 0 && poleMesh.selectBestParents()) {
    state = REG_CLIENT_READY_TO_REQUEST;
    return true;
  }
  return false;
}

bool PoleRegistrationClient::triggerRegistrationRequest() {
  if (state == REG_CLIENT_REGISTERED || state == REG_CLIENT_REVOKED_LOCKED ||
      state == REG_CLIENT_WAITING_FOR_HANDSHAKE || state == REG_CLIENT_APPROVAL_RECEIVED) {
    return false;
  }
  bool wasPendingApproval = (state == REG_CLIENT_PENDING_APPROVAL);

  // 1. MAC Validation (non-zero, non-FF, unicast)
  if (!mscbIsValidMac(myMac, true)) {
    lastError = REG_ERR_INVALID_MAC;
    state = REG_CLIENT_REGISTRATION_ERROR;
    Serial.println(F("[REG_CLIENT] ❌ Registration failed: Invalid device MAC address!"));
    return false;
  }

  // 2. LoRa Radio Readiness Check
  if (!poleRadio.isDetected()) {
    lastError = REG_ERR_RADIO_UNAVAILABLE;
    if (!wasPendingApproval) {
      state = REG_CLIENT_REGISTRATION_ERROR;
    }
    Serial.println(F("[REG_CLIENT] ❌ Registration failed: LoRa radio not ready/detected!"));
    return false;
  }

  // 3. Payload length check
  if (sizeof(PktRegisterRequestPayload) > MSCB_MAX_PAYLOAD_SIZE) {
    lastError = REG_ERR_PAYLOAD_INVALID;
    state = REG_CLIENT_REGISTRATION_ERROR;
    return false;
  }

  // 4. Cryptographic Key Provisioning Check (Fail-Closed)
  if (!cmdSecurity.isKeyProvisioned()) {
    lastError = REG_ERR_AUTH_KEY_UNAVAILABLE;
    state = REG_CLIENT_REGISTRATION_ERROR;
    Serial.println(F("[REG_CLIENT] ❌ Registration failed closed: Authentication key unavailable!"));
    return false;
  }

  // Preserve active registration session while pending; never rotate session unnecessarily mid-flow
  if (sessionToken == 0) {
    sessionToken = generateSessionToken();
  }

  if (!wasPendingApproval) {
    state = REG_CLIENT_READY_TO_REQUEST;
  }

  PktRegisterRequestPayload req;
  memset(&req, 0, sizeof(req));
  memcpy(req.macAddress, myMac, 6);
  req.nodeType = 1; // 1 = FIELD_POLE
  req.firmwareVersion = (MSCB_FW_VERSION_MAJOR * 10) + MSCB_FW_VERSION_MINOR;
  req.sessionToken = sessionToken;
  req.candidateParent1 = poleMesh.getPrimaryParent();
  req.candidateParent2 = poleMesh.getBackupParent();

  // Derive link metrics from neighbor table / radio driver rather than hardcoded constants
  int16_t parentRssi = poleRadio.getLastRssi();
  int8_t  parentSnr  = poleRadio.getLastSnr();
  for (uint8_t i = 0; i < poleMesh.getNeighborCount(); i++) {
    const MeshNeighbor* nb = poleMesh.getNeighbor(i);
    if (nb && nb->nodeAddress == req.candidateParent1) {
      parentRssi = nb->lastRssi;
      parentSnr  = nb->lastSnr;
      break;
    }
  }
  if (parentRssi == 0) parentRssi = -120;
  req.rssiToParent1 = parentRssi;
  req.snrToParent1 = parentSnr;

  reqNonceCounter++;
  if (reqNonceCounter == 0) reqNonceCounter = 1;
  req.nonce = reqNonceCounter;
  lastReqNonce = req.nonce;

  // Compute cryptographic HMAC-SHA256 authentication tag (never all-zero)
  if (!cmdSecurity.computeRegisterRequestAuth(req, req.authTag)) {
    lastError = REG_ERR_AUTH_KEY_UNAVAILABLE;
    state = REG_CLIENT_REGISTRATION_ERROR;
    return false;
  }

  // 5. Send uplink and verify real radio transmission return value
  bool txSuccess = poleMesh.sendUplinkPacket(PKT_REGISTER_REQUEST, (const uint8_t*)&req, sizeof(PktRegisterRequestPayload), PRIO_COMMAND);
  lastRequestMs = millis();

  if (!txSuccess) {
    lastError = REG_ERR_TX_FAILED;
    if (!wasPendingApproval) {
      state = REG_CLIENT_REGISTRATION_ERROR;
    }
    // Controlled exponential backoff on transmission failure
    if (retryIntervalMs < 60000) {
      uint32_t nextInt = ((uint32_t)retryIntervalMs * 3UL) / 2UL;
      retryIntervalMs = (nextInt > 60000UL) ? 60000 : (uint16_t)nextInt;
    }
    Serial.println(F("[REG_CLIENT] ⚠️ LoRa uplink send failed. State not marked transmitted; backing off."));
    return false;
  }

  hasPendingSession = true;
  if (sessionStartMs == 0) {
    sessionStartMs = lastRequestMs;
  }
  state = wasPendingApproval ? REG_CLIENT_PENDING_APPROVAL : REG_CLIENT_REQUEST_TRANSMITTED;
  lastError = REG_ERR_NONE;
  Serial.println(F("[REG_CLIENT] 📡 Registration request transmitted successfully. Awaiting Gateway."));
  return true;
}

bool PoleRegistrationClient::confirmHandshakeComplete() {
  if (state == REG_CLIENT_REVOKED_LOCKED || state == REG_CLIENT_REJECTED) {
    return false;
  }
  if (state != REG_CLIENT_WAITING_FOR_HANDSHAKE &&
      state != REG_CLIENT_APPROVAL_RECEIVED &&
      poleStorage.getRegState() != REG_CLIENT_WAITING_FOR_HANDSHAKE) {
    return false;
  }
  if (!poleStorage.confirmHandshakeRegistered()) {
    return false;
  }
  state = REG_CLIENT_REGISTERED;
  lastError = REG_ERR_NONE;
  hasPendingSession = false;
  return true;
}

bool PoleRegistrationClient::handleRegisterResponse(const PktRegisterResponsePayload &resp) {
  // Never unlock a permanently revoked device
  if (state == REG_CLIENT_REVOKED_LOCKED || poleStorage.isRevokedLocked()) {
    lastError = REG_ERR_REVOKED;
    return false;
  }

  // Validate MAC matches this device and is valid
  if (!mscbIsValidMac(resp.macAddress, true) || memcmp(resp.macAddress, myMac, 6) != 0) {
    return false;
  }

  // Fail-Closed Cryptographic Key & Auth Validation
  if (!cmdSecurity.isKeyProvisioned()) {
    lastError = REG_ERR_AUTH_KEY_UNAVAILABLE;
    Serial.println(F("[REG_CLIENT] ❌ Registration response failed closed: Auth key unavailable!"));
    return false;
  }

  // Validate session token correlation (reject zero or mismatched session tokens)
  if (resp.sessionToken == 0 || sessionToken == 0 || resp.sessionToken != sessionToken) {
    Serial.println(F("[REG_CLIENT] ❌ Registration response rejected: Session token mismatch!"));
    lastError = REG_ERR_SESSION_MISMATCH;
    return false;
  }

  // Validate non-zero correlated nonce
  if (resp.nonce == 0) {
    Serial.println(F("[REG_CLIENT] ❌ Registration response rejected: Zero nonce!"));
    lastError = REG_ERR_SESSION_MISMATCH;
    return false;
  }

  // Verify HMAC-SHA256 authentication tag (rejects zero tags too)
  if (!cmdSecurity.verifyRegisterResponseAuth(resp)) {
    Serial.println(F("[REG_CLIENT] ❌ Registration response rejected: Invalid cryptographic authTag!"));
    lastError = REG_ERR_AUTH_FAILED;
    return false;
  }

  // Validate key epoch matches provisioned key epoch
  if (cmdSecurity.getActiveKeyEpoch() != 0 && resp.networkKeyEpoch != cmdSecurity.getActiveKeyEpoch()) {
    Serial.println(F("[REG_CLIENT] ❌ Registration response rejected: Key epoch mismatch!"));
    lastError = REG_ERR_AUTH_FAILED;
    return false;
  }

  // Anti-replay & expiry check
  if (state == REG_CLIENT_REGISTERED ||
      (lastAcceptedRespNonce != 0 && resp.nonce == lastAcceptedRespNonce && resp.status == lastAcceptedRespStatus)) {
    lastError = REG_ERR_REPLAY_EXPIRED;
    return false;
  }
  if (sessionStartMs != 0 && state != REG_CLIENT_PENDING_APPROVAL &&
      (millis() - sessionStartMs > 1800000UL)) {
    lastError = REG_ERR_REPLAY_EXPIRED;
    return false;
  }

  if (resp.status == REG_RESP_APPROVED) {
    // Validate assigned Pole ID and parent are non-zero and valid
    if (!mscbIsValidAssignedPoleId(resp.assignedPoleId) ||
        resp.assignedParent == 0 ||
        resp.assignedParent == resp.assignedPoleId ||
        resp.assignedParent >= MSCB_EPHEMERAL_JOIN_MIN) {
      Serial.println(F("[REG_CLIENT] ❌ Registration response rejected: Invalid assigned Pole ID or parent!"));
      lastError = REG_ERR_INVALID_ASSIGNMENT;
      return false;
    }

    lastAcceptedRespNonce = resp.nonce;
    lastAcceptedRespStatus = resp.status;
    state = REG_CLIENT_APPROVAL_RECEIVED;

    char name[16];
    snprintf(name, sizeof(name), "POLE_%02u", (unsigned)resp.assignedPoleId);
    poleStorage.saveRegistration(resp.assignedPoleId, name, resp.assignedParent, sessionToken, true);
    if (resp.heartbeatIntervalSec > 0) {
      poleStorage.setHeartbeatIntervalSec(resp.heartbeatIntervalSec);
    }

    // Runtime mesh router identity update without requiring manual reboot!
    uint8_t rank = (resp.assignedParent == MSCB_GATEWAY_NODE_ID) ? 1 : 2;
    for (uint8_t i = 0; i < poleMesh.getNeighborCount(); i++) {
      const MeshNeighbor* nb = poleMesh.getNeighbor(i);
      if (nb && nb->nodeAddress == resp.assignedParent && nb->rank < 254) {
        rank = (uint8_t)(nb->rank + 1);
        break;
      }
    }
    poleMesh.setNodeIdentity(resp.assignedPoleId, resp.assignedParent, rank);

    // Move to WAITING_FOR_HANDSHAKE until Gateway confirms first valid heartbeat (HB_ACK_OK)
    state = REG_CLIENT_WAITING_FOR_HANDSHAKE;
    handshakeStartMs = millis();
    lastError = REG_ERR_NONE;

    // Send immediate first authenticated heartbeat to confirm handshake with Gateway!
    poleHeartbeat.sendHeartbeat();
    Serial.printf("[REG_CLIENT] ✅ APPROVED (ID 0x%04X, Par 0x%04X, Rank %u). Awaiting first HB_ACK handshake...\n",
                  resp.assignedPoleId, resp.assignedParent, rank);
    return true;
  } else if (resp.status == REG_RESP_PENDING_APPROVAL) {
    lastAcceptedRespNonce = resp.nonce;
    lastAcceptedRespStatus = resp.status;
    state = REG_CLIENT_PENDING_APPROVAL;
    poleStorage.setSessionToken(sessionToken);
    poleStorage.setRegistrationState(REG_CLIENT_PENDING_APPROVAL);
    lastError = REG_ERR_NONE;
    retryIntervalMs = 60000; // Stop rapid 12s requests! Back off to 60s
    Serial.println(F("[REG_CLIENT] ⏳ Gateway confirmed: PENDING_ADMIN_APPROVAL. Join retries halted."));
    return true;
  } else if (resp.status == REG_RESP_REJECTED_REVOKED) {
    lastAcceptedRespNonce = resp.nonce;
    lastAcceptedRespStatus = resp.status;
    state = REG_CLIENT_REVOKED_LOCKED;
    lastError = REG_ERR_REVOKED;
    poleStorage.setRevokedLocked("GATEWAY_REVOCATION_RESPONSE");
    poleActuators.emergencyShutdownAll("GATEWAY_REVOCATION_RESPONSE");
    poleActuators.setRelaysLockedDisabled(true);
    poleMesh.resetToUnassignedIdentity(myMac);
    Serial.println(F("[REG_CLIENT] ⛔ Gateway response: PERMANENTLY REVOKED. Relays locked OFF."));
    return false;
  } else {
    // REJECTED! Node cannot become active.
    lastAcceptedRespNonce = resp.nonce;
    lastAcceptedRespStatus = resp.status;
    state = REG_CLIENT_REJECTED;
    lastError = (resp.status == REG_RESP_REJECTED_CAPACITY) ? REG_ERR_PAYLOAD_INVALID : REG_ERR_REJECTED;
    poleStorage.setRegistrationState(REG_CLIENT_REJECTED);
    Serial.println(F("[REG_CLIENT] ⛔ Gateway response: REJECTED. Node cannot become active."));
    return false;
  }
}

void PoleRegistrationClient::service() {
  if (state == REG_CLIENT_REGISTERED || state == REG_CLIENT_REJECTED) {
    return;
  }

  uint32_t now = millis();
  if (state == REG_CLIENT_REVOKED_LOCKED) {
    // Reappearance announce: send dedicated PKT_ALERT_REVOKED_REAPPEAR every 60s
    if (now - lastRequestMs >= 60000UL) {
      lastRequestMs = now;
      PktRevokedReappearPayload alert;
      memset(&alert, 0, sizeof(alert));
      memcpy(alert.macAddress, myMac, 6);
      alert.previousPoleId = poleStorage.getAssignedPoleId();
      alert.uptimeSec = now / 1000UL;
      alert.timestamp = now;
      poleMesh.sendUplinkPacket(PKT_ALERT_REVOKED_REAPPEAR, (const uint8_t*)&alert, sizeof(PktRevokedReappearPayload), PRIO_COMMAND);
      Serial.println(F("[REG_CLIENT] 📢 Sent Revoked Pole Reappearance Alert to Gateway."));
    }
    return;
  }

  if (state == REG_CLIENT_WAITING_FOR_HANDSHAKE || state == REG_CLIENT_APPROVAL_RECEIVED) {
    // Heartbeat sender handles 5s handshake retries; do not re-send JOIN while handshake in progress
    return;
  }

  if (state == REG_CLIENT_UNREGISTERED) {
    beginDiscovery();
    if (state == REG_CLIENT_DISCOVERING_PARENT) {
      return;
    }
  }

  if (state == REG_CLIENT_DISCOVERING_PARENT) {
    if (poleMesh.getNeighborCount() > 0 || (now - lastDiscoveryMs >= 3000UL)) {
      poleMesh.selectBestParents();
      state = REG_CLIENT_READY_TO_REQUEST;
      triggerRegistrationRequest();
    }
    return;
  }

  if (state == REG_CLIENT_REQUEST_TRANSMITTED) {
    state = REG_CLIENT_WAITING_FOR_GATEWAY;
    return;
  }

  // If in PENDING_ADMIN_APPROVAL, do not spam! Check with 60s backoff interval
  uint32_t effectiveInterval = (state == REG_CLIENT_PENDING_APPROVAL) ? 60000UL : retryIntervalMs;
  if (now - lastRequestMs >= effectiveInterval) {
    if (state == REG_CLIENT_WAITING_FOR_GATEWAY) {
      lastError = REG_ERR_TIMEOUT;
      if (retryIntervalMs < 60000) {
        uint32_t nextInt = ((uint32_t)retryIntervalMs * 3UL) / 2UL;
        retryIntervalMs = (nextInt > 60000UL) ? 60000 : (uint16_t)nextInt;
      }
    }
    triggerRegistrationRequest();
  }
}
