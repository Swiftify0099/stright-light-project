/*
 * =====================================================================
 *  RESETMANAGER.CPP — SAFE REBOOT & RESET ORCHESTRATION
 * =====================================================================
 */

#include "ResetManager.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "TelegramManager.h"

ResetManager resetMgr;

ResetManager::ResetManager() {
  resetTracker.active = false;
  resetTracker.finalReportSent = true;
}

void ResetManager::init() {
  resetTracker.active = false;
  resetTracker.finalReportSent = true;
}

void ResetManager::executeResetPole(const String &chatId, const String &nodeId) {
  Serial.printf("[GW][RESET] Executing single pole reset for: %s\n", nodeId.c_str());
  if (chatId.length() > 0) {
    tgMgr.sendMessage(chatId, "⏳ *Transmitting REBOOT command to* `" + nodeId + "` *over LoRa...*", "Markdown");
  }

  // Queue RESET command with HIGH priority
  loraMgr.queueCommand(chatId, nodeId, "RESET", PRIO_HIGH);
}

void ResetManager::executeResetNeighbour(const String &chatId, const String &originPoleId, const String &neighbourId) {
  Serial.printf("[GW][RESET] Executing neighbour reset for %s via %s\n", neighbourId.c_str(), originPoleId.c_str());
  if (chatId.length() > 0) {
    tgMgr.sendMessage(chatId, "⏳ *Transmitting REBOOT command to neighbour* `" + neighbourId + "` *via* `" + originPoleId + "`...", "Markdown");
  }

  // Queue RESET command to neighbour node
  loraMgr.queueCommand(chatId, neighbourId, "RESET", PRIO_HIGH);
}

void ResetManager::executeResetAll(const String &chatId) {
  int total = meshMgr.getNodeCount();
  Serial.printf("[GW][RESET] Executing RESET ALL for %d registered poles\n", total);

  resetTracker.active = true;
  resetTracker.cmdType = "RESET_ALL";
  resetTracker.chatId = chatId;
  resetTracker.startTime = millis();
  resetTracker.timeoutMs = 8000UL; // 8 second window to aggregate reboot ACKs
  resetTracker.totalExpected = total;
  resetTracker.confirmedCount = 0;
  resetTracker.failedCount = 0;
  resetTracker.unreachableCount = 0;
  resetTracker.finalReportSent = false;

  for (int i = 0; i < MAX_NODES; i++) {
    resetTracker.nodeAcked[i] = false;
  }

  if (chatId.length() > 0) {
    tgMgr.sendMessage(chatId, "⏳ *Transmitting RESET command to all reachable field poles...*", "Markdown");
  }

  // Broadcast RESET command over mesh
  loraMgr.queueCommand(chatId, "ALL", "RESET", PRIO_HIGH);
}

void ResetManager::onResetAckReceived(const String &srcNodeId, uint16_t seq) {
  if (!resetTracker.active) return;

  int idx = meshMgr.findNodeIndex(srcNodeId);
  if (idx >= 0 && !resetTracker.nodeAcked[idx]) {
    resetTracker.nodeAcked[idx] = true;
    resetTracker.confirmedCount++;
    Serial.printf("[GW][RESET] Node %s confirmed reset reboot (Seq: %u, Confirmed: %d/%d)\n",
                  srcNodeId.c_str(), seq, resetTracker.confirmedCount, resetTracker.totalExpected);
  }
}

void ResetManager::processResetTracker() {
  if (!resetTracker.active || resetTracker.finalReportSent) return;

  unsigned long elapsed = millis() - resetTracker.startTime;
  bool allReceived = (resetTracker.confirmedCount >= resetTracker.totalExpected && resetTracker.totalExpected > 0);
  bool timedOut = (elapsed >= resetTracker.timeoutMs);

  if (allReceived || timedOut) {
    resetTracker.active = false;
    resetTracker.finalReportSent = true;

    // Calculate unreachables / failed
    int unreach = 0;
    int total = meshMgr.getNodeCount();
    for (int i = 0; i < total; i++) {
      if (!resetTracker.nodeAcked[i]) {
        NodeRecord *n = meshMgr.getNode(i);
        if (n && n->onlineState == "OFFLINE") {
          unreach++;
        } else {
          resetTracker.failedCount++;
        }
      }
    }
    resetTracker.unreachableCount = unreach;

    String rep = "━━━━━━━━━━━━━━━━━━━━\n"
                 "⚠️ *RESET ALL POLES RESULT*\n"
                 "━━━━━━━━━━━━━━━━━━━━\n\n"
                 "✅ *Reset Confirmed:* `" + String(resetTracker.confirmedCount) + "`\n"
                 "❌ *Failed / No ACK:* `" + String(resetTracker.failedCount) + "`\n"
                 "⏳ *Unreachable (Offline):* `" + String(resetTracker.unreachableCount) + "`\n\n"
                 "💡 *Note:* Nodes will reboot and rejoin the mesh automatically.";

    if (resetTracker.chatId.length() > 0) {
      tgMgr.sendMessage(resetTracker.chatId, rep, "Markdown");
    }
  }
}

bool ResetManager::isResetAllInProgress() const {
  return resetTracker.active;
}
