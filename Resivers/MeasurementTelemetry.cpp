/*
 * =====================================================================
 *  MEASUREMENTTELEMETRY.CPP — BOUNDED LORA TELEMETRY SCHEDULER
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#include "MeasurementTelemetry.h"
#include "LoRaManager.h"
#include "MeshManager.h"
#include "StorageManager.h"
#include <math.h>

MeasurementTelemetry telemetryScheduler;

MeasurementTelemetry::MeasurementTelemetry() :
  currentStaggerSlot(0),
  lastStaggerMs(0),
  lastAlertSentMs(0),
  lastAlertFault(""),
  airtimeUsedInWindowMs(0),
  windowStartMs(0)
{}

void MeasurementTelemetry::init() {
  lastStaggerMs = millis();
  windowStartMs = millis();
  airtimeUsedInWindowMs = 0;
}

uint32_t MeasurementTelemetry::calculateAirtimeMs(uint8_t payloadBytes) const {
  // LoRa Airtime equation for SF10, BW 125kHz, CR 4/5, CRC enabled, Explicit Header
  // Symbol duration: Tsym = 2^SF / BW = 1024 / 125000 = 8.192 ms
  double tSym = 8.192;
  double tPreamble = (8.0 + 4.25) * tSym; // 100.352 ms
  double temp = (8.0 * (double)payloadBytes - 4.0 * 10.0 + 28.0 + 16.0) / 40.0;
  int nPayloadSym = 8 + (int)ceil(temp) * 5;
  if (nPayloadSym < 8) nPayloadSym = 8;
  double tPayload = (double)nPayloadSym * tSym;
  return (uint32_t)ceil(tPreamble + tPayload);
}

void MeasurementTelemetry::recordAirtimeUsage(uint32_t airtimeMs) {
  unsigned long now = millis();
  if (now - windowStartMs >= ROLLING_WINDOW_MS) {
    windowStartMs = now;
    airtimeUsedInWindowMs = 0;
  }
  airtimeUsedInWindowMs += airtimeMs;
}

float MeasurementTelemetry::getRollingDutyCyclePercent() const {
  return ((float)airtimeUsedInWindowMs / (float)ROLLING_WINDOW_MS) * 100.0f;
}

bool MeasurementTelemetry::isAirtimeAvailable(uint32_t neededAirtimeMs) const {
  unsigned long now = millis();
  if (now - windowStartMs >= ROLLING_WINDOW_MS) {
    return true;
  }
  return ((airtimeUsedInWindowMs + neededAirtimeMs) <= MAX_AIRTIME_BUDGET_MS);
}

String MeasurementTelemetry::buildVoltageTelemetryPacket(uint16_t seq) {
  const CommonVoltageState &vs = metrologyEngine.getVoltageState();
  const char* pId = metrologyEngine.getPoleId();
  uint32_t ts = vs.timestamp;

  char buf[128];
  snprintf(buf, sizeof(buf),
           "P=%s|TS=%lu|VR=%.1f,%u|VY=%.1f,%u|VB=%.1f,%u|F=%.2f|SYM=%d",
           pId, (unsigned long)ts,
           vs.vR_rms, (unsigned int)vs.stateR,
           vs.vY_rms, (unsigned int)vs.stateY,
           vs.vB_rms, (unsigned int)vs.stateB,
           vs.gridFrequencyHz,
           vs.threePhaseSymmetric ? 1 : 0);

  return String(buf);
}

String MeasurementTelemetry::buildFeederBatchPacket(uint8_t batchIdx, uint16_t seq) {
  // batchIdx 0: Feeders 1, 2, 3
  // batchIdx 1: Feeders 4, 5, 6
  uint8_t startFeeder = (batchIdx == 0) ? 1 : 4;
  const char* pId = metrologyEngine.getPoleId();

  String payload = "P=" + String(pId) + "|B=" + String(batchIdx + 1);

  for (uint8_t f = startFeeder; f < startFeeder + 3; f++) {
    const FeederPowerState *st = metrologyEngine.getFeederState(f);
    if (!st) continue;

    char fBuf[96];
    // Compact format per feeder: Fx:IR,IY,IB,Ptot,PF,imbalance,state
    snprintf(fBuf, sizeof(fBuf), "|F%u:%.1f,%.1f,%.1f,%.0f,%.2f,%u",
             f, st->iR_rms, st->iY_rms, st->iB_rms,
             st->pTotal_active, st->pfTotal,
             (st->isOverCurrent ? 5 : (st->isImbalanceFault ? 6 : (unsigned int)st->stateR)));
    payload += String(fBuf);
  }

  return payload;
}

String MeasurementTelemetry::buildFaultAlertPacket(uint16_t seq, const String &faultType) {
  const char* pId = metrologyEngine.getPoleId();
  const CommonVoltageState &vs = metrologyEngine.getVoltageState();
  uint32_t ts = (uint32_t)(millis() / 1000);

  char buf[128];
  snprintf(buf, sizeof(buf),
           "P=%s|TS=%lu|ALERT=%s|VR=%.1f|VY=%.1f|VB=%.1f|F=%.2f",
           pId, (unsigned long)ts,
           faultType.c_str(),
           vs.vR_rms, vs.vY_rms, vs.vB_rms, vs.gridFrequencyHz);

  return String(buf);
}

void MeasurementTelemetry::triggerImmediateAlert(const String &faultDescription) {
  unsigned long now = millis();
  // Throttle consecutive duplicate alerts to once every 10 seconds
  if (faultDescription == lastAlertFault && (now - lastAlertSentMs < 10000UL)) {
    return;
  }

  uint16_t seq = loraMgr.nextSeq();
  String payload = buildFaultAlertPacket(seq, faultDescription);
  uint32_t airtime = calculateAirtimeMs(payload.length());

  if (isAirtimeAvailable(airtime)) {
    recordAirtimeUsage(airtime);
    String nh = meshMgr.findNextHop(GATEWAY_ID);
    loraMgr.txMesh(PKT_TEL_ALERT, GATEWAY_ID, nh, DEFAULT_TTL, seq, payload);
    lastAlertSentMs = now;
    lastAlertFault = faultDescription;
    Serial.printf("[TELEMETRY] CRITICAL ALERT DISPATCHED: %s (Airtime: %ums)\n", faultDescription.c_str(), airtime);
  } else {
    Serial.println(F("[TELEMETRY] ALERT SUPPRESSED: 433 MHz Airtime budget exceeded!"));
  }
}

void MeasurementTelemetry::checkTelemetrySchedule() {
  unsigned long now = millis();
  if (now - lastStaggerMs < TELEMETRY_STAGGER_INTERVAL_MS) {
    return;
  }
  lastStaggerMs = now;

  // Check for immediate pending faults before routine scheduled packet
  if (metrologyEngine.hasActiveFault()) {
    triggerImmediateAlert(metrologyEngine.getSystemFaultSummary());
  }

  uint16_t seq = loraMgr.nextSeq();
  String pktType = "";
  String payload = "";

  if (currentStaggerSlot == 0) {
    // Slot 0: 3-Phase Common Voltage Packet
    pktType = PKT_TEL_VOLT;
    payload = buildVoltageTelemetryPacket(seq);
    currentStaggerSlot = 1;
  } else if (currentStaggerSlot == 1) {
    // Slot 1: Feeders 1 to 3 Current & Power Packet
    pktType = PKT_TEL_FDR1;
    payload = buildFeederBatchPacket(0, seq);
    currentStaggerSlot = 2;
  } else {
    // Slot 2: Feeders 4 to 6 Current & Power Packet
    pktType = PKT_TEL_FDR2;
    payload = buildFeederBatchPacket(1, seq);
    currentStaggerSlot = 0;
  }

  uint32_t airtime = calculateAirtimeMs(payload.length());
  if (!isAirtimeAvailable(airtime)) {
    Serial.printf("[TELEMETRY] Scheduled batch %s skipped: Duty-cycle limit reached (%.2f%% used)\n",
                  pktType.c_str(), getRollingDutyCyclePercent());
    return;
  }

  recordAirtimeUsage(airtime);
  String nh = meshMgr.findNextHop(GATEWAY_ID);
  loraMgr.txMesh(pktType, GATEWAY_ID, nh, DEFAULT_TTL, seq, payload);

  Serial.printf("[TELEMETRY] Slot %d transmitted: %s (Bytes: %d, Airtime: %ums, Duty: %.2f%%)\n",
                (currentStaggerSlot == 0 ? 2 : currentStaggerSlot - 1),
                pktType.c_str(), payload.length(), airtime, getRollingDutyCyclePercent());
}
