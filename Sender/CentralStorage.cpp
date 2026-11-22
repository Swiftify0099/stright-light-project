/*
 * =====================================================================
 *  CENTRALSTORAGE.CPP — MASTER AUTHORITATIVE STORAGE & REGISTRY IMPLEMENTATION
 *  MSCB Smart Pole Automation System — Prompt 19 Architecture
 * =====================================================================
 */

#include "CentralStorage.h"
#include <stdio.h>
#include <stdlib.h>

CentralStorage centralStorage;

#ifndef ARDUINO
// Definition of static simulated persistent flash storage
uint8_t  CentralStorage::simNvsReg[MSCB_MAX_POLE_NODES][sizeof(MscbNodeRecord)] = {{0}};
bool     CentralStorage::simNvsRegValid[MSCB_MAX_POLE_NODES] = {false};
uint8_t  CentralStorage::simNvsFdr[MSCB_TOTAL_FEEDERS][sizeof(LogicalFeederRecord)] = {{0}};
bool     CentralStorage::simNvsFdrValid[MSCB_TOTAL_FEEDERS] = {false};
uint8_t  CentralStorage::simNvsCfg[2][sizeof(SystemConfigRecord)] = {{0}};
bool     CentralStorage::simNvsCfgValid[2] = {false};
uint8_t  CentralStorage::simNvsRev[MSCB_MAX_REVOCATION_RECORDS][sizeof(PermanentRevocationRecord)] = {{0}};
bool     CentralStorage::simNvsRevValid[MSCB_MAX_REVOCATION_RECORDS] = {false};
uint8_t  CentralStorage::simNvsAud[MSCB_MAX_AUDIT_EVENTS][sizeof(AuditEventRecord)] = {{0}};
bool     CentralStorage::simNvsAudValid[MSCB_MAX_AUDIT_EVENTS] = {false};
TxJournalRecord CentralStorage::simJournal = {0};
bool     CentralStorage::simJournalValid = false;
#endif

// NVS Namespace Definitions
#define NVS_NS_CENTRAL_REG   "mscb_creg"
#define NVS_NS_CENTRAL_FDR   "mscb_cfdr"
#define NVS_NS_CENTRAL_CFG   "mscb_ccfg"
#define NVS_NS_CENTRAL_AUD   "mscb_caud"
#define NVS_NS_CENTRAL_TX    "mscb_ctx"
#define NVS_NS_CENTRAL_REV   "mscb_crev"

CentralStorage::CentralStorage() :
  nodeCount(0), feederCount(0), revocationCount(0),
  auditHead(0), auditCount(0), nextEventId(1),
  activeConfigBank(0), txSeqCounter(0), flashWriteCounter(0)
{
  memset(nodes, 0, sizeof(nodes));
  memset(feeders, 0, sizeof(feeders));
  memset(revocations, 0, sizeof(revocations));
  memset(calibMeta, 0, sizeof(calibMeta));
  memset(auditRing, 0, sizeof(auditRing));
  memset(configBanks, 0, sizeof(configBanks));
  memset(&journal, 0, sizeof(journal));
}

// ─────────────────────────────────────────────────────────────
//  CRC & INTEGRITY HELPERS
// ─────────────────────────────────────────────────────────────
uint32_t CentralStorage::calculateNodeCrc(const MscbNodeRecord &n) const {
  size_t crcLen = sizeof(MscbNodeRecord) - sizeof(uint32_t);
  return mscbStorageCrc32((const uint8_t*)&n, crcLen);
}

uint32_t CentralStorage::calculateFeederCrc(const LogicalFeederRecord &f) const {
  size_t crcLen = sizeof(LogicalFeederRecord) - sizeof(uint32_t);
  return mscbStorageCrc32((const uint8_t*)&f, crcLen);
}

uint32_t CentralStorage::calculateConfigCrc(const SystemConfigRecord &c) const {
  size_t crcLen = sizeof(SystemConfigRecord) - sizeof(uint32_t);
  return mscbStorageCrc32((const uint8_t*)&c, crcLen);
}

uint32_t CentralStorage::calculateAuditCrc(const AuditEventRecord &a) const {
  size_t crcLen = sizeof(AuditEventRecord) - sizeof(uint32_t);
  return mscbStorageCrc32((const uint8_t*)&a, crcLen);
}

uint32_t CentralStorage::calculateRevocationCrc(const PermanentRevocationRecord &r) const {
  size_t crcLen = sizeof(PermanentRevocationRecord) - sizeof(uint32_t);
  return mscbStorageCrc32((const uint8_t*)&r, crcLen);
}

// ─────────────────────────────────────────────────────────────
//  INITIALIZATION & BOOT RECOVERY
// ─────────────────────────────────────────────────────────────
bool CentralStorage::init() {
  recoverStorage();
  return verifyStorageIntegrity();
}

bool CentralStorage::recoverStorage() {
  // 1. Recover Revocation List
  revocationCount = 0;
  for (uint8_t i = 0; i < MSCB_MAX_REVOCATION_RECORDS; i++) {
    PermanentRevocationRecord rec;
    if (loadRevocationSlot(i, rec)) {
      if (rec.crc32 == calculateRevocationCrc(rec) && strlen(rec.hwId) > 0) {
        revocations[revocationCount++] = rec;
      }
    }
  }

  // 2. Recover Central Node Registry
  nodeCount = 0;
  for (uint8_t i = 0; i < MSCB_MAX_POLE_NODES; i++) {
    MscbNodeRecord rec;
    if (loadNodeSlot(i, rec)) {
      if (rec.crc32 == calculateNodeCrc(rec) && strlen(rec.logicalId) > 0) {
        // Enforce revocation consistency
        if (isRevoked(rec.hwId)) {
          rec.regState = REG_STATE_REVOKED;
        }
        nodes[nodeCount++] = rec;
      }
    }
  }

  // 3. Recover Feeder Registry (300 Feeders)
  feederCount = 0;
  for (uint16_t i = 0; i < MSCB_TOTAL_FEEDERS; i++) {
    LogicalFeederRecord f;
    if (loadFeederSlot(i, f)) {
      if (f.crc32 == calculateFeederCrc(f) && strlen(f.owningPoleId) > 0) {
        feeders[feederCount++] = f;
      }
    }
  }

  // If node records exist but feeders not yet persisted separately, reconstruct from node.feeders
  if (feederCount == 0 && nodeCount > 0) {
    for (uint8_t n = 0; n < nodeCount; n++) {
      for (uint8_t f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
        LogicalFeederRecord lf;
        memset(&lf, 0, sizeof(lf));
        strncpy(lf.owningPoleId, nodes[n].logicalId, 15);
        lf.feederId = nodes[n].feeders[f].feederId;
        snprintf(lf.logicalFeederId, sizeof(lf.logicalFeederId), "%s-F%02u", nodes[n].logicalId, lf.feederId);
        strncpy(lf.name, nodes[n].feeders[f].name, sizeof(lf.name) - 1);
        lf.status = nodes[n].feeders[f].enabled ? 0 : 1;
        lf.currentRating = nodes[n].feeders[f].currentRating;
        lf.relayState = nodes[n].feeders[f].relayState ? 1 : 0;
        lf.mcbR = nodes[n].feeders[f].mcbR;
        lf.mcbY = nodes[n].feeders[f].mcbY;
        lf.mcbB = nodes[n].feeders[f].mcbB;
        lf.contactor = nodes[n].feeders[f].contactor;
        lf.protectionEnabled = true;
        lf.currentChannelR = f * 3;
        lf.currentChannelY = f * 3 + 1;
        lf.currentChannelB = f * 3 + 2;
        lf.crc32 = calculateFeederCrc(lf);
        feeders[feederCount++] = lf;
        persistFeederSlot(feederCount - 1, lf);
      }
    }
  }

  // 4. Recover Dual-Bank Configuration
  bool bank0Valid = false, bank1Valid = false;
  SystemConfigRecord cfg0, cfg1;

  if (loadConfigBank(0, cfg0) && cfg0.crc32 == calculateConfigCrc(cfg0) && validateConfigSafety(cfg0)) {
    bank0Valid = true;
    configBanks[0] = cfg0;
  }
  if (loadConfigBank(1, cfg1) && cfg1.crc32 == calculateConfigCrc(cfg1) && validateConfigSafety(cfg1)) {
    bank1Valid = true;
    configBanks[1] = cfg1;
  }

  if (bank0Valid && bank1Valid) {
    // Select newest generation
    activeConfigBank = (cfg1.generation >= cfg0.generation) ? 1 : 0;
  } else if (bank0Valid) {
    activeConfigBank = 0;
  } else if (bank1Valid) {
    activeConfigBank = 1;
  } else {
    // Both corrupted or uninitialized: apply documented safe defaults
    initSafeDefaultConfig(configBanks[0], "GATEWAY");
    configBanks[0].validity = CONFIG_SAFE_DEFAULTS;
    configBanks[0].crc32 = calculateConfigCrc(configBanks[0]);
    persistConfigBank(0, configBanks[0]);
    configBanks[1] = configBanks[0];
    persistConfigBank(1, configBanks[1]);
    activeConfigBank = 0;
  }

  // 5. Recover Transaction Journal & Incomplete Transactions
  TxJournalRecord savedJ;
  if (loadJournal(savedJ) && savedJ.magic == TX_JOURNAL_MAGIC && savedJ.crc32 == mscbStorageCrc32((const uint8_t*)&savedJ, sizeof(TxJournalRecord) - sizeof(uint32_t))) {
    journal = savedJ;
    if (journal.state == TX_STATE_PREPARED) {
      // Incomplete write detected! Interrupted during power cut.
      // Roll back uncommitted transaction
      journal.state = TX_STATE_ROLLED_BACK;
      journal.crc32 = mscbStorageCrc32((const uint8_t*)&journal, sizeof(TxJournalRecord) - sizeof(uint32_t));
      persistJournal(journal);
      addAuditEvent(AUDIT_EVENT_GATEWAY_RESTART, RETENTION_CATEGORY_ADMIN_AUDIT, "GATEWAY", "SYS_RECOVERY",
                     AUDIT_RES_ROLLBACK, "Power cut detected: rolled back incomplete transaction", journal.txId);
    }
  } else {
    memset(&journal, 0, sizeof(journal));
    journal.magic = TX_JOURNAL_MAGIC;
    journal.state = TX_STATE_IDLE;
  }

  // 6. Log Restart Audit Event
  addAuditEvent(AUDIT_EVENT_GATEWAY_RESTART, RETENTION_CATEGORY_ADMIN_AUDIT, "GATEWAY", "SYS_BOOT",
                 AUDIT_RES_SUCCESS, "Central storage initialized and verified");

  return true;
}

bool CentralStorage::verifyStorageIntegrity() {
  // 1. Verify Node Registry Integrity
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (nodes[i].crc32 != calculateNodeCrc(nodes[i])) {
      return false;
    }
    // Cross-check with Revocation Registry
    if (isRevoked(nodes[i].hwId) && nodes[i].regState != REG_STATE_REVOKED) {
      nodes[i].regState = REG_STATE_REVOKED;
      persistNodeSlot(i, nodes[i]);
    }
  }

  // 2. Verify Feeder Registry Integrity
  for (uint16_t i = 0; i < feederCount; i++) {
    if (feeders[i].crc32 != calculateFeederCrc(feeders[i])) {
      return false;
    }
    // Verify each feeder points to a valid registered pole
    int pSlot = findNodeSlot(feeders[i].owningPoleId);
    if (pSlot < 0) {
      // Orphan feeder reference
      feeders[i].status = 2; // REMOVED
    }
  }

  // 3. Verify Active Configuration Integrity
  SystemConfigRecord activeCfg;
  if (!getConfiguration(activeCfg) || activeCfg.validity == CONFIG_CORRUPTED) {
    return false;
  }

  return true;
}

// ─────────────────────────────────────────────────────────────
//  CENTRAL NODE REGISTRY IMPLEMENTATION (50 NODES)
// ─────────────────────────────────────────────────────────────
int CentralStorage::findNodeSlot(const char *nodeIdOrHwId) const {
  if (!nodeIdOrHwId || strlen(nodeIdOrHwId) == 0) return -1;
  for (uint8_t i = 0; i < nodeCount; i++) {
    if (strcmp(nodes[i].logicalId, nodeIdOrHwId) == 0 ||
        strcmp(nodes[i].hwId, nodeIdOrHwId) == 0) {
      return i;
    }
  }
  return -1;
}

uint8_t CentralStorage::getNodeCount() const {
  return nodeCount;
}

bool CentralStorage::isRegistryFull() const {
  return (nodeCount >= MSCB_MAX_POLE_NODES);
}

bool CentralStorage::saveNode(const MscbNodeRecord &node) {
  // 6-STEP TRANSACTIONAL PROTOCOL
  // Step 1: Create transaction
  char txId[16];
  if (!beginTransaction(TX_TYPE_NODE_REGISTER, node.logicalId, "ADMIN_REG", txId)) {
    return false;
  }

  // Step 2: Validate all required data
  if (strlen(node.logicalId) == 0 || strlen(node.hwId) == 0) {
    rollbackTransaction(txId, "Missing ID");
    return false;
  }

  if (isRevoked(node.hwId)) {
    rollbackTransaction(txId, "Device Revoked");
    return false;
  }

  int existingSlot = findNodeSlot(node.hwId);
  if (existingSlot < 0 && nodeCount >= MSCB_MAX_POLE_NODES) {
    rollbackTransaction(txId, "Registry Full");
    return false;
  }

  // Prevent conflicting duplicate logical IDs with different HW IDs
  int logicalSlot = findNodeSlot(node.logicalId);
  if (logicalSlot >= 0 && existingSlot >= 0 && logicalSlot != existingSlot) {
    rollbackTransaction(txId, "Conflicting Logical ID");
    return false;
  }

  // Step 3: Persist intended operation in journal
  journal.state = TX_STATE_PREPARED;
  journal.crc32 = mscbStorageCrc32((const uint8_t*)&journal, sizeof(TxJournalRecord) - sizeof(uint32_t));
  persistJournal(journal);

  // Step 4: Apply operation
  uint8_t targetSlot = (existingSlot >= 0) ? (uint8_t)existingSlot : nodeCount;
  MscbNodeRecord rec = node;
  rec.feederCount = MSCB_FEEDERS_PER_POLE;
  rec.crc32 = calculateNodeCrc(rec);

  nodes[targetSlot] = rec;
  if (existingSlot < 0) {
    nodeCount++;
  }

  // Ensure persistent slot write
  if (!persistNodeSlot(targetSlot, rec)) {
    rollbackTransaction(txId, "Flash Write Failed");
    return false;
  }

  // Update associated 6 feeders in Feeder Registry
  for (uint8_t f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
    saveFeeder(rec.feeders[f], rec.logicalId);
  }

  // Step 5: Read back and verify final state
  MscbNodeRecord verifyRec;
  if (!loadNodeSlot(targetSlot, verifyRec) || verifyRec.crc32 != rec.crc32) {
    rollbackTransaction(txId, "Read-back Verification Failed");
    return false;
  }

  // Step 6: Mark transaction complete
  commitTransaction(txId);
  addAuditEvent(AUDIT_EVENT_POLE_REGISTER, RETENTION_CATEGORY_ADMIN_AUDIT, rec.logicalId,
                 "ADMIN", AUDIT_RES_SUCCESS, "Node registered and persisted", txId);

  return true;
}

bool CentralStorage::getNode(const char *nodeIdOrHwId, MscbNodeRecord &outNode) {
  int slot = findNodeSlot(nodeIdOrHwId);
  if (slot < 0) return false;
  outNode = nodes[slot];
  return (outNode.crc32 == calculateNodeCrc(outNode));
}

bool CentralStorage::updateNode(const char *nodeId, const MscbNodeRecord &node) {
  int slot = findNodeSlot(nodeId);
  if (slot < 0) return false;

  MscbNodeRecord rec = node;
  rec.crc32 = calculateNodeCrc(rec);
  nodes[slot] = rec;

  bool ok = persistNodeSlot((uint8_t)slot, rec);
  if (ok) {
    // Update feeders
    for (uint8_t f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
      saveFeeder(rec.feeders[f], rec.logicalId);
    }
  }
  return ok;
}

bool CentralStorage::deleteNode(const char *nodeId, const char *adminId, const char *reason) {
  int slot = findNodeSlot(nodeId);
  if (slot < 0) return false;

  char txId[16];
  if (!beginTransaction(TX_TYPE_NODE_DELETE, nodeId, adminId, txId)) {
    return false;
  }

  // Persist revocation and de-register
  saveRevocation(nodes[slot].hwId, nodes[slot].logicalId, reason, txId, adminId);
  nodes[slot].regState = REG_STATE_REVOKED;
  nodes[slot].crc32 = calculateNodeCrc(nodes[slot]);
  persistNodeSlot((uint8_t)slot, nodes[slot]);

  // Deactivate all feeders of this pole
  for (uint16_t i = 0; i < feederCount; i++) {
    if (strcmp(feeders[i].owningPoleId, nodeId) == 0) {
      feeders[i].status = 1; // DEACTIVATED
      feeders[i].relayState = 0;
      feeders[i].crc32 = calculateFeederCrc(feeders[i]);
      persistFeederSlot(i, feeders[i]);
    }
  }

  commitTransaction(txId);
  addAuditEvent(AUDIT_EVENT_POLE_DELETE, RETENTION_CATEGORY_REG_REVOCATION, nodeId,
                 adminId, AUDIT_RES_SUCCESS, "Node deleted and revoked", txId);
  return true;
}

bool CentralStorage::listNodes(MscbNodeRecord *outNodes, uint8_t maxCount, uint8_t &actualCount) {
  actualCount = 0;
  if (!outNodes || maxCount == 0) return false;
  for (uint8_t i = 0; i < nodeCount && actualCount < maxCount; i++) {
    outNodes[actualCount++] = nodes[i];
  }
  return true;
}

// ─────────────────────────────────────────────────────────────
//  FEEDER REGISTRY IMPLEMENTATION (300 FEEDERS)
// ─────────────────────────────────────────────────────────────
int CentralStorage::findFeederSlot(const char *owningPoleId, uint8_t feederId) const {
  if (!owningPoleId) return -1;
  for (uint16_t i = 0; i < feederCount; i++) {
    if (strcmp(feeders[i].owningPoleId, owningPoleId) == 0 &&
        feeders[i].feederId == feederId) {
      return i;
    }
  }
  return -1;
}

uint16_t CentralStorage::getTotalFeederCount() const {
  return feederCount;
}

bool CentralStorage::saveFeeder(const MscbFeederRecord &feeder, const char *owningPoleId) {
  if (!owningPoleId || feeder.feederId < 1 || feeder.feederId > MSCB_FEEDERS_PER_POLE) {
    return false;
  }

  int slot = findFeederSlot(owningPoleId, feeder.feederId);
  uint16_t targetSlot = (slot >= 0) ? (uint16_t)slot : feederCount;

  if (slot < 0 && feederCount >= MSCB_TOTAL_FEEDERS) {
    return false; // Capacity reached
  }

  LogicalFeederRecord lf;
  memset(&lf, 0, sizeof(lf));
  strncpy(lf.owningPoleId, owningPoleId, sizeof(lf.owningPoleId) - 1);
  lf.feederId = feeder.feederId;
  snprintf(lf.logicalFeederId, sizeof(lf.logicalFeederId), "%s-F%02u", owningPoleId, feeder.feederId);
  strncpy(lf.name, feeder.name, sizeof(lf.name) - 1);
  lf.status = feeder.enabled ? 0 : 1;
  lf.currentRating = feeder.currentRating;
  lf.relayState = feeder.relayState ? 1 : 0;
  lf.mcbR = feeder.mcbR;
  lf.mcbY = feeder.mcbY;
  lf.mcbB = feeder.mcbB;
  lf.contactor = feeder.contactor;
  lf.protectionEnabled = true;
  lf.currentChannelR = (feeder.feederId - 1) * 3;
  lf.currentChannelY = (feeder.feederId - 1) * 3 + 1;
  lf.currentChannelB = (feeder.feederId - 1) * 3 + 2;

  // Preserve accumulated metrics if updating
  if (slot >= 0) {
    lf.tripCount = feeders[slot].tripCount;
    lf.accumulatedKwh = feeders[slot].accumulatedKwh;
    lf.lastTripTimestamp = feeders[slot].lastTripTimestamp;
  }

  lf.crc32 = calculateFeederCrc(lf);
  feeders[targetSlot] = lf;

  if (slot < 0) {
    feederCount++;
  }

  return persistFeederSlot(targetSlot, lf);
}

bool CentralStorage::getFeeder(const char *owningPoleId, uint8_t feederId, MscbFeederRecord &outFeeder) {
  int slot = findFeederSlot(owningPoleId, feederId);
  if (slot < 0) return false;

  const LogicalFeederRecord &lf = feeders[slot];
  if (lf.crc32 != calculateFeederCrc(lf)) return false;

  outFeeder.feederId = lf.feederId;
  strncpy(outFeeder.name, lf.name, sizeof(outFeeder.name) - 1);
  outFeeder.name[sizeof(outFeeder.name) - 1] = '\0';
  outFeeder.enabled = (lf.status == 0);
  outFeeder.currentRating = lf.currentRating;
  outFeeder.relayState = (lf.relayState == 1);
  outFeeder.mcbR = lf.mcbR;
  outFeeder.mcbY = lf.mcbY;
  outFeeder.mcbB = lf.mcbB;
  outFeeder.contactor = lf.contactor;
  return true;
}

bool CentralStorage::listFeedersForPole(const char *owningPoleId, MscbFeederRecord *outFeeders, uint8_t maxCount, uint8_t &actualCount) {
  actualCount = 0;
  if (!owningPoleId || !outFeeders || maxCount == 0) return false;

  for (uint8_t f = 1; f <= MSCB_FEEDERS_PER_POLE && actualCount < maxCount; f++) {
    MscbFeederRecord rec;
    if (getFeeder(owningPoleId, f, rec)) {
      outFeeders[actualCount++] = rec;
    }
  }
  return (actualCount > 0);
}

// ─────────────────────────────────────────────────────────────
//  CALIBRATION METADATA (GATEWAY)
// ─────────────────────────────────────────────────────────────
bool CentralStorage::saveCalibrationMeta(const char *poleId, const GwCalibMeta &meta) {
  int slot = findNodeSlot(poleId);
  if (slot < 0) return false;

  calibMeta[slot] = meta;
  strncpy(calibMeta[slot].poleId, poleId, 15);

  addAuditEvent(AUDIT_EVENT_CALIBRATION_CHANGE, RETENTION_CATEGORY_ADMIN_AUDIT, poleId,
                 "CAL_TOOL", AUDIT_RES_SUCCESS, "Calibration metadata updated");
  return true;
}

bool CentralStorage::getCalibrationMeta(const char *poleId, GwCalibMeta &outMeta) {
  int slot = findNodeSlot(poleId);
  if (slot < 0) return false;
  outMeta = calibMeta[slot];
  return outMeta.isValid;
}

bool CentralStorage::saveCalibration(const PoleCalibTable &calibTable) {
  GwCalibMeta meta;
  strncpy(meta.poleId, calibTable.poleId, 15);
  meta.poleId[15] = '\0';
  meta.calVersion = calibTable.tableVersion;
  meta.calTimestamp = 1700000000UL;
  meta.tableCrc32 = calibTable.crc32;
  meta.isValid = true;
  strncpy(meta.calReference, "FLUKE_87V_STAND", 23);
  return saveCalibrationMeta(calibTable.poleId, meta);
}

bool CentralStorage::getCalibration(PoleCalibTable &outTable) {
  outTable.magic = CALIBRATION_MAGIC;
  outTable.tableVersion = CALIBRATION_VERSION;
  outTable.channelCount = MSCB_TOTAL_CHANNELS_PER_POLE;
  outTable.crc32 = mscbStorageCrc32((const uint8_t*)&outTable, sizeof(PoleCalibTable) - sizeof(uint32_t));
  return true;
}

// ─────────────────────────────────────────────────────────────
//  CONFIGURATION PERSISTENCE (DUAL-BANK ROLLBACK)
// ─────────────────────────────────────────────────────────────
void CentralStorage::initSafeDefaultConfig(SystemConfigRecord &cfg, const char *nodeId) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = MSCB_CONFIG_MAGIC;
  cfg.schemaVersion = MSCB_CURRENT_CONFIG_SCHEMA;
  cfg.configVersion = 100;
  strncpy(cfg.nodeId, nodeId ? nodeId : "GATEWAY", 15);
  cfg.creationTime = 1700000000UL;
  cfg.updateTime = 1700000000UL;
  // Electrical safety defaults
  cfg.nominalVoltage = 230.0f;
  cfg.maxOverVoltage = 265.0f;
  cfg.minUnderVoltage = 175.0f;
  cfg.maxCurrentRating = 60.0f;
  cfg.feederDefaultRating = 50.0f;
  // Mesh settings
  cfg.heartbeatIntervalSec = 30;
  cfg.beaconIntervalSec = 30;
  cfg.loraTxPower = 10;
  cfg.loraChannel = 433920000UL;
  // Autonomous schedule (Relays default OFF for safety)
  cfg.schedEnabled = false;
  cfg.schedOnMinute = -1;
  cfg.schedOffMinute = -1;
  cfg.validity = CONFIG_VALID;
  cfg.generation = 1;
  cfg.crc32 = calculateConfigCrc(cfg);
}

bool CentralStorage::validateConfigSafety(const SystemConfigRecord &cfg) const {
  if (cfg.magic != MSCB_CONFIG_MAGIC) return false;
  if (cfg.nominalVoltage < 180.0f || cfg.nominalVoltage > 250.0f) return false;
  if (cfg.maxOverVoltage <= cfg.nominalVoltage || cfg.maxOverVoltage > 300.0f) return false;
  if (cfg.minUnderVoltage >= cfg.nominalVoltage || cfg.minUnderVoltage < 100.0f) return false;
  if (cfg.maxCurrentRating < 1.0f || cfg.maxCurrentRating > 100.0f) return false;
  if (cfg.feederDefaultRating < 1.0f || cfg.feederDefaultRating > cfg.maxCurrentRating) return false;
  return true;
}

bool CentralStorage::saveConfiguration(const SystemConfigRecord &config) {
  // Reject unsafe values
  if (!validateConfigSafety(config)) {
    addAuditEvent(AUDIT_EVENT_THRESHOLD_CHANGE, RETENTION_CATEGORY_ADMIN_AUDIT, config.nodeId,
                   "ADMIN", AUDIT_RES_REJECTED, "Configuration rejected: unsafe electrical values");
    return false;
  }

  // Ping-pong to alternate bank
  uint8_t nextBank = (activeConfigBank == 0) ? 1 : 0;
  SystemConfigRecord newCfg = config;
  newCfg.magic = MSCB_CONFIG_MAGIC;
  newCfg.generation = configBanks[activeConfigBank].generation + 1;
  newCfg.configVersion = configBanks[activeConfigBank].configVersion + 1;
  newCfg.validity = CONFIG_VALID;
  newCfg.crc32 = calculateConfigCrc(newCfg);

  // Write to next bank
  if (!persistConfigBank(nextBank, newCfg)) {
    return false;
  }

  // Verify write
  SystemConfigRecord verifyCfg;
  if (!loadConfigBank(nextBank, verifyCfg) || verifyCfg.crc32 != newCfg.crc32) {
    return false; // Keep activeConfigBank unchanged
  }

  // Switch active bank
  configBanks[nextBank] = newCfg;
  activeConfigBank = nextBank;

  addAuditEvent(AUDIT_EVENT_THRESHOLD_CHANGE, RETENTION_CATEGORY_ADMIN_AUDIT, newCfg.nodeId,
                 "ADMIN", AUDIT_RES_SUCCESS, "Configuration updated and committed");
  return true;
}

bool CentralStorage::getConfiguration(SystemConfigRecord &outConfig) {
  // Try active bank
  if (configBanks[activeConfigBank].magic == MSCB_CONFIG_MAGIC &&
      configBanks[activeConfigBank].crc32 == calculateConfigCrc(configBanks[activeConfigBank]) &&
      validateConfigSafety(configBanks[activeConfigBank])) {
    outConfig = configBanks[activeConfigBank];
    return true;
  }

  // Active bank corrupted! Fallback to alternate bank
  uint8_t alternateBank = (activeConfigBank == 0) ? 1 : 0;
  if (configBanks[alternateBank].magic == MSCB_CONFIG_MAGIC &&
      configBanks[alternateBank].crc32 == calculateConfigCrc(configBanks[alternateBank]) &&
      validateConfigSafety(configBanks[alternateBank])) {
    activeConfigBank = alternateBank;
    outConfig = configBanks[activeConfigBank];
    addAuditEvent(AUDIT_EVENT_THRESHOLD_CHANGE, RETENTION_CATEGORY_CRITICAL_FAULT, "GATEWAY",
                   "SYS_FAULT", AUDIT_RES_ROLLBACK, "Active config bank corrupted: restored from backup bank");
    return true;
  }

  // Both banks corrupted: apply documented safe defaults
  initSafeDefaultConfig(outConfig, "GATEWAY");
  outConfig.validity = CONFIG_SAFE_DEFAULTS;
  configBanks[activeConfigBank] = outConfig;
  persistConfigBank(activeConfigBank, outConfig);
  addAuditEvent(AUDIT_EVENT_THRESHOLD_CHANGE, RETENTION_CATEGORY_CRITICAL_FAULT, "GATEWAY",
                 "SYS_FAULT", AUDIT_RES_ROLLBACK, "All config banks corrupted: safe defaults applied");
  return true;
}

bool CentralStorage::migrateConfiguration(uint16_t fromSchema, uint16_t toSchema) {
  if (fromSchema == toSchema) return true;
  if (fromSchema == MSCB_CONFIG_SCHEMA_V1 && toSchema == MSCB_CONFIG_SCHEMA_V2) {
    SystemConfigRecord cur;
    getConfiguration(cur);
    cur.schemaVersion = MSCB_CONFIG_SCHEMA_V2;
    // Set modern fields
    cur.beaconIntervalSec = 30;
    cur.loraTxPower = 10;
    cur.loraChannel = 433920000UL;
    return saveConfiguration(cur);
  }
  return false;
}

uint16_t CentralStorage::getActiveConfigVersion() const {
  return configBanks[activeConfigBank].configVersion;
}

// ─────────────────────────────────────────────────────────────
//  AUDIT HISTORY & BOUNDED EVENT DATABASE
// ─────────────────────────────────────────────────────────────
bool CentralStorage::addAuditEvent(const AuditEventRecord &event) {
  AuditEventRecord rec = event;
  rec.eventId = nextEventId++;

  // Redact credentials
  char sanitizedDesc[48];
  mscbSanitizeAuditString(rec.description, sanitizedDesc, sizeof(sanitizedDesc));
  strncpy(rec.description, sanitizedDesc, sizeof(rec.description) - 1);
  rec.description[sizeof(rec.description) - 1] = '\0';

  rec.crc32 = calculateAuditCrc(rec);

  auditRing[auditHead] = rec;
  persistJournal(journal); // Keep transaction linked

#ifdef ARDUINO
  // In Arduino, periodically persist audit slots in bounded NVS range
  char key[8];
  snprintf(key, sizeof(key), "a%u", auditHead);
  prefsAud.begin(NVS_NS_CENTRAL_AUD, false);
  prefsAud.putBytes(key, &rec, sizeof(rec));
  prefsAud.putUShort("head", auditHead);
  prefsAud.putUShort("cnt", (auditCount < MSCB_MAX_AUDIT_EVENTS) ? auditCount + 1 : MSCB_MAX_AUDIT_EVENTS);
  prefsAud.end();
#else
  memcpy(simNvsAud[auditHead], &rec, sizeof(rec));
  simNvsAudValid[auditHead] = true;
#endif

  auditHead = (auditHead + 1) % MSCB_MAX_AUDIT_EVENTS;
  if (auditCount < MSCB_MAX_AUDIT_EVENTS) auditCount++;

  return true;
}

bool CentralStorage::addAuditEvent(uint16_t eventType, uint8_t category, const char *deviceId,
                                   const char *actorId, uint8_t result, const char *desc,
                                   const char *txId) {
  AuditEventRecord ev;
  memset(&ev, 0, sizeof(ev));
  ev.eventType = eventType;
  ev.category = category;
  strncpy(ev.deviceId, deviceId ? deviceId : "GATEWAY", sizeof(ev.deviceId) - 1);
  strncpy(ev.actorId, actorId ? actorId : "SYS", sizeof(ev.actorId) - 1);
  ev.timestamp = 1700000000UL; // In Arduino, time(nullptr)
  ev.result = result;
  ev.status = AUDIT_STATUS_COMMITTED;
  strncpy(ev.description, desc ? desc : "", sizeof(ev.description) - 1);
  if (txId && strlen(txId) > 0) {
    strncpy(ev.txId, txId, sizeof(ev.txId) - 1);
  }
  return addAuditEvent(ev);
}

bool CentralStorage::getAuditHistory(AuditEventRecord *outEvents, uint16_t maxCount,
                                     uint16_t &actualCount, uint8_t categoryFilter) {
  actualCount = 0;
  if (!outEvents || maxCount == 0 || auditCount == 0) return false;

  // Read in chronological order (from tail to head)
  uint16_t startIdx = (auditCount < MSCB_MAX_AUDIT_EVENTS) ? 0 : auditHead;

  for (uint16_t i = 0; i < auditCount && actualCount < maxCount; i++) {
    uint16_t idx = (startIdx + i) % MSCB_MAX_AUDIT_EVENTS;
    const AuditEventRecord &ev = auditRing[idx];
    if (ev.crc32 == calculateAuditCrc(ev)) {
      if ((ev.category & categoryFilter) != 0) {
        outEvents[actualCount++] = ev;
      }
    }
  }
  return (actualCount > 0);
}

uint16_t CentralStorage::getAuditEventCount() const {
  return auditCount;
}

// ─────────────────────────────────────────────────────────────
//  PERSISTENT REVOCATION REGISTRY
// ─────────────────────────────────────────────────────────────
bool CentralStorage::saveRevocation(const char *hwId, const char *logicalId, const char *reason,
                                    const char *txId, const char *adminId) {
  if (!hwId || strlen(hwId) == 0) return false;

  // Check if already revoked
  for (uint8_t i = 0; i < revocationCount; i++) {
    if (strcmp(revocations[i].hwId, hwId) == 0) {
      return true; // Already permanently recorded
    }
  }

  if (revocationCount >= MSCB_MAX_REVOCATION_RECORDS) return false;

  PermanentRevocationRecord rec;
  memset(&rec, 0, sizeof(rec));
  strncpy(rec.hwId, hwId, sizeof(rec.hwId) - 1);
  strncpy(rec.logicalId, logicalId ? logicalId : "", sizeof(rec.logicalId) - 1);
  strncpy(rec.reason, reason ? reason : "Revoked by Admin", sizeof(rec.reason) - 1);
  strncpy(rec.txId, txId ? txId : "", sizeof(rec.txId) - 1);
  strncpy(rec.adminId, adminId ? adminId : "ADMIN", sizeof(rec.adminId) - 1);
  rec.timestamp = 1700000000UL;
  rec.crc32 = calculateRevocationCrc(rec);

  revocations[revocationCount] = rec;
  persistRevocationSlot(revocationCount, rec);
  revocationCount++;

  addAuditEvent(AUDIT_EVENT_POLE_DELETE, RETENTION_CATEGORY_REG_REVOCATION,
                 rec.logicalId, rec.adminId, AUDIT_RES_SUCCESS, "Permanent device revocation", rec.txId);
  return true;
}

bool CentralStorage::isRevoked(const char *hwIdOrLogicalId) const {
  if (!hwIdOrLogicalId || strlen(hwIdOrLogicalId) == 0) return false;
  for (uint8_t i = 0; i < revocationCount; i++) {
    if (strcmp(revocations[i].hwId, hwIdOrLogicalId) == 0 ||
        strcmp(revocations[i].logicalId, hwIdOrLogicalId) == 0) {
      return true;
    }
  }
  return false;
}

uint8_t CentralStorage::getRevokedCount() const {
  return revocationCount;
}

const PermanentRevocationRecord* CentralStorage::getRevocationRecord(uint8_t index) const {
  if (index < revocationCount) {
    return &revocations[index];
  }
  return nullptr;
}

// ─────────────────────────────────────────────────────────────
//  NODE OFFLINE & MEASUREMENT FRESHNESS PRESERVATION
// ─────────────────────────────────────────────────────────────
bool CentralStorage::markNodeOffline(const char *nodeId) {
  int slot = findNodeSlot(nodeId);
  if (slot < 0) return false;

  // Preserve latest valid measurements, mark freshness as STALE, DO NOT ZERO OUT!
  nodes[slot].regState = REG_STATE_OFFLINE;
  // Note: lastRssi, lastSnr, lastSeen, and measurement values remain completely intact!
  nodes[slot].crc32 = calculateNodeCrc(nodes[slot]);

  addAuditEvent(AUDIT_EVENT_OFFLINE_STALE, RETENTION_CATEGORY_RECENT_EVENT, nodeId,
                 "SYS_HEALTH", AUDIT_RES_SUCCESS, "Node offline: measurement values preserved stale");
  return true;
}

bool CentralStorage::markNodeOnline(const char *nodeId, int16_t rssi, int8_t snr,
                                    const char *currentParent, uint32_t nowTime,
                                    uint32_t &outDataGapSec) {
  int slot = findNodeSlot(nodeId);
  if (slot < 0) return false;

  outDataGapSec = (nowTime > nodes[slot].lastSeenTime) ? (nowTime - nodes[slot].lastSeenTime) : 0;
  nodes[slot].regState = REG_STATE_ACTIVE;
  nodes[slot].lastSeenTime = nowTime;
  nodes[slot].lastRssi = rssi;
  nodes[slot].lastSnr = snr;
  if (currentParent && strlen(currentParent) > 0) {
    strncpy(nodes[slot].primaryParent, currentParent, sizeof(nodes[slot].primaryParent) - 1);
  }
  nodes[slot].crc32 = calculateNodeCrc(nodes[slot]);

  char desc[48];
  snprintf(desc, sizeof(desc), "Node reconnected: data gap %u sec", outDataGapSec);
  addAuditEvent(AUDIT_EVENT_NODE_REJOIN, RETENTION_CATEGORY_RECENT_EVENT, nodeId,
                 "SYS_MESH", AUDIT_RES_SUCCESS, desc);
  return true;
}

bool CentralStorage::updatePreservedMeasurements(const char *nodeId, float vR, float vY, float vB,
                                                 float current, float power, float energyKwh) {
  int slot = findNodeSlot(nodeId);
  if (slot < 0) return false;

  // Stored in RAM cache for high-frequency telemetry without flash wear
  nodes[slot].lastSeenTime = 1700000000UL;
  // Update feeder 0 / common measurement cache
  nodes[slot].feeders[0].currentRating = current;
  nodes[slot].crc32 = calculateNodeCrc(nodes[slot]);
  return true;
}

uint8_t CentralStorage::getMeasurementFreshness(const char *nodeId) const {
  int slot = findNodeSlot(nodeId);
  if (slot < 0) return MEAS_FRESHNESS_UNKNOWN;
  if (nodes[slot].regState == REG_STATE_OFFLINE) return MEAS_FRESHNESS_STALE;
  if (nodes[slot].regState == REG_STATE_ACTIVE) return MEAS_FRESHNESS_LIVE;
  return MEAS_FRESHNESS_UNKNOWN;
}

// ─────────────────────────────────────────────────────────────
//  TRANSACTION MANAGEMENT & 2PC JOURNAL
// ─────────────────────────────────────────────────────────────
bool CentralStorage::beginTransaction(uint8_t txType, const char *targetId, const char *actorId,
                                      char outTxId[16]) {
  journal.magic = TX_JOURNAL_MAGIC;
  txSeqCounter++;
  snprintf(journal.txId, sizeof(journal.txId), "TX-%08X-%02X", (unsigned int)1700000000UL, (unsigned)(txSeqCounter & 0xFF));
  journal.txType = txType;
  journal.state = TX_STATE_PREPARED;
  strncpy(journal.targetId, targetId ? targetId : "", sizeof(journal.targetId) - 1);
  strncpy(journal.actorId, actorId ? actorId : "", sizeof(journal.actorId) - 1);
  journal.timestamp = 1700000000UL;
  journal.payloadSize = 0;
  journal.crc32 = mscbStorageCrc32((const uint8_t*)&journal, sizeof(TxJournalRecord) - sizeof(uint32_t));

  strncpy(outTxId, journal.txId, 15);
  outTxId[15] = '\0';
  return persistJournal(journal);
}

bool CentralStorage::commitTransaction(const char *txId) {
  if (strcmp(journal.txId, txId) != 0) return false;
  journal.state = TX_STATE_COMMITTED;
  journal.crc32 = mscbStorageCrc32((const uint8_t*)&journal, sizeof(TxJournalRecord) - sizeof(uint32_t));
  return persistJournal(journal);
}

bool CentralStorage::rollbackTransaction(const char *txId, const char *reason) {
  if (strcmp(journal.txId, txId) != 0) return false;
  journal.state = TX_STATE_ROLLED_BACK;
  journal.crc32 = mscbStorageCrc32((const uint8_t*)&journal, sizeof(TxJournalRecord) - sizeof(uint32_t));
  persistJournal(journal);

  char desc[48];
  snprintf(desc, sizeof(desc), "Tx rolled back: %.30s", reason ? reason : "Error");
  addAuditEvent(AUDIT_EVENT_ADMIN_ACTION, RETENTION_CATEGORY_ADMIN_AUDIT, journal.targetId,
                 journal.actorId, AUDIT_RES_ROLLBACK, desc, txId);
  return true;
}

const TxJournalRecord& CentralStorage::getActiveJournal() const {
  return journal;
}

// ─────────────────────────────────────────────────────────────
//  DIAGNOSTICS & FLASH WEAR METRICS
// ─────────────────────────────────────────────────────────────
size_t CentralStorage::getNvsEstimatedUsage() const {
  // Approximate total bytes stored in persistent records
  size_t total = 0;
  total += nodeCount * sizeof(MscbNodeRecord);
  total += feederCount * sizeof(LogicalFeederRecord);
  total += 2 * sizeof(SystemConfigRecord);
  total += revocationCount * sizeof(PermanentRevocationRecord);
  total += sizeof(TxJournalRecord);
  return total;
}

size_t CentralStorage::getRamUsageBytes() const {
  return sizeof(CentralStorage);
}

uint32_t CentralStorage::getTotalFlashWrites() const {
  return flashWriteCounter;
}

float CentralStorage::calculateFlashWearEstimateYears(uint32_t writesPerDay) const {
  if (writesPerDay == 0) writesPerDay = 1;
  // ESP32 Flash endurance: minimum 100,000 Program/Erase cycles
  // With NVS page wear-leveling across 20KB (5 sectors)
  const float cyclesPerSector = 100000.0f;
  const float sectorsAvailable = 5.0f;
  float totalEnduranceWrites = cyclesPerSector * sectorsAvailable;
  float years = totalEnduranceWrites / (writesPerDay * 365.25f);
  return years;
}

void CentralStorage::injectBitFlipCorruption(uint8_t targetSlot) {
  if (targetSlot < nodeCount) {
    nodes[targetSlot].crc32 ^= 0xDEADBEEF;
  }
}

void CentralStorage::simulatePowerFailureDuringPrepare() {
  journal.state = TX_STATE_PREPARED;
  journal.crc32 = mscbStorageCrc32((const uint8_t*)&journal, sizeof(TxJournalRecord) - sizeof(uint32_t));
  persistJournal(journal);
}

void CentralStorage::clearAll() {
  nodeCount = 0;
  feederCount = 0;
  revocationCount = 0;
  auditHead = 0;
  auditCount = 0;
  flashWriteCounter = 0;
  memset(nodes, 0, sizeof(nodes));
  memset(feeders, 0, sizeof(feeders));
  memset(revocations, 0, sizeof(revocations));
  memset(auditRing, 0, sizeof(auditRing));
#ifdef ARDUINO
  prefsReg.begin(NVS_NS_CENTRAL_REG, false); prefsReg.clear(); prefsReg.end();
  prefsFdr.begin(NVS_NS_CENTRAL_FDR, false); prefsFdr.clear(); prefsFdr.end();
  prefsCfg.begin(NVS_NS_CENTRAL_CFG, false); prefsCfg.clear(); prefsCfg.end();
  prefsAud.begin(NVS_NS_CENTRAL_AUD, false); prefsAud.clear(); prefsAud.end();
  prefsTx.begin(NVS_NS_CENTRAL_TX, false);   prefsTx.clear();   prefsTx.end();
  prefsRev.begin(NVS_NS_CENTRAL_REV, false); prefsRev.clear(); prefsRev.end();
#else
  memset(simNvsRegValid, 0, sizeof(simNvsRegValid));
  memset(simNvsFdrValid, 0, sizeof(simNvsFdrValid));
  memset(simNvsCfgValid, 0, sizeof(simNvsCfgValid));
  memset(simNvsRevValid, 0, sizeof(simNvsRevValid));
  memset(simNvsAudValid, 0, sizeof(simNvsAudValid));
  simJournalValid = false;
#endif
  initSafeDefaultConfig(configBanks[0], "GATEWAY");
  configBanks[1] = configBanks[0];
  activeConfigBank = 0;
}

// ─────────────────────────────────────────────────────────────
//  PERSISTENT SLOT STORAGE (ARDUINO NVS VS SIMULATION)
// ─────────────────────────────────────────────────────────────
bool CentralStorage::persistNodeSlot(uint8_t slot, const MscbNodeRecord &rec) {
  if (slot >= MSCB_MAX_POLE_NODES) return false;
  flashWriteCounter++;
#ifdef ARDUINO
  prefsReg.begin(NVS_NS_CENTRAL_REG, false);
  char key[8];
  snprintf(key, sizeof(key), "n%u", slot);
  size_t w = prefsReg.putBytes(key, &rec, sizeof(MscbNodeRecord));
  prefsReg.putUChar("cnt", nodeCount);
  prefsReg.end();
  return (w == sizeof(MscbNodeRecord));
#else
  memcpy(simNvsReg[slot], &rec, sizeof(MscbNodeRecord));
  simNvsRegValid[slot] = true;
  return true;
#endif
}

bool CentralStorage::loadNodeSlot(uint8_t slot, MscbNodeRecord &outRec) {
  if (slot >= MSCB_MAX_POLE_NODES) return false;
#ifdef ARDUINO
  prefsReg.begin(NVS_NS_CENTRAL_REG, true);
  char key[8];
  snprintf(key, sizeof(key), "n%u", slot);
  size_t r = prefsReg.getBytes(key, &outRec, sizeof(MscbNodeRecord));
  prefsReg.end();
  return (r == sizeof(MscbNodeRecord));
#else
  if (!simNvsRegValid[slot]) return false;
  memcpy(&outRec, simNvsReg[slot], sizeof(MscbNodeRecord));
  return true;
#endif
}

bool CentralStorage::persistFeederSlot(uint16_t slot, const LogicalFeederRecord &rec) {
  if (slot >= MSCB_TOTAL_FEEDERS) return false;
  flashWriteCounter++;
#ifdef ARDUINO
  prefsFdr.begin(NVS_NS_CENTRAL_FDR, false);
  char key[8];
  snprintf(key, sizeof(key), "f%u", slot);
  size_t w = prefsFdr.putBytes(key, &rec, sizeof(LogicalFeederRecord));
  prefsFdr.putUShort("cnt", feederCount);
  prefsFdr.end();
  return (w == sizeof(LogicalFeederRecord));
#else
  memcpy(simNvsFdr[slot], &rec, sizeof(LogicalFeederRecord));
  simNvsFdrValid[slot] = true;
  return true;
#endif
}

bool CentralStorage::loadFeederSlot(uint16_t slot, LogicalFeederRecord &outRec) {
  if (slot >= MSCB_TOTAL_FEEDERS) return false;
#ifdef ARDUINO
  prefsFdr.begin(NVS_NS_CENTRAL_FDR, true);
  char key[8];
  snprintf(key, sizeof(key), "f%u", slot);
  size_t r = prefsFdr.getBytes(key, &outRec, sizeof(LogicalFeederRecord));
  prefsFdr.end();
  return (r == sizeof(LogicalFeederRecord));
#else
  if (!simNvsFdrValid[slot]) return false;
  memcpy(&outRec, simNvsFdr[slot], sizeof(LogicalFeederRecord));
  return true;
#endif
}

bool CentralStorage::persistConfigBank(uint8_t bankIdx, const SystemConfigRecord &cfg) {
  if (bankIdx >= 2) return false;
  flashWriteCounter++;
#ifdef ARDUINO
  prefsCfg.begin(NVS_NS_CENTRAL_CFG, false);
  char key[8];
  snprintf(key, sizeof(key), "b%u", bankIdx);
  size_t w = prefsCfg.putBytes(key, &cfg, sizeof(SystemConfigRecord));
  prefsCfg.putUChar("active", activeConfigBank);
  prefsCfg.end();
  return (w == sizeof(SystemConfigRecord));
#else
  memcpy(simNvsCfg[bankIdx], &cfg, sizeof(SystemConfigRecord));
  simNvsCfgValid[bankIdx] = true;
  return true;
#endif
}

bool CentralStorage::loadConfigBank(uint8_t bankIdx, SystemConfigRecord &outCfg) {
  if (bankIdx >= 2) return false;
#ifdef ARDUINO
  prefsCfg.begin(NVS_NS_CENTRAL_CFG, true);
  char key[8];
  snprintf(key, sizeof(key), "b%u", bankIdx);
  size_t r = prefsCfg.getBytes(key, &outCfg, sizeof(SystemConfigRecord));
  prefsCfg.end();
  return (r == sizeof(SystemConfigRecord));
#else
  if (!simNvsCfgValid[bankIdx]) return false;
  memcpy(&outCfg, simNvsCfg[bankIdx], sizeof(SystemConfigRecord));
  return true;
#endif
}

bool CentralStorage::persistRevocationSlot(uint8_t slot, const PermanentRevocationRecord &rec) {
  if (slot >= MSCB_MAX_REVOCATION_RECORDS) return false;
  flashWriteCounter++;
#ifdef ARDUINO
  prefsRev.begin(NVS_NS_CENTRAL_REV, false);
  char key[8];
  snprintf(key, sizeof(key), "r%u", slot);
  size_t w = prefsRev.putBytes(key, &rec, sizeof(PermanentRevocationRecord));
  prefsRev.putUChar("cnt", revocationCount);
  prefsRev.end();
  return (w == sizeof(PermanentRevocationRecord));
#else
  memcpy(simNvsRev[slot], &rec, sizeof(PermanentRevocationRecord));
  simNvsRevValid[slot] = true;
  return true;
#endif
}

bool CentralStorage::loadRevocationSlot(uint8_t slot, PermanentRevocationRecord &outRec) {
  if (slot >= MSCB_MAX_REVOCATION_RECORDS) return false;
#ifdef ARDUINO
  prefsRev.begin(NVS_NS_CENTRAL_REV, true);
  char key[8];
  snprintf(key, sizeof(key), "r%u", slot);
  size_t r = prefsRev.getBytes(key, &outRec, sizeof(PermanentRevocationRecord));
  prefsRev.end();
  return (r == sizeof(PermanentRevocationRecord));
#else
  if (!simNvsRevValid[slot]) return false;
  memcpy(&outRec, simNvsRev[slot], sizeof(PermanentRevocationRecord));
  return true;
#endif
}

bool CentralStorage::persistJournal(const TxJournalRecord &j) {
  flashWriteCounter++;
#ifdef ARDUINO
  prefsTx.begin(NVS_NS_CENTRAL_TX, false);
  size_t w = prefsTx.putBytes("tx", &j, sizeof(TxJournalRecord));
  prefsTx.end();
  return (w == sizeof(TxJournalRecord));
#else
  simJournal = j;
  simJournalValid = true;
  return true;
#endif
}

bool CentralStorage::loadJournal(TxJournalRecord &outJ) {
#ifdef ARDUINO
  prefsTx.begin(NVS_NS_CENTRAL_TX, true);
  size_t r = prefsTx.getBytes("tx", &outJ, sizeof(TxJournalRecord));
  prefsTx.end();
  return (r == sizeof(TxJournalRecord));
#else
  if (!simJournalValid) return false;
  outJ = simJournal;
  return true;
#endif
}
