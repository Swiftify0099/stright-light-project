/*
 * =====================================================================
 *  ALERTMANAGER.H — MSCB CENTRAL MONITORING & INTELLIGENT ALERTS
 *  MSCB Smart Pole Automation System — Prompt 18 Architecture
 *  P0-P3 Prioritization, Fault Confirmation, Offline Classification,
 *  Bounded Queue, Storm Suppression & Incident Lifecycle Management
 * =====================================================================
 */

#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

class AlertManager {
public:
  AlertManager();
  void init();

  // Periodic processing called from cooperative scheduler loop()
  void update();

  // ─────────────────────────────────────────────────────────────
  //  EVENT RECORDING & AUDIT HISTORY
  // ─────────────────────────────────────────────────────────────
  uint32_t recordEvent(EventType type, AlertSeverity severity, const char* nodeLogicalId,
                       uint8_t feederId, char phase, const char* description,
                       uint32_t ts = 0, bool tsCertain = true);

  EventRecord* getEvent(uint32_t eventId);
  int getEventHistoryCount() const;
  EventRecord* getEventByIndex(int index); // 0 = newest (reverse-chronological)
  void clearEventHistory();

  // ─────────────────────────────────────────────────────────────
  //  INCIDENT LIFECYCLE & ADMIN ACTIONS
  // ─────────────────────────────────────────────────────────────
  bool acknowledgeIncident(uint32_t eventId, const char* adminId);
  bool markResolved(uint32_t eventId, const char* adminId);

  // ─────────────────────────────────────────────────────────────
  //  NOTIFICATION QUEUE & STORM SUPPRESSION
  // ─────────────────────────────────────────────────────────────
  bool enqueueAlert(uint32_t eventId, AlertSeverity severity, const char* nodeLogicalId,
                    const char* area, const char* eventTitle, const char* priParent,
                    const char* bkpParent, uint32_t lastSeen, bool tsCertain,
                    const char* networkState, const char* details);

  int getQueueCount() const;
  QueuedAlert* getNextPendingAlert();
  void onAlertDispatched(uint32_t alertId, bool success);
  void purgeExpiredAlerts();

  // ─────────────────────────────────────────────────────────────
  //  FAULT CONFIRMATION & ELECTRICAL DEBOUNCE
  // ─────────────────────────────────────────────────────────────
  void evaluateOvercurrent(const char* nodeLogicalId, uint8_t feederId, char phase,
                           float currentA, float ratingA, MeasurementValidity validity,
                           unsigned long nowMs);

  void evaluateVoltage(const char* nodeLogicalId, char phase, float voltageV,
                       MeasurementValidity validity, unsigned long nowMs);

  void evaluateMcbTrip(const char* nodeLogicalId, uint8_t feederId, char phase,
                       bool mcbTripped, unsigned long nowMs);

  void evaluatePhaseLoss(const char* nodeLogicalId, float vR, float vY, float vB,
                         unsigned long nowMs);

  void evaluateSensorFault(const char* nodeLogicalId, uint8_t feederId, char phase,
                          MeasurementValidity validity, unsigned long nowMs);

  // ─────────────────────────────────────────────────────────────
  //  OFFLINE DETECTION & NETWORK CLASSIFICATION
  // ─────────────────────────────────────────────────────────────
  void checkNodeConnectivity(unsigned long nowMs);
  void onNodePacketReceived(const char* nodeLogicalId, int rssi, int snr, unsigned long nowMs);
  void onParentChange(const char* nodeLogicalId, const char* oldParent, const char* newParent, bool isBackup);
  void onRadioFailure(bool failed);

  // Aggregation & Storm Prevention
  void checkOutageAggregation(unsigned long nowMs);

  // Formatting helpers
  String buildAlertTelegramMessage(const QueuedAlert &alert);
  String buildAlertTelegramKeyboard(const QueuedAlert &alert);
  String formatTimestamp(uint32_t ts, bool isCertain);

  // Telemetry validity helper
  static const char* getValidityDescription(MeasurementValidity v);

  // Status counters
  uint32_t getTotalAlertsEnqueued() const { return totalAlertsEnqueued; }
  uint32_t getTotalAlertsSent() const { return totalAlertsSent; }
  uint32_t getTotalAlertsSuppressed() const { return totalAlertsSuppressed; }
  uint32_t getTotalAlertsDropped() const { return totalAlertsDropped; }
  uint32_t getActiveFaultCount();

private:
  // Circular Event History Buffer
  EventRecord eventHistory[EVENT_HISTORY_CAPACITY];
  int         eventCount;
  int         eventHead;
  uint32_t    nextEventId;

  // Bounded Notification Priority Queue
  QueuedAlert alertQueue[ALERT_QUEUE_CAPACITY];
  int         alertCount;
  uint32_t    nextAlertId;

  // Rate Limiting & Cooldown Tracking
  unsigned long lastAlertDispatchMs;
  bool          radioFaultActive;

  // Storm Aggregation Tracking
  char          recentOfflineNodes[16][16];
  uint32_t      recentOfflineTimes[16];
  int           recentOfflineCount;
  uint32_t      aggregationStartTime;
  bool          aggregatedAlertSent;

  // Telemetry metrics
  uint32_t      totalAlertsEnqueued;
  uint32_t      totalAlertsSent;
  uint32_t      totalAlertsSuppressed;
  uint32_t      totalAlertsDropped;

  int findHighestPriorityPendingIndex();
  bool isDuplicateInCooldown(const char* nodeLogicalId, const char* eventTitle, unsigned long nowMs);
};

extern AlertManager alertMgr;

#endif // ALERT_MANAGER_H
