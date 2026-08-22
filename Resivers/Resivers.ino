/*
 * =====================================================================
 *  SMART STREET LIGHT / FARMER MESH SYSTEM — FIELD POLE FIRMWARE
 *  Version   : POLE_V10
 *  Platform  : ESP32 Dev Module + SX1278 RA-02 433 MHz + Relay + Sensors
 *  Role      : Autonomous Field Node / Feeder Controller / Mesh Router
 *  Architecture: Modular Industrial Architecture
 * =====================================================================
 */

#include <Arduino.h>
#include "config.h"
#include "Types.h"
#include "StorageManager.h"
#include "VoltageSensor.h"
#include "CurrentSensor.h"
#include "FeederManager.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "CommandManager.h"
#include "HeartbeatManager.h"

// ─────────────────────────────────────────────────────────────
//  COOPERATIVE SCHEDULING TIMERS
// ─────────────────────────────────────────────────────────────
static unsigned long tSensorRead    = 0;
static unsigned long tBeacon        = 0;
static unsigned long tNewNodeRetry  = 0;
static unsigned long tSchedCheck    = 0;
static unsigned long tNvsFlush      = 0;
static unsigned long tTimerCheck    = 0;
static unsigned long tNeighReport   = 0;
static int           newNodeRetries = 0;

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=================================================="));
  Serial.printf("  SMART STREET LIGHT FIELD POLE FIRMWARE %s\n", POLE_VERSION);
  Serial.println(F("=================================================="));

  // 1. Initialize Subsystems in Dependency Order
  storageMgr.init();
  voltageSensor.init();
  currentSensor.init();
  feederMgr.init();
  meshMgr.init();
  loraMgr.init();
  cmdMgr.init();
  heartbeatMgr.init();

  // 2. Initialize Timestamps
  unsigned long now = millis();
  tSensorRead   = now;
  tBeacon       = now;
  tNewNodeRetry = now;
  tSchedCheck   = now;
  tNvsFlush     = now;
  tTimerCheck   = now;
  tNeighReport  = now;

  // 3. Initial Registration Probe if Unregistered
  if (!storageMgr.isRegistered()) {
    cmdMgr.sendNewNode();
    newNodeRetries++;
  }
}

// ─────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // 1. High Priority: LoRa Packet Processing & Mesh Routing
  loraMgr.processIncomingLoRa();

  // 2. Fast Non-blocking Sensor & MCB Updating (Every 5s)
  if (now - tSensorRead >= SENSOR_INTERVAL_MS) {
    tSensorRead = now;
    voltageSensor.update();
    currentSensor.update(voltageSensor.getVoltageR(), feederMgr.isMainRelayOn());
    feederMgr.updateMCBInputs();
  }

  // 3. Autonomous Countdown Timer Expiration Check (Every 1s)
  if (now - tTimerCheck >= 1000) {
    tTimerCheck = now;
    cmdMgr.checkTimerExpiration();
  }

  // 4. Neighbor Deep Health Check Completion Watcher
  meshMgr.checkHealthCheckTimeout();

  // 5. Autonomous Jittered Heartbeat Transmission
  heartbeatMgr.checkHeartbeatInterval();

  // 6. Periodic Neighbor Discovery Beacon (Every 30s)
  if (now - tBeacon >= BEACON_INTERVAL_MS) {
    tBeacon = now;
    meshMgr.expireNeighbors();
    meshMgr.sendBeacon();
  }

  // 7. Periodic Neighbor Health Report to Gateway (Every 60s)
  if (storageMgr.isRegistered() && (now - tNeighReport >= NEIGH_REPORT_INTERVAL_MS)) {
    tNeighReport = now;
    meshMgr.sendHealthReport();
  }

  // 8. Autonomous Time-Based Local Schedule (Every 10s)
  if (now - tSchedCheck >= 10000) {
    tSchedCheck = now;
    cmdMgr.checkAutonomousSchedule();
  }

  // 9. Unregistered Node Join Retries (Every 10s up to MAX_RETRIES)
  if (!storageMgr.isRegistered() && (now - tNewNodeRetry >= NEW_NODE_RETRY_INTERVAL_MS)) {
    tNewNodeRetry = now;
    if (newNodeRetries < NEW_NODE_MAX_RETRIES) {
      cmdMgr.sendNewNode();
      newNodeRetries++;
    }
  }

  // 10. Background NVS Persistence Flush (Every 60s)
  if (now - tNvsFlush >= NVS_SAVE_INTERVAL_MS) {
    tNvsFlush = now;
    storageMgr.savePoleStateToNVS();
  }
}
