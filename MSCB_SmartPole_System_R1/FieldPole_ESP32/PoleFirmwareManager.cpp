/**
 * @file PoleFirmwareManager.cpp
 * @brief MSCB Smart Pole Node - Cryptographic Local Maintenance OTA Implementation
 * @version 1.0.0
 */

#include "PoleFirmwareManager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#if defined(ARDUINO) && defined(ESP32)
  #include <mbedtls/sha256.h>
  #include <mbedtls/ecdsa.h>
  #include <mbedtls/ecp.h>
  #include <mbedtls/bignum.h>
  #include <esp_ota_ops.h>
  #include <esp_partition.h>
#endif

PoleFirmwareManager poleFwMgr;

PoleFirmwareManager::PoleFirmwareManager() :
  state(GW_OTA_IDLE), currentVersion(10), hasPinnedKey(false)
{
  memset(&currentManifest, 0, sizeof(GwFirmwareManifest));
  memset(pinnedPublicKey, 0, sizeof(pinnedPublicKey));
}

void PoleFirmwareManager::init(uint16_t currentFwVersion) {
  state = GW_OTA_IDLE;
  currentVersion = currentFwVersion;
}

void PoleFirmwareManager::setPinnedPublicKey(const uint8_t *keyBytes, size_t keyLen) {
  if (keyBytes && keyLen <= sizeof(pinnedPublicKey)) {
    memcpy(pinnedPublicKey, keyBytes, keyLen);
    hasPinnedKey = true;
  }
}

static bool isValidHex(const char *str, size_t expectedLen) {
  if (!str) return false;
  if (strlen(str) != expectedLen) return false;
  for (size_t i = 0; i < expectedLen; i++) {
    if (!isxdigit((unsigned char)str[i])) return false;
  }
  return true;
}

static int constantTimeCompare(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t result = 0;
  for (size_t i = 0; i < len; i++) {
    result |= a[i] ^ b[i];
  }
  return result;
}

bool PoleFirmwareManager::verifyDigitalSignature(const char *sha256Hex, const char *sigHex, uint8_t algorithm) {
  if (!sha256Hex || !sigHex) return false;
  if (!isValidHex(sha256Hex, 64)) return false;
  if (algorithm != SIG_ALG_ECDSA_P256_SHA256) return false;
  if (strlen(sigHex) != 128) return false;
  if (!isValidHex(sigHex, 128)) return false;

#if defined(ARDUINO) && defined(ESP32)
  if (!hasPinnedKey) return false;
  mbedtls_ecp_group grp;
  mbedtls_ecp_point Q;
  mbedtls_mpi r, s;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&Q);
  mbedtls_mpi_init(&r);
  mbedtls_mpi_init(&s);

  int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1);
  if (ret != 0) {
    mbedtls_ecp_group_free(&grp);
    return false;
  }

  ret = mbedtls_ecp_point_read_binary(&grp, &Q, pinnedPublicKey, 65);
  if (ret != 0) {
    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    return false;
  }

  uint8_t sigBytes[64];
  for (int i = 0; i < 64; i++) {
    char bs[3] = { sigHex[i*2], sigHex[i*2 + 1], '\0' };
    sigBytes[i] = (uint8_t)strtoul(bs, NULL, 16);
  }
  mbedtls_mpi_read_binary(&r, sigBytes, 32);
  mbedtls_mpi_read_binary(&s, sigBytes + 32, 32);

  uint8_t hashBytes[32];
  for (int i = 0; i < 32; i++) {
    char bs[3] = { sha256Hex[i*2], sha256Hex[i*2 + 1], '\0' };
    hashBytes[i] = (uint8_t)strtoul(bs, NULL, 16);
  }

  ret = mbedtls_ecdsa_verify(&grp, hashBytes, 32, &Q, &r, &s);

  mbedtls_ecp_group_free(&grp);
  mbedtls_ecp_point_free(&Q);
  mbedtls_mpi_free(&r);
  mbedtls_mpi_free(&s);

  return (ret == 0);
#else
  if (!hasPinnedKey) return false;
  uint8_t hashBytes[32];
  for (int i = 0; i < 32; i++) {
    char byteStr[3] = { sha256Hex[i*2], sha256Hex[i*2 + 1], '\0' };
    hashBytes[i] = (uint8_t)strtoul(byteStr, NULL, 16);
  }
  uint8_t expectedSigPrefix[32];
  for (int i = 0; i < 32; i++) {
    expectedSigPrefix[i] = hashBytes[i] ^ pinnedPublicKey[i % 32] ^ 0xA5;
  }
  uint8_t sigBytes[64];
  for (int i = 0; i < 64; i++) {
    char byteStr[3] = { sigHex[i*2], sigHex[i*2 + 1], '\0' };
    sigBytes[i] = (uint8_t)strtoul(byteStr, NULL, 16);
  }
  return (constantTimeCompare(sigBytes, expectedSigPrefix, 32) == 0);
#endif
}

bool PoleFirmwareManager::verifyManifest(const GwFirmwareManifest &manifest, String &outError) {
  outError = "";

  if (strncmp(manifest.magic, "MFST", 4) != 0) {
    outError = "INVALID_MANIFEST_MAGIC";
    return false;
  }

  if (strcmp(manifest.targetHardware, CURRENT_POLE_HW_TARGET) != 0) {
    outError = "HARDWARE_MISMATCH_EXPECTED_ESP32";
    return false;
  }

  if (strcmp(manifest.hardwareRevision, CURRENT_POLE_HW_REV) != 0) {
    outError = "HARDWARE_REVISION_INCOMPATIBLE";
    return false;
  }

  if (manifest.firmwareVersion < currentVersion) {
    outError = "ANTI_ROLLBACK_VIOLATION_VERSION_TOO_OLD";
    return false;
  }

  if (!verifyDigitalSignature(manifest.sha256Hex, manifest.signatureHex, manifest.sigAlgorithm)) {
    outError = "CRYPTOGRAPHIC_SIGNATURE_INVALID";
    return false;
  }

  currentManifest = manifest;
  return true;
}

bool PoleFirmwareManager::verifyImageSha256(const uint8_t *imageBuffer, size_t imageLen, const char *expectedSha256Hex) {
  if (!imageBuffer || imageLen == 0 || !expectedSha256Hex) return false;
  if (!isValidHex(expectedSha256Hex, 64)) return false;

  uint8_t hash[32];
#if defined(ARDUINO) && defined(ESP32)
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, imageBuffer, imageLen);
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
#else
  memset(hash, 0, 32);
  for (size_t i = 0; i < imageLen; i++) {
    hash[i % 32] ^= imageBuffer[i];
    hash[(i + 1) % 32] = (hash[(i + 1) % 32] + imageBuffer[i] * 33) & 0xFF;
  }
#endif

  char calculatedHex[65];
  for (int i = 0; i < 32; i++) {
    snprintf(&calculatedHex[i*2], 3, "%02x", hash[i]);
  }
  calculatedHex[64] = '\0';

  return (constantTimeCompare((const uint8_t*)calculatedHex, (const uint8_t*)expectedSha256Hex, 64) == 0);
}

bool PoleFirmwareManager::applyUpdate(const uint8_t *imageBuffer, size_t imageLen, String &outError) {
  state = GW_OTA_VERIFYING_IMAGE;

  if (imageLen != currentManifest.binarySize) {
    state = GW_OTA_FAILED;
    outError = "IMAGE_SIZE_MISMATCH_WITH_MANIFEST";
    return false;
  }

  if (!verifyImageSha256(imageBuffer, imageLen, currentManifest.sha256Hex)) {
    state = GW_OTA_FAILED;
    outError = "IMAGE_SHA256_HASH_VERIFICATION_FAILED";
    return false;
  }

  state = GW_OTA_FLASHING;
#if defined(ARDUINO) && defined(ESP32)
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    outError = "OTA_PARTITION_NOT_FOUND";
    state = GW_OTA_FAILED;
    return false;
  }
  esp_ota_handle_t ota_handle;
  esp_err_t err = esp_ota_begin(update_partition, imageLen, &ota_handle);
  if (err != ESP_OK) {
    outError = "ESP_OTA_BEGIN_FAILED";
    state = GW_OTA_FAILED;
    return false;
  }
  err = esp_ota_write(ota_handle, imageBuffer, imageLen);
  if (err != ESP_OK) {
    esp_ota_abort(ota_handle);
    outError = "ESP_OTA_WRITE_FAILED";
    state = GW_OTA_FAILED;
    return false;
  }
  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    outError = "ESP_OTA_END_FAILED";
    state = GW_OTA_FAILED;
    return false;
  }
  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    outError = "ESP_OTA_SET_BOOT_PARTITION_FAILED";
    state = GW_OTA_FAILED;
    return false;
  }
#endif
  state = GW_OTA_AWAITING_REBOOT;
  return true;
}

bool PoleFirmwareManager::performBootSelfTest(bool metrologyHardwareOk, bool radioOk) {
#if defined(ARDUINO) && defined(ESP32)
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      Serial.println(F("[POLE][OTA] New OTA image booted. Executing post-update health self-tests..."));
      if (radioOk) {
        Serial.println(F("[POLE][OTA] ✅ Self-tests PASSED (LoRa healthy). Marking application valid."));
        markAppValid();
        return true;
      } else {
        Serial.println(F("[POLE][OTA] ❌ Self-tests FAILED! Hardware check failure. Triggering automatic rollback..."));
        triggerRollback();
        return false;
      }
    }
  }
#else
  if (!radioOk) {
    triggerRollback();
    return false;
  }
#endif
  return true;
}

void PoleFirmwareManager::markAppValid() {
#if defined(ARDUINO) && defined(ESP32)
  esp_ota_mark_app_valid_cancel_rollback();
#endif
  state = GW_OTA_COMPLETED;
}

void PoleFirmwareManager::triggerRollback() {
#if defined(ARDUINO) && defined(ESP32)
  esp_ota_mark_app_invalid_rollback_and_reboot();
#endif
  state = GW_OTA_ROLLBACK_DONE;
}
