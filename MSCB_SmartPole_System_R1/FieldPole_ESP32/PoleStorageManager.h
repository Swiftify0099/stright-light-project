/**
 * @file PoleStorageManager.h
 * @brief MSCB Smart Pole Node - Persistent NVS Storage & Calibration Database
 * @version 1.0.0
 * 
 * Enforces:
 * - Persistent storage of assigned Logical Pole ID & Registration state.
 * - Persistent storage of Primary & Backup Parent routes.
 * - 21-Channel Metrology Calibration Table with CRC32 integrity.
 * - Feeder switch wear counts and fault logs.
 */

#ifndef POLE_STORAGE_MANAGER_H
#define POLE_STORAGE_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <Preferences.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "PoleConfig.h"
#include "../MSCBShared/StorageTypes.h"

class PoleStorageManager {
public:
  PoleStorageManager();

  bool begin();

  // Registration & Identity
  bool isRegistered() const { return registered; }
  uint16_t getAssignedPoleId() const { return assignedPoleId; }
  const char* getStringId() const { return stringId; }
  void saveRegistration(uint16_t poleId, const char* name, uint16_t primaryParent);
  void clearRegistration();

  // Routing State
  uint16_t getPrimaryParent() const { return primaryParent; }
  uint16_t getBackupParent() const { return backupParent; }
  uint8_t  getNetworkRank() const { return networkRank; }
  void saveRouting(uint16_t priParent, uint16_t bkpParent, uint8_t rank);

  // Calibration API (21 Channels)
  bool loadCalibration(ChannelCalibRecord outRecords[MSCB_TOTAL_CHANNELS_PER_POLE]);
  bool saveCalibration(const ChannelCalibRecord inRecords[MSCB_TOTAL_CHANNELS_PER_POLE]);

  // Heartbeat Config
  uint16_t getHeartbeatIntervalSec() const { return heartbeatIntervalSec; }
  void setHeartbeatIntervalSec(uint16_t sec);

  // Wear tracking & faults
  uint32_t getFeederSwitchCount(uint8_t feederIdx) const;
  void incrementFeederSwitchCount(uint8_t feederIdx);
  void logFault(uint8_t faultCode, uint8_t feederId, float val, const char* desc);

private:
  bool     registered;
  uint16_t assignedPoleId;
  char     stringId[16];
  uint16_t primaryParent;
  uint16_t backupParent;
  uint8_t  networkRank;
  uint16_t heartbeatIntervalSec;

  uint32_t switchCounts[6];

#if defined(ARDUINO)
  Preferences prefs;
#endif
};

extern PoleStorageManager poleStorage;

#endif // POLE_STORAGE_MANAGER_H
