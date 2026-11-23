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
#include "../MSCBShared/Protocol.h"

enum NodeRegClientState : uint8_t {
  REG_CLIENT_UNREGISTERED = 0,
  REG_CLIENT_REQUEST_SENT = 1,
  REG_CLIENT_PENDING      = 2,
  REG_CLIENT_REGISTERED   = 3,
  REG_CLIENT_REJECTED     = 4
};

inline const char* regClientStateToString(NodeRegClientState st) {
  switch (st) {
    case REG_CLIENT_UNREGISTERED: return "UNREGISTERED";
    case REG_CLIENT_REQUEST_SENT: return "JOIN_REQUEST_SENT";
    case REG_CLIENT_PENDING:      return "PENDING_ADMIN_APPROVAL";
    case REG_CLIENT_REGISTERED:   return "REGISTERED_ACTIVE";
    case REG_CLIENT_REJECTED:     return "REJECTED_UNAUTHORIZED";
    default:                      return "UNKNOWN";
  }
}

class PoleRegistrationClient {
public:
  PoleRegistrationClient();

  bool begin();
  void service(); // Request retry ticker

  bool handleRegisterResponse(const PktRegisterResponsePayload &resp);
  void triggerRegistrationRequest();

  NodeRegClientState getState() const { return state; }
  const uint8_t* getMacAddress() const { return myMac; }

private:
  void readHardwareMac();

  NodeRegClientState state;
  uint8_t            myMac[6];
  uint32_t           sessionToken;
  uint32_t           lastRequestMs;
  uint16_t           retryIntervalMs;
};

extern PoleRegistrationClient poleRegClient;

#endif // POLE_REGISTRATION_CLIENT_H

