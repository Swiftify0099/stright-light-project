#line 1 "C:\\Users\\panka\\OneDrive\\Desktop\\PS\\MSCB_SmartPole_System_R1\\Gateway_ESP32S3\\GwHeartbeatMonitor.cpp"
/**
 * @file GwHeartbeatMonitor.cpp
 * @brief MSCB Smart Pole Gateway - Heartbeat Tracking & Offline Alert Deduplication Implementation
 * @version 1.0.0
 */

#include "GwHeartbeatMonitor.h"
#include <string.h>
#include <stdio.h>

GwHeartbeatMonitor gwHeartbeat;

GwHeartbeatMonitor::GwHeartbeatMonitor() :
  heartbeatIntervalSec(30),
  offlineTimeoutSec(120),
  lastCheckMs(0),
  statusCount(0),
  alertPending(false)
{
  memset(statuses, 0, sizeof(statuses));
  memset(alertBuffer, 0, sizeof(alertBuffer));
}

bool GwHeartbeatMonitor::begin() {
#if defined(ARDUINO)
  prefs.begin("mscb_hb", false);
  heartbeatIntervalSec = prefs.getUShort("hb_int", 30);
  offlineTimeoutSec = prefs.getUShort("off_to", 120);
  prefs.end();
#endif
  lastCheckMs = millis();
  return true;
}

void GwHeartbeatMonitor::setHeartbeatIntervalSec(uint16_t intervalSec) {
  if (intervalSec < 5) intervalSec = 5;
  if (intervalSec > 3600) intervalSec = 3600;
  heartbeatIntervalSec = intervalSec;
#if defined(ARDUINO)
  prefs.begin("mscb_hb", false);
  prefs.putUShort("hb_int", heartbeatIntervalSec);
  prefs.end();
#endif
}

void GwHeartbeatMonitor::setOfflineTimeoutSec(uint16_t timeoutSec) {
  if (timeoutSec < 15) timeoutSec = 15;
  if (timeoutSec > 7200) timeoutSec = 7200;
  offlineTimeoutSec = timeoutSec;
#if defined(ARDUINO)
  prefs.begin("mscb_hb", false);
  prefs.putUShort("off_to", offlineTimeoutSec);
  prefs.end();
#endif
}

int GwHeartbeatMonitor::findIndex(uint16_t poleId) const {
  for (uint8_t i = 0; i < statusCount; i++) {
    if (statuses[i].poleId == poleId) {
      return (int)i;
    }
  }
  return -1;
}

void GwHeartbeatMonitor::onHeartbeatReceived(uint16_t poleId, uint32_t nowEpoch) {
  int idx = findIndex(poleId);
  if (idx < 0) {
    if (statusCount < GW_MAX_MANAGED_POLES) {
      idx = statusCount++;
      statuses[idx].poleId = poleId;
      statuses[idx].currentState = POLE_STATE_ONLINE;
      statuses[idx].reportedState = POLE_STATE_ONLINE;
      statuses[idx].stateChangeTimestamp = nowEpoch;
    } else {
      return;
    }
  }

  statuses[idx].lastSeenEpoch = nowEpoch;
  statuses[idx].missedCount = 0;

  if (statuses[idx].currentState == POLE_STATE_OFFLINE) {
    // Node recovering after being offline!
    statuses[idx].currentState = POLE_STATE_RECOVERING;
    uint32_t outageDuration = nowEpoch - statuses[idx].stateChangeTimestamp;
    statuses[idx].stateChangeTimestamp = nowEpoch;
    queueAlert(poleId, POLE_STATE_RECOVERING, outageDuration);
  } else if (statuses[idx].currentState != POLE_STATE_ONLINE) {
    statuses[idx].currentState = POLE_STATE_ONLINE;
    statuses[idx].reportedState = POLE_STATE_ONLINE;
  }
}

void GwHeartbeatMonitor::checkHealth(uint32_t nowEpoch) {
  uint8_t totalActive = gwRegistry.getActiveCount();
  for (uint8_t i = 0; i < totalActive; i++) {
    const RegisteredPole* pole = gwRegistry.getPoleByIndex(i);
    if (!pole) continue;

    uint16_t pid = pole->poleId;
    int idx = findIndex(pid);
    if (idx < 0) {
      if (statusCount < GW_MAX_MANAGED_POLES) {
        idx = statusCount++;
        statuses[idx].poleId = pid;
        statuses[idx].lastSeenEpoch = pole->lastSeenTimestamp;
        statuses[idx].currentState = POLE_STATE_ONLINE;
        statuses[idx].reportedState = POLE_STATE_ONLINE;
        statuses[idx].stateChangeTimestamp = nowEpoch;
      } else {
        continue;
      }
    }

    uint32_t elapsedSec = (nowEpoch >= statuses[idx].lastSeenEpoch) ? (nowEpoch - statuses[idx].lastSeenEpoch) : 0;
    statuses[idx].missedCount = (heartbeatIntervalSec > 0) ? (elapsedSec / heartbeatIntervalSec) : 0;

    if (elapsedSec >= offlineTimeoutSec) {
      if (statuses[idx].currentState != POLE_STATE_OFFLINE) {
        statuses[idx].currentState = POLE_STATE_OFFLINE;
        statuses[idx].stateChangeTimestamp = nowEpoch;

        // Deduplication: Only queue alert if we haven't already reported OFFLINE
        if (statuses[idx].reportedState != POLE_STATE_OFFLINE) {
          queueAlert(pid, POLE_STATE_OFFLINE, elapsedSec);
          statuses[idx].reportedState = POLE_STATE_OFFLINE;
        }
      }
    } else if (elapsedSec >= heartbeatIntervalSec) {
      if (statuses[idx].currentState == POLE_STATE_ONLINE) {
        statuses[idx].currentState = POLE_STATE_DELAYED;
      }
    }
  }
}

void GwHeartbeatMonitor::queueAlert(uint16_t poleId, PoleHealthStateMonitor newState, uint32_t durationSec) {
  RegisteredPole* pole = gwRegistry.findPoleById(poleId);
  const char* poleName = pole ? pole->stringId : "POLE";

  if (newState == POLE_STATE_OFFLINE) {
    snprintf(alertBuffer, sizeof(alertBuffer),
             "🚨 [ALERT: OFFLINE] %s (ID 0x%04X) is unreachable!\nMissed heartbeats: %lu s (Timeout: %u s)",
             poleName, poleId, (unsigned long)durationSec, offlineTimeoutSec);
    alertPending = true;
  } else if (newState == POLE_STATE_RECOVERING) {
    snprintf(alertBuffer, sizeof(alertBuffer),
             "✅ [RESTORED: ONLINE] %s (ID 0x%04X) reconnected after %lu s outage.",
             poleName, poleId, (unsigned long)durationSec);
    alertPending = true;
  }
}

const char* GwHeartbeatMonitor::getNextAlertText() {
  return alertBuffer;
}

PoleHealthStateMonitor GwHeartbeatMonitor::getPoleState(uint16_t poleId) const {
  int idx = findIndex(poleId);
  if (idx < 0) return POLE_STATE_ONLINE;
  return statuses[idx].currentState;
}

uint16_t GwHeartbeatMonitor::getMissedCount(uint16_t poleId) const {
  int idx = findIndex(poleId);
  if (idx < 0) return 0;
  return statuses[idx].missedCount;
}

uint32_t GwHeartbeatMonitor::getLastSeen(uint16_t poleId) const {
  int idx = findIndex(poleId);
  if (idx < 0) return 0;
  return statuses[idx].lastSeenEpoch;
}
