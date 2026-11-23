/**
 * @file GwNodeRegistry.h
 * @brief MSCB Smart Pole Gateway - Persistent Pole Registry & Revocation Blacklist
 * @version 1.0.0
 * 
 * Enforces:
 * - Up to 50 active Managed Pole Nodes.
 * - Discovery & pending registration queue with Admin Approve/Reject.
 * - Permanent hardware MAC revocation blacklist (NVS persisted).
 * - Child route migration trigger upon node deletion.
 */

#ifndef GW_NODE_REGISTRY_H
#define GW_NODE_REGISTRY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <Preferences.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "GwConfig.h"
#if __has_include("../MSCBShared/StorageTypes.h")
  #include "../MSCBShared/StorageTypes.h"
  #include "../MSCBShared/Protocol.h"
#elif __has_include(<StorageTypes.h>)
  #include <StorageTypes.h>
  #include <Protocol.h>
#else
  #include "StorageTypes.h"
  #include "Protocol.h"
#endif

#define GW_MAX_PENDING_REGISTRATIONS 16
#define GW_POLE_ID_START             0x0002 // Pole 2 (Gateway is 0x0001)

#define MSCB_REG_SCHEMA_V1           1
#define MSCB_REG_SCHEMA_V2           2
#define MSCB_CURRENT_REG_SCHEMA      MSCB_REG_SCHEMA_V2

enum PoleRegistrationStatus : uint8_t {
  POLE_STATUS_EMPTY    = 0,
  POLE_STATUS_PENDING  = 1,
  POLE_STATUS_ACTIVE   = 2,
  POLE_STATUS_REJECTED = 3,
  POLE_STATUS_REVOKED  = 4
};

enum PoleDeleteResult : uint8_t {
  DELETE_SUCCESS                    = 0,
  DELETE_ERR_POLE_NOT_FOUND         = 1,
  DELETE_ERR_REVOCATION_FULL        = 2,
  DELETE_ERR_REVOCATION_SAVE_FAILED = 3,
  DELETE_ERR_REGISTRY_SAVE_FAILED   = 4,
  DELETE_ERR_ROLLED_BACK            = 5,
  DELETE_ERR_ALREADY_REVOKED        = 6
};

inline const char* poleDeleteResultToString(PoleDeleteResult res) {
  switch (res) {
    case DELETE_SUCCESS: return "SUCCESS";
    case DELETE_ERR_POLE_NOT_FOUND: return "POLE_NOT_FOUND";
    case DELETE_ERR_REVOCATION_FULL: return "REVOCATION_CAPACITY_FULL";
    case DELETE_ERR_REVOCATION_SAVE_FAILED: return "REVOCATION_STORAGE_FAILED";
    case DELETE_ERR_REGISTRY_SAVE_FAILED: return "ACTIVE_REGISTRY_WRITE_FAILED";
    case DELETE_ERR_ROLLED_BACK: return "DELETE_TRANSACTION_ROLLED_BACK";
    case DELETE_ERR_ALREADY_REVOKED: return "POLE_REVOKED";
    default: return "UNKNOWN_ERROR";
  }
}

struct RegisteredPole {
  uint8_t  mac[6];
  uint16_t poleId;              // e.g. 0x0002
  char     stringId[16];        // e.g. "POLE_02"
  uint8_t  status;              // PoleRegistrationStatus
  uint16_t firmwareVersion;
  uint16_t primaryParent;
  uint16_t backupParent;
  uint8_t  rank;
  uint32_t sessionToken;
  uint32_t registeredTimestamp;
  uint32_t lastSeenTimestamp;
  int16_t  lastRssi;
  int8_t   lastSnr;
  uint8_t  feederStateMask;     // 6 feeders bitmask
  uint8_t  auxFeedbackMask;
  uint8_t  mcbStatusMask;
  float    lastBusVoltage;
  float    lastCurrent;
};

struct PendingRegistration {
  uint8_t  mac[6];
  uint16_t requestedParent;
  uint16_t firmwareVersion;
  uint32_t sessionToken;
  int16_t  rssi;
  int8_t   snr;
  uint32_t requestTimestamp;
  bool     notifiedAdmin;
};

class GwNodeRegistry {
public:
  GwNodeRegistry();

  bool begin();

  // Registration Engine
  uint8_t handleJoinRequest(const PktRegisterRequestPayload &req,
                            int16_t rssi, int8_t snr,
                            PktRegisterResponsePayload &outResp);

  bool approvePending(const uint8_t mac[6], uint16_t &outAssignedId);
  bool rejectPending(const uint8_t mac[6]);

  // Node Deletion & Permanent Revocation
  PoleDeleteResult deleteAndRevokePoleEx(uint16_t poleId, const char* adminId, const char* reason);
  bool deleteAndRevokePole(uint16_t poleId, const char* adminId, const char* reason);
  bool isMacRevoked(const uint8_t mac[6]) const;
  bool unrevokePole(const uint8_t mac[6], const char* adminId, const char* reason);

  // Query API
  RegisteredPole* findPoleById(uint16_t poleId);
  RegisteredPole* findPoleByMac(const uint8_t mac[6]);
  const RegisteredPole* getPoleByIndex(uint8_t index) const;

  uint8_t getActiveCount() const { return activeCount; }
  uint8_t getPendingCount() const { return pendingCount; }
  uint8_t getRevokedCount() const { return revokedCount; }
  const PendingRegistration* getPendingByIndex(uint8_t index) const;
  const PermanentRevocationRecord* getRevokedByIndex(uint8_t index) const;
  const PermanentRevocationRecord* findRevokedByMac(const uint8_t mac[6]) const;
  const PermanentRevocationRecord* findRevokedByPoleId(uint16_t poleId) const;

  // Reappearance Alert Management
  bool hasReappearedRevokedAlert() const { return reappearedAlertPending; }
  const char* getReappearedRevokedAlertText() const { return reappearedAlertBuf; }
  void clearReappearedRevokedAlert() { reappearedAlertPending = false; reappearedAlertBuf[0] = '\0'; }
  void setReappearedRevokedAlert(const uint8_t mac[6], const char* prevLogicalId, uint16_t prevPoleId);

  // Heartbeat & Telemetry update
  void updateNodeTelemetry(uint16_t poleId, uint16_t priParent, uint16_t bkpParent,
                           uint8_t rank, uint8_t feederMask, uint8_t auxMask, uint8_t mcbMask,
                           float busVolt, float current, int16_t rssi, int8_t snr);

  // Persistence
  bool saveRegistry();
  bool loadRegistry();

private:
  uint16_t allocateNextPoleId();
  int findPendingIndex(const uint8_t mac[6]) const;

  uint8_t             activeCount;
  uint8_t             pendingCount;
  uint8_t             revokedCount;

  RegisteredPole      poles[GW_MAX_MANAGED_POLES];
  PendingRegistration pending[GW_MAX_PENDING_REGISTRATIONS];
  PermanentRevocationRecord revoked[MSCB_MAX_REVOCATION_RECORDS];

  bool                reappearedAlertPending;
  char                reappearedAlertBuf[256];

#if defined(ARDUINO)
  Preferences         prefs;
#endif
};

extern GwNodeRegistry gwRegistry;

#endif // GW_NODE_REGISTRY_H
