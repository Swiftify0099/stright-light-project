/*
 * =====================================================================
 *  GWFIRMWAREMANAGER.H — GATEWAY FIRMWARE LIFECYCLE & HTTPS OTA
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 *  Dual OTA Partitions, Signed Manifest, SHA-256, Anti-Rollback
 * =====================================================================
 */

#ifndef GW_FIRMWARE_MANAGER_H
#define GW_FIRMWARE_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <WiFiClientSecure.h>
  #include <esp_ota_ops.h>
  #include <esp_partition.h>
#else
  #include "Arduino.h"
#endif

// OTA Lifecycle States
enum GwOtaState {
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

// Approved HTTPS Domain Prefix
#define APPROVED_OTA_URL_PREFIX "https://ota.mscb-smartpole.internal/"

struct GwFirmwareManifest {
  char     manifestVersion[16];    // "1.0"
  uint16_t firmwareVersion;        // e.g. 11
  char     firmwareVersionStr[16]; // e.g. "GW_V11"
  char     targetHardware[24];     // "ESP32-S3-N16R8"
  uint16_t minGatewayVersion;      // Anti-rollback threshold
  char     sha256Hex[65];          // 64-character SHA-256 hash
  char     signatureHex[129];      // Digital signature hex
  uint32_t binarySize;             // Expected size in bytes
  char     downloadUrl[128];       // Strict HTTPS download URL
  char     buildTimestamp[32];     // Build date/time
  bool     valid;
};

struct GwOtaAuditRecord {
  char     txId[32];
  char     adminId[24];
  uint16_t prevVersion;
  uint16_t targetVersion;
  uint32_t startTime;
  uint32_t finishTime;
  uint8_t  result; // 0=SUCCESS, 1=FAIL, 2=ROLLBACK
  char     failureReason[32];
};

class GwFirmwareManager {
public:
  GwFirmwareManager();

  void init();

  // Step 1: Manifest Verification & Compatibility Check
  bool verifyManifest(const GwFirmwareManifest &manifest, String &outError);

  // Step 2: Download, Flash & Verify OTA
  bool startHttpsOta(const GwFirmwareManifest &manifest, const char* adminId, String &outError);

  // Step 3: Bootloader Rollback & App Confirmation
  void markAppValid();
  bool rollbackToPreviousApp();

  // Status & Telemetry
  GwOtaState getOtaState() const;
  uint8_t    getProgressPercent() const;
  String     getOtaStatusReport() const;
  const GwOtaAuditRecord& getLastAudit() const;

  // Silicon & Partition Diagnostics
  String getPartitionSchemeReport() const;

private:
  GwOtaState         state;
  uint8_t            progressPct;
  GwFirmwareManifest currentManifest;
  GwOtaAuditRecord   lastAudit;
  uint32_t           downloadedBytes;
  uint32_t           totalBytes;

  uint8_t            publicKey[32];

  bool verifySha256(const uint8_t *data, size_t len, const char *expectedSha256Hex);
  bool verifyDigitalSignature(const char *sha256Hex, const char *sigHex);
};

extern GwFirmwareManager gwFwMgr;

#endif // GW_FIRMWARE_MANAGER_H
