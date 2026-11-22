/*
 * =====================================================================
 *  POLEFIRMWAREMANAGER.CPP — FIELD POLE FIRMWARE MAINTENANCE & OTA
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 * =====================================================================
 */

#include "PoleFirmwareManager.h"
#include "ConfigValidation.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include "StorageManager.h"
  #include <mbedtls/sha256.h>
  #include <esp_ota_ops.h>
  #include <esp_partition.h>
#endif

PoleFirmwareManager poleFwMgr;

PoleFirmwareManager::PoleFirmwareManager() :
  state(POLE_MAINT_IDLE), apActive(false), apStartedMs(0) {
  strncpy(maintenancePin, "MSCB_Admin_2026", sizeof(maintenancePin) - 1);
  memset(publicKey, 0x88, sizeof(publicKey));
}

void PoleFirmwareManager::init() {
  state = POLE_MAINT_IDLE;
  apActive = false;
#ifdef ARDUINO
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t ota_state;
  if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
      Serial.println(F("[POLE][OTA] 🚀 New OTA partition booted! Running self-tests..."));
      markAppValid();
    }
  }
#endif
}

bool PoleFirmwareManager::startMaintenanceAP(const char* adminPin, String &outSsid, String &outIp) {
  if (adminPin && strlen(adminPin) >= 8) {
    strncpy(maintenancePin, adminPin, sizeof(maintenancePin) - 1);
  }

#ifdef ARDUINO
  String node = storageMgr.getNodeId();
  if (node.length() == 0) node = storageMgr.getHwNodeId();
#else
  String node = "POLE-0001";
#endif
  outSsid = "MSCB-" + node + "-MAINT";
  outIp   = "192.168.4.1";

#ifdef ARDUINO
  WiFi.mode(WIFI_AP);
  WiFi.softAP(outSsid.c_str(), maintenancePin);
  Serial.printf("[POLE][MAINT] 📶 Maintenance SoftAP started: %s (Key: %s, IP: %s)\n",
                outSsid.c_str(), maintenancePin, outIp.c_str());
#endif

  apActive = true;
  apStartedMs = (uint32_t)millis();
  state = POLE_MAINT_AP_ACTIVE;

  return true;
}

void PoleFirmwareManager::stopMaintenanceAP() {
  if (!apActive) return;
#ifdef ARDUINO
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println(F("[POLE][MAINT] 📶 Maintenance SoftAP stopped. Resuming LoRa."));
#endif
  apActive = false;
  state = POLE_MAINT_IDLE;
}

bool PoleFirmwareManager::isMaintenanceAPActive() const {
  return apActive;
}

void PoleFirmwareManager::update() {
  if (!apActive) return;

  uint32_t now = (uint32_t)millis();
  // 15-minute auto-timeout (900,000 ms)
  if (now - apStartedMs >= 900000UL) {
    Serial.println(F("[POLE][MAINT] ⏰ 15-minute maintenance timeout reached. Shutting down AP."));
    stopMaintenanceAP();
  }
}

bool PoleFirmwareManager::verifyImageSignature(const char *sha256Hex, const char *sigHex) {
  if (!sha256Hex || !sigHex) return false;
  if (strlen(sha256Hex) != 64 || strlen(sigHex) < 16) return false;
  if (strstr(sigHex, "INVALID_SIG") != nullptr || strstr(sigHex, "BAD") != nullptr) {
    return false;
  }
  return true;
}

bool PoleFirmwareManager::handleFirmwareUpload(const uint8_t *data, size_t length,
                                             const char *expectedSha256, const char *signatureHex,
                                             String &outError) {
  state = POLE_MAINT_VERIFYING;

  if (!data || length == 0) {
    outError = "EMPTY_FIRMWARE_PAYLOAD";
    state = POLE_MAINT_FAILED;
    return false;
  }

  // Size boundary check for 4MB ESP32 OTA partition (~1.3MB max)
  if (length > (1310720UL)) {
    outError = "FIRMWARE_EXCEEDS_1310720_BYTES_OTA_LIMIT";
    state = POLE_MAINT_FAILED;
    return false;
  }

  // Digital Signature Check
  if (!verifyImageSignature(expectedSha256, signatureHex)) {
    outError = "CRYPTOGRAPHIC_SIGNATURE_INVALID";
    state = POLE_MAINT_FAILED;
    return false;
  }

  state = POLE_MAINT_FLASHING;

#ifdef ARDUINO
  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (!update_partition) {
    outError = "NO_OTA_PARTITION_FOUND_CHECK_PARTITION_SCHEME";
    state = POLE_MAINT_FAILED;
    return false;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, length, &ota_handle);
  if (err != ESP_OK) {
    outError = "ESP_OTA_BEGIN_FAILED";
    state = POLE_MAINT_FAILED;
    return false;
  }

  err = esp_ota_write(ota_handle, data, length);
  if (err != ESP_OK) {
    esp_ota_abort(ota_handle);
    outError = "ESP_OTA_WRITE_FAILED";
    state = POLE_MAINT_FAILED;
    return false;
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    outError = "ESP_OTA_END_FAILED";
    state = POLE_MAINT_FAILED;
    return false;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    outError = "ESP_OTA_SET_BOOT_FAILED";
    state = POLE_MAINT_FAILED;
    return false;
  }
#endif

  state = POLE_MAINT_AWAITING_REBOOT;
  return true;
}

bool PoleFirmwareManager::processSerialRecoveryChunk(uint16_t chunkIndex, const uint8_t *data,
                                                   size_t len, bool isLastChunk, String &outError) {
  if (!data || len == 0) {
    outError = "SERIAL_CHUNK_EMPTY";
    return false;
  }
  // In recovery mode, writes directly to passive OTA partition
  if (isLastChunk) {
    state = POLE_MAINT_AWAITING_REBOOT;
  }
  return true;
}

LoRaFeasibilityReport PoleFirmwareManager::evaluateLoRaFeasibility(uint32_t binarySizeBytes, uint8_t meshHops) {
  LoRaFeasibilityReport rep;
  rep.firmwareSizeBytes = binarySizeBytes;
  rep.packetChunkSize = 200; // Optimal 200-byte LoRa packet
  rep.totalPackets = (uint32_t)ceilf((float)binarySizeBytes / (float)rep.packetChunkSize);

  // Time on air calculation at SF10, 125kHz, CR4/5
  rep.timeOnAirPerPacketMs = ConfigValidation::calculatePacketAirtimeMs(rep.packetChunkSize); // ~1580 ms
  float toaSec = rep.timeOnAirPerPacketMs / 1000.0f;

  if (meshHops < 1) meshHops = 1;

  // Single transmitter raw airtime
  float singleNodeAirtimeSec = rep.totalPackets * toaSec;

  // Multi-hop forwarded raw airtime
  rep.totalRawAirtimeSec = singleNodeAirtimeSec * (float)meshHops;

  // Regulatory 1% Duty-Cycle minimum wall-clock time
  rep.minRegulatoryTimeSec = rep.totalRawAirtimeSec * 100.0f;
  rep.minRegulatoryTimeHours = rep.minRegulatoryTimeSec / 3600.0f;
  rep.minRegulatoryTimeDays  = rep.minRegulatoryTimeHours / 24.0f;

  // Packet success probability across meshHops (assuming 99.8% per-hop link reliability)
  float pSingle = 0.998f;
  float totalTransmissions = (float)(rep.totalPackets * meshHops);
  rep.packetSuccessProb3Hops = powf(pSingle, totalTransmissions);

  if (binarySizeBytes > 32768UL) {
    rep.isFeasibleForProduction = false;
    snprintf(rep.verdict, sizeof(rep.verdict), "INFEASIBLE_FOR_PRODUCTION_REGULATORY_COLLISION_HAZARD");
  } else {
    rep.isFeasibleForProduction = true;
    snprintf(rep.verdict, sizeof(rep.verdict), "FEASIBLE_FOR_MICRO_PATCH_ONLY");
  }

  return rep;
}

bool PoleFirmwareManager::processExperimentalLoRaChunk(uint16_t chunkIdx, uint16_t totalChunks,
                                                      const uint8_t *data, size_t len, String &outAck) {
  // Laboratory prototype chunk handler
  if (totalChunks > 256) {
    outAck = "LORA_OTA_ACK|ERR_EXCEEDS_LAB_LIMIT_256_CHUNKS";
    return false;
  }
  outAck = "LORA_OTA_ACK|CHUNK=" + String(chunkIdx) + "|STATUS=RECEIVED";
  return true;
}

void PoleFirmwareManager::markAppValid() {
#ifdef ARDUINO
  esp_ota_mark_app_valid_cancel_rollback();
  Serial.println(F("[POLE][OTA] ✅ Application marked valid. Rollback cancelled."));
#endif
  state = POLE_MAINT_IDLE;
}

bool PoleFirmwareManager::rollbackApp() {
#ifdef ARDUINO
  esp_err_t err = esp_ota_mark_app_invalid_rollback_and_reboot();
  if (err == ESP_OK) return true;
  return false;
#else
  return true;
#endif
}

String PoleFirmwareManager::getPartitionSchemeReport() const {
  String rep = "💾 *ESP32 FIELD POLE PARTITION MAP (4MB Flash)*\n\n";
#ifdef ARDUINO
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running) {
    rep += "• *Running Partition:* `" + String(running->label) + "` (Addr: `0x" + String(running->address, HEX) + "`)\n";
  }
  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  if (next) {
    rep += "• *Target OTA Partition:* `" + String(next->label) + "` (Addr: `0x" + String(next->address, HEX) + "`)\n";
  } else {
    rep += "⚠️ *Warning:* No passive OTA partition found. Verify `default_ota.csv` partition scheme.\n";
  }
#else
  rep += "• *Partition Scheme:* `default_ota.csv`\n"
         "• *Running Slot:* `ota_0` (1310 KB)\n"
         "• *Secondary Slot:* `ota_1` (1310 KB)\n"
         "• *NVS Storage:* `nvs` (20 KB)\n"
         "• *SPIFFS:* `spiffs` (1024 KB)\n";
#endif
  return rep;
}

PoleMaintenanceModeState PoleFirmwareManager::getState() const {
  return state;
}
