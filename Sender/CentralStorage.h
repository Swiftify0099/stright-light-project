/*
 * =====================================================================
 *  CENTRALSTORAGE.H — MASTER AUTHORITATIVE STORAGE & REGISTRY ARCHITECTURE
 *  MSCB Smart Pole Automation System — Prompt 19 Architecture
 *  Central Node Registry (50 Nodes), Feeder Registry (300 Feeders),
 *  Versioned Config, 2PC Journal, Bounded Audit History, Wear Control
 * =====================================================================
 */

#ifndef CENTRAL_STORAGE_H
#define CENTRAL_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <Preferences.h>
#else
  #include <string>
  typedef std::string String;
#endif

#include "StorageTypes.h"
#include "RegistrationTypes.h"

// ─────────────────────────────────────────────────────────────
//  CENTRAL STORAGE ENGINE CLASS
// ─────────────────────────────────────────────────────────────
class CentralStorage {
public:
  CentralStorage();

  // Initialization & Boot Recovery
  bool init();
  bool recoverStorage();
  bool verifyStorageIntegrity();

  // 1. Central Node Registry (50 Nodes)
  bool saveNode(const MscbNodeRecord &node);
  bool getNode(const char *nodeIdOrHwId, MscbNodeRecord &outNode);
  bool updateNode(const char *nodeId, const MscbNodeRecord &node);
  bool deleteNode(const char *nodeId, const char *adminId, const char *reason);
  bool listNodes(MscbNodeRecord *outNodes, uint8_t maxCount, uint8_t &actualCount);
  uint8_t getNodeCount() const;
  bool isRegistryFull() const;

  // 2. Feeder Registry (300 Feeders, 6 per Pole)
  bool saveFeeder(const MscbFeederRecord &feeder, const char *owningPoleId);
  bool getFeeder(const char *owningPoleId, uint8_t feederId, MscbFeederRecord &outFeeder);
  bool listFeedersForPole(const char *owningPoleId, MscbFeederRecord *outFeeders, uint8_t maxCount, uint8_t &actualCount);
  uint16_t getTotalFeederCount() const;

  // 3. Calibration Storage & Metadata
  bool saveCalibrationMeta(const char *poleId, const GwCalibMeta &meta);
  bool getCalibrationMeta(const char *poleId, GwCalibMeta &outMeta);
  // Optional local table compatibility API
  bool saveCalibration(const PoleCalibTable &calibTable);
  bool getCalibration(PoleCalibTable &outTable);

  // 4. Configuration Persistence & Versioning (Dual-Bank Rollback)
  bool saveConfiguration(const SystemConfigRecord &config);
  bool getConfiguration(SystemConfigRecord &outConfig);
  bool migrateConfiguration(uint16_t fromSchema, uint16_t toSchema);
  uint16_t getActiveConfigVersion() const;

  // 5. Audit History & Event Database (Bounded Ring Buffer)
  bool addAuditEvent(const AuditEventRecord &event);
  bool addAuditEvent(uint16_t eventType, uint8_t category, const char *deviceId,
                     const char *actorId, uint8_t result, const char *desc,
                     const char *txId = "");
  bool getAuditHistory(AuditEventRecord *outEvents, uint16_t maxCount,
                       uint16_t &actualCount, uint8_t categoryFilter = 0xFF);
  uint16_t getAuditEventCount() const;

  // 6. Persistent Revocation Registry
  bool saveRevocation(const char *hwId, const char *logicalId, const char *reason,
                      const char *txId, const char *adminId);
  bool isRevoked(const char *hwIdOrLogicalId) const;
  uint8_t getRevokedCount() const;
  const PermanentRevocationRecord* getRevocationRecord(uint8_t index) const;

  // 7. Node Offline & Measurement Freshness Preservation
  bool markNodeOffline(const char *nodeId);
  bool markNodeOnline(const char *nodeId, int16_t rssi, int8_t snr,
                      const char *currentParent, uint32_t nowTime,
                      uint32_t &outDataGapSec);
  bool updatePreservedMeasurements(const char *nodeId, float vR, float vY, float vB,
                                   float current, float power, float energyKwh);
  uint8_t getMeasurementFreshness(const char *nodeId) const;

  // 8. Transaction Management & 2PC Journal
  bool beginTransaction(uint8_t txType, const char *targetId, const char *actorId,
                        char outTxId[16]);
  bool commitTransaction(const char *txId);
  bool rollbackTransaction(const char *txId, const char *reason);
  const TxJournalRecord& getActiveJournal() const;

  // 9. Diagnostics, Memory & Wear Metrics
  size_t getNvsEstimatedUsage() const;
  size_t getRamUsageBytes() const;
  uint32_t getTotalFlashWrites() const;
  float calculateFlashWearEstimateYears(uint32_t writesPerDay = 10) const;

  // Fault Injection / Testing Hooks
  void injectBitFlipCorruption(uint8_t targetSlot);
  void simulatePowerFailureDuringPrepare();
  void clearAll();

  // Public CRC calculation utilities
  uint32_t calculateNodeCrc(const MscbNodeRecord &n) const;
  uint32_t calculateFeederCrc(const LogicalFeederRecord &f) const;
  uint32_t calculateConfigCrc(const SystemConfigRecord &c) const;
  uint32_t calculateAuditCrc(const AuditEventRecord &a) const;
  uint32_t calculateRevocationCrc(const PermanentRevocationRecord &r) const;

private:
  // In-memory Authoritative Caches
  MscbNodeRecord            nodes[MSCB_MAX_POLE_NODES];
  uint8_t                   nodeCount;

  LogicalFeederRecord       feeders[MSCB_TOTAL_FEEDERS]; // 300 Feeders
  uint16_t                  feederCount;

  PermanentRevocationRecord revocations[MSCB_MAX_REVOCATION_RECORDS];
  uint8_t                   revocationCount;

  GwCalibMeta               calibMeta[MSCB_MAX_POLE_NODES];

  AuditEventRecord          auditRing[MSCB_MAX_AUDIT_EVENTS];
  uint16_t                  auditHead;
  uint16_t                  auditCount;
  uint32_t                  nextEventId;

  // Dual-Bank Configuration
  SystemConfigRecord        configBanks[2]; // Bank 0 & Bank 1 (Ping-Pong)
  uint8_t                   activeConfigBank;

  // Transaction Journal
  TxJournalRecord           journal;
  uint32_t                  txSeqCounter;

  // Wear Tracking
  uint32_t                  flashWriteCounter;

#ifdef ARDUINO
  Preferences prefsReg;
  Preferences prefsCfg;
  Preferences prefsAud;
  Preferences prefsTx;
  Preferences prefsRev;
#else
  // Host simulation persistent flash storage layers
  static uint8_t  simNvsReg[MSCB_MAX_POLE_NODES][sizeof(MscbNodeRecord)];
  static bool     simNvsRegValid[MSCB_MAX_POLE_NODES];
  static uint8_t  simNvsFdr[MSCB_TOTAL_FEEDERS][sizeof(LogicalFeederRecord)];
  static bool     simNvsFdrValid[MSCB_TOTAL_FEEDERS];
  static uint8_t  simNvsCfg[2][sizeof(SystemConfigRecord)];
  static bool     simNvsCfgValid[2];
  static uint8_t  simNvsRev[MSCB_MAX_REVOCATION_RECORDS][sizeof(PermanentRevocationRecord)];
  static bool     simNvsRevValid[MSCB_MAX_REVOCATION_RECORDS];
  static uint8_t  simNvsAud[MSCB_MAX_AUDIT_EVENTS][sizeof(AuditEventRecord)];
  static bool     simNvsAudValid[MSCB_MAX_AUDIT_EVENTS];
  static TxJournalRecord simJournal;
  static bool     simJournalValid;
#endif

  // Internal helpers
  int  findNodeSlot(const char *nodeIdOrHwId) const;
  int  findFeederSlot(const char *owningPoleId, uint8_t feederId) const;
  void initSafeDefaultConfig(SystemConfigRecord &cfg, const char *nodeId);
  bool validateConfigSafety(const SystemConfigRecord &cfg) const;

  // Persistent slot read/write
  bool persistNodeSlot(uint8_t slot, const MscbNodeRecord &rec);
  bool loadNodeSlot(uint8_t slot, MscbNodeRecord &outRec);
  bool persistFeederSlot(uint16_t slot, const LogicalFeederRecord &rec);
  bool loadFeederSlot(uint16_t slot, LogicalFeederRecord &outRec);
  bool persistConfigBank(uint8_t bankIdx, const SystemConfigRecord &cfg);
  bool loadConfigBank(uint8_t bankIdx, SystemConfigRecord &outCfg);
  bool persistRevocationSlot(uint8_t slot, const PermanentRevocationRecord &rec);
  bool loadRevocationSlot(uint8_t slot, PermanentRevocationRecord &outRec);
  bool persistJournal(const TxJournalRecord &j);
  bool loadJournal(TxJournalRecord &outJ);
};

extern CentralStorage centralStorage;

#endif // CENTRAL_STORAGE_H
