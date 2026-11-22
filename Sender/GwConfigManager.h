/*
 * =====================================================================
 *  GWCONFIGMANAGER.H — GATEWAY CENTRAL CONFIGURATION MANAGER
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 *  Authoritative Central Configuration Records for All 50 Nodes
 * =====================================================================
 */

#ifndef GW_CONFIG_MANAGER_H
#define GW_CONFIG_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Preferences.h>
#else
  #include "Arduino.h"
#endif

#include "ConfigValidation.h"
#include "RegistrationTypes.h"

#define MAX_CONFIG_NODES             50
#define CONFIG_ACK_TIMEOUT_MS        5000UL
#define CONFIG_MAX_RETRIES           3
#define CONFIG_AUDIT_LOG_SIZE        32

// Configuration Lifecycle States
enum ConfigUpdateStatus {
  CFG_STATUS_IDLE = 0,
  CFG_STATUS_DRAFT_PENDING,
  CFG_STATUS_TRANSMITTING,
  CFG_STATUS_AWAITING_ACK,
  CFG_STATUS_APPLIED,
  CFG_STATUS_FAILED_TIMEOUT,
  CFG_STATUS_FAILED_REJECTED,
  CFG_STATUS_ROLLBACK_PENDING,
  CFG_STATUS_ROLLBACK_APPLIED
};

const char* configUpdateStatusToString(ConfigUpdateStatus st);

// Section Enums
enum ConfigSectionType {
  CFG_SEC_SYSTEM = 0,
  CFG_SEC_POLE,
  CFG_SEC_FEEDER,
  CFG_SEC_VOLT_CALIB,
  CFG_SEC_CURR_CALIB,
  CFG_SEC_FAULT,
  CFG_SEC_TIMING,
  CFG_SEC_ALL
};

// ─────────────────────────────────────────────────────────────
//  SECTION STRUCTS
// ─────────────────────────────────────────────────────────────
struct MscbSystemConfig {
  uint16_t networkId;
  uint8_t  hopLimit;
  uint16_t discoveryIntervalSec;
  uint16_t heartbeatIntervalSec;
  uint8_t  retryCount;
  uint16_t ackTimeoutMs;
  uint8_t  routeHysteresis;
  int8_t   minLinkRssi;
  int8_t   minLinkSnr;
};

struct MscbPoleConfig {
  char     poleName[32];
  char     poleArea[32];
  char     poleType[16];
  uint8_t  feederCount;
  uint8_t  monitoringMode;
  uint16_t offlineTimeoutSec;
};

struct MscbFeederConfig {
  uint8_t  feederId;
  char     name[24];
  uint8_t  relayMapping;
  uint8_t  controlPermissions;
  bool     monitoringEnabled;
  float    currentThresholdA;
  bool     schedEnabled;
  int16_t  schedOnMin;
  int16_t  schedOffMin;
};

struct MscbPhaseCalibConfig {
  float    offset;
  float    gain;
  float    sensitivity;
  float    phaseDeg;
};

struct MscbFaultConfig {
  float    overCurrentThresholdA;
  float    overVoltageThresholdV;
  float    underVoltageThresholdV;
  uint16_t faultConfirmTimeMs;
  float    hysteresisV;
  float    hysteresisA;
  uint16_t alertDelayMs;
  bool     sensorFailureDetect;
  uint8_t  notificationPriority;
};

struct MscbTimingConfig {
  uint16_t heartbeatIntervalSec;
  uint16_t offlineDetectSec;
  uint16_t alertCooldownSec;
  uint16_t telemetryIntervalSec;
  uint16_t configSyncIntervalSec;
};

// Comprehensive Per-Node Authoritative Configuration Record
struct MscbNodeConfigRecord {
  char                 logicalId[16];
  char                 hwId[17];
  uint16_t             currentConfigVersion;
  uint16_t             pendingConfigVersion;
  uint16_t             lastSuccessfulVersion;
  uint32_t             configHash;
  ConfigUpdateStatus   updateStatus;
  char                 lastTransactionId[32];
  char                 lastAdminId[24];
  uint32_t             lastUpdateTime;
  uint32_t             txSentTime;
  uint8_t              retriesCount;

  // Staged pending update cache
  char                 pendingSection[16];
  char                 pendingParam[24];
  char                 pendingValue[32];

  // 4 Core Configuration Tiers
  MscbSystemConfig     system;
  MscbPoleConfig       pole;
  MscbFeederConfig     feeders[6];
  MscbPhaseCalibConfig voltCalib[3];       // 3 Common Phases R, Y, B
  MscbPhaseCalibConfig currCalib[6][3];    // 6 Feeders x 3 Phases = 18 Channels
  MscbFaultConfig      fault;
  MscbTimingConfig     timing;
  bool                 isValid;
};

// Configuration Audit Log Entry
struct ConfigAuditRecord {
  char     transactionId[32];
  char     adminId[24];
  char     targetNode[16];
  char     operationType[24];
  uint16_t previousVersion;
  uint16_t requestedVersion;
  uint32_t startTime;
  uint32_t completionTime;
  uint8_t  result; // 0=SUCCESS, 1=REJECTED, 2=TIMEOUT, 3=ROLLBACK
  char     failureReason[32];
  char     rollbackResult[24];
  bool     valid;
};

// ─────────────────────────────────────────────────────────────
//  GW CONFIGURATION MANAGER CLASS
// ─────────────────────────────────────────────────────────────
class GwConfigManager {
public:
  GwConfigManager();

  void init();
  bool loadFromNVS();
  bool saveToNVS();

  // Registry & Lookup
  MscbNodeConfigRecord* getNodeConfig(const char* logicalId);
  MscbNodeConfigRecord* getNodeConfigByIndex(uint8_t idx);
  int  findNodeIndex(const char* logicalId) const;
  uint8_t getNodeConfigCount() const;
  bool initNodeDefaultConfig(const char* logicalId, const char* hwId, const char* name,
                             const char* area, const char* type);

  // Hash Calculation & Version Control
  uint32_t calculateConfigHash(const MscbNodeConfigRecord &cfg);

  // Workflow Steps: Preview -> Validate -> Stage -> Apply -> ACK -> Rollback
  ConfigValidationResult previewChange(const char* logicalId, const char* section,
                                       const char* param, const char* value,
                                       String &outDiff);

  bool stageChange(const char* logicalId, const char* section,
                   const char* param, const char* value, const char* adminId,
                   char outTxId[32]);

  bool cancelPendingChange(const char* logicalId);

  bool commitAndTransmit(const char* logicalId, char outLoRaPayload[128]);

  bool rollbackToVersion(const char* logicalId, uint16_t targetVersion,
                         const char* adminId, char outTxId[32],
                         char outLoRaPayload[128]);

  // Inbound ACK Handler
  void onConfigAck(const char* logicalId, uint16_t version, uint32_t hash,
                   const char* status, const char* txId, const char* errMsg);

  // Periodic Timeout & Retry Handler (called in loop)
  void checkTimeouts(uint32_t nowMs);

  // Audit Logging
  void addAuditRecord(const char* txId, const char* adminId, const char* targetNode,
                      const char* opType, uint16_t prevVer, uint16_t reqVer,
                      uint32_t startT, uint32_t compT, uint8_t res,
                      const char* failReason, const char* rollbackRes);

  uint8_t getAuditCount() const;
  const ConfigAuditRecord* getAuditRecord(uint8_t idx) const;

private:
  MscbNodeConfigRecord nodes[MAX_CONFIG_NODES];
  uint8_t              nodeCount;

  ConfigAuditRecord    auditLog[CONFIG_AUDIT_LOG_SIZE];
  uint8_t              auditHead;
  uint8_t              auditCount;

  uint32_t generateTxNonce();
  void initDefaults(MscbNodeConfigRecord &rec);
};

extern GwConfigManager gwConfigMgr;

#endif // GW_CONFIG_MANAGER_H
