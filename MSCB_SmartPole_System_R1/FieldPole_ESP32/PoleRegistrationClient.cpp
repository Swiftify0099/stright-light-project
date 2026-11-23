/**
 * @file PoleRegistrationClient.cpp
 * @brief MSCB Smart Pole Node - End-to-End Discovery & Registration Client Implementation
 * @version 1.0.0
 */

#include "PoleRegistrationClient.h"
#include <string.h>

#if defined(ARDUINO) && defined(ESP32)
  #include <esp_mac.h>
#endif

PoleRegistrationClient poleRegClient;

PoleRegistrationClient::PoleRegistrationClient() :
  state(REG_CLIENT_UNREGISTERED),
  sessionToken(0),
  lastRequestMs(0),
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

bool PoleRegistrationClient::begin() {
  readHardwareMac();
  sessionToken = (uint32_t)millis() + 0xA5A50000UL;

  if (poleStorage.isRegistered()) {
    state = REG_CLIENT_REGISTERED;
  } else {
    state = REG_CLIENT_UNREGISTERED;
  }
  return true;
}

void PoleRegistrationClient::triggerRegistrationRequest() {
  if (state == REG_CLIENT_REGISTERED) return;

  PktRegisterRequestPayload req;
  memcpy(req.macAddress, myMac, 6);
  req.nodeType = 1; // 1 = FIELD_POLE
  req.firmwareVersion = (MSCB_FW_VERSION_MAJOR * 10) + MSCB_FW_VERSION_MINOR;
  req.sessionToken = sessionToken;
  req.candidateParent1 = poleMesh.getPrimaryParent();
  req.candidateParent2 = poleMesh.getBackupParent();
  req.rssiToParent1 = -75;
  req.snrToParent1 = 8;
  req.nonce = millis();
  memset(req.authTag, 0, sizeof(req.authTag));

  poleMesh.sendUplinkPacket(PKT_REGISTER_REQUEST, (const uint8_t*)&req, sizeof(PktRegisterRequestPayload), PRIO_COMMAND);

  state = REG_CLIENT_REQUEST_SENT;
  lastRequestMs = millis();
}

bool PoleRegistrationClient::handleRegisterResponse(const PktRegisterResponsePayload &resp) {
  // Validate MAC matches this device
  if (memcmp(resp.macAddress, myMac, 6) != 0) {
    return false;
  }

  if (resp.status == REG_RESP_APPROVED) {
    char name[16];
    snprintf(name, sizeof(name), "POLE_%02d", resp.assignedPoleId);
    poleStorage.saveRegistration(resp.assignedPoleId, name, resp.assignedParent);
    if (resp.heartbeatIntervalSec > 0) {
      poleStorage.setHeartbeatIntervalSec(resp.heartbeatIntervalSec);
    }
    state = REG_CLIENT_REGISTERED;
    return true;
  } else if (resp.status == REG_RESP_PENDING_APPROVAL) {
    state = REG_CLIENT_PENDING;
    return true;
  } else {
    // REJECTED! Node cannot become active.
    state = REG_CLIENT_REJECTED;
    return false;
  }
}

void PoleRegistrationClient::service() {
  if (state == REG_CLIENT_REGISTERED || state == REG_CLIENT_REJECTED) {
    return;
  }

  uint32_t now = millis();
  if (now - lastRequestMs >= retryIntervalMs) {
    triggerRegistrationRequest();
  }
}
