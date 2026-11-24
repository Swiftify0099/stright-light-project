#line 1 "C:\\Users\\panka\\OneDrive\\Desktop\\PS\\MSCB_SmartPole_System_R1\\Gateway_ESP32S3\\GwFirmwareManager.h"
/**
 * @file GwFirmwareManager.h
 * @brief MSCB Smart Pole Gateway - Firmware Lifecycle, Secure Boot V2 & Signed Manifest OTA
 * @version 1.1.0
 * 
 * ARCHITECTURAL SPECIFICATION & SECURITY SEPARATION:
 * 
 * 1. NATIVE ESP32-S3 SECURE BOOT V2 (SILICON HARDWARE LEVEL):
 *    - Algorithm: RSA-3072 with RSA-PSS signature padding and SHA-256 digest.
 *    - Key Storage: Up to 3 RSA-3072 public key digests (SHA-256) permanently
 *      burned into eFuse BLOCK4..BLOCK9 (SECURE_BOOT_DIGEST0, 1, 2) and write-protected.
 *    - Silicon Enforcement: Hardware ROM bootloader verifies 2nd stage bootloader,
 *      which verifies signed application image in flash partition before execution.
 *    - Anti-Rollback: Hardware eFuse SECURE_VERSION monotonic counter.
 *    - NOTE: ECDSA P-256 is NOT the native ESP32-S3 Secure Boot V2 eFuse mechanism.
 * 
 * 2. APPLICATION-LEVEL SIGNED MANIFEST (APPLICATION LAYER):
 *    - Transport: HTTPS / Local Maintenance Wi-Fi SoftAP / Isolated UART.
 *    - Explicit Signing Algorithms:
 *        ALG_ECDSA_P256_SHA256 (secp256r1)
 *        ALG_RSA3072_PSS (Hardware-aligned)
 *    - Authenticity: Binary image SHA-256 verified against signed manifest.
 *    - Monotonic Anti-Rollback: Enforces firmwareVersion >= currentVersion.
 *    - Cryptographic Verification: Zero substring checks; strict constant-time signature verification.
 */

#ifndef GW_FIRMWARE_MANAGER_H
#define GW_FIRMWARE_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

// Manifest Signature Algorithms
enum ManifestSigAlgorithm : uint8_t {
  SIG_ALG_INVALID           = 0x00,
  SIG_ALG_ECDSA_P256_SHA256 = 0x01,
  SIG_ALG_RSA3072_PSS       = 0x02,
  SIG_ALG_ED25519           = 0x03
};

// OTA State Machine
enum GwOtaState : uint8_t {
  GW_OTA_IDLE = 0,
  GW_OTA_MANIFEST_VERIFYING,
  GW_OTA_DOWNLOADING,
  GW_OTA_VERIFYING_IMAGE,
  GW_OTA_FLASHING,
  GW_OTA_AWAITING_REBOOT,
  GW_OTA_COMPLETED,
  GW_OTA_FAILED,
  GW_OTA_ROLLBACK_DONE
};

const char* gwOtaStateToString(GwOtaState st);

// Application-Level Signed Firmware Manifest
#pragma pack(push, 1)
struct GwFirmwareManifest {
  char     magic[4];               // "MFST"
  char     targetHardware[16];     // "ESP32-S3-N16R8"
  char     hardwareRevision[12];   // e.g. "REV_1.0"
  uint16_t firmwareVersion;        // Monotonic version (e.g. 11)
  uint16_t minGatewayVersion;      // Anti-rollback floor
  uint32_t binarySize;             // Expected size in bytes
  char     sha256Hex[65];          // 64-char hex SHA-256 hash + null
  uint8_t  sigAlgorithm;           // ManifestSigAlgorithm
  char     signatureHex[130];      // Hex signature (e.g. 128 chars for ECDSA P-256 r||s)
  char     downloadUrl[128];       // Approved HTTPS URL
};
#pragma pack(pop)

// Audit Log Record
struct GwOtaAuditRecord {
  uint32_t timestamp;
  uint16_t versionAttempted;
  uint8_t  result;                 // 0 = Success, 1 = Error
  char     failureReason[48];
};

#define APPROVED_OTA_URL_PREFIX "https://firmware.mscb-smartpole.gov.in/ota/"
#define CURRENT_GATEWAY_HW_TARGET "ESP32-S3-N16R8"
#define CURRENT_GATEWAY_HW_REV    "REV_1.0"

class GwFirmwareManager {
public:
  GwFirmwareManager();

  void init(uint16_t currentFwVersion = 10);
  
  // Manifest Verification
  bool verifyManifest(const GwFirmwareManifest &manifest, String &outError);
  
  // Asymmetric Signature Verification (Strict cryptographic check, no substring matching)
  bool verifyDigitalSignature(const char *sha256Hex, const char *sigHex, uint8_t algorithm);

  // Set / Pin Trusted Public Key
  void setPinnedPublicKey(const uint8_t *keyBytes, size_t keyLen);

  // Execution
  bool startHttpsOta(const GwFirmwareManifest &manifest, const char* adminId, String &outError);
  bool verifyImageSha256(const uint8_t *imageBuffer, size_t imageLen, const char *expectedSha256Hex);
  bool applyUpdate(const uint8_t *imageBuffer, size_t imageLen, String &outError);

  // Post-Update Self-Test Validation & Boot Commitment
  bool performBootSelfTest(bool psramOk, bool radioOk);
  void markAppValid();
  void triggerRollback();

  GwOtaState getState() const { return state; }
  uint8_t getProgress() const { return progressPct; }
  uint16_t getCurrentVersion() const { return currentVersion; }

private:
  GwOtaState         state;
  uint8_t            progressPct;
  uint32_t           downloadedBytes;
  uint32_t           totalBytes;
  uint16_t           currentVersion;
  GwFirmwareManifest currentManifest;
  GwOtaAuditRecord   lastAudit;

  uint8_t            pinnedPublicKey[65]; // 65-byte uncompressed public key for P-256
  bool               hasPinnedKey;
};

extern GwFirmwareManager gwFwMgr;

#endif // GW_FIRMWARE_MANAGER_H
