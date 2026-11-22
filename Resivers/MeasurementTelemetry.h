/*
 * =====================================================================
 *  MEASUREMENTTELEMETRY.H — BOUNDED LORA TELEMETRY SCHEDULER
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 *  Fragmented 21-Channel Transmission, Duty-Cycle Throttling & Fault Alerts
 * =====================================================================
 */

#ifndef MEASUREMENT_TELEMETRY_H
#define MEASUREMENT_TELEMETRY_H

#include <Arduino.h>
#include <stdint.h>
#include "PowerMeasurementEngine.h"

// ─────────────────────────────────────────────────────────────
//  TELEMETRY PACKET TYPES
// ─────────────────────────────────────────────────────────────
#define PKT_TEL_VOLT                 "TEL_VOLT"  // 3-Phase Voltages, Freq, Status
#define PKT_TEL_FDR1                 "TEL_FDR1"  // Feeders 1 to 3 Currents, Power, PF
#define PKT_TEL_FDR2                 "TEL_FDR2"  // Feeders 4 to 6 Currents, Power, PF
#define PKT_TEL_ALERT                "TEL_ALERT" // Immediate High-Priority Fault

// Duty Cycle & Scheduling Constants
#define TELEMETRY_STAGGER_INTERVAL_MS 25000UL     // 25s between fragmented batches
#define ROLLING_WINDOW_MS            3600000UL   // 1 hour
#define MAX_AIRTIME_BUDGET_MS        36000UL     // 1% of 1 hour = 36s

// ─────────────────────────────────────────────────────────────
//  TELEMETRY DISPATCHER CLASS
// ─────────────────────────────────────────────────────────────
class MeasurementTelemetry {
public:
  MeasurementTelemetry();

  void init();
  void checkTelemetrySchedule();

  // Packet Generators
  String buildVoltageTelemetryPacket(uint16_t seq);
  String buildFeederBatchPacket(uint8_t batchIdx, uint16_t seq); // batchIdx 0: F1-F3, batchIdx 1: F4-F6
  String buildFaultAlertPacket(uint16_t seq, const String &faultType);

  // Immediate event trigger
  void triggerImmediateAlert(const String &faultDescription);

  // Airtime Accounting
  uint32_t calculateAirtimeMs(uint8_t payloadBytes) const;
  void recordAirtimeUsage(uint32_t airtimeMs);
  float getRollingDutyCyclePercent() const;
  bool isAirtimeAvailable(uint32_t neededAirtimeMs) const;

private:
  uint8_t       currentStaggerSlot; // 0=Voltages, 1=Feeders 1-3, 2=Feeders 4-6
  unsigned long lastStaggerMs;
  unsigned long lastAlertSentMs;
  String        lastAlertFault;

  // Airtime budget tracker
  uint32_t      airtimeUsedInWindowMs;
  unsigned long windowStartMs;
};

extern MeasurementTelemetry telemetryScheduler;

#endif // MEASUREMENT_TELEMETRY_H
