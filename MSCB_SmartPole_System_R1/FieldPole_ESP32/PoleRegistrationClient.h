/**
 * @file PoleRegistrationClient.h
 * @brief MSCB Smart Pole Node - End-to-End Discovery & Registration Client
 * @version 1.0.0
 * 
 * Enforces:
 * - Broadcasting JOIN / Registration requests with MAC identity.
 * - Secure persistence of assigned Logical Pole ID in NVS upon approval.
 * - Strict prohibition: Rejected or unauthorized nodes cannot become active.
 */

#ifndef POLE_REGISTRATION_CLIENT_H
#define POLE_REGISTRATION_CLIENT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <esp_system.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "PoleConfig.h"
#include "PoleStorageManager.h"
#include "PoleMeshRouter.h"
#if __has_include("../MSCBShared/Protocol.h")
  #include "../MSCBShared/Protocol.h"
#elif __has_include(<Protocol.h>)
  #include <Protocol.h>
#else
  #include "Protocol.h"
#endif

class PoleRegistrationClient {
public:
  PoleRegistrationClient();

  bool begin();
  void service(); // Request retry ticker

  bool beginDiscovery();
  bool handleRegisterResponse(const PktRegisterResponsePayload &resp);
  bool triggerRegistrationRequest();
  bool confirmHandshakeComplete();

  NodeRegClientState getState() const { return state; }
  NodeRegFailReason  getLastError() const { return lastError; }
  const uint8_t* getMacAddress() const { return myMac; }
  void setMacAddress(const uint8_t mac[6]) { if (mac) memcpy(myMac, mac, 6); }
  bool isRegistered() const { return (state == REG_CLIENT_REGISTERED || state == REG_CLIENT_WAITING_FOR_HANDSHAKE); }
  bool isHandshakePending() const { return (state == REG_CLIENT_WAITING_FOR_HANDSHAKE || state == REG_CLIENT_APPROVAL_RECEIVED); }
  bool isRevoked() const { return (state == REG_CLIENT_REVOKED_LOCKED); }
  uint16_t getAssignedId() const { return poleStorage.getAssignedPoleId(); }
  uint32_t getSessionToken() const { return sessionToken; }
  uint32_t getLastRequestNonce() const { return lastReqNonce; }
  uint32_t getLastRequestMs() const { return lastRequestMs; }
  uint16_t getRetryIntervalMs() const { return retryIntervalMs; }
  void setRetryIntervalMs(uint16_t ms) { retryIntervalMs = ms; }
  void clearRegistration();
  void setRevokedLockedState() {
    state = REG_CLIENT_REVOKED_LOCKED;
    lastError = REG_ERR_REVOKED;
  }
  void setRegistrationError(NodeRegFailReason reason) {
    state = REG_CLIENT_REGISTRATION_ERROR;
    lastError = reason;
  }

private:
  void readHardwareMac();
  uint32_t generateSessionToken();

  NodeRegClientState state;
  NodeRegFailReason  lastError;
  uint8_t            myMac[6];
  uint32_t           sessionToken;
  uint32_t           reqNonceCounter;
  uint32_t           lastReqNonce;
  uint32_t           lastAcceptedRespNonce;
  uint8_t            lastAcceptedRespStatus;
  uint32_t           lastRequestMs;
  uint32_t           lastDiscoveryMs;
  uint32_t           sessionStartMs;
  uint32_t           handshakeStartMs;
  bool               hasPendingSession;
  uint16_t           retryIntervalMs;
};

extern PoleRegistrationClient poleRegClient;

#endif // POLE_REGISTRATION_CLIENT_H

