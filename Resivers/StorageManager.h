/*
 * =====================================================================
 *  STORAGEMANAGER.H — FIELD POLE NVS PERSISTENCE & SETTINGS
 * =====================================================================
 */

#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

class StorageManager {
public:
  StorageManager();
  void init();

  void generateHwNodeId();
  void loadPoleStateFromNVS();
  void savePoleStateToNVS();
  void saveRelayState(const String &state);
  void saveSchedule(bool enabled, int onMin, int offMin);

  String getHwNodeId() const;
  String getNodeId() const;
  String getNodeName() const;
  String getNodeType() const;
  String getNodeLocation() const;
  bool   isRegistered() const;
  String getPrevRelayState() const;

  bool   isScheduleEnabled() const;
  int    getScheduleOnMin() const;
  int    getScheduleOffMin() const;

  void   setRegistration(const String &logicalId, const String &name, const String &type);

  // Revocation management (Prompt 12)
  bool   isRevoked() const;
  String getRevocationTxId() const;
  void   setRevoked(const String &txId, const String &reason);
  void   clearRevocation();

private:
  Preferences prefs;
  String hwNodeId;
  String nodeId;
  String nodeName;
  String nodeType;
  String nodeLoc;
  bool   registered;
  bool   revoked;
  String revocationTxId;
  String revocationReason;
  String prevRelayState;

  bool   schedEnabled;
  int    schedOnMin;
  int    schedOffMin;
};

extern StorageManager storageMgr;

#endif // STORAGE_MANAGER_H
