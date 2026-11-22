/*
 * =====================================================================
 *  POLECONFIGMANAGER.CPP — FIELD POLE LOCAL CONFIGURATION MANAGER
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 * =====================================================================
 */

#include "PoleConfigManager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include "CalibrationManager.h"
  #include "FeederManager.h"
  #include "StorageManager.h"
#endif

PoleConfigManager poleConfigMgr;

PoleConfigManager::PoleConfigManager() {
  memset(deviceKey, 0x5A, sizeof(deviceKey)); // Derived device key standard
  initDefaults();
}

void PoleConfigManager::initDefaults() {
  cfg.currentVersion = 1;
  cfg.pendingVersion = 0;
  cfg.lastSuccessfulVersion = 1;
  cfg.lastUpdateTime = 0;
  cfg.lastTxId[0] = '\0';

  cfg.networkId = 0x4D53;
  cfg.hopLimit = 6;
  cfg.discoveryIntervalSec = 30;
  cfg.heartbeatIntervalSec = 30;
  cfg.retryCount = 3;
  cfg.ackTimeoutMs = 3000;

  strncpy(cfg.poleName, "Pole 1", sizeof(cfg.poleName) - 1);
  strncpy(cfg.poleArea, "Sector 1", sizeof(cfg.poleArea) - 1);
  strncpy(cfg.poleType, "STREET", sizeof(cfg.poleType) - 1);
  cfg.feederCount = 6;
  cfg.offlineTimeoutSec = 180;

  for (int f = 0; f < 6; f++) {
    cfg.feeders[f].feederId = f + 1;
    snprintf(cfg.feeders[f].name, sizeof(cfg.feeders[f].name), "Feeder %d", f + 1);
    cfg.feeders[f].relayMapping = f;
    cfg.feeders[f].controlPermissions = 0x03;
    cfg.feeders[f].monitoringEnabled = true;
    cfg.feeders[f].currentThresholdA = 16.0f;
    cfg.feeders[f].schedEnabled = false;
    cfg.feeders[f].schedOnMin = -1;
    cfg.feeders[f].schedOffMin = -1;
  }

  for (int p = 0; p < 3; p++) {
    cfg.voltCalib[p].offset = 0.0f;
    cfg.voltCalib[p].gain = 1.000f;
    cfg.voltCalib[p].sensitivity = 1.000f;
    cfg.voltCalib[p].phaseDeg = 0.0f;
  }

  for (int f = 0; f < 6; f++) {
    for (int p = 0; p < 3; p++) {
      cfg.currCalib[f][p].offset = 0.0f;
      cfg.currCalib[f][p].gain = 1.000f;
      cfg.currCalib[f][p].sensitivity = 100.0f;
      cfg.currCalib[f][p].phaseDeg = 0.0f;
    }
  }

  cfg.fault.overCurrentThresholdA = 20.0f;
  cfg.fault.overVoltageThresholdV = 265.0f;
  cfg.fault.underVoltageThresholdV = 180.0f;
  cfg.fault.faultConfirmTimeMs = 100;
  cfg.fault.hysteresisV = 5.0f;
  cfg.fault.hysteresisA = 0.5f;
  cfg.fault.alertDelayMs = 500;
  cfg.fault.sensorFailureDetect = true;
  cfg.fault.notificationPriority = 2;

  cfg.telemetryIntervalSec = 60;
  cfg.alertCooldownSec = 60;

  cfg.configHash = calculateHash();
}

void PoleConfigManager::init() {
  loadFromNVS();
  syncHardwareDrivers();
}

uint32_t PoleConfigManager::calculateHash() {
  uint32_t crc = 0xFFFFFFFF;
  const uint8_t *p = (const uint8_t*)&cfg;
  // Hash all fields except the configHash itself
  size_t hashOffset = offsetof(PoleLocalConfigRecord, configHash);
  for (size_t i = 0; i < hashOffset; i++) {
    crc ^= p[i];
    for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  const uint8_t *pPost = p + hashOffset + sizeof(uint32_t);
  size_t postSize = sizeof(PoleLocalConfigRecord) - (hashOffset + sizeof(uint32_t));
  for (size_t i = 0; i < postSize; i++) {
    crc ^= pPost[i];
    for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

bool PoleConfigManager::loadFromNVS() {
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin("p_cfg", true)) return false;
  size_t sz = prefs.getBytes("cfg_rec", &cfg, sizeof(PoleLocalConfigRecord));
  prefs.end();
  if (sz == sizeof(PoleLocalConfigRecord)) {
    return true;
  }
  initDefaults();
  return false;
#else
  return true;
#endif
}

bool PoleConfigManager::saveToNVS() {
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin("p_cfg", false)) return false;
  cfg.configHash = calculateHash();
  prefs.putBytes("cfg_rec", &cfg, sizeof(PoleLocalConfigRecord));
  prefs.end();
  return true;
#else
  cfg.configHash = calculateHash();
  return true;
#endif
}

uint16_t PoleConfigManager::getCurrentVersion() const {
  return cfg.currentVersion;
}

uint16_t PoleConfigManager::getLastSuccessfulVersion() const {
  return cfg.lastSuccessfulVersion;
}

uint32_t PoleConfigManager::getConfigHash() const {
  return cfg.configHash;
}

const PoleLocalConfigRecord& PoleConfigManager::getConfig() const {
  return cfg;
}

void PoleConfigManager::syncHardwareDrivers() {
  // Sync calibrated metrology coefficients to CalibrationManager if needed
#ifdef ARDUINO
  for (int p = 0; p < 3; p++) {
    calibMgr.performSpanCalibration(p, 230.0f, 230.0f * cfg.voltCalib[p].gain);
  }
#endif
}

bool PoleConfigManager::processConfigPacket(const String &payload, const String &from, uint16_t seq,
                                           String &outAckPayload) {
  // Expected formats:
  // 1. CFG_SET|VER=<ver>|TX=<txId>|SEC=<sec>|P=<param>|V=<val>|TAG=<tag>
  // 2. CFG_ROLLBACK|TARGET_VER=<targetVer>|TX=<txId>|TOKEN=<token>|TAG=<tag>
  // 3. CFG_GET|SEC=<sec>|TAG=<tag>

  int idxTag = payload.lastIndexOf("|TAG=");
  if (idxTag < 0) {
    outAckPayload = "CONFIG_ACK|VER=0|STATUS=ERR_AUTH_MISSING|TX=NONE";
    return false;
  }

  String rawNoTag = payload.substring(0, idxTag);
  String tagHex = payload.substring(idxTag + 5);

  // Authenticate Tag
  char expectedTag[17];
  mscbComputeAuthTag(deviceKey, 32, (const uint8_t*)rawNoTag.c_str(), rawNoTag.length(), expectedTag);
  if (strcmp(tagHex.c_str(), expectedTag) != 0) {
    outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) + "|STATUS=ERR_AUTH_INVALID|TX=NONE";
    return false;
  }

  if (rawNoTag.startsWith("CFG_SET|")) {
    int pVer = rawNoTag.indexOf("VER=");
    int pTx  = rawNoTag.indexOf("TX=");
    int pSec = rawNoTag.indexOf("SEC=");
    int pP   = rawNoTag.indexOf("P=");
    int pV   = rawNoTag.indexOf("V=");

    if (pVer < 0 || pTx < 0 || pSec < 0 || pP < 0 || pV < 0) {
      outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) + "|STATUS=ERR_MALFORMED|TX=NONE";
      return false;
    }

    uint16_t newVer = (uint16_t)rawNoTag.substring(pVer + 4, rawNoTag.indexOf('|', pVer)).toInt();
    String txId     = rawNoTag.substring(pTx + 3, rawNoTag.indexOf('|', pTx));
    String sec      = rawNoTag.substring(pSec + 4, rawNoTag.indexOf('|', pSec));
    String param    = rawNoTag.substring(pP + 2, rawNoTag.indexOf('|', pP));
    String val      = rawNoTag.substring(pV + 2);

    // Anti-Rollback / Monotonic Version Check
    if (newVer <= cfg.currentVersion) {
      outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) +
                      "|STATUS=ERR_VERSION_DOWNGRADE_FORBIDDEN|TX=" + txId;
      return false;
    }

    // Range & Bound Validation
    ConfigValidationResult valRes = CFG_VAL_OK;
    if (sec == "NET") {
      if (param == "HOP_LIMIT") {
        uint8_t hl = (uint8_t)val.toInt();
        valRes = ConfigValidation::validateNetworkConfig(cfg.networkId, hl, cfg.discoveryIntervalSec,
                                                         cfg.heartbeatIntervalSec, cfg.retryCount,
                                                         cfg.ackTimeoutMs);
        if (valRes == CFG_VAL_OK) cfg.hopLimit = hl;
      } else if (param == "HB_SEC") {
        uint16_t hb = (uint16_t)val.toInt();
        valRes = ConfigValidation::validateRadioDutyCycle(hb, cfg.telemetryIntervalSec, cfg.feederCount);
        if (valRes == CFG_VAL_OK) cfg.heartbeatIntervalSec = hb;
      }
    } else if (sec == "FAULT") {
      if (param == "V_MAX") {
        float vm = val.toFloat();
        valRes = ConfigValidation::validateFaultConfig(cfg.fault.overCurrentThresholdA, vm,
                                                       cfg.fault.underVoltageThresholdV,
                                                       cfg.fault.faultConfirmTimeMs,
                                                       cfg.fault.hysteresisV, cfg.fault.hysteresisA);
        if (valRes == CFG_VAL_OK) cfg.fault.overVoltageThresholdV = vm;
      }
    } else if (sec == "CALIB_V") {
      float gain = val.toFloat();
      valRes = ConfigValidation::validateVoltageCalibration('R', 0.0f, gain, 1.0f, 0.0f);
      if (valRes == CFG_VAL_OK) {
        cfg.voltCalib[0].gain = gain;
        syncHardwareDrivers();
      }
    }

    if (valRes != CFG_VAL_OK) {
      outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) + "|STATUS=" +
                      String(configValidationResultToString(valRes)) + "|TX=" + txId;
      return false;
    }

    // Atomic Application Success
    cfg.lastSuccessfulVersion = cfg.currentVersion;
    cfg.currentVersion = newVer;
    strncpy(cfg.lastTxId, txId.c_str(), sizeof(cfg.lastTxId) - 1);
    saveToNVS();

    outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) +
                    "|HASH=" + String(cfg.configHash) +
                    "|STATUS=APPLIED|TX=" + txId;
    return true;
  }
  else if (rawNoTag.startsWith("CFG_ROLLBACK|")) {
    int pTgt = rawNoTag.indexOf("TARGET_VER=");
    int pTx  = rawNoTag.indexOf("TX=");
    int pTok = rawNoTag.indexOf("TOKEN=");

    if (pTgt < 0 || pTx < 0 || pTok < 0) {
      outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) + "|STATUS=ERR_MALFORMED|TX=NONE";
      return false;
    }

    uint16_t targetVer = (uint16_t)rawNoTag.substring(pTgt + 11, rawNoTag.indexOf('|', pTgt)).toInt();
    String txId        = rawNoTag.substring(pTx + 3, rawNoTag.indexOf('|', pTx));
    String token       = rawNoTag.substring(pTok + 6);

    if (token != "AUTH_ADMIN_ROLLBACK" || targetVer >= cfg.currentVersion) {
      outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) + "|STATUS=ERR_ROLLBACK_DENIED|TX=" + txId;
      return false;
    }

    cfg.currentVersion = targetVer;
    cfg.lastSuccessfulVersion = targetVer;
    strncpy(cfg.lastTxId, txId.c_str(), sizeof(cfg.lastTxId) - 1);
    saveToNVS();

    outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) +
                    "|HASH=" + String(cfg.configHash) +
                    "|STATUS=APPLIED|TX=" + txId;
    return true;
  }
  else if (rawNoTag.startsWith("CFG_GET|")) {
    outAckPayload = "CONFIG_RSP|VER=" + String(cfg.currentVersion) +
                    "|HASH=" + String(cfg.configHash) +
                    "|NET=" + String(cfg.networkId) +
                    "|HL=" + String(cfg.hopLimit) +
                    "|HB=" + String(cfg.heartbeatIntervalSec);
    return true;
  }

  outAckPayload = "CONFIG_ACK|VER=" + String(cfg.currentVersion) + "|STATUS=ERR_UNKNOWN_CMD|TX=NONE";
  return false;
}
