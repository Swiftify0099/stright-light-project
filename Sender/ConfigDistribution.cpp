/*
 * =====================================================================
 *  CONFIGDISTRIBUTION.CPP — PACED BULK CONFIGURATION DISTRIBUTION
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 * =====================================================================
 */

#include "ConfigDistribution.h"
#ifdef ARDUINO
  #include "LoRaManager.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

ConfigDistribution configDist;

ConfigDistribution::ConfigDistribution() : emergencyPaused(false), emergencyPauseTimeMs(0) {
  memset(&currentBatch, 0, sizeof(BatchTransaction));
  currentBatch.state = BATCH_STATE_IDLE;
}

void ConfigDistribution::init() {
  currentBatch.active = false;
  currentBatch.state = BATCH_STATE_IDLE;
}

bool ConfigDistribution::prepareBulkUpdate(BulkOperationType opType, const char* targetGroupOrId,
                                         const char* section, const char* param, const char* value,
                                         const char* adminId, String &outPreviewMsg, char outToken[16]) {
  if (currentBatch.active && currentBatch.state == BATCH_STATE_IN_PROGRESS) {
    return false; // Another batch is already running
  }

  memset(&currentBatch, 0, sizeof(BatchTransaction));
  currentBatch.opType = opType;
  if (adminId) strncpy(currentBatch.adminId, adminId, sizeof(currentBatch.adminId) - 1);
  if (section) strncpy(currentBatch.section, section, sizeof(currentBatch.section) - 1);
  if (param) strncpy(currentBatch.param, param, sizeof(currentBatch.param) - 1);
  if (value) strncpy(currentBatch.value, value, sizeof(currentBatch.value) - 1);

  uint32_t nowEpoch = (uint32_t)time(nullptr);
  uint16_t randToken = 1000 + (uint16_t)(rand() % 9000);
  snprintf(currentBatch.batchTxId, sizeof(currentBatch.batchTxId), "BATCH-CFG-%lu-%u", (unsigned long)nowEpoch, randToken);
  snprintf(currentBatch.confirmationToken, sizeof(currentBatch.confirmationToken), "CONF-%u", randToken);
  strncpy(outToken, currentBatch.confirmationToken, 15);
  outToken[15] = '\0';

  uint8_t targetCount = 0;
  uint8_t totalNodes = gwConfigMgr.getNodeConfigCount();

  for (uint8_t i = 0; i < totalNodes && targetCount < MAX_BATCH_NODES; i++) {
    MscbNodeConfigRecord* rec = gwConfigMgr.getNodeConfigByIndex(i);
    if (!rec || !rec->isValid) continue;

    bool match = false;
    switch (opType) {
      case BULK_OP_SINGLE_POLE:
        if (targetGroupOrId && strcmp(rec->logicalId, targetGroupOrId) == 0) match = true;
        break;
      case BULK_OP_SELECTED_GROUP:
        if (targetGroupOrId && (strcmp(rec->pole.poleArea, targetGroupOrId) == 0 ||
                                strstr(rec->logicalId, targetGroupOrId) != nullptr)) {
          match = true;
        }
        break;
      case BULK_OP_ALL_POLES:
      case BULK_OP_FEEDER_UPDATE:
      case BULK_OP_CALIBRATION_UPDATE:
      case BULK_OP_TIMING_UPDATE:
        match = true;
        break;
    }

    if (match) {
      strncpy(currentBatch.targets[targetCount].logicalId, rec->logicalId, 15);
      currentBatch.targets[targetCount].targetVersion = rec->currentConfigVersion + 1;
      currentBatch.targets[targetCount].inFlight = false;
      currentBatch.targets[targetCount].acknowledged = false;
      currentBatch.targets[targetCount].failed = false;
      currentBatch.targets[targetCount].sentTimeMs = 0;
      currentBatch.targets[targetCount].retries = 0;
      currentBatch.targets[targetCount].errorReason[0] = '\0';
      targetCount++;
    }
  }

  currentBatch.totalTargets = targetCount;
  currentBatch.successCount = 0;
  currentBatch.failedCount = 0;
  currentBatch.currentIndex = 0;
  currentBatch.state = BATCH_STATE_PREVIEW_CONFIRMATION_REQUIRED;
  currentBatch.active = true;

  outPreviewMsg = "📋 *BULK CONFIGURATION PREVIEW*\n\n"
                  "🆔 *Batch ID:* `" + String(currentBatch.batchTxId) + "`\n"
                  "🎯 *Affected Nodes:* `" + String(targetCount) + " Nodes`\n"
                  "⚙️ *Section:* `" + String(section) + "`\n"
                  "⚡ *Parameter:* `" + String(param) + "`\n"
                  "📝 *New Value:* `" + String(value) + "`\n"
                  "⏱️ *Pacing:* `750ms interval, window size 2`\n\n"
                  "⚠️ *Two-Step Confirmation Required*\n"
                  "To launch, reply with: `/confirm " + String(currentBatch.confirmationToken) + "`";

  return true;
}

bool ConfigDistribution::confirmAndStartBulk(const char* confirmationToken) {
  if (!currentBatch.active || currentBatch.state != BATCH_STATE_PREVIEW_CONFIRMATION_REQUIRED) {
    return false;
  }
  if (!confirmationToken || strcmp(currentBatch.confirmationToken, confirmationToken) != 0) {
    return false; // Token mismatch
  }

  currentBatch.state = BATCH_STATE_IN_PROGRESS;
  currentBatch.startedAtMs = (uint32_t)millis();
  currentBatch.lastTxTimeMs = 0;

  // Pre-stage changes in GwConfigManager for all target nodes
  for (uint8_t i = 0; i < currentBatch.totalTargets; i++) {
    char subTx[32];
    gwConfigMgr.stageChange(currentBatch.targets[i].logicalId, currentBatch.section,
                            currentBatch.param, currentBatch.value, currentBatch.adminId, subTx);
  }

  return true;
}

void ConfigDistribution::pauseForEmergencyTraffic() {
  emergencyPaused = true;
  emergencyPauseTimeMs = (uint32_t)millis();
}

void ConfigDistribution::resumeAfterEmergencyTraffic() {
  emergencyPaused = false;
}

bool ConfigDistribution::isEmergencyPaused() const {
  return emergencyPaused;
}

void ConfigDistribution::onNodeAck(const char* logicalId, uint16_t ver, bool success, const char* errReason) {
  if (!currentBatch.active || currentBatch.state != BATCH_STATE_IN_PROGRESS) return;

  for (uint8_t i = 0; i < currentBatch.totalTargets; i++) {
    if (strcmp(currentBatch.targets[i].logicalId, logicalId) == 0 && currentBatch.targets[i].inFlight) {
      currentBatch.targets[i].inFlight = false;
      if (success) {
        currentBatch.targets[i].acknowledged = true;
        currentBatch.successCount++;
      } else {
        currentBatch.targets[i].failed = true;
        currentBatch.failedCount++;
        if (errReason) strncpy(currentBatch.targets[i].errorReason, errReason, sizeof(currentBatch.targets[i].errorReason) - 1);
      }
      break;
    }
  }

  if ((currentBatch.successCount + currentBatch.failedCount) >= currentBatch.totalTargets) {
    currentBatch.state = (currentBatch.failedCount == 0) ? BATCH_STATE_COMPLETED : BATCH_STATE_FAILED;
  }
}

void ConfigDistribution::update() {
  if (!currentBatch.active || currentBatch.state != BATCH_STATE_IN_PROGRESS) return;

  uint32_t now = (uint32_t)millis();

  // Handle emergency pause timeout (auto-resume after 10s of silence)
  if (emergencyPaused) {
    if (now - emergencyPauseTimeMs >= 10000UL) {
      emergencyPaused = false;
    } else {
      return; // Do not transmit background config during emergency
    }
  }

  // 1. Check in-flight targets for timeouts
  uint8_t inFlightCount = 0;
  for (uint8_t i = 0; i < currentBatch.totalTargets; i++) {
    if (currentBatch.targets[i].inFlight) {
      if (now - currentBatch.targets[i].sentTimeMs >= BATCH_NODE_TIMEOUT_MS) {
        if (currentBatch.targets[i].retries < 2) {
          // Retry
          currentBatch.targets[i].retries++;
          currentBatch.targets[i].sentTimeMs = now;
          char payload[128];
          if (gwConfigMgr.commitAndTransmit(currentBatch.targets[i].logicalId, payload)) {
#ifdef ARDUINO
            loraMgr.dispatchLoRaPacket("CMD", currentBatch.targets[i].logicalId,
                                       currentBatch.targets[i].logicalId, 6,
                                       loraMgr.getNextSeq(), payload);
#endif
          }
        } else {
          // Failed timeout
          currentBatch.targets[i].inFlight = false;
          currentBatch.targets[i].failed = true;
          strncpy(currentBatch.targets[i].errorReason, "TIMEOUT", sizeof(currentBatch.targets[i].errorReason) - 1);
          currentBatch.failedCount++;
        }
      } else {
        inFlightCount++;
      }
    }
  }

  // 2. Check pacing interval & window capacity
  if (inFlightCount < BATCH_CONCURRENT_WINDOW && (now - currentBatch.lastTxTimeMs >= BATCH_PACING_INTERVAL_MS)) {
    while (currentBatch.currentIndex < currentBatch.totalTargets && inFlightCount < BATCH_CONCURRENT_WINDOW) {
      uint8_t idx = currentBatch.currentIndex;
      if (!currentBatch.targets[idx].inFlight && !currentBatch.targets[idx].acknowledged && !currentBatch.targets[idx].failed) {
        char payload[128];
        if (gwConfigMgr.commitAndTransmit(currentBatch.targets[idx].logicalId, payload)) {
          currentBatch.targets[idx].inFlight = true;
          currentBatch.targets[idx].sentTimeMs = now;
          currentBatch.targets[idx].retries = 0;
          currentBatch.lastTxTimeMs = now;
          inFlightCount++;

#ifdef ARDUINO
          loraMgr.dispatchLoRaPacket("CMD", currentBatch.targets[idx].logicalId,
                                     currentBatch.targets[idx].logicalId, 6,
                                     loraMgr.getNextSeq(), payload);
#endif
        }
      }
      currentBatch.currentIndex++;
    }
  }

  // Check if complete
  if ((currentBatch.successCount + currentBatch.failedCount) >= currentBatch.totalTargets) {
    currentBatch.state = (currentBatch.failedCount == 0) ? BATCH_STATE_COMPLETED : BATCH_STATE_FAILED;
  }
}

bool ConfigDistribution::isBatchActive() const {
  return currentBatch.active && (currentBatch.state == BATCH_STATE_IN_PROGRESS ||
                                currentBatch.state == BATCH_STATE_PREVIEW_CONFIRMATION_REQUIRED);
}

String ConfigDistribution::generateBatchStatusReport() const {
  String statusStr = "UNKNOWN";
  switch (currentBatch.state) {
    case BATCH_STATE_IDLE: statusStr = "IDLE"; break;
    case BATCH_STATE_PREVIEW_CONFIRMATION_REQUIRED: statusStr = "AWAITING_CONFIRMATION"; break;
    case BATCH_STATE_IN_PROGRESS: statusStr = "IN_PROGRESS ⏳"; break;
    case BATCH_STATE_PAUSED_EMERGENCY: statusStr = "PAUSED_FOR_EMERGENCY 🚨"; break;
    case BATCH_STATE_COMPLETED: statusStr = "COMPLETED ✅"; break;
    case BATCH_STATE_FAILED: statusStr = "PARTIAL_FAILURE ⚠️"; break;
  }

  String r = "📊 *BATCH CONFIGURATION REPORT*\n\n"
             "🆔 *Batch ID:* `" + String(currentBatch.batchTxId) + "`\n"
             "🚦 *Status:* `" + statusStr + "`\n"
             "🎯 *Total Targets:* `" + String(currentBatch.totalTargets) + "`\n"
             "✅ *Successful:* `" + String(currentBatch.successCount) + "`\n"
             "❌ *Failed:* `" + String(currentBatch.failedCount) + "`\n";

  if (currentBatch.failedCount > 0) {
    r += "\n*Failed Nodes:*\n";
    for (uint8_t i = 0; i < currentBatch.totalTargets; i++) {
      if (currentBatch.targets[i].failed) {
        r += "• `" + String(currentBatch.targets[i].logicalId) + "`: " +
             String(currentBatch.targets[i].errorReason) + "\n";
      }
    }
  }
  return r;
}

const BatchTransaction& ConfigDistribution::getTransaction() const {
  return currentBatch;
}
