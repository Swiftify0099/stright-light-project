/*
 * =====================================================================
 *  HEARTBEATMANAGER.CPP — FIELD POLE AUTONOMOUS HEARTBEAT DISPATCHER
 * =====================================================================
 */

#include "HeartbeatManager.h"
#include "LoRaManager.h"
#include "MeshManager.h"
#include "FeederManager.h"
#include "VoltageSensor.h"
#include "CurrentSensor.h"
#include "StorageManager.h"

HeartbeatManager heartbeatMgr;

HeartbeatManager::HeartbeatManager() :
  tLastHeartbeat(0), currentInterval(HEARTBEAT_INTERVAL_MS)
{}

void HeartbeatManager::init() {
  tLastHeartbeat = millis();
  currentInterval = HEARTBEAT_INTERVAL_MS + random(0, 3000);
}

void HeartbeatManager::sendHeartbeat() {
  voltageSensor.update();
  float vR = voltageSensor.getVoltageR();
  float vY = voltageSensor.getVoltageY();
  float vB = voltageSensor.getVoltageB();
  String vsR = voltageSensor.getVoltageStateR();
  
  currentSensor.update(vR, feederMgr.isMainRelayOn());
  float a = currentSensor.getCurrent();
  String lgt = feederMgr.isMainRelayOn() ? "ON" : "OFF";
  unsigned long up = millis() / 1000;

  // Main telemetry
  String data = "LIGHT=" + lgt + 
                "|V=" + String(vR, 1) + 
                "|VY=" + String(vY, 1) + 
                "|VB=" + String(vB, 1) + 
                "|VS=" + vsR + 
                "|A=" + String(a, 2) + 
                "|UPTIME=" + String(up);

  // Multi-feeder telemetry
  String fdrData = feederMgr.buildFeederTelemetryString(vR, vY, vB);
  if (fdrData.length() > 0) {
    data += "|" + fdrData;
  }

  String dst = GATEWAY_ID;
  String nh  = meshMgr.findNextHop(GATEWAY_ID);

  loraMgr.txMesh("HEARTBEAT", dst, nh, DEFAULT_TTL, loraMgr.nextSeq(), data);
  Serial.printf("[POLE][HB] Autonomous heartbeat pushed (V=%.1fV, A=%.2fA, Uptime: %lus)\n", vR, a, up);
}

void HeartbeatManager::checkHeartbeatInterval() {
  unsigned long now = millis();
  if (now - tLastHeartbeat >= currentInterval) {
    tLastHeartbeat = now;
    sendHeartbeat();
    currentInterval = HEARTBEAT_INTERVAL_MS + random(0, 3000); // Re-jitter
  }
}
