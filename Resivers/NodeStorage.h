/*
 * =====================================================================
 *  NODESTORAGE.H — SECONDARY POLE NODE LOCAL STORAGE & CALIBRATION ENGINE
 *  MSCB Smart Pole Automation System — Prompt 19 Architecture
 *  21-Channel Calibration, 6 Feeder Configs, Dual-Bank Settings,
 *  Local Fault Ring Buffer, Autonomous Schedule & Revocation Persistence
 * =====================================================================
 */

#ifndef NODE_STORAGE_H
#define NODE_STORAGE_H

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

class NodeStorage {
public:
  NodeStorage();

  // Initialization & Boot Recovery
  bool init();
  bool recoverStorage();
  bool verifyStorageIntegrity();

  // 1. Unique Identity & Registration State
  bool saveNodeIdentity(const char *hwId, const char *logicalId, const char *name, const char *type);
  bool getNodeIdentity(char *outHwId, char *outLogicalId, char *outName, char *outType);
  bool saveRegistrationState(uint8_t state);
  uint8_t getRegistrationState() const;
  bool isRegistered() const;

  // 2. Parent & Routing Topology
  bool saveParents(const char *priParent, const char *bkpParent, uint8_t hopCount, uint8_t rank);
  bool getParents(char *outPri, char *outBkp, uint8_t &outHops, uint8_t &outRank);

  // 3. Six Feeder Configurations
  bool saveFeederConfig(uint8_t feederIdx, const MscbFeederRecord &fdr);
  bool getFeederConfig(uint8_t feederIdx, MscbFeederRecord &outFdr);

  // 4. 21-Channel Calibration Persistence (18 Current + 3 Voltage)
  bool saveCalibrationTable(const PoleCalibTable &table);
  bool getCalibrationTable(PoleCalibTable &outTable);
  bool saveChannelCalibration(uint8_t channelIdx, const ChannelCalibRecord &ch);
  bool getChannelCalibration(uint8_t channelIdx, ChannelCalibRecord &outCh);
  void resetFactoryCalibration();

  // 5. Authorized Configuration (Dual-Bank Rollback)
  bool saveConfiguration(const SystemConfigRecord &config);
  bool getConfiguration(SystemConfigRecord &outConfig);
  uint16_t getActiveConfigVersion() const;

  // 6. Local Autonomous Schedule
  bool saveSchedule(bool enabled, int16_t onMin, int16_t offMin);
  bool getSchedule(bool &outEnabled, int16_t &outOnMin, int16_t &outOffMin);

  // 7. Local Fault History (Ring Buffer)
  bool addFaultRecord(uint8_t faultCode, uint8_t feederId, float triggerValue, const char *desc);
  bool getFaultHistory(NodeFaultRecord *outFaults, uint8_t maxCount, uint8_t &actualCount);
  uint8_t getFaultCount() const;

  // 8. Revocation State Persistence
  bool setRevoked(const char *txId, const char *reason);
  bool isRevoked() const;
  const char* getRevocationTxId() const;
  const char* getRevocationReason() const;
  void clearRevocation();

  // 9. Relay & Sensor Mapping
  const NodeSensorMapping* getSensorMapping(uint8_t chIdx) const;
  const NodeRelayMapping* getRelayMapping(uint8_t fdrIdx) const;

  // Fault Injection & Testing Hooks
  void clearAll();
  void injectBitFlipCalibration();

private:
  char               hwId[17];
  char               logicalId[16];
  char               nodeName[32];
  char               nodeType[16];
  uint8_t            regState;

  char               primaryParent[16];
  char               backupParent[16];
  uint8_t            hopCount;
  uint8_t            dagRank;

  MscbFeederRecord   feeders[MSCB_FEEDERS_PER_POLE]; // 6 Feeders
  PoleCalibTable     calibTable;                    // 21 Channels
  SystemConfigRecord configBanks[2];                // Bank 0 & 1
  uint8_t            activeConfigBank;

  bool               schedEnabled;
  int16_t            schedOnMin;
  int16_t            schedOffMin;

  bool               revoked;
  char               revocationTxId[16];
  char               revocationReason[32];

  NodeFaultRecord    faultRing[MSCB_NODE_FAULT_HISTORY_SIZE];
  uint8_t            faultHead;
  uint8_t            faultCount;

  NodeSensorMapping  sensorMap[MSCB_TOTAL_CHANNELS_PER_POLE];
  NodeRelayMapping   relayMap[MSCB_FEEDERS_PER_POLE];

#ifdef ARDUINO
  Preferences prefsPole;
  Preferences prefsCal;
  Preferences prefsCfg;
  Preferences prefsFlt;
#else
  static uint8_t  simNodeData[512];
  static bool     simNodeDataValid;
  static uint8_t  simCalibData[sizeof(PoleCalibTable)];
  static bool     simCalibDataValid;
  static uint8_t  simCfgData[2][sizeof(SystemConfigRecord)];
  static bool     simCfgDataValid[2];
  static uint8_t  simFdrData[MSCB_FEEDERS_PER_POLE][sizeof(MscbFeederRecord)];
  static bool     simFdrDataValid[MSCB_FEEDERS_PER_POLE];
  static uint8_t  simFaultData[MSCB_NODE_FAULT_HISTORY_SIZE][sizeof(NodeFaultRecord)];
  static bool     simFaultDataValid[MSCB_NODE_FAULT_HISTORY_SIZE];
#endif

  void initHardwareMappings();
  void initFactoryCalibration();
  void initSafeDefaultConfig(SystemConfigRecord &cfg);
  bool validateConfigSafety(const SystemConfigRecord &cfg) const;
  uint32_t calculateCalibCrc(const PoleCalibTable &t) const;
  uint32_t calculateConfigCrc(const SystemConfigRecord &c) const;

  bool persistCalibTable(const PoleCalibTable &t);
  bool loadCalibTable(PoleCalibTable &outT);
  bool persistConfigBank(uint8_t bankIdx, const SystemConfigRecord &cfg);
  bool loadConfigBank(uint8_t bankIdx, SystemConfigRecord &outCfg);
  bool persistNodeState();
  bool loadNodeState();
};

extern NodeStorage nodeStorage;

#endif // NODE_STORAGE_H
