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
  POLE_STATE_ONLINE                 = 0,
  POLE_STATE_SUSPECTED              = 1,
  POLE_STATE_DELAYED                = 1, // Alias for backward compatibility
  POLE_STATE_OFFLINE                = 2,
  POLE_STATE_RECOVERING             = 3,
  POLE_STATE_UNREACHABLE_VIA_PARENT = 4,
  POLE_STATE_UNKNOWN                = 5
};

inline const char* poleHealthStateToString(PoleHealthStateMonitor st) {
  switch (st) {
    case POLE_STATE_ONLINE:                 return "ONLINE";
    case POLE_STATE_SUSPECTED:              return "SUSPECTED";
    case POLE_STATE_OFFLINE:                return "OFFLINE";
    case POLE_STATE_RECOVERING:             return "RECOVERING";
    case POLE_STATE_UNREACHABLE_VIA_PARENT: return "UNREACHABLE_VIA_PARENT";
    case POLE_STATE_UNKNOWN:
    default:                                return "UNKNOWN";
  }
}

#define GW_ALERT_QUEUE_CAPACITY 16

struct GwAlertEvent {
  uint16_t               poleId;
  PoleHealthStateMonitor newState;
  uint32_t               eventTimestampEpoch;
  uint16_t               parentId;
  uint32_t               durationSec;
  char                   reason[48];
};

struct PoleHeartbeatStatus {
  uint16_t               poleId;
  uint32_t               lastSeenEpoch;
  uint16_t               missedCount;
  PoleHealthStateMonitor currentState;
  PoleHealthStateMonitor reportedState; // for alert deduplication
  uint32_t               stateChangeTimestamp;
  uint32_t               lastAlertEpoch;       // per-pole cooldown tracking
};

class GwHeartbeatMonitor {
public:
  GwHeartbeatMonitor();

  bool begin();
  void checkHealth(uint32_t nowEpoch);

  // Configuration API
  void setHeartbeatIntervalSec(uint16_t intervalSec);
  void setOfflineTimeoutSec(uint16_t timeoutSec);
  void setAlertCooldownSec(uint16_t cooldownSec) { alertCooldownSec = cooldownSec; }
  uint16_t getHeartbeatIntervalSec() const { return heartbeatIntervalSec; }
  uint16_t getOfflineTimeoutSec() const { return offlineTimeoutSec; }
  uint16_t getAlertCooldownSec() const { return alertCooldownSec; }

  // Query API
  PoleHealthStateMonitor getPoleState(uint16_t poleId) const;
  bool isPoleUnreachableViaParent(uint16_t poleId) const {
    return getPoleState(poleId) == POLE_STATE_UNREACHABLE_VIA_PARENT;
  }
  uint16_t getMissedCount(uint16_t poleId) const;
  uint32_t getLastSeen(uint16_t poleId) const;

  // Bounded Telegram alert queue API (R3.2.1)
  bool hasPendingAlert() const { return alertQueueCount > 0; }
  uint8_t getAlertQueueCount() const { return alertQueueCount; }
  const char* getNextAlertText();
  bool popNextAlert(GwAlertEvent &outEvent);
  void clearPendingAlert();

  // Diagnostic counters
  uint32_t getAlertsQueuedCount() const { return alertsQueuedCount; }
  uint32_t getAlertsDroppedCount() const { return alertsDroppedCount; }
  uint32_t getAlertsDeduplicatedCount() const { return alertsDeduplicatedCount; }

  // Record heartbeat ping received
  void onHeartbeatReceived(uint16_t poleId, uint32_t nowEpoch);

private:
  int findIndex(uint16_t poleId) const;
  void queueAlert(uint16_t poleId, PoleHealthStateMonitor newState, uint32_t outageDurationSec = 0,
                  uint16_t parentId = 0, const char* reason = "TIMEOUT", uint32_t eventEpoch = 0);

  uint16_t            heartbeatIntervalSec;
  uint16_t            offlineTimeoutSec;
  uint16_t            alertCooldownSec;
  uint32_t            lastCheckMs;

  PoleHeartbeatStatus statuses[GW_MAX_MANAGED_POLES];
  uint8_t             statusCount;

  GwAlertEvent        alertQueue[GW_ALERT_QUEUE_CAPACITY];
  uint8_t             alertQueueHead;
  uint8_t             alertQueueTail;
  uint8_t             alertQueueCount;

  uint32_t            alertsQueuedCount;
  uint32_t            alertsDroppedCount;
  uint32_t            alertsDeduplicatedCount;

  char                alertFormattedBuffer[384];

#if defined(ARDUINO)
  Preferences         prefs;
#endif
};

extern GwHeartbeatMonitor gwHeartbeat;

#endif // GW_HEARTBEAT_MONITOR_H
