/*
 * =====================================================================
 *  NODESTORAGE.CPP — SECONDARY POLE NODE LOCAL STORAGE & CALIBRATION ENGINE
 *  MSCB Smart Pole Automation System — Prompt 19 Architecture
 * =====================================================================
 */

#include "NodeStorage.h"
#include <stdio.h>
#include <stdlib.h>

NodeStorage nodeStorage;

#ifndef ARDUINO
uint8_t  NodeStorage::simNodeData[512] = {0};
bool     NodeStorage::simNodeDataValid = false;
uint8_t  NodeStorage::simCalibData[sizeof(PoleCalibTable)] = {0};
bool     NodeStorage::simCalibDataValid = false;
uint8_t  NodeStorage::simCfgData[2][sizeof(SystemConfigRecord)] = {{0}};
bool     NodeStorage::simCfgDataValid[2] = {false};
uint8_t  NodeStorage::simFdrData[MSCB_FEEDERS_PER_POLE][sizeof(MscbFeederRecord)] = {{0}};
bool     NodeStorage::simFdrDataValid[MSCB_FEEDERS_PER_POLE] = {false};
uint8_t  NodeStorage::simFaultData[MSCB_NODE_FAULT_HISTORY_SIZE][sizeof(NodeFaultRecord)] = {{0}};
bool     NodeStorage::simFaultDataValid[MSCB_NODE_FAULT_HISTORY_SIZE] = {false};
#endif

#define NVS_NS_POLE_LOCAL   "mscb_pole"
#define NVS_NS_POLE_CAL     "mscb_pcal"
#define NVS_NS_POLE_CFG     "mscb_pcfg"
#define NVS_NS_POLE_FLT     "mscb_pflt"

static const char* const POLE_CH_NAMES[MSCB_TOTAL_CHANNELS_PER_POLE] = {
  "POLE-R-Voltage", "POLE-Y-Voltage", "POLE-B-Voltage",
  "F01-R-Current", "F01-Y-Current", "F01-B-Current",
  "F02-R-Current", "F02-Y-Current", "F02-B-Current",
  "F03-R-Current", "F03-Y-Current", "F03-B-Current",
  "F04-R-Current", "F04-Y-Current", "F04-B-Current",
  "F05-R-Current", "F05-Y-Current", "F05-B-Current",
  "F06-R-Current", "F06-Y-Current", "F06-B-Current"
};

NodeStorage::NodeStorage() :
  regState(REG_STATE_FACTORY), hopCount(255), dagRank(255),
  activeConfigBank(0), schedEnabled(false), schedOnMin(-1), schedOffMin(-1),
  revoked(false), faultHead(0), faultCount(0)
{
  hwId[0] = '\0';
  logicalId[0] = '\0';
  strncpy(nodeName, "Field Pole", sizeof(nodeName) - 1);
  strncpy(nodeType, "STREET", sizeof(nodeType) - 1);
  strncpy(primaryParent, "NONE", sizeof(primaryParent) - 1);
  strncpy(backupParent, "NONE", sizeof(backupParent) - 1);
  revocationTxId[0] = '\0';
  revocationReason[0] = '\0';

  memset(feeders, 0, sizeof(feeders));
  memset(faultRing, 0, sizeof(faultRing));
  memset(configBanks, 0, sizeof(configBanks));

  initHardwareMappings();
  initFactoryCalibration();
  initSafeDefaultConfig(configBanks[0]);
  configBanks[1] = configBanks[0];
}

void NodeStorage::initHardwareMappings() {
  // 1. Sensor Mapping: Map 21 ADC channels to Voltages & Feeder Currents
  for (uint8_t i = 0; i < MSCB_TOTAL_CHANNELS_PER_POLE; i++) {
    sensorMap[i].adcChannel = i;
    if (i < MSCB_VOLTAGE_CHANNELS_PER_POLE) {
      sensorMap[i].feederIndex = 255; // Pole common bus
      sensorMap[i].isVoltage = true;
      sensorMap[i].phase = (i == 0) ? 'R' : (i == 1 ? 'Y' : 'B');
    } else {
      uint8_t currIdx = i - MSCB_VOLTAGE_CHANNELS_PER_POLE;
      sensorMap[i].feederIndex = currIdx / 3; // 0..5
      sensorMap[i].isVoltage = false;
      uint8_t phIdx = currIdx % 3;
      sensorMap[i].phase = (phIdx == 0) ? 'R' : (phIdx == 1 ? 'Y' : 'B');
    }
  }

  // 2. Relay Mapping: Map 6 Feeders to Relays
  for (uint8_t f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
    relayMap[f].feederIndex = f;
    relayMap[f].gpioPin = 25 + f; // Default GPIO mapping
    relayMap[f].activeLow = false;
  }
}

void NodeStorage::initFactoryCalibration() {
  calibTable.magic = CALIBRATION_MAGIC;
  calibTable.tableVersion = CALIBRATION_VERSION;
  strncpy(calibTable.poleId, logicalId, 15);
  calibTable.channelCount = MSCB_TOTAL_CHANNELS_PER_POLE;

  for (uint8_t i = 0; i < MSCB_TOTAL_CHANNELS_PER_POLE; i++) {
    strncpy(calibTable.channels[i].channelId, POLE_CH_NAMES[i], sizeof(calibTable.channels[i].channelId) - 1);
    calibTable.channels[i].channelId[sizeof(calibTable.channels[i].channelId) - 1] = '\0';
    calibTable.channels[i].zeroOffset = 0.0f;
    calibTable.channels[i].gainMultiplier = 1.0f;
    calibTable.channels[i].phaseCorrectionDeg = 0.0f;
    calibTable.channels[i].calVersion = CALIBRATION_VERSION;
    calibTable.channels[i].calTimestamp = 1700000000UL;
    calibTable.channels[i].isValid = true;

    if (i < MSCB_VOLTAGE_CHANNELS_PER_POLE) {
      strncpy(calibTable.channels[i].sensorIdentity, "ZMPT101B_PT", sizeof(calibTable.channels[i].sensorIdentity) - 1);
      calibTable.channels[i].sensorSensitivity = 325.27f; // Nominal PT ratio
      strncpy(calibTable.channels[i].calReference, "LAB_230V_CAL", sizeof(calibTable.channels[i].calReference) - 1);
    } else {
      strncpy(calibTable.channels[i].sensorIdentity, "CT_SCT013_50A", sizeof(calibTable.channels[i].sensorIdentity) - 1);
      calibTable.channels[i].sensorSensitivity = 50.0f; // Nominal 50A CT
      strncpy(calibTable.channels[i].calReference, "LAB_50A_CAL", sizeof(calibTable.channels[i].calReference) - 1);
    }
    calibTable.channels[i].crc32 = mscbStorageCrc32((const uint8_t*)&calibTable.channels[i], sizeof(ChannelCalibRecord) - sizeof(uint32_t));
  }

  calibTable.crc32 = calculateCalibCrc(calibTable);
}

void NodeStorage::initSafeDefaultConfig(SystemConfigRecord &cfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = MSCB_CONFIG_MAGIC;
  cfg.schemaVersion = MSCB_CURRENT_CONFIG_SCHEMA;
  cfg.configVersion = 100;
  strncpy(cfg.nodeId, (strlen(logicalId) > 0) ? logicalId : "POLE-NEW", 15);
  cfg.creationTime = 1700000000UL;
  cfg.updateTime = 1700000000UL;
  cfg.nominalVoltage = 230.0f;
  cfg.maxOverVoltage = 265.0f;
  cfg.minUnderVoltage = 175.0f;
  cfg.maxCurrentRating = 60.0f;
  cfg.feederDefaultRating = 50.0f;
  cfg.heartbeatIntervalSec = 30;
  cfg.beaconIntervalSec = 30;
  cfg.loraTxPower = 10;
  cfg.loraChannel = 433920000UL;
  cfg.schedEnabled = false;
  cfg.schedOnMinute = -1;
  cfg.schedOffMinute = -1;
  cfg.validity = CONFIG_VALID;
  cfg.generation = 1;
  cfg.crc32 = calculateConfigCrc(cfg);
}

uint32_t NodeStorage::calculateCalibCrc(const PoleCalibTable &t) const {
  size_t crcLen = sizeof(PoleCalibTable) - sizeof(uint32_t);
  return mscbStorageCrc32((const uint8_t*)&t, crcLen);
}

uint32_t NodeStorage::calculateConfigCrc(const SystemConfigRecord &c) const {
  size_t crcLen = sizeof(SystemConfigRecord) - sizeof(uint32_t);
  return mscbStorageCrc32((const uint8_t*)&c, crcLen);
}

bool NodeStorage::validateConfigSafety(const SystemConfigRecord &cfg) const {
  if (cfg.magic != MSCB_CONFIG_MAGIC) return false;
  if (cfg.nominalVoltage < 180.0f || cfg.nominalVoltage > 250.0f) return false;
  if (cfg.maxOverVoltage <= cfg.nominalVoltage || cfg.maxOverVoltage > 300.0f) return false;
  if (cfg.minUnderVoltage >= cfg.nominalVoltage || cfg.minUnderVoltage < 100.0f) return false;
  if (cfg.maxCurrentRating < 1.0f || cfg.maxCurrentRating > 100.0f) return false;
  return true;
}

// ─────────────────────────────────────────────────────────────
//  BOOT RECOVERY & INTEGRITY
// ─────────────────────────────────────────────────────────────
bool NodeStorage::init() {
  recoverStorage();
  return verifyStorageIntegrity();
}

bool NodeStorage::recoverStorage() {
  // 1. Load Node State & Identity
  loadNodeState();

  // 2. Load 21-Channel Calibration
  PoleCalibTable loadedCal;
  if (loadCalibTable(loadedCal) && loadedCal.magic == CALIBRATION_MAGIC && loadedCal.crc32 == calculateCalibCrc(loadedCal)) {
    calibTable = loadedCal;
  } else {
    initFactoryCalibration();
    persistCalibTable(calibTable);
  }

  // 3. Load Dual-Bank Configuration
  bool b0 = false, b1 = false;
  SystemConfigRecord c0, c1;
  if (loadConfigBank(0, c0) && c0.crc32 == calculateConfigCrc(c0) && validateConfigSafety(c0)) {
    b0 = true; configBanks[0] = c0;
  }
  if (loadConfigBank(1, c1) && c1.crc32 == calculateConfigCrc(c1) && validateConfigSafety(c1)) {
    b1 = true; configBanks[1] = c1;
  }

  if (b0 && b1) {
    activeConfigBank = (c1.generation >= c0.generation) ? 1 : 0;
  } else if (b0) {
    activeConfigBank = 0;
  } else if (b1) {
    activeConfigBank = 1;
  } else {
    initSafeDefaultConfig(configBanks[0]);
    configBanks[0].validity = CONFIG_SAFE_DEFAULTS;
    persistConfigBank(0, configBanks[0]);
    configBanks[1] = configBanks[0];
    persistConfigBank(1, configBanks[1]);
    activeConfigBank = 0;
  }

  // 4. Enforce persistent revocation lock
  if (revoked) {
    regState = REG_STATE_REVOKED;
  }

  return true;
}

bool NodeStorage::verifyStorageIntegrity() {
  if (calibTable.crc32 != calculateCalibCrc(calibTable)) {
    return false;
  }
  SystemConfigRecord activeCfg;
  if (!getConfiguration(activeCfg) || activeCfg.validity == CONFIG_CORRUPTED) {
    return false;
  }
  return true;
}

// ─────────────────────────────────────────────────────────────
//  IDENTITY & REGISTRATION
// ─────────────────────────────────────────────────────────────
bool NodeStorage::saveNodeIdentity(const char *inHwId, const char *inLogicalId, const char *name, const char *type) {
  if (inHwId) strncpy(hwId, inHwId, 16);
  if (inLogicalId) strncpy(logicalId, inLogicalId, 15);
  if (name) strncpy(nodeName, name, 31);
  if (type) strncpy(nodeType, type, 15);
  return persistNodeState();
}

bool NodeStorage::getNodeIdentity(char *outHwId, char *outLogicalId, char *outName, char *outType) {
  if (outHwId) strncpy(outHwId, hwId, 16);
  if (outLogicalId) strncpy(outLogicalId, logicalId, 15);
  if (outName) strncpy(outName, nodeName, 31);
  if (outType) strncpy(outType, nodeType, 15);
  return true;
}

bool NodeStorage::saveRegistrationState(uint8_t state) {
  if (revoked && state != REG_STATE_REVOKED) {
    return false; // Cannot register a revoked device!
  }
  regState = state;
  return persistNodeState();
}

uint8_t NodeStorage::getRegistrationState() const {
  return regState;
}

bool NodeStorage::isRegistered() const {
  return (regState == REG_STATE_ACTIVE && !revoked);
}

// ─────────────────────────────────────────────────────────────
//  PARENTS & ROUTING TOPOLOGY
// ─────────────────────────────────────────────────────────────
bool NodeStorage::saveParents(const char *priParent, const char *bkpParent, uint8_t inHops, uint8_t rank) {
  if (priParent) strncpy(primaryParent, priParent, 15);
  if (bkpParent) strncpy(backupParent, bkpParent, 15);
  hopCount = inHops;
  dagRank = rank;
  return persistNodeState();
}

bool NodeStorage::getParents(char *outPri, char *outBkp, uint8_t &outHops, uint8_t &outRank) {
  if (outPri) strncpy(outPri, primaryParent, 15);
  if (outBkp) strncpy(outBkp, backupParent, 15);
  outHops = hopCount;
  outRank = dagRank;
  return true;
}

// ─────────────────────────────────────────────────────────────
//  FEEDER CONFIGURATIONS (6 FEEDERS)
// ─────────────────────────────────────────────────────────────
bool NodeStorage::saveFeederConfig(uint8_t feederIdx, const MscbFeederRecord &fdr) {
  if (feederIdx >= MSCB_FEEDERS_PER_POLE) return false;
  feeders[feederIdx] = fdr;
#ifdef ARDUINO
  char key[8];
  snprintf(key, sizeof(key), "f%u", feederIdx);
  prefsPole.begin(NVS_NS_POLE_LOCAL, false);
  prefsPole.putBytes(key, &fdr, sizeof(MscbFeederRecord));
  prefsPole.end();
#else
  memcpy(simFdrData[feederIdx], &fdr, sizeof(MscbFeederRecord));
  simFdrDataValid[feederIdx] = true;
#endif
  return true;
}

bool NodeStorage::getFeederConfig(uint8_t feederIdx, MscbFeederRecord &outFdr) {
  if (feederIdx >= MSCB_FEEDERS_PER_POLE) return false;
  outFdr = feeders[feederIdx];
  return true;
}

// ─────────────────────────────────────────────────────────────
//  CALIBRATION STORAGE (21 CHANNELS)
// ─────────────────────────────────────────────────────────────
bool NodeStorage::saveCalibrationTable(const PoleCalibTable &table) {
  PoleCalibTable tCopy = table;
  tCopy.crc32 = calculateCalibCrc(tCopy);
  if (!persistCalibTable(tCopy)) return false;

  // Read-back verification
  PoleCalibTable verifyT;
  if (!loadCalibTable(verifyT) || verifyT.crc32 != tCopy.crc32) {
    return false;
  }
  calibTable = tCopy;
  return true;
}

bool NodeStorage::getCalibrationTable(PoleCalibTable &outTable) {
  if (calibTable.crc32 != calculateCalibCrc(calibTable)) {
    return false;
  }
  outTable = calibTable;
  return true;
}

bool NodeStorage::saveChannelCalibration(uint8_t channelIdx, const ChannelCalibRecord &ch) {
  if (channelIdx >= MSCB_TOTAL_CHANNELS_PER_POLE) return false;
  calibTable.channels[channelIdx] = ch;
  calibTable.channels[channelIdx].crc32 = mscbStorageCrc32((const uint8_t*)&calibTable.channels[channelIdx], sizeof(ChannelCalibRecord) - sizeof(uint32_t));
  calibTable.crc32 = calculateCalibCrc(calibTable);
  return persistCalibTable(calibTable);
}

bool NodeStorage::getChannelCalibration(uint8_t channelIdx, ChannelCalibRecord &outCh) {
  if (channelIdx >= MSCB_TOTAL_CHANNELS_PER_POLE) return false;
  outCh = calibTable.channels[channelIdx];
  return outCh.isValid;
}

void NodeStorage::resetFactoryCalibration() {
  initFactoryCalibration();
  persistCalibTable(calibTable);
}

// ─────────────────────────────────────────────────────────────
//  CONFIGURATION (DUAL-BANK)
// ─────────────────────────────────────────────────────────────
bool NodeStorage::saveConfiguration(const SystemConfigRecord &config) {
  if (!validateConfigSafety(config)) return false;

  uint8_t nextBank = (activeConfigBank == 0) ? 1 : 0;
  SystemConfigRecord newCfg = config;
  newCfg.magic = MSCB_CONFIG_MAGIC;
  newCfg.generation = configBanks[activeConfigBank].generation + 1;
  newCfg.configVersion = configBanks[activeConfigBank].configVersion + 1;
  newCfg.validity = CONFIG_VALID;
  newCfg.crc32 = calculateConfigCrc(newCfg);

  if (!persistConfigBank(nextBank, newCfg)) return false;

  SystemConfigRecord verifyCfg;
  if (!loadConfigBank(nextBank, verifyCfg) || verifyCfg.crc32 != newCfg.crc32) {
    return false;
  }

  configBanks[nextBank] = newCfg;
  activeConfigBank = nextBank;
  return true;
}

bool NodeStorage::getConfiguration(SystemConfigRecord &outConfig) {
  if (configBanks[activeConfigBank].magic == MSCB_CONFIG_MAGIC &&
      configBanks[activeConfigBank].crc32 == calculateConfigCrc(configBanks[activeConfigBank]) &&
      validateConfigSafety(configBanks[activeConfigBank])) {
    outConfig = configBanks[activeConfigBank];
    return true;
  }

  uint8_t alt = (activeConfigBank == 0) ? 1 : 0;
  if (configBanks[alt].magic == MSCB_CONFIG_MAGIC &&
      configBanks[alt].crc32 == calculateConfigCrc(configBanks[alt]) &&
      validateConfigSafety(configBanks[alt])) {
    activeConfigBank = alt;
    outConfig = configBanks[activeConfigBank];
    return true;
  }

  initSafeDefaultConfig(outConfig);
  outConfig.validity = CONFIG_SAFE_DEFAULTS;
  return true;
}

uint16_t NodeStorage::getActiveConfigVersion() const {
  return configBanks[activeConfigBank].configVersion;
}

// ─────────────────────────────────────────────────────────────
//  SCHEDULE & FAULT RING BUFFER
// ─────────────────────────────────────────────────────────────
bool NodeStorage::saveSchedule(bool enabled, int16_t onMin, int16_t offMin) {
  schedEnabled = enabled;
  schedOnMin = onMin;
  schedOffMin = offMin;
  return persistNodeState();
}

bool NodeStorage::getSchedule(bool &outEnabled, int16_t &outOnMin, int16_t &outOffMin) {
  outEnabled = schedEnabled;
  outOnMin = schedOnMin;
  outOffMin = schedOffMin;
  return true;
}

bool NodeStorage::addFaultRecord(uint8_t faultCode, uint8_t feederId, float triggerValue, const char *desc) {
  NodeFaultRecord rec;
  rec.timestamp = 1700000000UL;
  rec.faultCode = faultCode;
  rec.feederId = feederId;
  rec.triggerValue = triggerValue;
  strncpy(rec.description, desc ? desc : "FAULT", sizeof(rec.description) - 1);
  rec.description[sizeof(rec.description) - 1] = '\0';

  faultRing[faultHead] = rec;
#ifdef ARDUINO
  char key[8];
  snprintf(key, sizeof(key), "flt%u", faultHead);
  prefsFlt.begin(NVS_NS_POLE_FLT, false);
  prefsFlt.putBytes(key, &rec, sizeof(rec));
  prefsFlt.end();
#else
  memcpy(simFaultData[faultHead], &rec, sizeof(rec));
  simFaultDataValid[faultHead] = true;
#endif

  faultHead = (faultHead + 1) % MSCB_NODE_FAULT_HISTORY_SIZE;
  if (faultCount < MSCB_NODE_FAULT_HISTORY_SIZE) faultCount++;
  return true;
}

bool NodeStorage::getFaultHistory(NodeFaultRecord *outFaults, uint8_t maxCount, uint8_t &actualCount) {
  actualCount = 0;
  if (!outFaults || maxCount == 0 || faultCount == 0) return false;

  uint8_t startIdx = (faultCount < MSCB_NODE_FAULT_HISTORY_SIZE) ? 0 : faultHead;
  for (uint8_t i = 0; i < faultCount && actualCount < maxCount; i++) {
    uint8_t idx = (startIdx + i) % MSCB_NODE_FAULT_HISTORY_SIZE;
    outFaults[actualCount++] = faultRing[idx];
  }
  return (actualCount > 0);
}

uint8_t NodeStorage::getFaultCount() const {
  return faultCount;
}

// ─────────────────────────────────────────────────────────────
//  REVOCATION MANAGEMENT
// ─────────────────────────────────────────────────────────────
bool NodeStorage::setRevoked(const char *txId, const char *reason) {
  revoked = true;
  regState = REG_STATE_REVOKED;
  logicalId[0] = '\0';
  if (txId) strncpy(revocationTxId, txId, 15);
  if (reason) strncpy(revocationReason, reason, 31);
  return persistNodeState();
}

bool NodeStorage::isRevoked() const {
  return revoked;
}

const char* NodeStorage::getRevocationTxId() const {
  return revocationTxId;
}

const char* NodeStorage::getRevocationReason() const {
  return revocationReason;
}

void NodeStorage::clearRevocation() {
  revoked = false;
  revocationTxId[0] = '\0';
  revocationReason[0] = '\0';
  regState = REG_STATE_FACTORY;
  persistNodeState();
}

const NodeSensorMapping* NodeStorage::getSensorMapping(uint8_t chIdx) const {
  if (chIdx < MSCB_TOTAL_CHANNELS_PER_POLE) return &sensorMap[chIdx];
  return nullptr;
}

const NodeRelayMapping* NodeStorage::getRelayMapping(uint8_t fdrIdx) const {
  if (fdrIdx < MSCB_FEEDERS_PER_POLE) return &relayMap[fdrIdx];
  return nullptr;
}

void NodeStorage::injectBitFlipCalibration() {
  calibTable.crc32 ^= 0xDEADBEEF;
}

void NodeStorage::clearAll() {
  regState = REG_STATE_FACTORY;
  revoked = false;
  logicalId[0] = '\0';
  revocationTxId[0] = '\0';
  revocationReason[0] = '\0';
  faultCount = 0;
  faultHead = 0;
  initFactoryCalibration();
  initSafeDefaultConfig(configBanks[0]);
  configBanks[1] = configBanks[0];
  activeConfigBank = 0;
}

// ─────────────────────────────────────────────────────────────
//  PERSISTENT STORAGE LOW-LEVEL
// ─────────────────────────────────────────────────────────────
bool NodeStorage::persistCalibTable(const PoleCalibTable &t) {
#ifdef ARDUINO
  prefsCal.begin(NVS_NS_POLE_CAL, false);
  size_t w = prefsCal.putBytes("table", &t, sizeof(PoleCalibTable));
  prefsCal.end();
  return (w == sizeof(PoleCalibTable));
#else
  memcpy(simCalibData, &t, sizeof(PoleCalibTable));
  simCalibDataValid = true;
  return true;
#endif
}

bool NodeStorage::loadCalibTable(PoleCalibTable &outT) {
#ifdef ARDUINO
  prefsCal.begin(NVS_NS_POLE_CAL, true);
  size_t r = prefsCal.getBytes("table", &outT, sizeof(PoleCalibTable));
  prefsCal.end();
  return (r == sizeof(PoleCalibTable));
#else
  if (!simCalibDataValid) return false;
  memcpy(&outT, simCalibData, sizeof(PoleCalibTable));
  return true;
#endif
}

bool NodeStorage::persistConfigBank(uint8_t bankIdx, const SystemConfigRecord &cfg) {
  if (bankIdx >= 2) return false;
#ifdef ARDUINO
  prefsCfg.begin(NVS_NS_POLE_CFG, false);
  char key[8];
  snprintf(key, sizeof(key), "b%u", bankIdx);
  size_t w = prefsCfg.putBytes(key, &cfg, sizeof(SystemConfigRecord));
  prefsCfg.end();
  return (w == sizeof(SystemConfigRecord));
#else
  memcpy(simCfgData[bankIdx], &cfg, sizeof(SystemConfigRecord));
  simCfgDataValid[bankIdx] = true;
  return true;
#endif
}

bool NodeStorage::loadConfigBank(uint8_t bankIdx, SystemConfigRecord &outCfg) {
  if (bankIdx >= 2) return false;
#ifdef ARDUINO
  prefsCfg.begin(NVS_NS_POLE_CFG, true);
  char key[8];
  snprintf(key, sizeof(key), "b%u", bankIdx);
  size_t r = prefsCfg.getBytes(key, &outCfg, sizeof(SystemConfigRecord));
  prefsCfg.end();
  return (r == sizeof(SystemConfigRecord));
#else
  if (!simCfgDataValid[bankIdx]) return false;
  memcpy(&outCfg, simCfgData[bankIdx], sizeof(SystemConfigRecord));
  return true;
#endif
}

bool NodeStorage::persistNodeState() {
#ifdef ARDUINO
  prefsPole.begin(NVS_NS_POLE_LOCAL, false);
  prefsPole.putString("hw", hwId);
  prefsPole.putString("id", logicalId);
  prefsPole.putString("nm", nodeName);
  prefsPole.putString("tp", nodeType);
  prefsPole.putUChar("st", regState);
  prefsPole.putString("pri", primaryParent);
  prefsPole.putString("bkp", backupParent);
  prefsPole.putUChar("hop", hopCount);
  prefsPole.putUChar("rnk", dagRank);
  prefsPole.putBool("rev", revoked);
  prefsPole.putString("rtx", revocationTxId);
  prefsPole.putString("rsn", revocationReason);
  prefsPole.putBool("se", schedEnabled);
  prefsPole.putShort("so", schedOnMin);
  prefsPole.putShort("sf", schedOffMin);
  prefsPole.end();
  return true;
#else
  simNodeDataValid = true;
  return true;
#endif
}

bool NodeStorage::loadNodeState() {
#ifdef ARDUINO
  prefsPole.begin(NVS_NS_POLE_LOCAL, true);
  String sHw = prefsPole.getString("hw", "");
  String sId = prefsPole.getString("id", "");
  String sNm = prefsPole.getString("nm", "Field Pole");
  String sTp = prefsPole.getString("tp", "STREET");
  regState   = prefsPole.getUChar("st", REG_STATE_FACTORY);
  String sPr = prefsPole.getString("pri", "NONE");
  String sBk = prefsPole.getString("bkp", "NONE");
  hopCount   = prefsPole.getUChar("hop", 255);
  dagRank    = prefsPole.getUChar("rnk", 255);
  revoked    = prefsPole.getBool("rev", false);
  String sTx = prefsPole.getString("rtx", "");
  String sRn = prefsPole.getString("rsn", "");
  schedEnabled = prefsPole.getBool("se", false);
  schedOnMin = prefsPole.getShort("so", -1);
  schedOffMin= prefsPole.getShort("sf", -1);
  prefsPole.end();

  strncpy(hwId, sHw.c_str(), 16);
  strncpy(logicalId, sId.c_str(), 15);
  strncpy(nodeName, sNm.c_str(), 31);
  strncpy(nodeType, sTp.c_str(), 15);
  strncpy(primaryParent, sPr.c_str(), 15);
  strncpy(backupParent, sBk.c_str(), 15);
  strncpy(revocationTxId, sTx.c_str(), 15);
  strncpy(revocationReason, sRn.c_str(), 31);
  return true;
#else
  return simNodeDataValid;
#endif
}
