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
#include "TelegramMenus.h"
#include "TelegramManager.h"
#include "HeartbeatManager.h"
#include "StorageManager.h"
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
  meshMgr.init();
  feederMgr.init();
  storageMgr.init();
  resetMgr.init();
  heartbeatMgr.init();
  loraMgr.init();
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
}

// ─────────────────────────────────────────────────────────────
//  MAIN LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // 1. High Priority: LoRa Radio Reception (Never blocked)
  loraMgr.processIncomingLoRa();

  // 2. Outgoing Command Queue Processing (Every 50ms)
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

  // 5. WiFi & Telegram Watchdog
  tgMgr.checkWiFiAndTelegram();

  // 6. Node Health & Timeout Transitions (Every 10s)
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
}
