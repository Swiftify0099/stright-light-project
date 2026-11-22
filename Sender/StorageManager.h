/*
 * =====================================================================
 *  STORAGEMANAGER.H — NVS STORAGE & LOGGING MANAGER
 * =====================================================================
 */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "Types.h"

class StorageManager {
public:
  StorageManager();
  void init();

  void loadDatabaseFromNVS();
  void saveDatabaseToNVS();

  void loadWiFiCredentials(String &ssid, String &pass);
  void saveWiFiCredentials(const String &ssid, const String &pass);

  // Secure Wi-Fi Configuration Storage (Prompt 17)
  void loadWiFiConfig(String &activeSsid, String &activePass, String &prevSsid, String &prevPass, uint8_t &commitState);
  void saveWiFiActive(const String &ssid, const String &pass);
  void saveWiFiPrevious(const String &ssid, const String &pass);
  void setWiFiCommitState(uint8_t state);
  uint8_t getWiFiCommitState();

  void addGwLog(const char* action, const char* target, uint8_t res);
  GwLogEntry* getGwLog(int index);
  uint8_t getGwLogCount() const;

private:
  Preferences prefs;
  GwLogEntry gwLog[MAX_CMD_LOG_ENTRIES];
  uint8_t gwLogHead;
  uint8_t gwLogCount;
};

extern StorageManager storageMgr;

#endif // STORAGE_MANAGER_H
