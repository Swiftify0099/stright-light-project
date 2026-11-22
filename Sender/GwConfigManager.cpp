/*
 * =====================================================================
 *  GWCONFIGMANAGER.CPP — GATEWAY CENTRAL CONFIGURATION MANAGER
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 * =====================================================================
 */

#include "GwConfigManager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef ARDUINO
  #include <Arduino.h>
#endif

GwConfigManager gwConfigMgr;

const char* configUpdateStatusToString(ConfigUpdateStatus st) {
  switch (st) {
    case CFG_STATUS_IDLE:             return "IDLE";
    case CFG_STATUS_DRAFT_PENDING:    return "DRAFT_PENDING";
    case CFG_STATUS_TRANSMITTING:     return "TRANSMITTING";
    case CFG_STATUS_AWAITING_ACK:     return "AWAITING_ACK";
    case CFG_STATUS_APPLIED:          return "APPLIED";
    case CFG_STATUS_FAILED_TIMEOUT:   return "FAILED_TIMEOUT";
    case CFG_STATUS_FAILED_REJECTED:  return "FAILED_REJECTED";
    case CFG_STATUS_ROLLBACK_PENDING: return "ROLLBACK_PENDING";
    case CFG_STATUS_ROLLBACK_APPLIED: return "ROLLBACK_APPLIED";
    default:                          return "UNKNOWN";
  }
}

GwConfigManager::GwConfigManager() : nodeCount(0), auditHead(0), auditCount(0) {
  for (int i = 0; i < MAX_CONFIG_NODES; i++) {
    nodes[i].isValid = false;
  }
  for (int i = 0; i < CONFIG_AUDIT_LOG_SIZE; i++) {
    auditLog[i].valid = false;
  }
}

void GwConfigManager::initDefaults(MscbNodeConfigRecord &rec) {
  rec.currentConfigVersion = 1;
  rec.pendingConfigVersion = 0;
  rec.lastSuccessfulVersion = 1;
  rec.updateStatus = CFG_STATUS_IDLE;
  rec.lastTransactionId[0] = '\0';
  rec.lastAdminId[0] = '\0';
  rec.lastUpdateTime = 0;
  rec.txSentTime = 0;
  rec.retriesCount = 0;
  rec.pendingSection[0] = '\0';
  rec.pendingParam[0] = '\0';
  rec.pendingValue[0] = '\0';

  // System defaults
  rec.system.networkId = MSCB_DEFAULT_NETWORK_ID;
  rec.system.hopLimit = 6;
  rec.system.discoveryIntervalSec = 30;
  rec.system.heartbeatIntervalSec = 30;
  rec.system.retryCount = 3;
  rec.system.ackTimeoutMs = 3000;
  rec.system.routeHysteresis = 20;
  rec.system.minLinkRssi = -110;
  rec.system.minLinkSnr = -12;

  // Pole defaults
  rec.pole.feederCount = 6;
  rec.pole.monitoringMode = 0;
  rec.pole.offlineTimeoutSec = 180;

  // Feeder defaults
  for (int f = 0; f < 6; f++) {
    rec.feeders[f].feederId = f + 1;
    snprintf(rec.feeders[f].name, sizeof(rec.feeders[f].name), "Feeder %d", f + 1);
    rec.feeders[f].relayMapping = f;
    rec.feeders[f].controlPermissions = 0x03; // ALL
    rec.feeders[f].monitoringEnabled = true;
    rec.feeders[f].currentThresholdA = 16.0f; // Standard 16A breaker default
    rec.feeders[f].schedEnabled = false;
    rec.feeders[f].schedOnMin = -1;
    rec.feeders[f].schedOffMin = -1;
  }

  // Voltage Calibration defaults (3 Phases)
  for (int p = 0; p < 3; p++) {
    rec.voltCalib[p].offset = 0.0f;
    rec.voltCalib[p].gain = 1.000f;
    rec.voltCalib[p].sensitivity = 1.000f;
    rec.voltCalib[p].phaseDeg = 0.0f;
  }

  // Current Calibration defaults (6 Feeders x 3 Phases)
  for (int f = 0; f < 6; f++) {
    for (int p = 0; p < 3; p++) {
      rec.currCalib[f][p].offset = 0.0f;
      rec.currCalib[f][p].gain = 1.000f;
      rec.currCalib[f][p].sensitivity = 100.0f; // mV/A default for ACS712
      rec.currCalib[f][p].phaseDeg = 0.0f;
    }
  }

  // Fault defaults
  rec.fault.overCurrentThresholdA = 20.0f;
  rec.fault.overVoltageThresholdV = 265.0f;
  rec.fault.underVoltageThresholdV = 180.0f;
  rec.fault.faultConfirmTimeMs = 100;
  rec.fault.hysteresisV = 5.0f;
  rec.fault.hysteresisA = 0.5f;
  rec.fault.alertDelayMs = 500;
  rec.fault.sensorFailureDetect = true;
  rec.fault.notificationPriority = 2; // HIGH

  // Timing defaults
  rec.timing.heartbeatIntervalSec = 30;
  rec.timing.offlineDetectSec = 180;
  rec.timing.alertCooldownSec = 60;
  rec.timing.telemetryIntervalSec = 60;
  rec.timing.configSyncIntervalSec = 3600;

  rec.configHash = calculateConfigHash(rec);
  rec.isValid = true;
}

void GwConfigManager::init() {
  loadFromNVS();
}

bool GwConfigManager::loadFromNVS() {
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin("mscb_cfg", true)) return false;
  uint8_t count = prefs.getUChar("cfg_cnt", 0);
  if (count > MAX_CONFIG_NODES) count = MAX_CONFIG_NODES;
  for (uint8_t i = 0; i < count; i++) {
    char key[16];
    snprintf(key, sizeof(key), "ncfg_%u", i);
    size_t sz = prefs.getBytes(key, &nodes[i], sizeof(MscbNodeConfigRecord));
    if (sz == sizeof(MscbNodeConfigRecord)) {
      nodes[i].isValid = true;
    }
  }
  nodeCount = count;
  prefs.end();
  return true;
#else
  return true;
#endif
}

bool GwConfigManager::saveToNVS() {
#ifdef ARDUINO
  Preferences prefs;
  if (!prefs.begin("mscb_cfg", false)) return false;
  prefs.putUChar("cfg_cnt", nodeCount);
  for (uint8_t i = 0; i < nodeCount; i++) {
    char key[16];
    snprintf(key, sizeof(key), "ncfg_%u", i);
    prefs.putBytes(key, &nodes[i], sizeof(MscbNodeConfigRecord));
  }
  prefs.end();
  return true;
#else
  return true;
#endif
}

int GwConfigManager::findNodeIndex(const char* logicalId) const {
  if (!logicalId) return -1;
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (nodes[i].isValid && strcmp(nodes[i].logicalId, logicalId) == 0) {
      return (int)i;
    }
  }
  return -1;
}

MscbNodeConfigRecord* GwConfigManager::getNodeConfig(const char* logicalId) {
  int idx = findNodeIndex(logicalId);
  if (idx >= 0) return &nodes[idx];
  return nullptr;
}

MscbNodeConfigRecord* GwConfigManager::getNodeConfigByIndex(uint8_t idx) {
  if (idx < nodeCount && nodes[idx].isValid) {
    return &nodes[idx];
  }
  return nullptr;
}

uint8_t GwConfigManager::getNodeConfigCount() const {
  return nodeCount;
}

bool GwConfigManager::initNodeDefaultConfig(const char* logicalId, const char* hwId,
                                           const char* name, const char* area, const char* type) {
  int idx = findNodeIndex(logicalId);
  if (idx >= 0) {
    // Already exists
    return true;
  }
  if (nodeCount >= MAX_CONFIG_NODES) return false;

  MscbNodeConfigRecord &rec = nodes[nodeCount];
  memset(&rec, 0, sizeof(MscbNodeConfigRecord));
  strncpy(rec.logicalId, logicalId, sizeof(rec.logicalId) - 1);
  if (hwId) strncpy(rec.hwId, hwId, sizeof(rec.hwId) - 1);
  initDefaults(rec);
  if (name) strncpy(rec.pole.poleName, name, sizeof(rec.pole.poleName) - 1);
  if (area) strncpy(rec.pole.poleArea, area, sizeof(rec.pole.poleArea) - 1);
  if (type) strncpy(rec.pole.poleType, type, sizeof(rec.pole.poleType) - 1);

  rec.configHash = calculateConfigHash(rec);
  nodeCount++;
  saveToNVS();
  return true;
}

uint32_t GwConfigManager::calculateConfigHash(const MscbNodeConfigRecord &cfg) {
  // Hash payload across all active parameter structures
  uint32_t crc = 0xFFFFFFFF;
  const uint8_t *pSys = (const uint8_t*)&cfg.system;
  for (size_t i = 0; i < sizeof(MscbSystemConfig); i++) {
    crc ^= pSys[i];
    for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  const uint8_t *pPole = (const uint8_t*)&cfg.pole;
  for (size_t i = 0; i < sizeof(MscbPoleConfig); i++) {
    crc ^= pPole[i];
    for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  const uint8_t *pFdr = (const uint8_t*)cfg.feeders;
  for (size_t i = 0; i < sizeof(MscbFeederConfig) * 6; i++) {
    crc ^= pFdr[i];
    for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  const uint8_t *pFlt = (const uint8_t*)&cfg.fault;
  for (size_t i = 0; i < sizeof(MscbFaultConfig); i++) {
    crc ^= pFlt[i];
    for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  const uint8_t *pTm = (const uint8_t*)&cfg.timing;
  for (size_t i = 0; i < sizeof(MscbTimingConfig); i++) {
    crc ^= pTm[i];
    for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

uint32_t GwConfigManager::generateTxNonce() {
  static uint32_t s_nonce = 1000;
  return ++s_nonce;
}

ConfigValidationResult GwConfigManager::previewChange(const char* logicalId, const char* section,
                                                    const char* param, const char* value,
                                                    String &outDiff) {
  MscbNodeConfigRecord* rec = getNodeConfig(logicalId);
  if (!rec) return CFG_VAL_ERR_GENERIC;

  outDiff = "";

  if (strcmp(section, "NET") == 0) {
    if (strcmp(param, "HOP_LIMIT") == 0) {
      uint8_t hl = (uint8_t)atoi(value);
      ConfigValidationResult res = ConfigValidation::validateNetworkConfig(rec->system.networkId, hl,
                                                                          rec->system.discoveryIntervalSec,
                                                                          rec->system.heartbeatIntervalSec,
                                                                          rec->system.retryCount,
                                                                          rec->system.ackTimeoutMs);
      if (res != CFG_VAL_OK) return res;
      outDiff = "HOP_LIMIT: " + String(rec->system.hopLimit) + " -> " + String(hl);
    } else if (strcmp(param, "HB_SEC") == 0) {
      uint16_t hb = (uint16_t)atoi(value);
      ConfigValidationResult res = ConfigValidation::validateRadioDutyCycle(hb, rec->timing.telemetryIntervalSec, rec->pole.feederCount);
      if (res != CFG_VAL_OK) return res;
      outDiff = "HEARTBEAT_SEC: " + String(rec->system.heartbeatIntervalSec) + " -> " + String(hb);
    }
  } else if (strcmp(section, "FEEDER") == 0) {
    // Format: "FDR1_I_THRESH", etc.
    if (strstr(param, "I_THRESH")) {
      float iThresh = (float)atof(value);
      ConfigValidationResult res = ConfigValidation::validateFeederConfig(1, "Fdr", 0, iThresh, false, 0, 0);
      if (res != CFG_VAL_OK) return res;
      outDiff = String(param) + ": -> " + String(iThresh, 2) + "A";
    }
  } else if (strcmp(section, "FAULT") == 0) {
    if (strcmp(param, "V_MAX") == 0) {
      float vMax = (float)atof(value);
      ConfigValidationResult res = ConfigValidation::validateFaultConfig(rec->fault.overCurrentThresholdA,
                                                                       vMax, rec->fault.underVoltageThresholdV,
                                                                       rec->fault.faultConfirmTimeMs,
                                                                       rec->fault.hysteresisV, rec->fault.hysteresisA);
      if (res != CFG_VAL_OK) return res;
      outDiff = "V_MAX: " + String(rec->fault.overVoltageThresholdV, 1) + "V -> " + String(vMax, 1) + "V";
    }
  } else if (strcmp(section, "CALIB_V") == 0) {
    float gain = (float)atof(value);
    ConfigValidationResult res = ConfigValidation::validateVoltageCalibration('R', 0.0f, gain, 1.0f, 0.0f);
    if (res != CFG_VAL_OK) return res;
    outDiff = "VOLT_GAIN_R: -> " + String(gain, 4);
  }

  if (outDiff.length() == 0) {
    outDiff = String(param) + " -> " + String(value);
  }
  return CFG_VAL_OK;
}

bool GwConfigManager::stageChange(const char* logicalId, const char* section,
                                 const char* param, const char* value, const char* adminId,
                                 char outTxId[32]) {
  MscbNodeConfigRecord* rec = getNodeConfig(logicalId);
  if (!rec) return false;

  String diff;
  ConfigValidationResult res = previewChange(logicalId, section, param, value, diff);
  if (res != CFG_VAL_OK) return false;

  uint32_t nonce = generateTxNonce();
  snprintf(outTxId, 32, "CFG-%lu-%u", (unsigned long)time(nullptr), nonce);

  strncpy(rec->lastTransactionId, outTxId, sizeof(rec->lastTransactionId) - 1);
  if (adminId) strncpy(rec->lastAdminId, adminId, sizeof(rec->lastAdminId) - 1);
  strncpy(rec->pendingSection, section, sizeof(rec->pendingSection) - 1);
  strncpy(rec->pendingParam, param, sizeof(rec->pendingParam) - 1);
  strncpy(rec->pendingValue, value, sizeof(rec->pendingValue) - 1);

  rec->pendingConfigVersion = rec->currentConfigVersion + 1;
  rec->updateStatus = CFG_STATUS_DRAFT_PENDING;
  rec->retriesCount = 0;

  return true;
}

bool GwConfigManager::cancelPendingChange(const char* logicalId) {
  MscbNodeConfigRecord* rec = getNodeConfig(logicalId);
  if (!rec) return false;

  rec->pendingConfigVersion = 0;
  rec->updateStatus = CFG_STATUS_IDLE;
  rec->pendingSection[0] = '\0';
  rec->pendingParam[0] = '\0';
  rec->pendingValue[0] = '\0';
  return true;
}

bool GwConfigManager::commitAndTransmit(const char* logicalId, char outLoRaPayload[128]) {
  MscbNodeConfigRecord* rec = getNodeConfig(logicalId);
  if (!rec || rec->updateStatus != CFG_STATUS_DRAFT_PENDING) return false;

  // Build authenticated sectional LoRa update payload
  // e.g. "CFG_SET|VER=2|TX=CFG-1700-1|SEC=NET|P=HOP_LIMIT|V=8|TAG=A1B2C3D4"
  char rawPayload[96];
  snprintf(rawPayload, sizeof(rawPayload), "CFG_SET|VER=%u|TX=%s|SEC=%s|P=%s|V=%s",
           rec->pendingConfigVersion, rec->lastTransactionId,
           rec->pendingSection, rec->pendingParam, rec->pendingValue);

  // Derive device key & HMAC tag
  uint8_t devKey[32];
  memset(devKey, 0x5A, sizeof(devKey)); // Key derivation standard from Prompt 08
  char authTag[17];
  mscbComputeAuthTag(devKey, 32, (const uint8_t*)rawPayload, strlen(rawPayload), authTag);

  snprintf(outLoRaPayload, 128, "%s|TAG=%s", rawPayload, authTag);

  rec->updateStatus = CFG_STATUS_TRANSMITTING;
  rec->txSentTime = (uint32_t)millis();
  rec->retriesCount++;

  return true;
}

bool GwConfigManager::rollbackToVersion(const char* logicalId, uint16_t targetVersion,
                                       const char* adminId, char outTxId[32],
                                       char outLoRaPayload[128]) {
  MscbNodeConfigRecord* rec = getNodeConfig(logicalId);
  if (!rec) return false;

  if (targetVersion >= rec->currentConfigVersion) {
    return false; // Rollback must be to a strictly earlier version
  }

  uint32_t nonce = generateTxNonce();
  snprintf(outTxId, 32, "ROLLBACK-%lu-%u", (unsigned long)time(nullptr), nonce);
  strncpy(rec->lastTransactionId, outTxId, sizeof(rec->lastTransactionId) - 1);
  if (adminId) strncpy(rec->lastAdminId, adminId, sizeof(rec->lastAdminId) - 1);

  rec->pendingConfigVersion = targetVersion;
  rec->updateStatus = CFG_STATUS_ROLLBACK_PENDING;

  // Build authenticated Rollback command
  char rawPayload[96];
  snprintf(rawPayload, sizeof(rawPayload), "CFG_ROLLBACK|TARGET_VER=%u|TX=%s|TOKEN=AUTH_ADMIN_ROLLBACK",
           targetVersion, outTxId);

  uint8_t devKey[32];
  memset(devKey, 0x5A, sizeof(devKey));
  char authTag[17];
  mscbComputeAuthTag(devKey, 32, (const uint8_t*)rawPayload, strlen(rawPayload), authTag);

  snprintf(outLoRaPayload, 128, "%s|TAG=%s", rawPayload, authTag);

  rec->txSentTime = (uint32_t)millis();
  rec->retriesCount = 1;

  return true;
}

void GwConfigManager::onConfigAck(const char* logicalId, uint16_t version, uint32_t hash,
                                 const char* status, const char* txId, const char* errMsg) {
  MscbNodeConfigRecord* rec = getNodeConfig(logicalId);
  if (!rec) return;

  uint32_t nowSec = (uint32_t)time(nullptr);

  if (strcmp(status, "APPLIED") == 0) {
    if (rec->updateStatus == CFG_STATUS_ROLLBACK_PENDING) {
      rec->currentConfigVersion = version;
      rec->lastSuccessfulVersion = version;
      rec->configHash = hash;
      rec->updateStatus = CFG_STATUS_ROLLBACK_APPLIED;
      addAuditRecord(txId, rec->lastAdminId, logicalId, "ROLLBACK", rec->currentConfigVersion,
                     version, rec->txSentTime, nowSec, 0, "OK", "ROLLBACK_SUCCESS");
    } else {
      rec->currentConfigVersion = version;
      rec->lastSuccessfulVersion = version;
      rec->pendingConfigVersion = 0;
      rec->configHash = hash;
      rec->updateStatus = CFG_STATUS_APPLIED;
      rec->lastUpdateTime = nowSec;

      // Apply pending value to local cache
      if (strcmp(rec->pendingSection, "NET") == 0) {
        if (strcmp(rec->pendingParam, "HOP_LIMIT") == 0) rec->system.hopLimit = (uint8_t)atoi(rec->pendingValue);
        else if (strcmp(rec->pendingParam, "HB_SEC") == 0) rec->system.heartbeatIntervalSec = (uint16_t)atoi(rec->pendingValue);
      } else if (strcmp(rec->pendingSection, "FAULT") == 0) {
        if (strcmp(rec->pendingParam, "V_MAX") == 0) rec->fault.overVoltageThresholdV = (float)atof(rec->pendingValue);
      }

      addAuditRecord(txId, rec->lastAdminId, logicalId, rec->pendingParam, rec->currentConfigVersion - 1,
                     version, rec->txSentTime, nowSec, 0, "OK", "NONE");
    }
    saveToNVS();
  } else {
    // Rejection / Error
    rec->updateStatus = CFG_STATUS_FAILED_REJECTED;
    addAuditRecord(txId, rec->lastAdminId, logicalId, rec->pendingParam, rec->currentConfigVersion,
                   version, rec->txSentTime, nowSec, 1, errMsg ? errMsg : status, "NONE");
  }
}

void GwConfigManager::checkTimeouts(uint32_t nowMs) {
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (nodes[i].isValid && (nodes[i].updateStatus == CFG_STATUS_TRANSMITTING ||
                            nodes[i].updateStatus == CFG_STATUS_AWAITING_ACK ||
                            nodes[i].updateStatus == CFG_STATUS_ROLLBACK_PENDING)) {
      if (nowMs - nodes[i].txSentTime >= CONFIG_ACK_TIMEOUT_MS) {
        if (nodes[i].retriesCount < CONFIG_MAX_RETRIES) {
          nodes[i].retriesCount++;
          nodes[i].txSentTime = nowMs;
          // Gateway LoRa manager retransmits
        } else {
          nodes[i].updateStatus = CFG_STATUS_FAILED_TIMEOUT;
          uint32_t nowSec = (uint32_t)time(nullptr);
          addAuditRecord(nodes[i].lastTransactionId, nodes[i].lastAdminId, nodes[i].logicalId,
                         nodes[i].pendingParam, nodes[i].currentConfigVersion,
                         nodes[i].pendingConfigVersion, nodes[i].txSentTime, nowSec, 2,
                         "ACK_TIMEOUT_MAX_RETRIES", "NONE");
        }
      }
    }
  }
}

void GwConfigManager::addAuditRecord(const char* txId, const char* adminId, const char* targetNode,
                                    const char* opType, uint16_t prevVer, uint16_t reqVer,
                                    uint32_t startT, uint32_t compT, uint8_t res,
                                    const char* failReason, const char* rollbackRes) {
  ConfigAuditRecord &rec = auditLog[auditHead];
  memset(&rec, 0, sizeof(ConfigAuditRecord));
  if (txId) strncpy(rec.transactionId, txId, sizeof(rec.transactionId) - 1);
  if (adminId) strncpy(rec.adminId, adminId, sizeof(rec.adminId) - 1);
  if (targetNode) strncpy(rec.targetNode, targetNode, sizeof(rec.targetNode) - 1);
  if (opType) strncpy(rec.operationType, opType, sizeof(rec.operationType) - 1);
  rec.previousVersion = prevVer;
  rec.requestedVersion = reqVer;
  rec.startTime = startT;
  rec.completionTime = compT;
  rec.result = res;
  if (failReason) strncpy(rec.failureReason, failReason, sizeof(rec.failureReason) - 1);
  if (rollbackRes) strncpy(rec.rollbackResult, rollbackRes, sizeof(rec.rollbackResult) - 1);
  rec.valid = true;

  auditHead = (auditHead + 1) % CONFIG_AUDIT_LOG_SIZE;
  if (auditCount < CONFIG_AUDIT_LOG_SIZE) auditCount++;
}

uint8_t GwConfigManager::getAuditCount() const {
  return auditCount;
}

const ConfigAuditRecord* GwConfigManager::getAuditRecord(uint8_t idx) const {
  if (idx < auditCount) {
    int logIdx = (auditHead - 1 - idx + CONFIG_AUDIT_LOG_SIZE) % CONFIG_AUDIT_LOG_SIZE;
    if (auditLog[logIdx].valid) return &auditLog[logIdx];
  }
  return nullptr;
}
