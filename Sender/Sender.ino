/*
 * =====================================================================
 *  SMART STREET LIGHT / FARMER MESH SYSTEM — MASTER GATEWAY FIRMWARE
 *  Version   : GW_V10
 *  Platform  : ESP32 Dev Module + SX1278 RA-02 433 MHz + WiFi + Telegram
 *  Role      : Master Central Gateway & Interactive Telegram Bot
 *  Architecture: Modular Industrial Architecture
 * =====================================================================
 */

#include <Arduino.h>
#include "config.h"
#include "Types.h"
#include "MeshManager.h"
#include "FeederManager.h"
#include "ResetManager.h"
#include "LoRaManager.h"
#include "DiscoveryManager.h"
#include "GwTelegram.h"
#include "WiFiConfigManager.h"
#include "TelegramMenuManager.h"
#include "TelegramCallbackManager.h"
#include "TelegramSessionManager.h"
#include "TelegramNotificationManager.h"
#include "HeartbeatManager.h"
#include "StorageManager.h"
#include "CentralStorage.h"
#include "GwRegistry.h"
#include "AlertManager.h"
#include "GwConfigManager.h"
#include "ConfigDistribution.h"
#include "GwFirmwareManager.h"
#include "RevocationManager.h"
#include "AuthSecurity.h"
#include <time.h>

// ─────────────────────────────────────────────────────────────
//  COOPERATIVE SCHEDULING TIMERS
// ─────────────────────────────────────────────────────────────
static unsigned long tTelegramPoll  = 0;
static unsigned long tQueueProcess  = 0;
static unsigned long tHeartbeat     = 0;
static unsigned long tNodeTimeout   = 0;
static unsigned long tNvsFlush      = 0;
static unsigned long tNtpResync     = 0;
static unsigned long tTimeSync      = 0;
static unsigned long tRootBeacon    = 0;
static unsigned long tRouteExpire   = 0;

void processTimeSyncPush() {
  time_t raw = time(nullptr);
  if (raw > 1700000000UL) {
    loraMgr.dispatchLoRaPacket("CMD", BROADCAST_ID, BROADCAST_ID, 1, loraMgr.getNextSeq(), "SET_TIME|" + String((unsigned long)raw));
  }
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=================================================="));
  Serial.printf("  SMART STREET LIGHT MASTER GATEWAY %s\n", GW_VERSION);
  Serial.println(F("=================================================="));

  // 1. Initialize Subsystems
  authSec.init();
  centralStorage.init();
  gwRegistry.init();
  gwConfigMgr.init();
  configDist.init();
  gwFwMgr.init();
  alertMgr.init();
  revocationMgr.init();
  meshMgr.init();
  feederMgr.init();
  storageMgr.init();
  resetMgr.init();
  heartbeatMgr.init();
  discoveryMgr.init();
  loraMgr.init();
  wifiCfgMgr.init();
  tgMgr.init();

  // 2. Initialize Scheduler Timestamps
  unsigned long now = millis();
  tTelegramPoll  = now;
  tQueueProcess  = now;
  tHeartbeat     = now;
  tNodeTimeout   = now;
  tNvsFlush      = now;
  tNtpResync     = now;
  tTimeSync      = now;
  tRootBeacon    = now;
  tRouteExpire   = now;
}

// ─────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // 1. High Priority: LoRa Radio Reception (Never blocked)
  loraMgr.processIncomingLoRa();

  // 2. Gateway Root Discovery & Link Quality Processing
  discoveryMgr.update();

  // 3. Central Alert Processing & Config Distribution Updates
  alertMgr.update();
  configDist.update();
  gwConfigMgr.checkTimeouts(now);

  // 4. Outgoing Command Queue Processing (Every 50ms)
  if (now - tQueueProcess >= 50) {
    tQueueProcess = now;
    loraMgr.processCommandQueue();
  }

  // 3. Batch Command & Reset Aggregation Watchers
  tgMgr.processBatchTracker();
  resetMgr.processResetTracker();

  // 4. Telegram Update Polling (Non-blocking cycle)
  if (tgMgr.isWiFiConnected() && tgMgr.isTelegramOnline()) {
    if (now - tTelegramPoll >= TG_POLL_INTERVAL) {
      tTelegramPoll = now;
      tgMgr.handleTelegramUpdates();
    }
  }

  // 5. WiFi & Telegram Watchdog & Non-blocking Config Manager
  wifiCfgMgr.update();
  wifiCfgMgr.processSerialCommands();
  tgMgr.checkWiFiAndTelegram();

  // 6. Node Health & Timeout Transitions (Every 10s)
  revocationMgr.checkTimeout();
  if (now - tNodeTimeout >= 10000) {
    tNodeTimeout = now;
    heartbeatMgr.checkNodeTimeouts();
  }

  // 7. Time Synchronization Broadcast (Every 10 min)
  if (now - tTimeSync >= TIME_PUSH_INTERVAL_MS) {
    tTimeSync = now;
    processTimeSyncPush();
  }

  // 8. Background NVS Persistence Flush (Every 60s)
  if (now - tNvsFlush >= NVS_SAVE_INTERVAL_MS) {
    tNvsFlush = now;
    storageMgr.saveDatabaseToNVS();
  }

  // 9. NTP Re-sync (Every 1 hour)
  if (now - tNtpResync >= NTP_SYNC_INTERVAL_MS) {
    tNtpResync = now;
    tgMgr.checkNtpSync();
  }

  // 10. Gateway Root DAG Beacon Broadcast (Every 30s)
  if (now - tRootBeacon >= BEACON_INTERVAL_MS) {
    tRootBeacon = now;
    loraMgr.sendRootBeacon();
  }

  // 11. Mesh Route Expiration Sweep (Every 60s)
  if (now - tRouteExpire >= 60000UL) {
    tRouteExpire = now;
    meshMgr.expireRoutes();
  }
}
