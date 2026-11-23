/**
 * @file PoleFirmwareManager.h
 * @brief MSCB Smart Pole Node - Cryptographic Local Maintenance OTA & Signed Manifest
 * @version 1.0.0
 * 
 * Enforces:
 * - Authenticated signed manifest with pinned trusted public key (ECDSA P-256 / SHA-256).
 * - Zero substring checks; strict cryptographic verification.
 * - Hardware target compatibility check ("ESP32-WROOM-32").
 * - Monotonic anti-rollback version enforcement.
 * - Actual ESP-IDF OTA partition writing and boot commitment.
 */

#ifndef POLE_FIRMWARE_MANAGER_H
#define POLE_FIRMWARE_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "PoleConfig.h"
#include "../Gateway_ESP32S3/GwFirmwareManager.h" // Shared manifest schema

#define CURRENT_POLE_HW_TARGET "ESP32-WROOM-32"
#define CURRENT_POLE_HW_REV    "REV_1.0"

class PoleFirmwareManager {
public:
  PoleFirmwareManager();

  void init(uint16_t currentFwVersion = 10);

  // Manifest & Signature Verification
  bool verifyManifest(const GwFirmwareManifest &manifest, String &outError);
  bool verifyDigitalSignature(const char *sha256Hex, const char *sigHex, uint8_t algorithm);
  void setPinnedPublicKey(const uint8_t *keyBytes, size_t keyLen);

  // Image SHA-256 and Partition Writing
  bool verifyImageSha256(const uint8_t *imageBuffer, size_t imageLen, const char *expectedSha256Hex);
  bool applyUpdate(const uint8_t *imageBuffer, size_t imageLen, String &outError);

  bool performBootSelfTest(bool metrologyHardwareOk, bool radioOk);
  void markAppValid();
  void triggerRollback();

  GwOtaState getState() const { return state; }
  uint16_t getCurrentVersion() const { return currentVersion; }

private:
  GwOtaState         state;
  uint16_t           currentVersion;
  GwFirmwareManifest currentManifest;
  uint8_t            pinnedPublicKey[65];
  bool               hasPinnedKey;
};

extern PoleFirmwareManager poleFwMgr;

#endif // POLE_FIRMWARE_MANAGER_H
