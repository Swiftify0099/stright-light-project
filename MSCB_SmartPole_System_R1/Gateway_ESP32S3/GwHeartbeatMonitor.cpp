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
  alertCooldownSec(300),
  lastCheckMs(0),
  statusCount(0),
  alertQueueHead(0),
  alertQueueTail(0),
  alertQueueCount(0),
  alertsQueuedCount(0),
  alertsDroppedCount(0),
  alertsDeduplicatedCount(0)
{
  memset(statuses, 0, sizeof(statuses));
  memset(alertQueue, 0, sizeof(alertQueue));
  memset(alertFormattedBuffer, 0, sizeof(alertFormattedBuffer));
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
      statuses[idx].lastAlertEpoch = 0;
    } else {
      return;
    }
  }

  statuses[idx].lastSeenEpoch = nowEpoch;
  statuses[idx].missedCount = 0;

  if (statuses[idx].currentState == POLE_STATE_OFFLINE ||
      statuses[idx].currentState == POLE_STATE_UNREACHABLE_VIA_PARENT) {
    // Node recovering after being offline!
    statuses[idx].currentState = POLE_STATE_RECOVERING;
    uint32_t outageDuration = (nowEpoch >= statuses[idx].stateChangeTimestamp) ?
                              (nowEpoch - statuses[idx].stateChangeTimestamp) : 0;
    statuses[idx].stateChangeTimestamp = nowEpoch;
    queueAlert(poleId, POLE_STATE_RECOVERING, outageDuration, 0, "RESTORED_ONLINE", nowEpoch);
    statuses[idx].reportedState = POLE_STATE_RECOVERING;
    statuses[idx].lastAlertEpoch = nowEpoch;
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
        statuses[idx].lastAlertEpoch = 0;
      } else {
        continue;
      }
    }

    uint32_t elapsedSec = (nowEpoch >= statuses[idx].lastSeenEpoch) ? (nowEpoch - statuses[idx].lastSeenEpoch) : 0;
    statuses[idx].missedCount = (heartbeatIntervalSec > 0) ? (elapsedSec / heartbeatIntervalSec) : 0;

    if (elapsedSec >= offlineTimeoutSec) {
      // Check if parent node is offline
      uint16_t parentId = pole->primaryParent;
      bool parentOffline = false;
      if (parentId != 0 && parentId != MSCB_GATEWAY_NODE_ID && parentId != pid) {
        PoleHealthStateMonitor parentState = getPoleState(parentId);
        if (parentState == POLE_STATE_OFFLINE || parentState == POLE_STATE_UNREACHABLE_VIA_PARENT) {
          parentOffline = true;
        }
      }

      PoleHealthStateMonitor targetState = parentOffline ? POLE_STATE_UNREACHABLE_VIA_PARENT : POLE_STATE_OFFLINE;
      char cascadeReasonBuf[48];
      if (parentOffline) {
        snprintf(cascadeReasonBuf, sizeof(cascadeReasonBuf), "Parent 0x%04X Offline", parentId);
      }
      const char* reason = parentOffline ? cascadeReasonBuf : "HEARTBEAT_TIMEOUT";

      if (statuses[idx].currentState != targetState) {
        statuses[idx].currentState = targetState;
        statuses[idx].stateChangeTimestamp = nowEpoch;

        // Deduplication & Cooldown: Only queue alert if state changed and cooldown elapsed
        bool cooldownOk = (statuses[idx].lastAlertEpoch == 0) || 
                          (nowEpoch >= statuses[idx].lastAlertEpoch + alertCooldownSec);

        if (statuses[idx].reportedState != targetState && cooldownOk) {
          queueAlert(pid, targetState, elapsedSec, parentId, reason, nowEpoch);
          statuses[idx].reportedState = targetState;
          statuses[idx].lastAlertEpoch = nowEpoch;
        } else if (statuses[idx].reportedState != targetState) {
          alertsDeduplicatedCount++;
        }
      }
    } else if (elapsedSec >= heartbeatIntervalSec) {
      if (statuses[idx].currentState == POLE_STATE_ONLINE) {
        statuses[idx].currentState = POLE_STATE_SUSPECTED;
      }
    }
  }
}

void GwHeartbeatMonitor::queueAlert(uint16_t poleId, PoleHealthStateMonitor newState,
                                    uint32_t durationSec, uint16_t parentId,
                                    const char* reason, uint32_t eventEpoch) {
  if (alertQueueCount >= GW_ALERT_QUEUE_CAPACITY) {
    alertsDroppedCount++;
    return; // Safe queue overflow: do not corrupt or overwrite unread alerts
  }

  GwAlertEvent &ev = alertQueue[alertQueueHead];
  ev.poleId = poleId;
  ev.newState = newState;
  ev.eventTimestampEpoch = (eventEpoch != 0) ? eventEpoch : (uint32_t)millis() / 1000;
  ev.parentId = parentId;
  ev.durationSec = durationSec;
  strncpy(ev.reason, (reason != NULL) ? reason : "TIMEOUT", sizeof(ev.reason) - 1);
  ev.reason[sizeof(ev.reason) - 1] = '\0';

  alertQueueHead = (alertQueueHead + 1) % GW_ALERT_QUEUE_CAPACITY;
  alertQueueCount++;
  alertsQueuedCount++;
}

const char* GwHeartbeatMonitor::getNextAlertText() {
  if (alertQueueCount == 0) return "";
  const GwAlertEvent &ev = alertQueue[alertQueueTail];
  RegisteredPole* pole = gwRegistry.findPoleById(ev.poleId);
  const char* poleName = (pole && strlen(pole->displayName) > 0) ? pole->displayName : ((pole && strlen(pole->stringId) > 0) ? pole->stringId : "UNKNOWN");
  const char* loc = (pole && strlen(pole->location) > 0) ? pole->location : "UNKNOWN";
  const char* area = (pole && strlen(pole->area) > 0) ? pole->area : "UNKNOWN";

  if (ev.newState == POLE_STATE_OFFLINE) {
    snprintf(alertFormattedBuffer, sizeof(alertFormattedBuffer),
             "🚨 [ALERT: OFFLINE] %s (ID 0x%04X)\n"
             "• Location: %s (%s)\n"
             "• State: OFFLINE\n"
             "• Outage Duration: %lu s (Timeout: %u s)\n"
             "• Reason: %s\n"
             "• Timestamp: %lu",
             poleName, ev.poleId, loc, area,
             (unsigned long)ev.durationSec, offlineTimeoutSec,
             ev.reason, (unsigned long)ev.eventTimestampEpoch);
  } else if (ev.newState == POLE_STATE_UNREACHABLE_VIA_PARENT) {
    snprintf(alertFormattedBuffer, sizeof(alertFormattedBuffer),
             "⚠️ [CASCADE ISSUE: UNREACHABLE] %s (ID 0x%04X)\n"
             "• Location: %s (%s)\n"
             "• Parent ID: 0x%04X\n"
             "• State: UNREACHABLE_VIA_PARENT\n"
             "• Duration: %lu s\n"
             "• Reason: Parent node unreachable / cascade\n"
             "• Timestamp: %lu",
             poleName, ev.poleId, loc, area, ev.parentId,
             (unsigned long)ev.durationSec, (unsigned long)ev.eventTimestampEpoch);
  } else if (ev.newState == POLE_STATE_RECOVERING) {
    snprintf(alertFormattedBuffer, sizeof(alertFormattedBuffer),
             "✅ [RESTORED: ONLINE] %s (ID 0x%04X)\n"
             "• Location: %s (%s)\n"
             "• State: ONLINE\n"
             "• Outage Duration: %lu s\n"
             "• Timestamp: %lu",
             poleName, ev.poleId, loc, area,
             (unsigned long)ev.durationSec, (unsigned long)ev.eventTimestampEpoch);
  } else {
    snprintf(alertFormattedBuffer, sizeof(alertFormattedBuffer),
             "ℹ️ [STATUS UPDATE] %s (ID 0x%04X) state: %s",
             poleName, ev.poleId, poleHealthStateToString(ev.newState));
  }
  return alertFormattedBuffer;
}

bool GwHeartbeatMonitor::popNextAlert(GwAlertEvent &outEvent) {
  if (alertQueueCount == 0) return false;
  outEvent = alertQueue[alertQueueTail];
  alertQueueTail = (alertQueueTail + 1) % GW_ALERT_QUEUE_CAPACITY;
  alertQueueCount--;
  return true;
}

void GwHeartbeatMonitor::clearPendingAlert() {
  if (alertQueueCount > 0) {
    alertQueueTail = (alertQueueTail + 1) % GW_ALERT_QUEUE_CAPACITY;
    alertQueueCount--;
  }
}

PoleHealthStateMonitor GwHeartbeatMonitor::getPoleState(uint16_t poleId) const {
  int idx = findIndex(poleId);
  if (idx < 0) return POLE_STATE_UNKNOWN;
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
