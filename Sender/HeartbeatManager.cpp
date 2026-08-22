/*
 * =====================================================================
 *  HEARTBEATMANAGER.CPP — GATEWAY HEARTBEAT & TIMEOUT MONITOR
 * =====================================================================
 */

#include "HeartbeatManager.h"
#include "MeshManager.h"
#include "TelegramManager.h"
#include "StorageManager.h"

HeartbeatManager heartbeatMgr;

HeartbeatManager::HeartbeatManager() {}

void HeartbeatManager::init() {}

void HeartbeatManager::checkNodeTimeouts() {
  unsigned long now = millis();
  int count = meshMgr.getNodeCount();

  for (int i = 0; i < count; i++) {
    NodeRecord *node = meshMgr.getNode(i);
    if (!node) continue;

    // Ignore nodes not yet heard from in this session
    if (node->lastSeen == 0) continue;

    unsigned long elapsed = now - node->lastSeen;

    if (node->onlineState != "OFFLINE" && elapsed >= NODE_OFFLINE_TIMEOUT_MS) {
      node->onlineState = "OFFLINE";
      for (int f = 0; f < MAX_FEEDERS; f++) {
        node->feeders[f].onlineState = "OFFLINE";
        node->feeders[f].loraState = "OFFLINE";
      }

      Serial.printf("[GW][HB] 🔴 Node %s went OFFLINE (last seen %lu ms ago)\n",
                    node->logicalId.c_str(), elapsed);
      
      // Send Telegram alert only on active transition
      if (tgMgr.isTelegramOnline() && node->registered) {
        String typeLabel = (node->type == "FARMER") ? "🌾 *FARMER STATION OFFLINE*" : "🔴 *NODE OFFLINE ALERT*";
        String alert = typeLabel + "\n\n"
                       "📍 *Name:* *" + node->name + "* (`" + node->logicalId + "`)\n"
                       "⚠️ *Status:* No LoRa communication for 3 minutes.\n"
                       "🕒 *Time:* `" + tgMgr.getFormattedTime() + "`";
        tgMgr.sendMessage(OWNER_CHAT, alert, "Markdown");
      }
    } 
    else if (node->onlineState == "ONLINE" && elapsed >= (HEARTBEAT_INTERVAL_MS * 3)) {
      node->onlineState = "DEGRADED";
      for (int f = 0; f < MAX_FEEDERS; f++) {
        if (node->feeders[f].onlineState == "ONLINE") {
          node->feeders[f].onlineState = "DEGRADED";
          node->feeders[f].loraState = "DEGRADED";
        }
      }
      Serial.printf("[GW][HB] 🟡 Node %s DEGRADED (last seen %lu ms ago)\n",
                    node->logicalId.c_str(), elapsed);
    }
  }
}

void HeartbeatManager::processHeartbeatCycle() {
  // Non-blocking cooperative heartbeat scheduler.
  // Poles autonomously push heartbeat packets.
}
