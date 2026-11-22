/*
 * =====================================================================
 *  CONFIGDISTRIBUTION.H — PACED BULK CONFIGURATION DISTRIBUTION
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 *  Controlled Batching, Window Pacing & Emergency Traffic Preemption
 * =====================================================================
 */

#ifndef CONFIG_DISTRIBUTION_H
#define CONFIG_DISTRIBUTION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include "Arduino.h"
#endif

#include "GwConfigManager.h"

#define MAX_BATCH_NODES               50
#define BATCH_CONCURRENT_WINDOW       2        // Max 2 nodes in-flight simultaneously
#define BATCH_PACING_INTERVAL_MS      750UL    // 750ms minimum spacing between LoRa packets
#define BATCH_NODE_TIMEOUT_MS         5000UL

enum BatchDistributionState {
  BATCH_STATE_IDLE = 0,
  BATCH_STATE_PREVIEW_CONFIRMATION_REQUIRED,
  BATCH_STATE_IN_PROGRESS,
  BATCH_STATE_PAUSED_EMERGENCY,
  BATCH_STATE_COMPLETED,
  BATCH_STATE_FAILED
};

enum BulkOperationType {
  BULK_OP_SINGLE_POLE = 0,
  BULK_OP_SELECTED_GROUP,
  BULK_OP_ALL_POLES,
  BULK_OP_FEEDER_UPDATE,
  BULK_OP_CALIBRATION_UPDATE,
  BULK_OP_TIMING_UPDATE
};

struct BatchTargetEntry {
  char     logicalId[16];
  uint16_t targetVersion;
  bool     inFlight;
  bool     acknowledged;
  bool     failed;
  uint32_t sentTimeMs;
  uint8_t  retries;
  char     errorReason[24];
};

struct BatchTransaction {
  bool                   active;
  char                   batchTxId[32];
  char                   adminId[24];
  char                   confirmationToken[16];
  BulkOperationType      opType;
  BatchDistributionState state;
  char                   section[16];
  char                   param[24];
  char                   value[32];
  uint8_t                totalTargets;
  uint8_t                successCount;
  uint8_t                failedCount;
  uint8_t                currentIndex;
  uint32_t               startedAtMs;
  uint32_t               lastTxTimeMs;
  BatchTargetEntry       targets[MAX_BATCH_NODES];
};

class ConfigDistribution {
public:
  ConfigDistribution();

  void init();

  // Step 1: Prepare Bulk Operation Preview
  bool prepareBulkUpdate(BulkOperationType opType, const char* targetGroupOrId,
                         const char* section, const char* param, const char* value,
                         const char* adminId, String &outPreviewMsg, char outToken[16]);

  // Step 2: Confirm and Launch
  bool confirmAndStartBulk(const char* confirmationToken);

  // Step 3: Pacing Engine (invoked in loop)
  void update();

  // Emergency Traffic Control
  void pauseForEmergencyTraffic();
  void resumeAfterEmergencyTraffic();
  bool isEmergencyPaused() const;

  // Inbound ACK notification from LoRa Manager
  void onNodeAck(const char* logicalId, uint16_t ver, bool success, const char* errReason);

  // Status & Reporting
  bool isBatchActive() const;
  String generateBatchStatusReport() const;
  const BatchTransaction& getTransaction() const;

private:
  BatchTransaction currentBatch;
  bool             emergencyPaused;
  uint32_t         emergencyPauseTimeMs;

  void dispatchNextInWindow();
};

extern ConfigDistribution configDist;

#endif // CONFIG_DISTRIBUTION_H
