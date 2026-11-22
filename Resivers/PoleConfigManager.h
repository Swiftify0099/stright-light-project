/*
 * =====================================================================
 *  POLECONFIGMANAGER.H — FIELD POLE LOCAL CONFIGURATION MANAGER
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 *  Atomic Configuration Application, Local Validation & Anti-Rollback
 * =====================================================================
 */

#ifndef POLE_CONFIG_MANAGER_H
#define POLE_CONFIG_MANAGER_H

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

// Section Enums
enum PoleConfigSection {
  POLE_CFG_SEC_NET = 0,
  POLE_CFG_SEC_POLE,
  POLE_CFG_SEC_FEEDER,
  POLE_CFG_SEC_FAULT,
  POLE_CFG_SEC_TIMING,
  POLE_CFG_SEC_CALIB_V,
  POLE_CFG_SEC_CALIB_I
};

// Configuration Model for Secondary Pole
struct PoleFeederLocalConfig {
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

struct PoleCalibChannelConfig {
  float    offset;
  float    gain;
  float    sensitivity;
  float    phaseDeg;
};

struct PoleFaultLocalConfig {
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

struct PoleLocalConfigRecord {
  uint16_t               currentVersion;
  uint16_t               pendingVersion;
  uint16_t               lastSuccessfulVersion;
  uint32_t               configHash;
  uint32_t               lastUpdateTime;
  char                   lastTxId[32];

  // Network & System
  uint16_t               networkId;
  uint8_t                hopLimit;
  uint16_t               discoveryIntervalSec;
  uint16_t               heartbeatIntervalSec;
  uint8_t                retryCount;
  uint16_t               ackTimeoutMs;

  // Pole Info
  char                   poleName[32];
  char                   poleArea[32];
  char                   poleType[16];
  uint8_t                feederCount;
  uint16_t               offlineTimeoutSec;

  // Feeders
  PoleFeederLocalConfig  feeders[6];

  // Metrology Calibration
  PoleCalibChannelConfig voltCalib[3];    // R, Y, B
  PoleCalibChannelConfig currCalib[6][3]; // 6 Feeders x 3 Phases

  // Fault & Protection
  PoleFaultLocalConfig   fault;

  // Timers
  uint16_t               telemetryIntervalSec;
  uint16_t               alertCooldownSec;
};

class PoleConfigManager {
public:
  PoleConfigManager();

  void init();
  bool loadFromNVS();
  bool saveToNVS();

  // Packet Processing
  bool processConfigPacket(const String &payload, const String &from, uint16_t seq,
                           String &outAckPayload);

  // Accessors
  uint16_t getCurrentVersion() const;
  uint16_t getLastSuccessfulVersion() const;
  uint32_t getConfigHash() const;
  const PoleLocalConfigRecord& getConfig() const;

  // Hardware Drivers synchronization
  void syncHardwareDrivers();

private:
  PoleLocalConfigRecord cfg;
  uint8_t               deviceKey[32];

  void initDefaults();
  uint32_t calculateHash();
};

extern PoleConfigManager poleConfigMgr;

#endif // POLE_CONFIG_MANAGER_H
