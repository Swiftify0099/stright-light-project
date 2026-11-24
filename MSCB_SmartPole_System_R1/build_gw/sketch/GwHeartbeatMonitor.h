#line 1 "C:\\Users\\panka\\OneDrive\\Desktop\\PS\\MSCB_SmartPole_System_R1\\Gateway_ESP32S3\\GwHeartbeatMonitor.h"
/**
 * @file GwHeartbeatMonitor.h
 * @brief MSCB Smart Pole Gateway - Heartbeat Tracking & Offline Alert Deduplication
 * @version 1.0.0
 * 
 * Enforces:
 * - States: ONLINE, DELAYED, OFFLINE, RECOVERING.
 * - Configurable and persisted heartbeat interval (default 30s) and offline timeout (default 120s).
 * - Alert deduplication: Telegram alerts emitted strictly upon state transition.
 * - Suppresses repetitive alert spam during ongoing outages.
 */

#ifndef GW_HEARTBEAT_MONITOR_H
#define GW_HEARTBEAT_MONITOR_H

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
#include "GwNodeRegistry.h"

enum PoleHealthStateMonitor : uint8_t {
  POLE_STATE_ONLINE     = 0,
  POLE_STATE_DELAYED    = 1,
  POLE_STATE_OFFLINE    = 2,
  POLE_STATE_RECOVERING = 3
};

inline const char* poleHealthStateToString(PoleHealthStateMonitor st) {
  switch (st) {
    case POLE_STATE_ONLINE:     return "ONLINE";
    case POLE_STATE_DELAYED:    return "DELAYED";
    case POLE_STATE_OFFLINE:    return "OFFLINE";
    case POLE_STATE_RECOVERING: return "RECOVERING";
    default:                    return "UNKNOWN";
  }
}

struct PoleHeartbeatStatus {
  uint16_t               poleId;
  uint32_t               lastSeenEpoch;
  uint16_t               missedCount;
  PoleHealthStateMonitor currentState;
  PoleHealthStateMonitor reportedState; // for alert deduplication
  uint32_t               stateChangeTimestamp;
};

class GwHeartbeatMonitor {
public:
  GwHeartbeatMonitor();

  bool begin();
  void checkHealth(uint32_t nowEpoch);

  // Configuration API
  void setHeartbeatIntervalSec(uint16_t intervalSec);
  void setOfflineTimeoutSec(uint16_t timeoutSec);
  uint16_t getHeartbeatIntervalSec() const { return heartbeatIntervalSec; }
  uint16_t getOfflineTimeoutSec() const { return offlineTimeoutSec; }

  // Query API
  PoleHealthStateMonitor getPoleState(uint16_t poleId) const;
  uint16_t getMissedCount(uint16_t poleId) const;
  uint32_t getLastSeen(uint16_t poleId) const;

  // Telegram alert queue
  bool hasPendingAlert() const { return alertPending; }
  const char* getNextAlertText();
  void clearPendingAlert() { alertPending = false; }

  // Record heartbeat ping received
  void onHeartbeatReceived(uint16_t poleId, uint32_t nowEpoch);

private:
  int findIndex(uint16_t poleId) const;
  void queueAlert(uint16_t poleId, PoleHealthStateMonitor newState, uint32_t outageDurationSec = 0);

  uint16_t            heartbeatIntervalSec;
  uint16_t            offlineTimeoutSec;
  uint32_t            lastCheckMs;

  PoleHeartbeatStatus statuses[GW_MAX_MANAGED_POLES];
  uint8_t             statusCount;

  bool                alertPending;
  char                alertBuffer[192];

#if defined(ARDUINO)
  Preferences         prefs;
#endif
};

extern GwHeartbeatMonitor gwHeartbeat;

#endif // GW_HEARTBEAT_MONITOR_H
