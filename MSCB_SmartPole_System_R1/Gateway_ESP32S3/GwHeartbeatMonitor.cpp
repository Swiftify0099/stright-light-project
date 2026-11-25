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
  lastEpochSec(0),
  statusCount(0),
  alertQueueHead(0),
  alertQueueTail(0),
  alertQueueCount(0),
  alertsQueuedCount(0),
  alertsDeliveredCount(0),
  alertsPendingCount(0),
  alertsRetriedCount(0),
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

void GwHeartbeatMonitor::setPendingAlert(uint8_t idx, PoleHealthStateMonitor pendingState,
                                           uint16_t parentId, uint32_t durationSec,
                                           const char* reason) {
  if (idx >= statusCount) return;
  if (!statuses[idx].hasPendingAlert) {
    alertsPendingCount++;
  }
  statuses[idx].hasPendingAlert = true;
  statuses[idx].pendingState = pendingState;
  statuses[idx].pendingParentId = parentId;
  statuses[idx].pendingDurationSec = durationSec;
  strncpy(statuses[idx].pendingReason, (reason != NULL) ? reason : "STATUS_CHANGE",
          sizeof(statuses[idx].pendingReason) - 1);
  statuses[idx].pendingReason[sizeof(statuses[idx].pendingReason) - 1] = '\0';
}

void GwHeartbeatMonitor::onHeartbeatReceived(uint16_t poleId, uint32_t nowEpoch) {
  if (nowEpoch > lastEpochSec) lastEpochSec = nowEpoch;
  int idx = findIndex(poleId);
  if (idx < 0) {
    if (statusCount < GW_MAX_MANAGED_POLES) {
      idx = statusCount++;
      statuses[idx].poleId = poleId;
      statuses[idx].currentState = POLE_STATE_ONLINE;
      statuses[idx].reportedState = POLE_STATE_ONLINE;
      statuses[idx].stateChangeTimestamp = nowEpoch;
      statuses[idx].lastAlertEpoch = 0;
      statuses[idx].hasPendingAlert = false;
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
    bool queued = queueAlert(poleId, POLE_STATE_RECOVERING, outageDuration, 0, "RESTORED_ONLINE", nowEpoch);
    if (queued) {
      statuses[idx].reportedState = POLE_STATE_RECOVERING;
      statuses[idx].lastAlertEpoch = nowEpoch;
      if (statuses[idx].hasPendingAlert) {
        statuses[idx].hasPendingAlert = false;
        if (alertsPendingCount > 0) alertsPendingCount--;
      }
    } else {
      setPendingAlert((uint8_t)idx, POLE_STATE_RECOVERING, 0, outageDuration, "RESTORED_ONLINE");
    }
  } else if (statuses[idx].currentState != POLE_STATE_ONLINE) {
    statuses[idx].currentState = POLE_STATE_ONLINE;
    if (!statuses[idx].hasPendingAlert) {
      statuses[idx].reportedState = POLE_STATE_ONLINE;
    }
  }
}

void GwHeartbeatMonitor::drainPendingAlerts(uint32_t nowEpoch) {
  if (nowEpoch > lastEpochSec) lastEpochSec = nowEpoch;
  uint32_t evalEpoch = (nowEpoch != 0) ? nowEpoch : lastEpochSec;
  for (uint8_t i = 0; i < statusCount; i++) {
    if (statuses[i].hasPendingAlert) {
      if (alertQueueCount >= GW_ALERT_QUEUE_CAPACITY) break;
      bool cooldownOk = (statuses[i].lastAlertEpoch == 0) ||
                        (evalEpoch >= statuses[i].lastAlertEpoch + alertCooldownSec);
      if (cooldownOk) {
        if (queueAlert(statuses[i].poleId, statuses[i].pendingState,
                       statuses[i].pendingDurationSec, statuses[i].pendingParentId,
                       statuses[i].pendingReason, evalEpoch)) {
          statuses[i].reportedState = statuses[i].pendingState;
          statuses[i].lastAlertEpoch = evalEpoch;
          statuses[i].hasPendingAlert = false;
          if (alertsPendingCount > 0) alertsPendingCount--;
          alertsRetriedCount++;
        }
      }
    }
  }
}

void GwHeartbeatMonitor::checkHealth(uint32_t nowEpoch) {
  if (nowEpoch > lastEpochSec) lastEpochSec = nowEpoch;
  // First drain pending alerts that could not be queued previously due to full queue/cooldown
  drainPendingAlerts(nowEpoch);

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
        statuses[idx].hasPendingAlert = false;
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

        // Deduplication & Cooldown: Never mark reported until queue insertion succeeds
        bool cooldownOk = (statuses[idx].lastAlertEpoch == 0) ||
                          (nowEpoch >= statuses[idx].lastAlertEpoch + alertCooldownSec);

        if (statuses[idx].reportedState != targetState && cooldownOk) {
          bool queued = queueAlert(pid, targetState, elapsedSec, parentId, reason, nowEpoch);
          if (queued) {
            statuses[idx].reportedState = targetState;
            statuses[idx].lastAlertEpoch = nowEpoch;
            if (statuses[idx].hasPendingAlert) {
              statuses[idx].hasPendingAlert = false;
              if (alertsPendingCount > 0) alertsPendingCount--;
            }
          } else {
            // Queue was full! Retain in pending retry buffer (never lose alert!)
            setPendingAlert((uint8_t)idx, targetState, parentId, elapsedSec, reason);
          }
        } else if (statuses[idx].reportedState != targetState) {
          alertsDeduplicatedCount++;
          setPendingAlert((uint8_t)idx, targetState, parentId, elapsedSec, reason);
        }
      }
    } else if (elapsedSec >= heartbeatIntervalSec) {
      if (statuses[idx].currentState == POLE_STATE_ONLINE) {
        statuses[idx].currentState = POLE_STATE_SUSPECTED;
      }
    }
  }
}

bool GwHeartbeatMonitor::queueAlert(uint16_t poleId, PoleHealthStateMonitor newState,
                                    uint32_t durationSec, uint16_t parentId,
                                    const char* reason, uint32_t eventEpoch) {
  if (eventEpoch > lastEpochSec) lastEpochSec = eventEpoch;

  // Deduplicate identical unread (poleId, newState) already sitting in queue
  for (uint8_t offset = 0; offset < alertQueueCount; offset++) {
    uint8_t pos = (alertQueueTail + offset) % GW_ALERT_QUEUE_CAPACITY;
    if (alertQueue[pos].poleId == poleId && alertQueue[pos].newState == newState) {
      alertsDeduplicatedCount++;
      return true;
    }
  }

  if (alertQueueCount >= GW_ALERT_QUEUE_CAPACITY) {
    alertsDroppedCount++;
    return false; // Safe queue overflow: return false, caller must retain pending retry
  }

  GwAlertEvent &ev = alertQueue[alertQueueHead];
  ev.poleId = poleId;
  ev.newState = newState;
  ev.eventTimestampEpoch = (eventEpoch != 0) ? eventEpoch : (uint32_t)(millis() / 1000);
  ev.parentId = parentId;
  ev.durationSec = durationSec;
  strncpy(ev.reason, (reason != NULL) ? reason : "TIMEOUT", sizeof(ev.reason) - 1);
  ev.reason[sizeof(ev.reason) - 1] = '\0';

  alertQueueHead = (alertQueueHead + 1) % GW_ALERT_QUEUE_CAPACITY;
  alertQueueCount++;
  alertsQueuedCount++;
  return true;
}

void GwHeartbeatMonitor::recordChildOfflineFromParent(uint16_t childPoleId, uint16_t parentPoleId, uint32_t eventEpoch, const char* reason) {
  if (eventEpoch > lastEpochSec) lastEpochSec = eventEpoch;
  int idx = findIndex(childPoleId);
  if (idx < 0) {
    if (statusCount < GW_MAX_MANAGED_POLES) {
      idx = statusCount++;
      statuses[idx].poleId = childPoleId;
      statuses[idx].lastSeenEpoch = 0;
      statuses[idx].currentState = POLE_STATE_OFFLINE;
      statuses[idx].reportedState = POLE_STATE_ONLINE;
      statuses[idx].stateChangeTimestamp = eventEpoch;
      statuses[idx].lastAlertEpoch = 0;
      statuses[idx].hasPendingAlert = false;
    } else {
      return;
    }
  }

  statuses[idx].currentState = POLE_STATE_OFFLINE;
  statuses[idx].stateChangeTimestamp = eventEpoch;

  bool cooldownOk = (statuses[idx].lastAlertEpoch == 0) ||
                    (eventEpoch >= statuses[idx].lastAlertEpoch + alertCooldownSec);
  if (statuses[idx].reportedState != POLE_STATE_OFFLINE && cooldownOk) {
    bool queued = queueAlert(childPoleId, POLE_STATE_OFFLINE, 0, parentPoleId, reason, eventEpoch);
    if (queued) {
      statuses[idx].reportedState = POLE_STATE_OFFLINE;
      statuses[idx].lastAlertEpoch = eventEpoch;
      if (statuses[idx].hasPendingAlert) {
        statuses[idx].hasPendingAlert = false;
        if (alertsPendingCount > 0) alertsPendingCount--;
      }
    } else {
      setPendingAlert((uint8_t)idx, POLE_STATE_OFFLINE, parentPoleId, 0, reason);
    }
  } else if (statuses[idx].reportedState != POLE_STATE_OFFLINE) {
    alertsDeduplicatedCount++;
    setPendingAlert((uint8_t)idx, POLE_STATE_OFFLINE, parentPoleId, 0, reason);
  }
}

void GwHeartbeatMonitor::recordChildRecoveredFromParent(uint16_t childPoleId, uint16_t parentPoleId, uint32_t eventEpoch) {
  if (eventEpoch > lastEpochSec) lastEpochSec = eventEpoch;
  int idx = findIndex(childPoleId);
  if (idx < 0) return;

  statuses[idx].currentState = POLE_STATE_RECOVERING;
  statuses[idx].lastSeenEpoch = eventEpoch;
  statuses[idx].stateChangeTimestamp = eventEpoch;

  bool cooldownOk = (statuses[idx].lastAlertEpoch == 0) ||
                    (eventEpoch >= statuses[idx].lastAlertEpoch + alertCooldownSec);
  if (statuses[idx].reportedState != POLE_STATE_RECOVERING && cooldownOk) {
    bool queued = queueAlert(childPoleId, POLE_STATE_RECOVERING, 0, parentPoleId, "CHILD_RESTORED_VIA_PARENT", eventEpoch);
    if (queued) {
      statuses[idx].reportedState = POLE_STATE_RECOVERING;
      statuses[idx].lastAlertEpoch = eventEpoch;
      if (statuses[idx].hasPendingAlert) {
        statuses[idx].hasPendingAlert = false;
        if (alertsPendingCount > 0) alertsPendingCount--;
      }
    } else {
      setPendingAlert((uint8_t)idx, POLE_STATE_RECOVERING, parentPoleId, 0, "CHILD_RESTORED_VIA_PARENT");
    }
  } else if (statuses[idx].reportedState != POLE_STATE_RECOVERING) {
    alertsDeduplicatedCount++;
    setPendingAlert((uint8_t)idx, POLE_STATE_RECOVERING, parentPoleId, 0, "CHILD_RESTORED_VIA_PARENT");
  }
}

uint8_t GwHeartbeatMonitor::getPendingPolesCount() const {
  uint8_t count = 0;
  for (uint8_t i = 0; i < statusCount; i++) {
    if (statuses[i].hasPendingAlert) count++;
  }
  return count;
}

bool GwHeartbeatMonitor::peekNextAlert(GwAlertEvent &outEvent) const {
  return peekAlertAt(0, outEvent);
}

bool GwHeartbeatMonitor::peekAlertAt(uint8_t offset, GwAlertEvent &outEvent) const {
  if (offset >= alertQueueCount) return false;
  uint8_t pos = (alertQueueTail + offset) % GW_ALERT_QUEUE_CAPACITY;
  outEvent = alertQueue[pos];
  return true;
}

bool GwHeartbeatMonitor::formatAlertEvent(const GwAlertEvent &ev, char* outBuf, size_t outBufLen) const {
  if (!outBuf || outBufLen == 0) return false;
  RegisteredPole* pole = gwRegistry.findPoleById(ev.poleId);
  const char* poleName = (pole && strlen(pole->displayName) > 0) ? pole->displayName : ((pole && strlen(pole->stringId) > 0) ? pole->stringId : "UNKNOWN");
  const char* loc = (pole && strlen(pole->location) > 0) ? pole->location : "UNKNOWN";
  const char* area = (pole && strlen(pole->area) > 0) ? pole->area : "UNKNOWN";

  if (ev.newState == POLE_STATE_OFFLINE) {
    snprintf(outBuf, outBufLen,
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
    snprintf(outBuf, outBufLen,
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
    snprintf(outBuf, outBufLen,
             "✅ [RESTORED: ONLINE] %s (ID 0x%04X)\n"
             "• Location: %s (%s)\n"
             "• State: ONLINE\n"
             "• Outage Duration: %lu s\n"
             "• Timestamp: %lu",
             poleName, ev.poleId, loc, area,
             (unsigned long)ev.durationSec, (unsigned long)ev.eventTimestampEpoch);
  } else {
    snprintf(outBuf, outBufLen,
             "ℹ️ [STATUS UPDATE] %s (ID 0x%04X) state: %s",
             poleName, ev.poleId, poleHealthStateToString(ev.newState));
  }
  return true;
}

const char* GwHeartbeatMonitor::getNextAlertText() {
  if (alertQueueCount == 0) return "";
  formatAlertEvent(alertQueue[alertQueueTail], alertFormattedBuffer, sizeof(alertFormattedBuffer));
  return alertFormattedBuffer;
}

bool GwHeartbeatMonitor::popNextAlert(GwAlertEvent &outEvent) {
  if (alertQueueCount == 0) return false;
  outEvent = alertQueue[alertQueueTail];
  alertQueueTail = (alertQueueTail + 1) % GW_ALERT_QUEUE_CAPACITY;
  alertQueueCount--;

  // Drain pending alerts using consistent simulation/wall epoch
  uint32_t drainEpoch = (lastEpochSec != 0) ? lastEpochSec : (uint32_t)(millis() / 1000);
  drainPendingAlerts(drainEpoch);
  return true;
}

bool GwHeartbeatMonitor::confirmAlertsDelivered(uint8_t count, uint32_t nowEpoch) {
  if (count == 0 || alertQueueCount == 0) return false;
  uint8_t actual = (count <= alertQueueCount) ? count : alertQueueCount;
  for (uint8_t i = 0; i < actual; i++) {
    alertQueueTail = (alertQueueTail + 1) % GW_ALERT_QUEUE_CAPACITY;
    alertQueueCount--;
    alertsDeliveredCount++;
  }
  uint32_t drainEpoch = (nowEpoch != 0) ? nowEpoch : ((lastEpochSec != 0) ? lastEpochSec : (uint32_t)(millis() / 1000));
  drainPendingAlerts(drainEpoch);
  return true;
}

void GwHeartbeatMonitor::clearPendingAlert() {
  if (alertQueueCount > 0) {
    alertQueueTail = (alertQueueTail + 1) % GW_ALERT_QUEUE_CAPACITY;
    alertQueueCount--;
    uint32_t drainEpoch = (lastEpochSec != 0) ? lastEpochSec : (uint32_t)(millis() / 1000);
    drainPendingAlerts(drainEpoch);
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
