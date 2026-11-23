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

  bool handleRegisterResponse(const PktRegisterResponsePayload &resp);
  void triggerRegistrationRequest();

  NodeRegClientState getState() const { return state; }
  const uint8_t* getMacAddress() const { return myMac; }
  void setMacAddress(const uint8_t mac[6]) { if (mac) memcpy(myMac, mac, 6); }
  bool isRegistered() const { return (state == REG_CLIENT_REGISTERED); }
  bool isRevoked() const { return (state == REG_CLIENT_REVOKED_LOCKED); }
  uint16_t getAssignedId() const { return poleStorage.getAssignedPoleId(); }

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

