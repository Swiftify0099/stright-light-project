/*
 * =====================================================================
 *  ALERTMANAGER.CPP — MSCB CENTRAL MONITORING & INTELLIGENT ALERTS
 *  MSCB Smart Pole Automation System — Prompt 18 Architecture
 * =====================================================================
 */

#include "AlertManager.h"
#include "MeshManager.h"
#include "TelegramManager.h"
#include "TelegramMenus.h"
#include "FeederManager.h"
#include <time.h>

AlertManager alertMgr;

AlertManager::AlertManager() :
  eventCount(0),
  eventHead(0),
  nextEventId(1001),
  alertCount(0),
  nextAlertId(5001),
  lastAlertDispatchMs(0),
  radioFaultActive(false),
  recentOfflineCount(0),
  aggregationStartTime(0),
  aggregatedAlertSent(false),
  totalAlertsEnqueued(0),
  totalAlertsSent(0),
  totalAlertsSuppressed(0),
  totalAlertsDropped(0)
{
  for (int i = 0; i < EVENT_HISTORY_CAPACITY; i++) {
    eventHistory[i].valid = false;
  }
  for (int i = 0; i < ALERT_QUEUE_CAPACITY; i++) {
    alertQueue[i].active = false;
  }
}

void AlertManager::init() {
  lastAlertDispatchMs = millis();
  Serial.println(F("[GW][ALERT] Intelligent Alert Manager Initialized."));
}

String AlertManager::formatTimestamp(uint32_t ts, bool isCertain) {
  if (!isCertain || ts == 0) {
    return "[UNCERTAIN - NO NTP SYNC]";
  }
  time_t raw = (time_t)ts;
  struct tm *ti = localtime(&raw);
  if (!ti || raw < 1700000000UL) {
    return "[UNCERTAIN - NO NTP SYNC]";
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d IST",
           ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
           ti->tm_hour, ti->tm_min, ti->tm_sec);
  return String(buf);
}

const char* AlertManager::getValidityDescription(MeasurementValidity v) {
  switch (v) {
    case MEAS_VALID:               return "Valid Reading";
    case MEAS_ZERO_CURRENT:        return "Zero Current (Load OFF)";
    case MEAS_NO_SIGNAL:           return "Mains Dead / No Signal";
    case MEAS_UNDER_VOLTAGE:       return "Under-Voltage Fault";
    case MEAS_OVER_VOLTAGE:        return "Over-Voltage Fault";
    case MEAS_OVER_CURRENT:        return "Overcurrent Fault";
    case MEAS_CURRENT_IMBALANCE:   return "Phase Current Imbalance";
    case MEAS_PHASE_LOSS:          return "Phase Loss Detected";
    case MEAS_SENSOR_DISCONNECTED: return "Sensor Disconnected / Fault";
    case MEAS_SATURATED:           return "ADC Input Saturated";
    case MEAS_ADC_OFFLINE:         return "ADC SPI Hardware Failure";
    case MEAS_CAL_INVALID:         return "Calibration Corrupt";
    case MEAS_UNAVAILABLE:         return "Measurement Unavailable";
    case MEAS_STALE:               return "Stale Measurement";
    default:                       return "Unknown Validity";
  }
}

// ─────────────────────────────────────────────────────────────
//  EVENT RECORDING & AUDIT HISTORY
// ─────────────────────────────────────────────────────────────
uint32_t AlertManager::recordEvent(EventType type, AlertSeverity severity, const char* nodeLogicalId,
                                   uint8_t feederId, char phase, const char* description,
                                   uint32_t ts, bool tsCertain) {
  uint32_t eId = nextEventId++;

  // Derive timestamp if not explicitly supplied
  if (ts == 0) {
    time_t raw = time(nullptr);
    if (raw > 1700000000UL) {
      ts = (uint32_t)raw;
      tsCertain = true;
    } else {
      ts = (uint32_t)(millis() / 1000);
      tsCertain = false;
    }
  }

  // Insert into circular ring buffer
  EventRecord &rec = eventHistory[eventHead];
  rec.eventId = eId;
  rec.type = type;
  rec.severity = severity;
  strncpy(rec.nodeLogicalId, (nodeLogicalId && strlen(nodeLogicalId) > 0) ? nodeLogicalId : "GATEWAY", sizeof(rec.nodeLogicalId) - 1);
  rec.nodeLogicalId[sizeof(rec.nodeLogicalId) - 1] = '\0';
  rec.feederId = feederId;
  rec.phase = phase;
  rec.timestamp = ts;
  rec.timestampCertain = tsCertain;
  strncpy(rec.description, description ? description : "System Event", sizeof(rec.description) - 1);
  rec.description[sizeof(rec.description) - 1] = '\0';
  rec.ackStatus = ACK_UNACKNOWLEDGED;
  rec.ackBy[0] = '\0';
  rec.ackTime = 0;
  rec.resolutionStatus = RES_ACTIVE;
  rec.resolvedTime = 0;
  rec.valid = true;

  eventHead = (eventHead + 1) % EVENT_HISTORY_CAPACITY;
  if (eventCount < EVENT_HISTORY_CAPACITY) {
    eventCount++;
  }

  Serial.printf("[GW][EVENT] #%u | %s | %s | Node: %s | F%u %c | %s (TS: %s)\n",
                eId, alertSeverityToString(severity), eventTypeToString(type),
                rec.nodeLogicalId, feederId, phase, rec.description,
                formatTimestamp(ts, tsCertain).c_str());

  return eId;
}

EventRecord* AlertManager::getEvent(uint32_t eventId) {
  for (int i = 0; i < EVENT_HISTORY_CAPACITY; i++) {
    if (eventHistory[i].valid && eventHistory[i].eventId == eventId) {
      return &eventHistory[i];
    }
  }
  return nullptr;
}

int AlertManager::getEventHistoryCount() const {
  return eventCount;
}

EventRecord* AlertManager::getEventByIndex(int index) {
  // 0 = most recent event, 1 = second most recent, etc.
  if (index < 0 || index >= eventCount) return nullptr;
  int pos = (eventHead - 1 - index);
  while (pos < 0) pos += EVENT_HISTORY_CAPACITY;
  if (eventHistory[pos].valid) {
    return &eventHistory[pos];
  }
  return nullptr;
}

void AlertManager::clearEventHistory() {
  for (int i = 0; i < EVENT_HISTORY_CAPACITY; i++) {
    eventHistory[i].valid = false;
  }
  eventCount = 0;
  eventHead = 0;
}

// ─────────────────────────────────────────────────────────────
//  INCIDENT LIFECYCLE & ADMIN ACTIONS
// ─────────────────────────────────────────────────────────────
bool AlertManager::acknowledgeIncident(uint32_t eventId, const char* adminId) {
  EventRecord *ev = getEvent(eventId);
  if (!ev) return false;

  ev->ackStatus = ACK_ACKNOWLEDGED;
  strncpy(ev->ackBy, (adminId && strlen(adminId) > 0) ? adminId : "ADMIN", sizeof(ev->ackBy) - 1);
  ev->ackBy[sizeof(ev->ackBy) - 1] = '\0';

  time_t raw = time(nullptr);
  ev->ackTime = (raw > 1700000000UL) ? (uint32_t)raw : (uint32_t)(millis() / 1000);

  Serial.printf("[GW][ALERT] Incident #%u ACKNOWLEDGED by %s. Underlying physical fault state unchanged.\n",
                eventId, ev->ackBy);
  return true;
}

bool AlertManager::markResolved(uint32_t eventId, const char* adminId) {
  EventRecord *ev = getEvent(eventId);
  if (!ev) return false;

  ev->resolutionStatus = RES_RESOLVED;
  time_t raw = time(nullptr);
  ev->resolvedTime = (raw > 1700000000UL) ? (uint32_t)raw : (uint32_t)(millis() / 1000);

  Serial.printf("[GW][ALERT] Incident #%u MARKED RESOLVED by %s.\n", eventId, adminId ? adminId : "ADMIN");
  return true;
}

// ─────────────────────────────────────────────────────────────
//  NOTIFICATION QUEUE & STORM SUPPRESSION
// ─────────────────────────────────────────────────────────────
bool AlertManager::isDuplicateInCooldown(const char* nodeLogicalId, const char* eventTitle, unsigned long nowMs) {
  for (int i = 0; i < ALERT_QUEUE_CAPACITY; i++) {
    if (alertQueue[i].active) {
      if (strcmp(alertQueue[i].nodeLogicalId, nodeLogicalId) == 0 &&
          strcmp(alertQueue[i].eventTitle, eventTitle) == 0) {
        if (nowMs - alertQueue[i].queuedAt < ALERT_COOLDOWN_MS) {
          return true; // Ongoing duplicate within cooldown
        }
      }
    }
  }
  return false;
}

bool AlertManager::enqueueAlert(uint32_t eventId, AlertSeverity severity, const char* nodeLogicalId,
                                const char* area, const char* eventTitle, const char* priParent,
                                const char* bkpParent, uint32_t lastSeen, bool tsCertain,
                                const char* networkState, const char* details) {
  unsigned long now = millis();

  // 1. Check duplicate suppression cooldown
  if (isDuplicateInCooldown(nodeLogicalId, eventTitle, now)) {
    totalAlertsSuppressed++;
    Serial.printf("[GW][ALERT] Suppressed duplicate alert: '%s' for %s (Cooldown active)\n",
                  eventTitle, nodeLogicalId);
    return false;
  }

  // 2. Check if queue has space
  int targetSlot = -1;
  for (int i = 0; i < ALERT_QUEUE_CAPACITY; i++) {
    if (!alertQueue[i].active) {
      targetSlot = i;
      break;
    }
  }

  // 3. If queue full, prioritize: P0/P1 displace oldest P3/P2
  if (targetSlot == -1) {
    int lowestPrioIdx = -1;
    AlertSeverity lowestSeverity = ALERT_SEV_P0_CRITICAL;

    for (int i = 0; i < ALERT_QUEUE_CAPACITY; i++) {
      if (alertQueue[i].active && alertQueue[i].severity >= lowestSeverity) {
        lowestSeverity = alertQueue[i].severity;
        lowestPrioIdx = i;
      }
    }

    if (lowestPrioIdx >= 0 && severity < alertQueue[lowestPrioIdx].severity) {
      Serial.printf("[GW][ALERT] Queue Congestion: Displacing lower-priority alert #%u (%s) for critical #%u\n",
                    alertQueue[lowestPrioIdx].alertId, alertSeverityToString(alertQueue[lowestPrioIdx].severity), eventId);
      targetSlot = lowestPrioIdx;
      totalAlertsDropped++;
    } else {
      totalAlertsDropped++;
      Serial.printf("[GW][ALERT] Queue Full (Capacity %d): Dropped alert '%s' (%s)\n",
                    ALERT_QUEUE_CAPACITY, eventTitle, alertSeverityToString(severity));
      return false;
    }
  }

  // 4. Fill slot
  QueuedAlert &a = alertQueue[targetSlot];
  a.alertId = nextAlertId++;
  a.eventId = eventId;
  a.severity = severity;
  strncpy(a.nodeLogicalId, nodeLogicalId ? nodeLogicalId : "MSCB-P001", sizeof(a.nodeLogicalId) - 1);
  a.nodeLogicalId[sizeof(a.nodeLogicalId) - 1] = '\0';
  strncpy(a.area, (area && strlen(area) > 0) ? area : "Main District", sizeof(a.area) - 1);
  a.area[sizeof(a.area) - 1] = '\0';
  strncpy(a.eventTitle, eventTitle ? eventTitle : "Electrical Incident", sizeof(a.eventTitle) - 1);
  a.eventTitle[sizeof(a.eventTitle) - 1] = '\0';
  strncpy(a.primaryParent, priParent ? priParent : "GATEWAY", sizeof(a.primaryParent) - 1);
  a.primaryParent[sizeof(a.primaryParent) - 1] = '\0';
  strncpy(a.backupParent, bkpParent ? bkpParent : "NONE", sizeof(a.backupParent) - 1);
  a.backupParent[sizeof(a.backupParent) - 1] = '\0';
  a.lastSeen = lastSeen;
  a.timestampCertain = tsCertain;
  strncpy(a.networkState, networkState ? networkState : "Mesh Active", sizeof(a.networkState) - 1);
  a.networkState[sizeof(a.networkState) - 1] = '\0';
  strncpy(a.details, details ? details : "", sizeof(a.details) - 1);
  a.details[sizeof(a.details) - 1] = '\0';
  a.retries = 0;
  a.queuedAt = now;
  a.nextRetryAt = now;
  a.active = true;
  a.isAggregated = false;
  a.aggregatedCount = 0;

  alertCount++;
  totalAlertsEnqueued++;

  Serial.printf("[GW][ALERT] Enqueued Alert #%u [P%u] '%s' for %s (Queue Depth: %d/%d)\n",
                a.alertId, (unsigned int)severity, a.eventTitle, a.nodeLogicalId, alertCount, ALERT_QUEUE_CAPACITY);

  return true;
}

int AlertManager::getQueueCount() const {
  return alertCount;
}

int AlertManager::findHighestPriorityPendingIndex() {
  int bestIdx = -1;
  AlertSeverity highestSev = (AlertSeverity)255;
  unsigned long now = millis();

  for (int i = 0; i < ALERT_QUEUE_CAPACITY; i++) {
    if (alertQueue[i].active && now >= alertQueue[i].nextRetryAt) {
      if (alertQueue[i].severity < highestSev) {
        highestSev = alertQueue[i].severity;
        bestIdx = i;
      }
    }
  }
  return bestIdx;
}

QueuedAlert* AlertManager::getNextPendingAlert() {
  int idx = findHighestPriorityPendingIndex();
  return (idx >= 0) ? &alertQueue[idx] : nullptr;
}

void AlertManager::onAlertDispatched(uint32_t alertId, bool success) {
  for (int i = 0; i < ALERT_QUEUE_CAPACITY; i++) {
    if (alertQueue[i].active && alertQueue[i].alertId == alertId) {
      if (success) {
        alertQueue[i].active = false;
        alertCount = max(0, alertCount - 1);
        totalAlertsSent++;
        Serial.printf("[GW][ALERT] Alert #%u successfully dispatched to Telegram.\n", alertId);
      } else {
        alertQueue[i].retries++;
        if (alertQueue[i].retries >= TG_ALERT_MAX_RETRIES) {
          alertQueue[i].active = false;
          alertCount = max(0, alertCount - 1);
          totalAlertsDropped++;
          Serial.printf("[GW][ALERT] Alert #%u failed after %d retries. Dropped from queue.\n",
                        alertId, TG_ALERT_MAX_RETRIES);
        } else {
          // Exponential backoff: 2s, 4s, 8s
          unsigned long backoffMs = (1UL << alertQueue[i].retries) * 1000UL;
          alertQueue[i].nextRetryAt = millis() + backoffMs;
          Serial.printf("[GW][ALERT] Alert #%u failed (attempt %d/%d). Retry scheduled in %lums\n",
                        alertId, alertQueue[i].retries, TG_ALERT_MAX_RETRIES, backoffMs);
        }
      }
      break;
    }
  }
}

void AlertManager::purgeExpiredAlerts() {
  unsigned long now = millis();
  for (int i = 0; i < ALERT_QUEUE_CAPACITY; i++) {
    if (alertQueue[i].active && (now - alertQueue[i].queuedAt > 600000UL)) { // 10 min TTL
      alertQueue[i].active = false;
      alertCount = max(0, alertCount - 1);
      totalAlertsDropped++;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  MESSAGE & KEYBOARD BUILDERS (PROMPT 18 SECTION 11 SPEC)
// ─────────────────────────────────────────────────────────────
String AlertManager::buildAlertTelegramMessage(const QueuedAlert &alert) {
  String sevLabel = "HIGH";
  if (alert.severity == ALERT_SEV_P0_CRITICAL) sevLabel = "CRITICAL (P0)";
  else if (alert.severity == ALERT_SEV_P1_HIGH) sevLabel = "HIGH (P1)";
  else if (alert.severity == ALERT_SEV_P2_MEDIUM) sevLabel = "MEDIUM (P2)";
  else sevLabel = "INFO (P3)";

  String msg = "🚨 *MSCB SMART POLE ALERT*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "*Severity:* `" + sevLabel + "`\n\n";
  msg += "*Pole ID:* `" + String(alert.nodeLogicalId) + "`\n\n";
  msg += "*Area:* " + String(alert.area) + "\n\n";
  msg += "*Event:* *" + String(alert.eventTitle) + "*\n\n";
  msg += "*Primary Parent:* `" + String(alert.primaryParent) + "`\n\n";
  msg += "*Backup Parent:* `" + String(alert.backupParent) + "`\n\n";
  msg += "*Last Seen:* `" + formatTimestamp(alert.lastSeen, alert.timestampCertain) + "`\n\n";
  msg += "*Network State:* " + String(alert.networkState) + "\n\n";

  if (strlen(alert.details) > 0) {
    msg += "*Details:*\n" + String(alert.details) + "\n\n";
  }

  msg += "*Actions:*\n";
  msg += "• View Pole\n";
  msg += "• View Network Route\n";
  msg += "• View Event History\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";

  return msg;
}

String AlertManager::buildAlertTelegramKeyboard(const QueuedAlert &alert) {
  String pid = String(alert.nodeLogicalId);
  String eid = String(alert.eventId);

  String kb = "[[{\"text\":\"🔍 View Pole\",\"callback_data\":\"P:" + pid + "\"},"
              "{\"text\":\"🗺️ Network Route\",\"callback_data\":\"TOPOLOGY\"}],"
             "[{\"text\":\"📜 Event History\",\"callback_data\":\"EVT_HIST:0\"},"
              "{\"text\":\"✅ Acknowledge\",\"callback_data\":\"ACK_INC:" + eid + "\"}]]";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  PERIODIC DISPATCHER (NON-BLOCKING)
// ─────────────────────────────────────────────────────────────
void AlertManager::update() {
  unsigned long now = millis();

  // 1. Process storm aggregation
  checkOutageAggregation(now);

  // 2. Check offline timeouts for all registered nodes
  checkNodeConnectivity(now);

  // 3. Enforce rate limiting between Telegram dispatches
  if (now - lastAlertDispatchMs < TG_ALERT_RATE_LIMIT_MS) {
    return;
  }

  // 4. Require verified Telegram connectivity
  if (!tgMgr.isTelegramOnline()) {
    return;
  }

  // 5. Fetch highest priority pending alert
  QueuedAlert *pending = getNextPendingAlert();
  if (!pending) return;

  lastAlertDispatchMs = now;

  String text = buildAlertTelegramMessage(*pending);
  String kb   = buildAlertTelegramKeyboard(*pending);

  bool ok = tgMgr.sendMessageWithInlineKeyboard(OWNER_CHAT, text, "Markdown", kb);
  onAlertDispatched(pending->alertId, ok);
}

// ─────────────────────────────────────────────────────────────
//  FAULT CONFIRMATION & ELECTRICAL DEBOUNCE
// ─────────────────────────────────────────────────────────────
void AlertManager::evaluateOvercurrent(const char* nodeLogicalId, uint8_t feederId, char phase,
                                      float currentA, float ratingA, MeasurementValidity validity,
                                      unsigned long nowMs) {
  NodeRecord *node = meshMgr.findNode(nodeLogicalId);
  if (!node || feederId < 1 || feederId > MAX_FEEDERS) return;

  uint8_t fIdx = feederId - 1;
  ChannelFaultTracker *tracker = nullptr;
  if (phase == 'R') tracker = &node->feeders[fIdx].faultTrackerR;
  else if (phase == 'Y') tracker = &node->feeders[fIdx].faultTrackerY;
  else tracker = &node->feeders[fIdx].faultTrackerB;

  // Measurement validity gate: ignore noisy, open, or offline sensors
  if (validity == MEAS_SENSOR_DISCONNECTED || validity == MEAS_ADC_OFFLINE || validity == MEAS_CAL_INVALID) {
    return;
  }

  // Fault initiation check
  if (currentA > ratingA) {
    if (!tracker->inFault) {
      tracker->inFault = true;
      tracker->firstTriggerTime = nowMs;
      tracker->peakObserved = currentA;
    } else {
      if (currentA > tracker->peakObserved) tracker->peakObserved = currentA;

      // Confirmation duration check (3000ms persistent condition)
      if (!tracker->incidentGenerated && (nowMs - tracker->firstTriggerTime >= CONF_OVERCURRENT_DUR_MS)) {
        tracker->incidentGenerated = true;
        tracker->lastAlertTime = nowMs;

        char desc[64];
        snprintf(desc, sizeof(desc), "Overcurrent %.1fA on F%u Phase %c (Limit: %.1fA)",
                 currentA, feederId, phase, ratingA);

        uint32_t eId = recordEvent(EVT_OVERCURRENT, ALERT_SEV_P1_HIGH, nodeLogicalId,
                                   feederId, phase, desc);

        char det[96];
        snprintf(det, sizeof(det), "Feeder %u Phase %c current reached %.1f A (Trip limit: %.1f A). Persisted > 3.0s.",
                 feederId, phase, currentA, ratingA);

        enqueueAlert(eId, ALERT_SEV_P1_HIGH, nodeLogicalId, node->area,
                     "Confirmed Overcurrent", node->primaryParent.c_str(),
                     node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                     (time(nullptr) > 1700000000UL), "Mesh Operational", det);

        node->feeders[fIdx].faultState = "OVER_CURR";
        node->fault = "OVER_CURR";
      }
    }
  } else if (currentA <= (ratingA - CONF_OVERCURRENT_HYS_A)) {
    // Hysteresis clearing check: current must drop below (rating - 1.0A)
    if (tracker->inFault) {
      if (tracker->incidentGenerated) {
        // Recovery Event!
        char desc[64];
        snprintf(desc, sizeof(desc), "Overcurrent Cleared on F%u Phase %c (Now: %.1fA)",
                 feederId, phase, currentA);

        uint32_t eId = recordEvent(EVT_POLE_RECOVERED, ALERT_SEV_P3_INFO, nodeLogicalId,
                                   feederId, phase, desc);

        char det[96];
        snprintf(det, sizeof(det), "Feeder %u Phase %c current returned to normal (%.1f A).",
                 feederId, phase, currentA);

        enqueueAlert(eId, ALERT_SEV_P3_INFO, nodeLogicalId, node->area,
                     "Overcurrent Cleared", node->primaryParent.c_str(),
                     node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                     (time(nullptr) > 1700000000UL), "Mesh Operational", det);

        node->feeders[fIdx].faultState = "OK";
        node->fault = "OK";
      }
      tracker->inFault = false;
      tracker->incidentGenerated = false;
    }
  }
}

void AlertManager::evaluateVoltage(const char* nodeLogicalId, char phase, float voltageV,
                                   MeasurementValidity validity, unsigned long nowMs) {
  NodeRecord *node = meshMgr.findNode(nodeLogicalId);
  if (!node) return;

  ChannelFaultTracker *tracker = nullptr;
  if (phase == 'R') tracker = &node->voltFaultTrackerR;
  else if (phase == 'Y') tracker = &node->voltFaultTrackerY;
  else tracker = &node->voltFaultTrackerB;

  // Ignore mains dead or sensor failure for voltage band evaluation
  if (validity == MEAS_NO_SIGNAL || validity == MEAS_SENSOR_DISCONNECTED || validity == MEAS_ADC_OFFLINE) {
    return;
  }

  bool isOverVolt  = (voltageV > CONF_OVERVOLT_THRESH_V);
  bool isUnderVolt = (voltageV < CONF_UNDERVOLT_THRESH_V && voltageV >= CONF_PHASE_LOSS_THRESH_V);

  if (isOverVolt || isUnderVolt) {
    unsigned long durLimit = isOverVolt ? CONF_OVERVOLT_DUR_MS : CONF_UNDERVOLT_DUR_MS;

    if (!tracker->inFault) {
      tracker->inFault = true;
      tracker->firstTriggerTime = nowMs;
      tracker->peakObserved = voltageV;
    } else {
      if (nowMs - tracker->firstTriggerTime >= durLimit && !tracker->incidentGenerated) {
        tracker->incidentGenerated = true;
        tracker->lastAlertTime = nowMs;

        const char *faultType = isOverVolt ? "Over-Voltage" : "Under-Voltage";
        char desc[64];
        snprintf(desc, sizeof(desc), "Confirmed %s %.1fV on Phase %c", faultType, voltageV, phase);

        uint32_t eId = recordEvent(EVT_VOLTAGE_FAULT, ALERT_SEV_P1_HIGH, nodeLogicalId,
                                   0, phase, desc);

        char det[96];
        snprintf(det, sizeof(det), "Bus Phase %c voltage %.1f V outside permissible limits (%s). Persisted > %lus.",
                 phase, voltageV, faultType, durLimit / 1000);

        enqueueAlert(eId, ALERT_SEV_P1_HIGH, nodeLogicalId, node->area,
                     desc, node->primaryParent.c_str(),
                     node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                     (time(nullptr) > 1700000000UL), "Mesh Operational", det);
      }
    }
  } else {
    // Normalization check with hysteresis
    bool normalR = (voltageV <= (CONF_OVERVOLT_THRESH_V - CONF_OVERVOLT_HYS_V) &&
                    voltageV >= (CONF_UNDERVOLT_THRESH_V + CONF_UNDERVOLT_HYS_V));

    if (normalR && tracker->inFault) {
      if (tracker->incidentGenerated) {
        char desc[64];
        snprintf(desc, sizeof(desc), "Voltage Normalised to %.1fV on Phase %c", voltageV, phase);

        uint32_t eId = recordEvent(EVT_POLE_RECOVERED, ALERT_SEV_P3_INFO, nodeLogicalId,
                                   0, phase, desc);

        enqueueAlert(eId, ALERT_SEV_P3_INFO, nodeLogicalId, node->area,
                     desc, node->primaryParent.c_str(),
                     node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                     (time(nullptr) > 1700000000UL), "Mesh Operational", "Voltage returned within nominal band.");
      }
      tracker->inFault = false;
      tracker->incidentGenerated = false;
    }
  }
}

void AlertManager::evaluateMcbTrip(const char* nodeLogicalId, uint8_t feederId, char phase,
                                   bool mcbTripped, unsigned long nowMs) {
  NodeRecord *node = meshMgr.findNode(nodeLogicalId);
  if (!node || feederId < 1 || feederId > MAX_FEEDERS) return;

  uint8_t fIdx = feederId - 1;
  FeederState &f = node->feeders[fIdx];

  if (mcbTripped) {
    if (f.mcbStatus) { // Transition from Normal (true) to Tripped (false)
      f.mcbStatus = false;
      f.faultState = "MCB_TRIP";
      f.tripCount++;

      char desc[64];
      snprintf(desc, sizeof(desc), "Confirmed MCB Trip on Feeder %u Phase %c", feederId, phase);

      // CRITICAL P0 INCIDENT!
      uint32_t eId = recordEvent(EVT_MCB_TRIP, ALERT_SEV_P0_CRITICAL, nodeLogicalId,
                                 feederId, phase, desc);

      char det[96];
      snprintf(det, sizeof(det), "MCB Trip auxiliary contact triggered on Feeder %u Phase %c. Relay open, contactor opened.",
               feederId, phase);

      enqueueAlert(eId, ALERT_SEV_P0_CRITICAL, nodeLogicalId, node->area,
                   "Confirmed MCB Trip", node->primaryParent.c_str(),
                   node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                   (time(nullptr) > 1700000000UL), "Protection Tripped", det);
    }
  } else {
    if (!f.mcbStatus) {
      f.mcbStatus = true;
      if (f.faultState == "MCB_TRIP") f.faultState = "OK";

      char desc[64];
      snprintf(desc, sizeof(desc), "MCB Reclosed / Normal on Feeder %u Phase %c", feederId, phase);

      uint32_t eId = recordEvent(EVT_POLE_RECOVERED, ALERT_SEV_P3_INFO, nodeLogicalId,
                                 feederId, phase, desc);

      enqueueAlert(eId, ALERT_SEV_P3_INFO, nodeLogicalId, node->area,
                   "MCB Reset Normal", node->primaryParent.c_str(),
                   node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                   (time(nullptr) > 1700000000UL), "Mesh Operational", "MCB contacts closed and verified.");
    }
  }
}

void AlertManager::evaluatePhaseLoss(const char* nodeLogicalId, float vR, float vY, float vB,
                                     unsigned long nowMs) {
  NodeRecord *node = meshMgr.findNode(nodeLogicalId);
  if (!node) return;

  bool deadR = (vR < CONF_PHASE_LOSS_THRESH_V);
  bool deadY = (vY < CONF_PHASE_LOSS_THRESH_V);
  bool deadB = (vB < CONF_PHASE_LOSS_THRESH_V);

  // If ALL 3 are dead, it is Mains Power Failure / Blackout (handled by offline/no signal)
  if (deadR && deadY && deadB) return;

  // Single or double phase loss while other phases remain alive
  if (deadR || deadY || deadB) {
    char missingPhases[8] = "";
    if (deadR) strcat(missingPhases, "R ");
    if (deadY) strcat(missingPhases, "Y ");
    if (deadB) strcat(missingPhases, "B");

    char desc[64];
    snprintf(desc, sizeof(desc), "Phase Loss Detected on %s (VR=%.0f VY=%.0f VB=%.0f)",
             missingPhases, vR, vY, vB);

    uint32_t eId = recordEvent(EVT_VOLTAGE_FAULT, ALERT_SEV_P1_HIGH, nodeLogicalId, 0,
                               deadR ? 'R' : (deadY ? 'Y' : 'B'), desc);

    char det[96];
    snprintf(det, sizeof(det), "Severe Phase Unbalance / Loss: Missing %s. Mains: %.0fV / %.0fV / %.0fV.",
             missingPhases, vR, vY, vB);

    enqueueAlert(eId, ALERT_SEV_P1_HIGH, nodeLogicalId, node->area,
                 "Phase Loss Detected", node->primaryParent.c_str(),
                 node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                 (time(nullptr) > 1700000000UL), "Asymmetric Bus", det);
  }
}

void AlertManager::evaluateSensorFault(const char* nodeLogicalId, uint8_t feederId, char phase,
                                      MeasurementValidity validity, unsigned long nowMs) {
  if (validity == MEAS_SENSOR_DISCONNECTED || validity == MEAS_ADC_OFFLINE) {
    NodeRecord *node = meshMgr.findNode(nodeLogicalId);
    if (!node) return;

    char desc[64];
    snprintf(desc, sizeof(desc), "Sensor Fault: %s on F%u Phase %c",
             getValidityDescription(validity), feederId, phase);

    uint32_t eId = recordEvent(EVT_SENSOR_FAILURE, ALERT_SEV_P2_MEDIUM, nodeLogicalId,
                               feederId, phase, desc);

    enqueueAlert(eId, ALERT_SEV_P2_MEDIUM, nodeLogicalId, node->area,
                 "Sensor Degradation", node->primaryParent.c_str(),
                 node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                 (time(nullptr) > 1700000000UL), "Sensor Degraded", desc);
  }
}

// ─────────────────────────────────────────────────────────────
//  OFFLINE DETECTION & NETWORK CLASSIFICATION
// ─────────────────────────────────────────────────────────────
void AlertManager::checkNodeConnectivity(unsigned long nowMs) {
  int count = meshMgr.getNodeCount();

  for (int i = 0; i < count; i++) {
    NodeRecord *node = meshMgr.getNode(i);
    if (!node || !node->registered || node->lastSeen == 0) continue;

    unsigned long elapsed = nowMs - node->lastSeen;

    if (elapsed < COMM_UNREACHABLE_TIMEOUT_MS) {
      // Normal connection
      if (node->connectivityDetail != CONN_ONLINE) {
        node->connectivityDetail = CONN_ONLINE;
        node->onlineState = "ONLINE";
        node->offlineConfirmationCount = 0;
      }
    }
    else if (elapsed >= COMM_UNREACHABLE_TIMEOUT_MS && elapsed < COMM_OFFLINE_CONFIRM_MS) {
      // Unreachable stage: do NOT declare electrically disconnected
      if (node->connectivityDetail == CONN_ONLINE) {
        node->offlineCandidateTime = nowMs;
        node->offlineConfirmationCount = 1;

        // Check if communicating via backup route
        if (node->connectionMode == "ROUTE_RECOVERY" || node->backupParent == node->primaryNextHop) {
          node->connectivityDetail = CONN_BACKUP_PARENT_ACTIVE;
          node->onlineState = "DEGRADED";
        } else if (node->orphan) {
          node->connectivityDetail = CONN_ROUTE_LOST;
          node->onlineState = "DEGRADED";
        } else {
          node->connectivityDetail = CONN_UNREACHABLE;
          node->onlineState = "DEGRADED";
        }
      }
    }
    else if (elapsed >= COMM_OFFLINE_CONFIRM_MS) {
      // Offline timeout reached
      node->offlineConfirmationCount++;

      if (node->offlineConfirmationCount >= OFFLINE_CONFIRMATION_CYCLES &&
          node->connectivityDetail != CONN_CONFIRMED_OFFLINE) {
        node->connectivityDetail = CONN_CONFIRMED_OFFLINE;
        node->onlineState = "OFFLINE";

        for (int f = 0; f < MAX_FEEDERS; f++) {
          node->feeders[f].onlineState = "OFFLINE";
          node->feeders[f].loraState = "OFFLINE";
        }

        uint32_t eId = recordEvent(EVT_POLE_OFFLINE, ALERT_SEV_P1_HIGH, node->logicalId.c_str(),
                                   0, '-', "Communication Lost / Node Confirmed Offline");

        char det[96];
        snprintf(det, sizeof(det), "No LoRa communication for %lu minutes. Radio route lost. (Electrical state unverified).",
                 elapsed / 60000UL);

        enqueueAlert(eId, ALERT_SEV_P1_HIGH, node->logicalId.c_str(), node->area,
                     "Communication Unreachable", node->primaryParent.c_str(),
                     node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                     (time(nullptr) > 1700000000UL), "Route Unavailable", det);

        // Record into storm aggregation tracker
        if (recentOfflineCount < 16) {
          strncpy(recentOfflineNodes[recentOfflineCount], node->logicalId.c_str(), 15);
          recentOfflineNodes[recentOfflineCount][15] = '\0';
          recentOfflineTimes[recentOfflineCount] = nowMs;
          recentOfflineCount++;
          if (recentOfflineCount == 1) aggregationStartTime = nowMs;
        }
      }
    }
  }
}

void AlertManager::onNodePacketReceived(const char* nodeLogicalId, int rssi, int snr, unsigned long nowMs) {
  NodeRecord *node = meshMgr.findNode(nodeLogicalId);
  if (!node) return;

  bool wasOffline = (node->connectivityDetail == CONN_CONFIRMED_OFFLINE ||
                     node->connectivityDetail == CONN_UNREACHABLE ||
                     node->connectivityDetail == CONN_ROUTE_LOST);

  node->lastSeen = nowMs;
  node->rssi = rssi;
  node->snr = snr;
  node->connectivityDetail = CONN_ONLINE;
  node->onlineState = "ONLINE";
  node->offlineConfirmationCount = 0;

  for (int f = 0; f < MAX_FEEDERS; f++) {
    node->feeders[f].onlineState = "ONLINE";
    node->feeders[f].loraState = "ONLINE";
  }

  if (wasOffline && node->registered) {
    char desc[64];
    snprintf(desc, sizeof(desc), "Pole Recovered (RSSI: %d dBm, SNR: %d dB)", rssi, snr);

    uint32_t eId = recordEvent(EVT_POLE_RECOVERED, ALERT_SEV_P3_INFO, nodeLogicalId,
                               0, '-', desc);

    char det[96];
    snprintf(det, sizeof(det), "Mesh RF link restored. RSSI: %d dBm. Primary Route: %s.",
             rssi, node->primaryParent.c_str());

    enqueueAlert(eId, ALERT_SEV_P3_INFO, nodeLogicalId, node->area,
                 "Node Recovered", node->primaryParent.c_str(),
                 node->backupParent.c_str(), (uint32_t)(nowMs / 1000),
                 (time(nullptr) > 1700000000UL), "Link Restored", det);
  }
}

void AlertManager::onParentChange(const char* nodeLogicalId, const char* oldParent,
                                  const char* newParent, bool isBackup) {
  NodeRecord *node = meshMgr.findNode(nodeLogicalId);
  if (node) {
    if (isBackup) {
      node->backupParent = newParent;
      node->connectivityDetail = CONN_BACKUP_PARENT_ACTIVE;

      char desc[64];
      snprintf(desc, sizeof(desc), "Backup Parent Activated: %s -> %s", oldParent, newParent);

      uint32_t eId = recordEvent(EVT_BACKUP_PARENT_ACTIVE, ALERT_SEV_P2_MEDIUM, nodeLogicalId,
                                 0, '-', desc);

      enqueueAlert(eId, ALERT_SEV_P2_MEDIUM, nodeLogicalId, node->area,
                   "Backup Parent Activated", node->primaryParent.c_str(),
                   node->backupParent.c_str(), (uint32_t)(node->lastSeen / 1000),
                   (time(nullptr) > 1700000000UL), "Mesh Rerouted", desc);
    } else {
      node->primaryParent = newParent;

      char desc[64];
      snprintf(desc, sizeof(desc), "Parent Changed: %s -> %s", oldParent, newParent);

      recordEvent(EVT_PARENT_CHANGED, ALERT_SEV_P3_INFO, nodeLogicalId, 0, '-', desc);
    }
  }
}

void AlertManager::onRadioFailure(bool failed) {
  if (failed && !radioFaultActive) {
    radioFaultActive = true;
    uint32_t eId = recordEvent(EVT_RADIO_FAILURE, ALERT_SEV_P1_HIGH, "GATEWAY", 0, '-',
                               "SX1278 LoRa Hardware Radio SPI Failure");

    enqueueAlert(eId, ALERT_SEV_P1_HIGH, "GATEWAY", "Control Center",
                 "Gateway Radio Failure", "NONE", "NONE",
                 (uint32_t)(millis() / 1000), (time(nullptr) > 1700000000UL),
                 "Radio Down", "Gateway SX1278 transceiver failed communication test.");
  } else if (!failed && radioFaultActive) {
    radioFaultActive = false;
    recordEvent(EVT_POLE_RECOVERED, ALERT_SEV_P3_INFO, "GATEWAY", 0, '-',
                "SX1278 LoRa Hardware Radio Recovered");
  }
}

// ─────────────────────────────────────────────────────────────
//  STORM AGGREGATION (OUTAGE CLUSTER SUPPRESSION)
// ─────────────────────────────────────────────────────────────
void AlertManager::checkOutageAggregation(unsigned long nowMs) {
  if (recentOfflineCount >= AGGREGATION_MIN_NODES && !aggregatedAlertSent) {
    if (nowMs - aggregationStartTime <= AGGREGATION_WINDOW_MS) {
      aggregatedAlertSent = true;

      String clusterList = "";
      for (int i = 0; i < recentOfflineCount && i < 6; i++) {
        clusterList += String(recentOfflineNodes[i]) + ", ";
      }
      if (clusterList.endsWith(", ")) clusterList = clusterList.substring(0, clusterList.length() - 2);

      char desc[96];
      snprintf(desc, sizeof(desc), "Aggregated Outage: %d Nodes Unreachable [%s]",
               recentOfflineCount, clusterList.c_str());

      uint32_t eId = recordEvent(EVT_POLE_OFFLINE, ALERT_SEV_P1_HIGH, "NETWORK", 0, '-', desc);

      enqueueAlert(eId, ALERT_SEV_P1_HIGH, "NETWORK", "All Sectors",
                   "Multi-Node Outage Detected", "GATEWAY", "NONE",
                   (uint32_t)(nowMs / 1000), (time(nullptr) > 1700000000UL),
                   "Multiple Branches Down", desc);

      Serial.printf("[GW][ALERT] ⚡ STORM AGGREGATION TRIGGERED: %d nodes offline in %lums!\n",
                    recentOfflineCount, nowMs - aggregationStartTime);
    }
  }

  if (nowMs - aggregationStartTime > (AGGREGATION_WINDOW_MS * 2)) {
    recentOfflineCount = 0;
    aggregatedAlertSent = false;
  }
}

uint32_t AlertManager::getActiveFaultCount() {
  uint32_t count = 0;
  int nNodes = meshMgr.getNodeCount();
  for (int i = 0; i < nNodes; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;
    if (n->onlineState == "OFFLINE" || n->connectivityDetail == CONN_CONFIRMED_OFFLINE) count++;
    else if (n->fault != "OK" && n->fault.length() > 0 && n->fault != "NONE") count++;
    for (int f = 0; f < MAX_FEEDERS; f++) {
      if (!n->feeders[f].mcbStatus || (n->feeders[f].faultState != "OK" && n->feeders[f].faultState != "NONE" && n->feeders[f].faultState.length() > 0)) {
        count++;
      }
    }
  }
  return count;
}
