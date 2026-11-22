/*
 * =====================================================================
 *  HEARTBEATMANAGER.CPP — GATEWAY HEARTBEAT & TIMEOUT MONITOR
 * =====================================================================
 */

#include "HeartbeatManager.h"
#include "MeshManager.h"
#include "TelegramManager.h"
#include "StorageManager.h"
#include "AlertManager.h"

HeartbeatManager heartbeatMgr;

HeartbeatManager::HeartbeatManager() {}

void HeartbeatManager::init() {}

void HeartbeatManager::checkNodeTimeouts() {
  // Delegate to Prompt 18 Intelligent Offline Detection Engine
  alertMgr.checkNodeConnectivity(millis());
}

void HeartbeatManager::processHeartbeatCycle() {
  // Non-blocking cooperative heartbeat scheduler.
  // Poles autonomously push heartbeat packets.
}
