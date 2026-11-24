/**
 * @file PoleStorageManager.cpp
 * @brief MSCB Smart Pole Node - Persistent NVS Storage & Calibration Database Implementation
 * @version 1.0.0
 */

#include "PoleStorageManager.h"
#include <string.h>
#include <stdio.h>

PoleStorageManager poleStorage;

PoleStorageManager::PoleStorageManager() :
  registered(false),
  isRevoked(false),
  regState(REG_CLIENT_UNREGISTERED),
  assignedPoleId(POLE_DEFAULT_NODE_ADDRESS),
  primaryParent(MSCB_GATEWAY_NODE_ID),
  backupParent(MSCB_GATEWAY_NODE_ID),
  networkRank(1),
  heartbeatIntervalSec(30)
{
  strncpy(stringId, POLE_DEFAULT_STRING_ID, sizeof(stringId) - 1);
  strncpy(revocationReason, "NONE", sizeof(revocationReason) - 1);
  memset(switchCounts, 0, sizeof(switchCounts));
}

bool PoleStorageManager::begin() {
#if defined(ARDUINO)
  prefs.begin("mscb_pole", false);
  registered = prefs.getBool("reg", false);
  isRevoked = prefs.getBool("revoked", false);
  uint8_t st = prefs.getUChar("reg_st", 0);
  assignedPoleId = prefs.getUShort("pole_id", POLE_DEFAULT_NODE_ADDRESS);
  String sid = prefs.getString("str_id", POLE_DEFAULT_STRING_ID);
  strncpy(stringId, sid.c_str(), sizeof(stringId) - 1);
  String rReason = prefs.getString("rev_rsn", "NONE");
  strncpy(revocationReason, rReason.c_str(), sizeof(revocationReason) - 1);
  primaryParent = prefs.getUShort("pri_p", MSCB_GATEWAY_NODE_ID);
  backupParent = prefs.getUShort("bkp_p", MSCB_GATEWAY_NODE_ID);
  networkRank = prefs.getUChar("rank", 1);
  heartbeatIntervalSec = prefs.getUShort("hb_int", 30);
  prefs.getBytes("sw_cnt", switchCounts, sizeof(switchCounts));
  prefs.end();

  if (isRevoked || st == REG_CLIENT_REVOKED_LOCKED) {
    regState = REG_CLIENT_REVOKED_LOCKED;
    isRevoked = true;
    registered = false;
  } else if (st == REG_CLIENT_REJECTED) {
    regState = REG_CLIENT_REJECTED;
    registered = false;
  } else if (registered && st == REG_CLIENT_REGISTERED) {
    regState = REG_CLIENT_REGISTERED;
  } else {
    regState = (NodeRegClientState)st;
  }
#else
  if (isRevoked || regState == REG_CLIENT_REVOKED_LOCKED) {
    regState = REG_CLIENT_REVOKED_LOCKED;
    isRevoked = true;
    registered = false;
  }
#endif
  return true;
}

void PoleStorageManager::setRevokedLocked(const char* reason) {
  isRevoked = true;
  registered = false;
  regState = REG_CLIENT_REVOKED_LOCKED;
  if (reason && strlen(reason) > 0) {
    strncpy(revocationReason, reason, sizeof(revocationReason) - 1);
  }

#if defined(ARDUINO)
  prefs.begin("mscb_pole", false);
  prefs.putBool("revoked", true);
  prefs.putBool("reg", false);
  prefs.putUChar("reg_st", REG_CLIENT_REVOKED_LOCKED);
  prefs.putString("rev_rsn", revocationReason);
  prefs.end();
#endif
}

void PoleStorageManager::setRegistrationState(NodeRegClientState st) {
  regState = st;
  if (st == REG_CLIENT_REGISTERED) {
    registered = true;
    isRevoked = false;
  } else if (st == REG_CLIENT_REVOKED_LOCKED) {
    isRevoked = true;
    registered = false;
  } else {
    registered = false;
  }

#if defined(ARDUINO)
  prefs.begin("mscb_pole", false);
  prefs.putUChar("reg_st", (uint8_t)st);
  prefs.putBool("reg", registered);
  prefs.putBool("revoked", isRevoked);
  prefs.end();
#endif
}

void PoleStorageManager::saveRegistration(uint16_t poleId, const char* name, uint16_t priParent) {
  assignedPoleId = poleId;
  registered = true;
  isRevoked = false;
  regState = REG_CLIENT_REGISTERED;
  if (name && strlen(name) > 0) {
    strncpy(stringId, name, sizeof(stringId) - 1);
  } else {
    snprintf(stringId, sizeof(stringId), "POLE_%02d", poleId);
  }
  primaryParent = priParent;

#if defined(ARDUINO)
  prefs.begin("mscb_pole", false);
  prefs.putBool("reg", true);
  prefs.putBool("revoked", false);
  prefs.putUChar("reg_st", REG_CLIENT_REGISTERED);
  prefs.putUShort("pole_id", assignedPoleId);
  prefs.putString("str_id", stringId);
  prefs.putUShort("pri_p", primaryParent);
  prefs.end();
#endif
}

void PoleStorageManager::clearRegistration() {
  registered = false;
  regState = REG_CLIENT_UNREGISTERED;
  assignedPoleId = POLE_DEFAULT_NODE_ADDRESS;
  strncpy(stringId, POLE_DEFAULT_STRING_ID, sizeof(stringId) - 1);
  primaryParent = MSCB_GATEWAY_NODE_ID;
  backupParent = MSCB_GATEWAY_NODE_ID;
  networkRank = 1;

#if defined(ARDUINO)
  prefs.begin("mscb_pole", false);
  prefs.putBool("reg", false);
  prefs.putUChar("reg_st", REG_CLIENT_UNREGISTERED);
  prefs.putUShort("pole_id", assignedPoleId);
  prefs.putString("str_id", stringId);
  prefs.end();
#endif
}

void PoleStorageManager::saveRouting(uint16_t priParent, uint16_t bkpParent, uint8_t rank) {
  primaryParent = priParent;
  backupParent = bkpParent;
  networkRank = rank;

#if defined(ARDUINO)
  prefs.begin("mscb_pole", false);
  prefs.putUShort("pri_p", primaryParent);
  prefs.putUShort("bkp_p", backupParent);
  prefs.putUChar("rank", networkRank);
  prefs.end();
#endif
}

void PoleStorageManager::setHeartbeatIntervalSec(uint16_t sec) {
  if (sec < 5) sec = 5;
  heartbeatIntervalSec = sec;
#if defined(ARDUINO)
  prefs.begin("mscb_pole", false);
  prefs.putUShort("hb_int", heartbeatIntervalSec);
  prefs.end();
#endif
}

bool PoleStorageManager::loadCalibration(ChannelCalibRecord outRecords[MSCB_TOTAL_CHANNELS_PER_POLE]) {
  if (!outRecords) return false;
#if defined(ARDUINO)
  prefs.begin("mscb_cal", true);
  size_t bytesRead = prefs.getBytes("cal_tab", outRecords, sizeof(ChannelCalibRecord) * MSCB_TOTAL_CHANNELS_PER_POLE);
  prefs.end();
  return (bytesRead == sizeof(ChannelCalibRecord) * MSCB_TOTAL_CHANNELS_PER_POLE);
#else
  return false;
#endif
}

bool PoleStorageManager::saveCalibration(const ChannelCalibRecord inRecords[MSCB_TOTAL_CHANNELS_PER_POLE]) {
  if (!inRecords) return false;
#if defined(ARDUINO)
  prefs.begin("mscb_cal", false);
  size_t bytesWritten = prefs.putBytes("cal_tab", inRecords, sizeof(ChannelCalibRecord) * MSCB_TOTAL_CHANNELS_PER_POLE);
  prefs.end();
  return (bytesWritten == sizeof(ChannelCalibRecord) * MSCB_TOTAL_CHANNELS_PER_POLE);
#else
  return true;
#endif
}

uint32_t PoleStorageManager::getFeederSwitchCount(uint8_t feederIdx) const {
  if (feederIdx >= 6) return 0;
  return switchCounts[feederIdx];
}

void PoleStorageManager::incrementFeederSwitchCount(uint8_t feederIdx) {
  if (feederIdx >= 6) return;
  switchCounts[feederIdx]++;
#if defined(ARDUINO)
  // Save periodically or on shutdown
  prefs.begin("mscb_pole", false);
  prefs.putBytes("sw_cnt", switchCounts, sizeof(switchCounts));
  prefs.end();
#endif
}

void PoleStorageManager::logFault(uint8_t faultCode, uint8_t feederId, float val, const char* desc) {
#if defined(ARDUINO)
  Serial.printf("[FAULT_LOG] Code %u | Feeder %u | Val %0.2f | %s\n",
                faultCode, feederId, val, desc ? desc : "UNKNOWN");
#endif
}

bool PoleStorageManager::loadPinConfig(PoleHardwarePinConfig &outConfig) {
#if defined(ARDUINO)
  prefs.begin("mscb_pins", true);
  size_t len = prefs.getBytes("pin_cfg", &outConfig, sizeof(PoleHardwarePinConfig));
  prefs.end();
  if (len == sizeof(PoleHardwarePinConfig) && validateHardwarePinConfig(outConfig)) {
    return true;
  }
#endif
  outConfig = getDefaultHardwarePinConfig();
  return false;
}

bool PoleStorageManager::savePinConfig(const PoleHardwarePinConfig &inConfig) {
  if (!validateHardwarePinConfig(inConfig)) return false;
#if defined(ARDUINO)
  prefs.begin("mscb_pins", false);
  size_t written = prefs.putBytes("pin_cfg", &inConfig, sizeof(PoleHardwarePinConfig));
  prefs.end();
  return (written == sizeof(PoleHardwarePinConfig));
#else
  return true;
#endif
}
