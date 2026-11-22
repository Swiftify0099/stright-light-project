/*
 * =====================================================================
 *  TELEGRAMMENUS.CPP — TELEGRAM DASHBOARD UI & KEYBOARD BUILDERS
 *  MSCB Smart Pole Automation System — Prompt 18 Architecture
 * =====================================================================
 */

#include "TelegramMenus.h"
#include "TelegramMenuManager.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "FeederManager.h"
#include "TelegramManager.h"
#include "AlertManager.h"
#include "RevocationManager.h"
#include "ResetManager.h"
#include <WiFi.h>
#include <time.h>

TelegramMenus tgMenus;

TelegramMenus::TelegramMenus() {}

String TelegramMenus::formatUptime(unsigned long ms) {
  unsigned long sec = ms / 1000;
  int d = sec / 86400;
  int h = (sec % 86400) / 3600;
  int m = (sec % 3600) / 60;
  int s = sec % 60;
  char buf[40];
  if (d > 0) {
    snprintf(buf, sizeof(buf), "%dd %dh %dm %ds", d, h, m, s);
  } else {
    snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, s);
  }
  return String(buf);
}

String TelegramMenus::formatChannelCurrent(float val, MeasurementValidity valState, unsigned long updateTime) {
  if (valState == MEAS_SENSOR_DISCONNECTED) return "[Sensor Fault]";
  if (valState == MEAS_ADC_OFFLINE)         return "[ADC Offline]";
  if (valState == MEAS_UNAVAILABLE || valState == MEAS_CAL_INVALID) return "[Invalid]";
  if (valState == MEAS_ZERO_CURRENT || val < 0.05f) return "0.0 A (Zero)";

  unsigned long now = millis();
  char buf[28];
  if (updateTime > 0 && (now - updateTime > 180000UL)) {
    snprintf(buf, sizeof(buf), "%.1f A [Stale]", val);
  } else {
    snprintf(buf, sizeof(buf), "%.1f A", val);
  }
  return String(buf);
}

String TelegramMenus::formatChannelVoltage(float val, MeasurementValidity valState, unsigned long updateTime) {
  if (valState == MEAS_SENSOR_DISCONNECTED) return "[Sensor Fault]";
  if (valState == MEAS_ADC_OFFLINE)         return "[ADC Offline]";
  if (valState == MEAS_UNAVAILABLE || valState == MEAS_CAL_INVALID) return "[Invalid]";
  if (valState == MEAS_NO_SIGNAL || val < 20.0f) return "0.0 V (No Signal)";

  unsigned long now = millis();
  char buf[28];
  if (updateTime > 0 && (now - updateTime > 180000UL)) {
    snprintf(buf, sizeof(buf), "%.1f V [Stale]", val);
  } else {
    snprintf(buf, sizeof(buf), "%.1f V", val);
  }
  return String(buf);
}

// ─────────────────────────────────────────────────────────────
//  1. MAIN DASHBOARD & GATEWAY STATUS (PROMPT 18 SECTION 2)
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildMainDashboardMessage(const String &wifiSsid) {
  int totalPoles      = meshMgr.getNodeCount();
  int onlinePoles     = meshMgr.getOnlineNodeCount();
  int offlinePoles    = meshMgr.getOfflineNodeCount();
  int pendingReg      = meshMgr.findPendingRegIndex(""); // pending count
  int pendingCount    = 0;
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    PendingRegistration *p = meshMgr.getPendingReg(i);
    if (p && p->valid) pendingCount++;
  }
  int revokedCount    = revocationMgr.getRevokedCount();
  int orphanCount     = meshMgr.getOrphanNodeCount();

  int activeFeeders = 0;
  for (int i = 0; i < totalPoles; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n && n->onlineState == "ONLINE") {
      for (int f = 0; f < MAX_FEEDERS; f++) {
        if (n->feeders[f].status == FEEDER_STATUS_ACTIVE && n->feeders[f].relayState == "ON") {
          activeFeeders++;
        }
      }
    }
  }

  uint32_t activeFaults = alertMgr.getActiveFaultCount();

  String msg = "🌟 *MSCB SMART POLE CENTRAL DASHBOARD*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🖥 *MAIN GATEWAY STATUS*\n";
  msg += "• Gateway State: `ONLINE 🟢`\n";
  msg += "• Wi-Fi Status: `" + String(WiFi.status() == WL_CONNECTED ? "CONNECTED 📶" : "DISCONNECTED 🔴") + "` (`" + wifiSsid + "`)\n";
  msg += "• Internet Status: `" + String(WiFi.status() == WL_CONNECTED ? "ONLINE 🌐" : "OFFLINE 🔴") + "`\n";
  msg += "• Telegram API: `" + String(tgMgr.isTelegramOnline() ? "CONNECTED ✅" : "CONNECTING ⏳") + "`\n";
  msg += "• LoRa Radio: `SX1278 (433MHz SF10 BW125) 🟢`\n";
  msg += "• Firmware Version: `" + String(GW_VERSION) + "`\n";
  msg += "• Last Restart: `" + alertMgr.formatTimestamp((uint32_t)(millis() / 1000), (time(nullptr) > 1700000000UL)) + "`\n";
  msg += "• Uptime: `" + formatUptime(millis()) + "`\n\n";

  msg += "📊 *NETWORK SUMMARY (Capacity: 50 Poles / 300 Feeders)*\n";
  msg += "• Registered Poles: `" + String(totalPoles) + " / 50`\n";
  msg += "• Online Poles: `" + String(onlinePoles) + " 🟢`\n";
  msg += "• Offline / Unreachable: `" + String(offlinePoles) + " 🔴`\n";
  msg += "• Pending Registration: `" + String(pendingCount) + " 📝`\n";
  msg += "• Revoked Devices: `" + String(revokedCount) + " ⛔`\n";
  msg += "• Orphan Nodes: `" + String(orphanCount) + " ⚠️`\n";
  msg += "• Active Feeder Channels: `" + String(activeFeeders) + " / 300`\n";
  msg += "• Active Fault Count: `" + String(activeFaults) + (activeFaults > 0 ? " 🚨" : " 🟢") + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Select a monitoring section below:";
  return msg;
}

String TelegramMenus::buildMainDashboardKeyboard() {
  // 12 Navigation buttons arranged cleanly in 2 columns + Quick actions
  return "[[{\"text\":\"🖥 Gateway Status\",\"callback_data\":\"GW_STAT\"},"
          "{\"text\":\"📋 All Poles\",\"callback_data\":\"POLES_ALL\"}],"
         "[{\"text\":\"🟢 Online Poles\",\"callback_data\":\"POLES_ON\"},"
          "{\"text\":\"🔴 Offline Poles\",\"callback_data\":\"POLES_OFF\"}],"
         "[{\"text\":\"🔌 Feeder Monitor\",\"callback_data\":\"FEEDER_MON\"},"
          "{\"text\":\"⚡ Voltage Monitor\",\"callback_data\":\"VOLT_MON\"}],"
         "[{\"text\":\"📈 Current Monitor\",\"callback_data\":\"CURR_MON\"},"
          "{\"text\":\"🚨 Fault Alerts\",\"callback_data\":\"ALERTS_LIST\"}],"
         "[{\"text\":\"🗺️ Network Topology\",\"callback_data\":\"TOPOLOGY\"},"
          "{\"text\":\"📝 Registration\",\"callback_data\":\"REGLIST\"}],"
         "[{\"text\":\"🗑️ Delete Mgmt\",\"callback_data\":\"DEL_MGMT\"},"
          "{\"text\":\"📜 Event History\",\"callback_data\":\"EVT_HIST:0\"}],"
         "[{\"text\":\"💡 ALL LIGHTS ON\",\"callback_data\":\"ALL_ON\"},"
          "{\"text\":\"🌙 ALL LIGHTS OFF\",\"callback_data\":\"ALL_OFF\"}],"
         "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"REFRESH\"}]]";
}

String TelegramMenus::buildGatewayStatusMessage(const String &wifiSsid) {
  String lastPkt = loraMgr.getLastPacketInfo();

  String msg = "🖥 *GATEWAY STATUS & HEALTH*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "• Gateway ID: `" + String(GATEWAY_ID) + "`\n";
  msg += "• Status: `ONLINE 🟢`\n";
  msg += "• Firmware: `" + String(GW_VERSION) + "`\n";
  msg += "• Hardware: `ESP32-S3 + Semtech SX1278 (VSPI)`\n";
  msg += "• LoRa Frequency: `433.0 MHz | SF10 | BW 125kHz | CR 4/5`\n";
  msg += "• Wi-Fi SSID: `" + wifiSsid + "`\n";
  msg += "• Gateway IP: `" + WiFi.localIP().toString() + "`\n";
  msg += "• Wi-Fi RSSI: `" + String(WiFi.RSSI()) + " dBm`\n";
  msg += "• Telegram API: `CONNECTED (Bot Token Active)`\n";
  msg += "• Free Heap: `" + String(ESP.getFreeHeap()) + " bytes`\n";
  msg += "• Uptime: `" + formatUptime(millis()) + "`\n";
  msg += "• System Time: `" + tgMgr.getFormattedTime() + "`\n";
  msg += "• Last LoRa Frame: `" + lastPkt + "`\n";
  msg += "• Alert Queue Depth: `" + String(alertMgr.getQueueCount()) + " / " + String(ALERT_QUEUE_CAPACITY) + "`\n";
  msg += "• Total Alerts Dispatched: `" + String(alertMgr.getTotalAlertsSent()) + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenus::buildGatewayStatusKeyboard() {
  return "[[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"GW_STAT\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

// ─────────────────────────────────────────────────────────────
//  2. POLE DASHBOARD & PAGINATION (PROMPT 18 SECTION 3)
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildAllPolesMessage(UserSession &sess, const String &filter) {
  int totalInDb = meshMgr.getNodeCount();
  String title = "📋 *ALL REGISTERED POLES*";
  if (filter == "ONLINE") title = "🟢 *ONLINE POLES*";
  else if (filter == "OFFLINE") title = "🔴 *OFFLINE / UNREACHABLE POLES*";

  String msg = title + "\n━━━━━━━━━━━━━━━━━━━━\n";

  if (totalInDb == 0) {
    msg += "No registered Pole nodes found.\nAwaiting provisioning.\n";
    return msg;
  }

  // Count filtered nodes
  int matchIndices[MAX_NODES];
  int matchCount = 0;
  for (int i = 0; i < totalInDb; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;
    if (filter == "ONLINE" && n->onlineState != "ONLINE") continue;
    if (filter == "OFFLINE" && n->onlineState == "ONLINE") continue;
    matchIndices[matchCount++] = i;
  }

  if (matchCount == 0) {
    msg += "No poles matching filter `" + filter + "`.\n";
    return msg;
  }

  int totalPages = ((matchCount - 1) / POLES_PER_PAGE) + 1;
  if (sess.pageIndex >= totalPages) sess.pageIndex = totalPages - 1;
  if (sess.pageIndex < 0) sess.pageIndex = 0;

  int start = sess.pageIndex * POLES_PER_PAGE;
  int end = min(start + POLES_PER_PAGE, matchCount);

  unsigned long now = millis();

  for (int idx = start; idx < end; idx++) {
    NodeRecord *n = meshMgr.getNode(matchIndices[idx]);
    if (!n) continue;

    String icon = (n->onlineState == "ONLINE") ? "🟢" : ((n->onlineState == "DEGRADED") ? "🟡" : "🔴");
    String conn = nodeConnectivityStateToString(n->connectivityDetail);

    // Feeder availability: count enabled feeders
    int activeFdr = 0;
    for (int f = 0; f < MAX_FEEDERS; f++) {
      if (n->feeders[f].status == FEEDER_STATUS_ACTIVE) activeFdr++;
    }

    // Last seen formatting
    String lastSeenStr = "Never";
    if (n->lastSeen > 0) {
      unsigned long secAgo = (now - n->lastSeen) / 1000;
      lastSeenStr = String(secAgo) + "s ago";
    }

    String priP = (n->primaryParent.length() > 0) ? n->primaryParent : "GATEWAY";
    String bkP  = (n->backupParent.length() > 0) ? n->backupParent : "NONE";
    String areaStr = (strlen(n->area) > 0) ? String(n->area) : ((n->location.length() > 0) ? n->location : "Main Sector");
    String rHealth = (n->routeHealth.length() > 0) ? n->routeHealth : "GOOD";

    msg += icon + " *Pole ID:* `" + n->logicalId + "`\n";
    msg += "  • Name: *" + n->name + "*\n";
    msg += "  • Area: `" + areaStr + "`\n";
    msg += "  • Reg Status: `" + String(n->registered ? "APPROVED / ACTIVE" : "UNAPPROVED") + "`\n";
    msg += "  • Conn: `" + conn + "` | Last Seen: `" + lastSeenStr + "`\n";
    msg += "  • Primary Parent: `" + priP + "` | Backup: `" + bkP + "`\n";
    msg += "  • Hops: `" + String(n->hopCount) + "` | Route Health: `" + rHealth + "`\n";
    msg += "  • RF Link: `" + String(n->rssi) + " dBm` (SNR: `" + String(n->snr) + " dB`)\n";
    msg += "  • Firmware: `POLE_V10 (v" + String(n->firmwareVersion > 0 ? n->firmwareVersion : 10) + ")`\n";
    msg += "  • Feeder Availability: `" + String(activeFdr) + "/6 Active`\n\n";
  }

  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Page " + String(sess.pageIndex + 1) + " of " + String(totalPages) + " (Total: " + String(matchCount) + " Poles)";
  return msg;
}

String TelegramMenus::buildAllPolesKeyboard(UserSession &sess, const String &filter) {
  int totalInDb = meshMgr.getNodeCount();
  int matchIndices[MAX_NODES];
  int matchCount = 0;
  for (int i = 0; i < totalInDb; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;
    if (filter == "ONLINE" && n->onlineState != "ONLINE") continue;
    if (filter == "OFFLINE" && n->onlineState == "ONLINE") continue;
    matchIndices[matchCount++] = i;
  }

  int totalPages = (matchCount > 0) ? (((matchCount - 1) / POLES_PER_PAGE) + 1) : 1;
  int start = sess.pageIndex * POLES_PER_PAGE;
  int end = min(start + POLES_PER_PAGE, matchCount);

  String kb = "[";

  // Individual Pole Select Buttons (using STABLE logicalId callback: P:MSCB-P001)
  for (int idx = start; idx < end; idx++) {
    NodeRecord *n = meshMgr.getNode(matchIndices[idx]);
    if (n) {
      String icon = (n->onlineState == "ONLINE") ? "🟢" : "🔴";
      kb += "[{\"text\":\"" + icon + " " + n->name + " (" + n->logicalId + ")\",\"callback_data\":\"P:" + n->logicalId + "\"}],";
    }
  }

  // Pagination navigation row
  String navRow = "[";
  if (sess.pageIndex > 0) {
    navRow += "{\"text\":\"◀️ Prev\",\"callback_data\":\"PG:P:" + String(sess.pageIndex) + ":" + filter + "\"},";
  }
  if (end < matchCount) {
    navRow += "{\"text\":\"Next ▶️\",\"callback_data\":\"PG:N:" + String(sess.pageIndex) + ":" + filter + "\"},";
  }
  navRow += "{\"text\":\"🔄 Refresh\",\"callback_data\":\"POLES_" + filter + "\"}";
  navRow += "]";
  kb += navRow + ",";

  // Filter selection row
  kb += "[{\"text\":\"📋 All Poles\",\"callback_data\":\"POLES_ALL\"},"
        "{\"text\":\"🟢 Online\",\"callback_data\":\"POLES_ON\"},"
        "{\"text\":\"🔴 Offline\",\"callback_data\":\"POLES_OFF\"}],"
        "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"HOME\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";

  return kb;
}

String TelegramMenus::buildPoleDetailMessage(UserSession &sess) {
  NodeRecord *n = meshMgr.findNode(sess.selectedPole);
  if (!n) return "❌ Pole `" + sess.selectedPole + "` not found.";

  String icon = (n->onlineState == "ONLINE") ? "🟢" : ((n->onlineState == "DEGRADED") ? "🟡" : "🔴");
  String priP = (n->primaryParent.length() > 0) ? n->primaryParent : "GATEWAY";
  String bkP  = (n->backupParent.length() > 0) ? n->backupParent : "NONE";
  String areaStr = (strlen(n->area) > 0) ? String(n->area) : ((n->location.length() > 0) ? n->location : "Main Sector");

  String msg = icon + " *POLE DASHBOARD: " + n->name + "*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "• Pole ID: `" + n->logicalId + "`\n";
  msg += "• Hardware ID: `" + n->hwId + "`\n";
  msg += "• Area: `" + areaStr + "`\n";
  msg += "• Registration: `" + String(n->registered ? "APPROVED / ACTIVE" : "PENDING") + "`\n";
  msg += "• Connectivity: `" + String(nodeConnectivityStateToString(n->connectivityDetail)) + "`\n";
  msg += "• Primary Parent: `" + priP + "`\n";
  msg += "• Backup Parent: `" + bkP + "`\n";
  msg += "• Mesh Distance: `" + String(n->hopCount) + " hops` (Cost: `" + String(n->routeCost) + "`)\n";
  msg += "• RF Signal: `" + String(n->rssi) + " dBm` | SNR: `" + String(n->snr) + " dB`\n";
  msg += "• Route Health: `" + String(n->routeHealth.length() > 0 ? n->routeHealth : "GOOD") + "`\n";
  msg += "• Firmware: `POLE_V10`\n";
  msg += "• Active Fault: `" + String((n->fault.length() > 0 && n->fault != "OK") ? n->fault : "NONE (Normal)") + "`\n\n";

  msg += "⚡ *COMMON BUS VOLTAGE (3-Phase Mains):*\n";
  msg += "• Phase R: `" + formatChannelVoltage(n->commonVoltageR, n->commonVoltageValidityR, n->voltageUpdatedAt) + "`\n";
  msg += "• Phase Y: `" + formatChannelVoltage(n->commonVoltageY, n->commonVoltageValidityY, n->voltageUpdatedAt) + "`\n";
  msg += "• Phase B: `" + formatChannelVoltage(n->commonVoltageB, n->commonVoltageValidityB, n->voltageUpdatedAt) + "`\n";
  msg += "• Bus Frequency: `" + String(n->gridFrequency, 2) + " Hz`\n\n";

  msg += "🔌 *FEEDER OVERVIEW (F1–F6):*\n";
  for (int f = 0; f < MAX_FEEDERS; f++) {
    FeederState &fdr = n->feeders[f];
    String fIcon = (fdr.status == FEEDER_STATUS_DEACTIVATED) ? "⚪" : ((fdr.relayState == "ON") ? "💡" : "🌙");
    msg += "• F" + String(f + 1) + ": " + fIcon + " " + fdr.relayState + " | MCB: `" + (fdr.mcbStatus ? "OK" : "TRIP") +
           "` | IR: `" + formatChannelCurrent(fdr.currentR, fdr.currentValidityR, fdr.lastMeasurementTime) + "`\n";
  }
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenus::buildPoleDetailKeyboard(UserSession &sess) {
  String pid = sess.selectedPole;
  return "[[{\"text\":\"🔌 6 Feeders\",\"callback_data\":\"FDR_P:" + pid + "\"},"
          "{\"text\":\"⚡ Common Voltages\",\"callback_data\":\"V_P:" + pid + "\"}],"
         "[{\"text\":\"📈 18-Ch Currents\",\"callback_data\":\"C_P:" + pid + "\"},"
          "{\"text\":\"⏱ Timer Control\",\"callback_data\":\"TMR_SEL:" + pid + "\"}],"
         "[{\"text\":\"💡 LIGHT ON\",\"callback_data\":\"PON:" + pid + "\"},"
          "{\"text\":\"🌙 LIGHT OFF\",\"callback_data\":\"POFF:" + pid + "\"}],"
         "[{\"text\":\"🗺️ Network Route\",\"callback_data\":\"TOPOLOGY\"},"
          "{\"text\":\"🔁 Reset Node\",\"callback_data\":\"RST_P:" + pid + "\"}],"
         "[{\"text\":\"⬅️ ALL POLES\",\"callback_data\":\"POLES_ALL\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

// ─────────────────────────────────────────────────────────────
//  3. FEEDER DASHBOARD — 6 FEEDERS PER POLE (PROMPT 18 SECTION 4)
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildFeederDashboardMessage(UserSession &sess) {
  NodeRecord *targetNode = nullptr;
  if (sess.selectedPole.length() > 0) {
    targetNode = meshMgr.findNode(sess.selectedPole);
  }
  if (!targetNode && meshMgr.getNodeCount() > 0) {
    targetNode = meshMgr.getNode(0);
    if (targetNode) sess.selectedPole = targetNode->logicalId;
  }

  if (!targetNode) {
    return "⚠️ *No registered field nodes available.*\nAwaiting node provisioning.";
  }

  String msg = "🔌 *FEEDER MONITORING (6 FEEDERS / POLE)*\n";
  msg += "📍 *Pole:* *" + targetNode->name + "* (`" + targetNode->logicalId + "`)\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";

  for (int f = 0; f < MAX_FEEDERS; f++) {
    FeederState &fdr = targetNode->feeders[f];
    String fHealth = (fdr.status == FEEDER_STATUS_DEACTIVATED) ? "DEACTIVATED ⚪" :
                     ((!fdr.mcbStatus) ? "MCB TRIPPED 🔴" :
                      ((fdr.faultState == "OVER_CURR") ? "OVERCURRENT 🔴" :
                       ((fdr.faultState == "SENSOR_ERROR") ? "SENSOR FAULT 🔴" : "NORMAL 🟢")));

    String contactorStr = fdr.contactorFeedback ? "CLOSED" : "OPEN";
    String mcbStr       = fdr.mcbStatus ? "NORMAL" : "TRIPPED";
    String rlyStr       = (fdr.relayState == "ON") ? "ON 🟢" : "OFF 🌙";

    // Measurement time
    String measTime = "Stale";
    if (fdr.lastMeasurementTime > 0) {
      unsigned long secAgo = (millis() - fdr.lastMeasurementTime) / 1000;
      measTime = String(secAgo) + "s ago";
    }

    msg += "🔹 *Feeder " + String(f + 1) + ": " + fdr.name + "*\n";
    msg += "  • Relay Command: `" + rlyStr + "` | Contactor: `" + contactorStr + "`\n";
    msg += "  • MCB Status: `" + mcbStr + "` | Feeder Health: `" + fHealth + "`\n";
    msg += "  • Measurement Time: `" + measTime + "`\n";
    msg += "  • R Current: `" + formatChannelCurrent(fdr.currentR, fdr.currentValidityR, fdr.lastMeasurementTime) + "`\n";
    msg += "  • Y Current: `" + formatChannelCurrent(fdr.currentY, fdr.currentValidityY, fdr.lastMeasurementTime) + "`\n";
    msg += "  • B Current: `" + formatChannelCurrent(fdr.currentB, fdr.currentValidityB, fdr.lastMeasurementTime) + "`\n";
    msg += "  • Active Power: `" + String(fdr.totalPowerW / 1000.0f, 2) + " kW` | PF: `" + String(fdr.powerFactor, 2) + "`\n\n";
  }

  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "⚠️ Note: Current channels are individually measured (3 independent phases).";
  return msg;
}

String TelegramMenus::buildFeederDashboardKeyboard(UserSession &sess) {
  int totalPoles = meshMgr.getNodeCount();
  int currentIdx = meshMgr.findNodeIndex(sess.selectedPole);
  if (currentIdx < 0) currentIdx = 0;

  int prevIdx = (currentIdx - 1 + totalPoles) % max(1, totalPoles);
  int nextIdx = (currentIdx + 1) % max(1, totalPoles);

  String prevId = (meshMgr.getNode(prevIdx)) ? meshMgr.getNode(prevIdx)->logicalId : "GATEWAY";
  String nextId = (meshMgr.getNode(nextIdx)) ? meshMgr.getNode(nextIdx)->logicalId : "GATEWAY";

  return "[[{\"text\":\"◀️ Prev Pole\",\"callback_data\":\"FDR_SW:" + prevId + "\"},"
          "{\"text\":\"Next Pole ▶️\",\"callback_data\":\"FDR_SW:" + nextId + "\"}],"
         "[{\"text\":\"⚡ Common Bus Voltages\",\"callback_data\":\"V_P:" + sess.selectedPole + "\"},"
          "{\"text\":\"📈 18-Ch Currents\",\"callback_data\":\"C_P:" + sess.selectedPole + "\"}],"
         "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"FEEDER_MON\"},"
          "{\"text\":\"📋 ALL POLES\",\"callback_data\":\"POLES_ALL\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

// ─────────────────────────────────────────────────────────────
//  4. VOLTAGE DASHBOARD — COMMON 3-PHASE VOLTAGES (PROMPT 18 SEC 5)
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildVoltageDashboardMessage(UserSession &sess) {
  NodeRecord *targetNode = nullptr;
  if (sess.selectedPole.length() > 0) {
    targetNode = meshMgr.findNode(sess.selectedPole);
  }
  if (!targetNode && meshMgr.getNodeCount() > 0) {
    targetNode = meshMgr.getNode(0);
    if (targetNode) sess.selectedPole = targetNode->logicalId;
  }

  String msg = "⚡ *COMMON POLE VOLTAGE MONITORING*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "ℹ️ *NOTE: Measurements are Common Pole Bus Voltages (R/Y/B), NOT independent Feeder Output voltages.*\n\n";

  if (!targetNode) {
    msg += "⚠️ *No registered field nodes available.*";
    return msg;
  }

  msg += "📍 *Selected Pole:* *" + targetNode->name + "* (`" + targetNode->logicalId + "`)\n";
  msg += "• R Phase Voltage: `" + formatChannelVoltage(targetNode->commonVoltageR, targetNode->commonVoltageValidityR, targetNode->voltageUpdatedAt) + "`\n";
  msg += "• Y Phase Voltage: `" + formatChannelVoltage(targetNode->commonVoltageY, targetNode->commonVoltageValidityY, targetNode->voltageUpdatedAt) + "`\n";
  msg += "• B Phase Voltage: `" + formatChannelVoltage(targetNode->commonVoltageB, targetNode->commonVoltageValidityB, targetNode->voltageUpdatedAt) + "`\n";
  msg += "• Grid Frequency: `" + String(targetNode->gridFrequency, 2) + " Hz`\n\n";

  // Phase comparison & symmetry
  float maxV = max(targetNode->commonVoltageR, max(targetNode->commonVoltageY, targetNode->commonVoltageB));
  float minV = min(targetNode->commonVoltageR, min(targetNode->commonVoltageY, targetNode->commonVoltageB));
  float unbalance = (maxV > 0) ? (((maxV - minV) / maxV) * 100.0f) : 0.0f;

  msg += "⚖️ *Phase Symmetry & Comparison:*\n";
  msg += "• Max Deviation: `" + String(maxV - minV, 1) + " V` (" + String(unbalance, 1) + "% unbalance)\n";
  msg += "• Symmetry Status: `" + String(unbalance < 5.0f ? "BALANCED 🟢" : (unbalance < 15.0f ? "ACCEPTABLE 🟡" : "HIGH UNBALANCE 🔴")) + "`\n\n";

  // System Over/Under voltage alerts
  int underCount = 0;
  int overCount  = 0;
  int totalP = meshMgr.getNodeCount();
  for (int i = 0; i < totalP; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n && n->onlineState == "ONLINE") {
      if (n->commonVoltageR < 180.0f || n->commonVoltageY < 180.0f || n->commonVoltageB < 180.0f) underCount++;
      if (n->commonVoltageR > 260.0f || n->commonVoltageY > 260.0f || n->commonVoltageB > 260.0f) overCount++;
    }
  }

  msg += "📊 *Network Voltage Alerts Summary:*\n";
  msg += "• Under-Voltage Poles (< 180V): `" + String(underCount) + (underCount > 0 ? " ⚠️" : " 🟢") + "`\n";
  msg += "• Over-Voltage Poles (> 260V): `" + String(overCount) + (overCount > 0 ? " ⚠️" : " 🟢") + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";

  return msg;
}

String TelegramMenus::buildVoltageDashboardKeyboard(UserSession &sess) {
  String pid = sess.selectedPole;
  return "[[{\"text\":\"⚡ All Poles Voltage Summary\",\"callback_data\":\"V_ALL:0\"}],"
         "[{\"text\":\"📈 Network Voltage Trend\",\"callback_data\":\"V_TREND\"},"
          "{\"text\":\"⚖️ Phase Comparison\",\"callback_data\":\"V_COMPARE\"}],"
         "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"VOLT_MON\"},"
          "{\"text\":\"📋 ALL POLES\",\"callback_data\":\"POLES_ALL\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildVoltageSummaryAllMessage(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  String msg = "⚡ *ALL POLES VOLTAGE SUMMARY (150 CHANNELS)*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "ℹ️ *Common Pole Bus Voltages (R/Y/B)*\n\n";

  if (total == 0) {
    msg += "No registered poles.\n";
    return msg;
  }

  int totalPages = ((total - 1) / POLES_PER_PAGE) + 1;
  int start = sess.pageIndex * POLES_PER_PAGE;
  int end = min(start + POLES_PER_PAGE, total);

  msg += "```\n";
  msg += "Pole ID    | R(V)  | Y(V)  | B(V)  | Freq \n";
  msg += "-----------+-------+-------+-------+------\n";

  for (int i = start; i < end; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;

    char row[64];
    snprintf(row, sizeof(row), "%-10s | %5.1f | %5.1f | %5.1f | %4.1f\n",
             n->logicalId.c_str(),
             n->commonVoltageR, n->commonVoltageY, n->commonVoltageB,
             n->gridFrequency);
    msg += String(row);
  }
  msg += "```\n";
  msg += "Page " + String(sess.pageIndex + 1) + " of " + String(totalPages) + " (50 Poles Capacity)";
  return msg;
}

String TelegramMenus::buildVoltageSummaryAllKeyboard(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  int totalPages = (total > 0) ? (((total - 1) / POLES_PER_PAGE) + 1) : 1;
  int end = min((sess.pageIndex + 1) * POLES_PER_PAGE, total);

  String nav = "[";
  if (sess.pageIndex > 0) {
    nav += "{\"text\":\"◀️ Prev\",\"callback_data\":\"V_PG:" + String(sess.pageIndex - 1) + "\"},";
  }
  if (end < total) {
    nav += "{\"text\":\"Next ▶️\",\"callback_data\":\"V_PG:" + String(sess.pageIndex + 1) + "\"},";
  }
  nav += "{\"text\":\"🔄 Refresh\",\"callback_data\":\"V_ALL:" + String(sess.pageIndex) + "\"}";
  nav += "]";

  return "[" + nav + ","
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"VOLT_MON\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildVoltageTrendMessage(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  float minR = 999, maxR = 0, sumR = 0;
  float minY = 999, maxY = 0, sumY = 0;
  float minB = 999, maxB = 0, sumB = 0;
  int activeCount = 0;

  for (int i = 0; i < total; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n && n->onlineState == "ONLINE" && n->commonVoltageR > 20.0f) {
      activeCount++;
      if (n->commonVoltageR < minR) minR = n->commonVoltageR;
      if (n->commonVoltageR > maxR) maxR = n->commonVoltageR;
      sumR += n->commonVoltageR;

      if (n->commonVoltageY < minY) minY = n->commonVoltageY;
      if (n->commonVoltageY > maxY) maxY = n->commonVoltageY;
      sumY += n->commonVoltageY;

      if (n->commonVoltageB < minB) minB = n->commonVoltageB;
      if (n->commonVoltageB > maxB) maxB = n->commonVoltageB;
      sumB += n->commonVoltageB;
    }
  }

  if (activeCount == 0) {
    minR = minY = minB = 0;
  }

  float avgR = (activeCount > 0) ? (sumR / activeCount) : 0;
  float avgY = (activeCount > 0) ? (sumY / activeCount) : 0;
  float avgB = (activeCount > 0) ? (sumB / activeCount) : 0;

  String msg = "📈 *MSCB NETWORK VOLTAGE TREND*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Active Reporting Poles: `" + String(activeCount) + " / " + String(total) + "`\n\n";

  msg += "🔴 *Phase R Voltage Statistics:*\n";
  msg += "• Min: `" + String(minR, 1) + " V` | Max: `" + String(maxR, 1) + " V`\n";
  msg += "• Mean (Average): `" + String(avgR, 1) + " V` (Nominal: 230V)\n\n";

  msg += "🟡 *Phase Y Voltage Statistics:*\n";
  msg += "• Min: `" + String(minY, 1) + " V` | Max: `" + String(maxY, 1) + " V`\n";
  msg += "• Mean (Average): `" + String(avgY, 1) + " V` (Nominal: 230V)\n\n";

  msg += "🔵 *Phase B Voltage Statistics:*\n";
  msg += "• Min: `" + String(minB, 1) + " V` | Max: `" + String(maxB, 1) + " V`\n";
  msg += "• Mean (Average): `" + String(avgB, 1) + " V` (Nominal: 230V)\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";

  return msg;
}

String TelegramMenus::buildPhaseComparisonMessage(UserSession &sess) {
  NodeRecord *n = meshMgr.findNode(sess.selectedPole);
  if (!n && meshMgr.getNodeCount() > 0) n = meshMgr.getNode(0);

  if (!n) return "❌ No node available for phase comparison.";

  float vr = n->commonVoltageR;
  float vy = n->commonVoltageY;
  float vb = n->commonVoltageB;

  float diffRY = fabs(vr - vy);
  float diffYB = fabs(vy - vb);
  float diffBR = fabs(vb - vr);

  float maxV = max(vr, max(vy, vb));
  float unbalance = (maxV > 0) ? ((max(diffRY, max(diffYB, diffBR)) / maxV) * 100.0f) : 0.0f;

  String msg = "⚖️ *THREE-PHASE BUS COMPARISON*\n";
  msg += "📍 *Target:* *" + n->name + "* (`" + n->logicalId + "`)\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "• Phase R Voltage: `" + String(vr, 1) + " V`\n";
  msg += "• Phase Y Voltage: `" + String(vy, 1) + " V`\n";
  msg += "• Phase B Voltage: `" + String(vb, 1) + " V`\n\n";

  msg += "🔹 *Phase-to-Phase Differences:*\n";
  msg += "• |R - Y|: `" + String(diffRY, 1) + " V`\n";
  msg += "• |Y - B|: `" + String(diffYB, 1) + " V`\n";
  msg += "• |B - R|: `" + String(diffBR, 1) + " V`\n\n";

  msg += "🔹 *Balance Evaluation:*\n";
  msg += "• Max Phase Deviation: `" + String(unbalance, 1) + "%`\n";
  msg += "• Status: `" + String(unbalance < 5.0f ? "IDEAL BALANCE 🟢" : (unbalance < 15.0f ? "MODERATE UNBALANCE 🟡" : "CRITICAL UNBALANCE 🔴")) + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";

  return msg;
}

// ─────────────────────────────────────────────────────────────
//  5. CURRENT DASHBOARD — 300 FEEDERS / 900 CHANNELS (SEC 6)
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildCurrentDashboardMessage(UserSession &sess) {
  String mode = sess.currentMode.length() > 0 ? sess.currentMode : "A";
  String msg = "";

  if (mode == "A") {
    // Mode A: Individual Pole Current Summary (6 Feeders x 3 Phases = 18 Channels)
    NodeRecord *n = meshMgr.findNode(sess.selectedPole);
    if (!n && meshMgr.getNodeCount() > 0) {
      n = meshMgr.getNode(0);
      if (n) sess.selectedPole = n->logicalId;
    }
    if (!n) return "❌ No pole available.";

    msg = "📈 *CURRENT SUMMARY — 18 CHANNELS*\n";
    msg += "📍 *Pole:* *" + n->name + "* (`" + n->logicalId + "`)\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "```\n";
    msg += "Fdr | R(A)  | Y(A)  | B(A)  | Power\n";
    msg += "----+-------+-------+-------+------\n";

    for (int f = 0; f < MAX_FEEDERS; f++) {
      FeederState &fdr = n->feeders[f];
      char row[64];
      snprintf(row, sizeof(row), " F%d | %5.1f | %5.1f | %5.1f | %4.1fkW\n",
               f + 1, fdr.currentR, fdr.currentY, fdr.currentB,
               fdr.totalPowerW / 1000.0f);
      msg += String(row);
    }
    msg += "```\n";
    msg += "ℹ️ Distinct Phase Currents: 3 independent CT channels per feeder.";
  }
  else if (mode == "B") {
    // Mode B: Individual Feeder Current Detail
    NodeRecord *n = meshMgr.findNode(sess.selectedPole);
    if (!n && meshMgr.getNodeCount() > 0) n = meshMgr.getNode(0);
    if (!n) return "❌ No pole available.";

    uint8_t fNum = sess.selectedFeeder > 0 ? sess.selectedFeeder : 1;
    FeederState &fdr = n->feeders[fNum - 1];

    msg = "🔌 *FEEDER " + String(fNum) + " CURRENT DETAIL*\n";
    msg += "📍 *Pole:* *" + n->name + "* (`" + n->logicalId + "`)\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
    msg += "• Feeder Name: *" + fdr.name + "*\n";
    msg += "• R Phase Current: `" + formatChannelCurrent(fdr.currentR, fdr.currentValidityR, fdr.lastMeasurementTime) + "`\n";
    msg += "• Y Phase Current: `" + formatChannelCurrent(fdr.currentY, fdr.currentValidityY, fdr.lastMeasurementTime) + "`\n";
    msg += "• B Phase Current: `" + formatChannelCurrent(fdr.currentB, fdr.currentValidityB, fdr.lastMeasurementTime) + "`\n";
    msg += "• Active Power: `" + String(fdr.totalPowerW / 1000.0f, 2) + " kW`\n";
    msg += "• Power Factor: `" + String(fdr.powerFactor, 2) + "`\n";
    msg += "• MCB Auxiliary: `" + String(fdr.mcbStatus ? "NORMAL (Closed)" : "TRIPPED (Open)") + "`\n";
    msg += "• Contactor: `" + String(fdr.contactorFeedback ? "CLOSED" : "OPEN") + "`\n";
    msg += "• Relay State: `" + fdr.relayState + "`\n";
    msg += "• Fault State: `" + fdr.faultState + "`\n";
    msg += "━━━━━━━━━━━━━━━━━━━━";
  }
  else if (mode == "C" || mode == "D" || mode == "E") {
    // Modes C, D, E: Phase R, Y, B Current Summary across all nodes
    char targetPhase = (mode == "C") ? 'R' : ((mode == "D") ? 'Y' : 'B');
    int totalPoles = meshMgr.getNodeCount();
    int totalPages = ((totalPoles - 1) / POLES_PER_PAGE) + 1;
    int start = sess.pageIndex * POLES_PER_PAGE;
    int end = min(start + POLES_PER_PAGE, totalPoles);

    msg = "📊 *PHASE " + String(targetPhase) + " CURRENT SUMMARY*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "```\n";
    msg += "Pole ID    | F1   | F2   | F3   | F4   | F5   | F6  \n";
    msg += "-----------+------+------+------+------+------+-----\n";

    for (int i = start; i < end; i++) {
      NodeRecord *n = meshMgr.getNode(i);
      if (!n) continue;

      float i1 = (mode == "C") ? n->feeders[0].currentR : ((mode == "D") ? n->feeders[0].currentY : n->feeders[0].currentB);
      float i2 = (mode == "C") ? n->feeders[1].currentR : ((mode == "D") ? n->feeders[1].currentY : n->feeders[1].currentB);
      float i3 = (mode == "C") ? n->feeders[2].currentR : ((mode == "D") ? n->feeders[2].currentY : n->feeders[2].currentB);
      float i4 = (mode == "C") ? n->feeders[3].currentR : ((mode == "D") ? n->feeders[3].currentY : n->feeders[3].currentB);
      float i5 = (mode == "C") ? n->feeders[4].currentR : ((mode == "D") ? n->feeders[4].currentY : n->feeders[4].currentB);
      float i6 = (mode == "C") ? n->feeders[5].currentR : ((mode == "D") ? n->feeders[5].currentY : n->feeders[5].currentB);

      char row[70];
      snprintf(row, sizeof(row), "%-10s | %4.1f | %4.1f | %4.1f | %4.1f | %4.1f | %4.1f\n",
               n->logicalId.c_str(), i1, i2, i3, i4, i5, i6);
      msg += String(row);
    }
    msg += "```\n";
    msg += "Page " + String(sess.pageIndex + 1) + " of " + String(totalPages);
  }
  else if (mode == "F") {
    // Mode F: Six-Feeder Comparison
    NodeRecord *n = meshMgr.findNode(sess.selectedPole);
    if (!n && meshMgr.getNodeCount() > 0) n = meshMgr.getNode(0);
    if (!n) return "❌ No pole available.";

    msg = "⚖️ *SIX-FEEDER COMPARISON TABLE*\n";
    msg += "📍 *Pole:* *" + n->name + "* (`" + n->logicalId + "`)\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "```\n";
    msg += "Feeder | State | R(A)  | Y(A)  | B(A)  | Status \n";
    msg += "-------+-------+-------+-------+-------+--------\n";

    for (int f = 0; f < MAX_FEEDERS; f++) {
      FeederState &fdr = n->feeders[f];
      char row[64];
      snprintf(row, sizeof(row), " F%-5d | %-5s | %5.1f | %5.1f | %5.1f | %-6s\n",
               f + 1, fdr.relayState.c_str(),
               fdr.currentR, fdr.currentY, fdr.currentB,
               fdr.mcbStatus ? "OK" : "TRIP");
      msg += String(row);
    }
    msg += "```\n";
  }
  else if (mode == "G") {
    // Mode G: Current Abnormality Summary
    msg = "⚠️ *CURRENT ABNORMALITY SUMMARY*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    int abnormalCount = 0;
    int totalPoles = meshMgr.getNodeCount();

    for (int i = 0; i < totalPoles; i++) {
      NodeRecord *n = meshMgr.getNode(i);
      if (!n) continue;
      for (int f = 0; f < MAX_FEEDERS; f++) {
        FeederState &fdr = n->feeders[f];
        if (fdr.currentR > CONF_OVERCURRENT_THRESH_A ||
            fdr.currentY > CONF_OVERCURRENT_THRESH_A ||
            fdr.currentB > CONF_OVERCURRENT_THRESH_A ||
            fdr.faultState == "OVER_CURR") {
          abnormalCount++;
          msg += "🚨 *" + n->logicalId + "* F" + String(f + 1) + ": ";
          msg += "R=" + String(fdr.currentR, 1) + "A, Y=" + String(fdr.currentY, 1) + "A, B=" + String(fdr.currentB, 1) + "A (Overcurrent)\n";
        }
      }
    }
    if (abnormalCount == 0) {
      msg += "✅ No overcurrent or current abnormalities detected across 300 feeders.\n";
    }
  }
  else if (mode == "H") {
    // Mode H: Sensor Fault Summary
    msg = "🛠 *CURRENT SENSOR FAULT SUMMARY*\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    int faultCount = 0;
    int totalPoles = meshMgr.getNodeCount();

    for (int i = 0; i < totalPoles; i++) {
      NodeRecord *n = meshMgr.getNode(i);
      if (!n) continue;
      for (int f = 0; f < MAX_FEEDERS; f++) {
        FeederState &fdr = n->feeders[f];
        if (fdr.currentValidityR >= MEAS_SENSOR_DISCONNECTED ||
            fdr.currentValidityY >= MEAS_SENSOR_DISCONNECTED ||
            fdr.currentValidityB >= MEAS_SENSOR_DISCONNECTED) {
          faultCount++;
          msg += "⚠️ *" + n->logicalId + "* F" + String(f + 1) + ": ";
          msg += "R=" + String(alertMgr.getValidityDescription(fdr.currentValidityR)) + ", ";
          msg += "Y=" + String(alertMgr.getValidityDescription(fdr.currentValidityY)) + ", ";
          msg += "B=" + String(alertMgr.getValidityDescription(fdr.currentValidityB)) + "\n";
        }
      }
    }
    if (faultCount == 0) {
      msg += "✅ All 900 current sensors reporting normal valid signals.\n";
    }
  }

  return msg;
}

String TelegramMenus::buildCurrentDashboardKeyboard(UserSession &sess) {
  String mode = sess.currentMode.length() > 0 ? sess.currentMode : "A";
  return "[[{\"text\":\"📍 Pole Summary (A)\",\"callback_data\":\"CURR_M:A\"},"
          "{\"text\":\"🔌 Feeder Detail (B)\",\"callback_data\":\"CURR_M:B\"}],"
         "[{\"text\":\"🔴 Phase R (C)\",\"callback_data\":\"CURR_M:C\"},"
          "{\"text\":\"🟡 Phase Y (D)\",\"callback_data\":\"CURR_M:D\"},"
          "{\"text\":\"🔵 Phase B (E)\",\"callback_data\":\"CURR_M:E\"}],"
         "[{\"text\":\"⚖️ 6-Fdr Compare (F)\",\"callback_data\":\"CURR_M:F\"},"
          "{\"text\":\"⚠️ Abnormalities (G)\",\"callback_data\":\"CURR_M:G\"}],"
         "[{\"text\":\"🛠 Sensor Faults (H)\",\"callback_data\":\"CURR_M:H\"},"
          "{\"text\":\"🔄 REFRESH\",\"callback_data\":\"CURR_MON\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

// ─────────────────────────────────────────────────────────────
//  6. FAULT ALERTS & INCIDENT MANAGEMENT (SEC 7, 11, 13)
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildFaultAlertsMessage() {
  String msg = "🚨 *ACTIVE FAULT & INCIDENT SUMMARY*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";

  uint32_t faultCount = alertMgr.getActiveFaultCount();
  if (faultCount == 0) {
    msg += "🟢 *All Systems Normal*\n";
    msg += "• No active MCB trips.\n";
    msg += "• No overcurrent conditions.\n";
    msg += "• Voltage within nominal limits.\n";
    msg += "• All active radio links operating normally.\n";
    return msg;
  }

  msg += "⚠️ *Active Fault Conditions (" + String(faultCount) + "):*\n\n";

  int totalPoles = meshMgr.getNodeCount();
  for (int i = 0; i < totalPoles; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;

    if (n->onlineState == "OFFLINE") {
      msg += "🔴 *Offline:* `" + n->logicalId + "` (" + n->name + ") — No RF response\n";
    }

    for (int f = 0; f < MAX_FEEDERS; f++) {
      FeederState &fdr = n->feeders[f];
      if (!fdr.mcbStatus) {
        msg += "🔴 *MCB Trip:* `" + n->logicalId + "` F" + String(f + 1) + " (" + fdr.name + ")\n";
      }
      if (fdr.faultState == "OVER_CURR") {
        msg += "🔴 *Overcurrent:* `" + n->logicalId + "` F" + String(f + 1) + " (IR=" + String(fdr.currentR, 1) + "A)\n";
      }
    }
  }

  msg += "\n━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Use Event History to review and acknowledge incidents.";
  return msg;
}

String TelegramMenus::buildFaultAlertsKeyboard() {
  return "[[{\"text\":\"📜 View Event History\",\"callback_data\":\"EVT_HIST:0\"}],"
         "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"ALERTS_LIST\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildIncidentDetailMessage(uint32_t eventId) {
  EventRecord *ev = alertMgr.getEvent(eventId);
  if (!ev) return "❌ Incident #" + String(eventId) + " not found.";

  String msg = "🚨 *INCIDENT RECORD #" + String(eventId) + "*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "• Severity: `" + String(alertSeverityToString(ev->severity)) + "`\n";
  msg += "• Event Type: *" + String(eventTypeToString(ev->type)) + "*\n";
  msg += "• Target Node: `" + String(ev->nodeLogicalId) + "`\n";
  if (ev->feederId > 0) msg += "• Feeder: `Feeder " + String(ev->feederId) + "`\n";
  if (ev->phase != '-') msg += "• Phase: `" + String(ev->phase) + "`\n";
  msg += "• Timestamp: `" + alertMgr.formatTimestamp(ev->timestamp, ev->timestampCertain) + "`\n";
  msg += "• Description: *" + String(ev->description) + "*\n\n";

  msg += "📋 *Status & Lifecycle:*\n";
  msg += "• Acknowledged: `" + String(ev->ackStatus == ACK_ACKNOWLEDGED ? "YES ✅" : "NO ❌") + "`\n";
  if (ev->ackStatus == ACK_ACKNOWLEDGED) {
    msg += "• Acknowledged By: `" + String(ev->ackBy) + "`\n";
    msg += "• Ack Time: `" + alertMgr.formatTimestamp(ev->ackTime, true) + "`\n";
  }
  msg += "• Resolution: `" + String(ev->resolutionStatus == RES_RESOLVED ? "RESOLVED 🟢" : "ACTIVE 🔴") + "`\n\n";

  msg += "⚠️ *Policy:* Acknowledging an alert does NOT clear the physical fault state.\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";

  return msg;
}

String TelegramMenus::buildIncidentDetailKeyboard(uint32_t eventId) {
  String eid = String(eventId);
  EventRecord *ev = alertMgr.getEvent(eventId);
  String pid = (ev) ? String(ev->nodeLogicalId) : "GATEWAY";

  return "[[{\"text\":\"✅ Acknowledge Incident\",\"callback_data\":\"ACK_INC:" + eid + "\"},"
          "{\"text\":\"🛠 Mark Resolved\",\"callback_data\":\"RES_INC:" + eid + "\"}],"
         "[{\"text\":\"🔍 View Pole\",\"callback_data\":\"P:" + pid + "\"},"
          "{\"text\":\"📜 Event History\",\"callback_data\":\"EVT_HIST:0\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"EVT_HIST:0\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

// ─────────────────────────────────────────────────────────────
//  7. EVENT HISTORY AUDIT LOG & PAGINATION (SEC 12)
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildEventHistoryMessage(UserSession &sess) {
  int totalEvents = alertMgr.getEventHistoryCount();
  String msg = "📜 *GATEWAY EVENT HISTORY AUDIT LOG*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";

  if (totalEvents == 0) {
    msg += "No events recorded in audit history.\n";
    return msg;
  }

  int totalPages = ((totalEvents - 1) / EVENTS_PER_PAGE) + 1;
  if (sess.pageIndex >= totalPages) sess.pageIndex = totalPages - 1;
  if (sess.pageIndex < 0) sess.pageIndex = 0;

  int start = sess.pageIndex * EVENTS_PER_PAGE;
  int end = min(start + EVENTS_PER_PAGE, totalEvents);

  for (int i = start; i < end; i++) {
    EventRecord *ev = alertMgr.getEventByIndex(i);
    if (!ev) continue;

    String icon = (ev->severity == ALERT_SEV_P0_CRITICAL) ? "🔴" :
                  ((ev->severity == ALERT_SEV_P1_HIGH) ? "🟠" :
                   ((ev->severity == ALERT_SEV_P2_MEDIUM) ? "🟡" : "🟢"));

    String ackIcon = (ev->ackStatus == ACK_ACKNOWLEDGED) ? "✅" : "⏳";

    msg += icon + " *#" + String(ev->eventId) + "* | `" + alertSeverityToString(ev->severity) + "` | " + ackIcon + "\n";
    msg += "  • Event: *" + String(eventTypeToString(ev->type)) + "*\n";
    msg += "  • Target: `" + String(ev->nodeLogicalId) + "`";
    if (ev->feederId > 0) msg += " (F" + String(ev->feederId) + " " + String(ev->phase) + ")";
    msg += "\n";
    msg += "  • Time: `" + alertMgr.formatTimestamp(ev->timestamp, ev->timestampCertain) + "`\n";
    msg += "  • Desc: _" + String(ev->description) + "_\n\n";
  }

  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Page " + String(sess.pageIndex + 1) + " of " + String(totalPages) + " (" + String(totalEvents) + " Events)";
  return msg;
}

String TelegramMenus::buildEventHistoryKeyboard(UserSession &sess) {
  int totalEvents = alertMgr.getEventHistoryCount();
  int totalPages = (totalEvents > 0) ? (((totalEvents - 1) / EVENTS_PER_PAGE) + 1) : 1;
  int start = sess.pageIndex * EVENTS_PER_PAGE;
  int end = min(start + EVENTS_PER_PAGE, totalEvents);

  String kb = "[";

  // Individual Incident select buttons on current page
  for (int i = start; i < end; i++) {
    EventRecord *ev = alertMgr.getEventByIndex(i);
    if (ev) {
      String icon = (ev->severity == ALERT_SEV_P0_CRITICAL) ? "🔴" : "⚠️";
      kb += "[{\"text\":\"" + icon + " Incident #" + String(ev->eventId) + " (" + String(ev->nodeLogicalId) + ")\",\"callback_data\":\"VIEW_INC:" + String(ev->eventId) + "\"}],";
    }
  }

  // Pagination row
  String nav = "[";
  if (sess.pageIndex > 0) {
    nav += "{\"text\":\"◀️ Prev\",\"callback_data\":\"EVT_HIST:" + String(sess.pageIndex - 1) + "\"},";
  }
  if (end < totalEvents) {
    nav += "{\"text\":\"Next ▶️\",\"callback_data\":\"EVT_HIST:" + String(sess.pageIndex + 1) + "\"},";
  }
  nav += "{\"text\":\"🔄 Refresh\",\"callback_data\":\"EVT_HIST:" + String(sess.pageIndex) + "\"}";
  nav += "]";
  kb += nav + ",";

  kb += "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"HOME\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";

  return kb;
}

// ─────────────────────────────────────────────────────────────
//  8. TOPOLOGY & MESH ROUTING
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildTopologyMessage() {
  String msg = "🗺️ *MSCB LORA MESH TOPOLOGY*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "• Mesh Net ID: `MSCB (0x4D53)`\n";
  msg += "• Root: `GATEWAY (Rank 0)`\n";
  msg += "• Registered Nodes: `" + String(meshMgr.getNodeCount()) + "`\n";
  msg += "• Active Routes: `" + String(meshMgr.getRouteCount()) + "`\n\n";

  msg += "```\n";
  msg += "Destination | Next-Hop   | Hops | RSSI | Health\n";
  msg += "------------+------------+------+------+-------\n";

  int rCount = meshMgr.getRouteCount();
  for (int i = 0; i < rCount && i < 10; i++) {
    GwRouteEntry *r = meshMgr.getRoute(i);
    if (r && r->valid) {
      char row[64];
      snprintf(row, sizeof(row), "%-11s | %-10s | %4d | %4d | GOOD \n",
               r->destination.c_str(), r->primaryNextHop.c_str(),
               r->hopCount, r->rssi);
      msg += String(row);
    }
  }
  msg += "```\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenus::buildTopologyKeyboard() {
  return "[[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"TOPOLOGY\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

// ─────────────────────────────────────────────────────────────
//  9. LEGACY & UTILITY MENUS
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildSettingsMessage() {
  String msg = "⚙️ *SYSTEM SETTINGS & CONTROLS*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "• 📋 *All Poles* — Full 50-pole roster\n";
  msg += "• 🔌 *Feeder Monitoring* — 300 Feeder Channels\n";
  msg += "• ⚡ *Voltage Monitoring* — 150 Common Channels\n";
  msg += "• 📈 *Current Monitoring* — 900 Current Channels\n";
  msg += "• 🚨 *Fault Alerts* — Intelligent Alert System\n";
  msg += "• 🔁 *Reset Controls* — Safe device restart\n";
  msg += "• 📝 *Registration* — Node provisioning\n";
  msg += "• 🗑️ *Delete Management* — Decommissioning\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenus::buildSettingsKeyboard() {
  return "[[{\"text\":\"📋 All Poles\",\"callback_data\":\"POLES_ALL\"},"
          "{\"text\":\"🔌 Feeder Monitor\",\"callback_data\":\"FEEDER_MON\"}],"
         "[{\"text\":\"⚡ Voltage Monitor\",\"callback_data\":\"VOLT_MON\"},"
          "{\"text\":\"📈 Current Monitor\",\"callback_data\":\"CURR_MON\"}],"
         "[{\"text\":\"🚨 Fault Alerts\",\"callback_data\":\"ALERTS_LIST\"},"
          "{\"text\":\"📜 Event History\",\"callback_data\":\"EVT_HIST:0\"}],"
         "[{\"text\":\"🔁 Reset Controls\",\"callback_data\":\"RESET_SEL\"},"
          "{\"text\":\"📝 Registration\",\"callback_data\":\"REGLIST\"}],"
         "[{\"text\":\"⬅️ Back\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildFeederStatusKeyboard() {
  return "[[{\"text\":\"🔄 Refresh\",\"callback_data\":\"FEEDER_MON\"}],"
         "[{\"text\":\"⬅️ Back\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildNeighboursMessage(UserSession &sess) {
  return buildAllPolesMessage(sess, "ALL");
}

String TelegramMenus::buildNeighboursKeyboard(UserSession &sess) {
  return buildAllPolesKeyboard(sess, "ALL");
}

String TelegramMenus::buildPoleNeighboursMessage(UserSession &sess) {
  NodeRecord *n = meshMgr.findNode(sess.selectedPole);
  String pName = n ? n->name : sess.selectedPole;
  String msg = "📡 *NEIGHBOURS REPORTED BY " + pName + "*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  int count = meshMgr.getNeighborSummaryCount();
  int found = 0;

  for (int i = 0; i < count; i++) {
    NeighborSummary *nb = meshMgr.getNeighborSummary(i);
    if (nb && nb->valid && (nb->reportingNode == sess.selectedPole || (n && nb->reportingNode == n->hwId))) {
      found++;
      msg += "• `" + nb->neighborId + "` | RSSI: `" + String(nb->rssi) + " dBm` | SNR: `" + String(nb->snr) + " dB`\n";
    }
  }

  if (found == 0) {
    msg += "No local neighbor beacons heard yet.\n";
  }
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenus::buildPoleNeighboursKeyboard(UserSession &sess) {
  return "[[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"PNEIGH:" + sess.selectedPole + "\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"P:" + sess.selectedPole + "\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildPoleCtrlMessage(UserSession &sess) {
  return buildPoleDetailMessage(sess);
}

String TelegramMenus::buildPoleCtrlKeyboard(UserSession &sess) {
  return buildPoleDetailKeyboard(sess);
}

String TelegramMenus::buildFarmerListMessage(UserSession &sess) {
  return buildAllPolesMessage(sess, "ALL");
}

String TelegramMenus::buildFarmerListKeyboard(UserSession &sess) {
  return buildAllPolesKeyboard(sess, "ALL");
}

String TelegramMenus::buildFarmerCtrlMessage(UserSession &sess) {
  return buildPoleDetailMessage(sess);
}

String TelegramMenus::buildFarmerCtrlKeyboard(UserSession &sess) {
  return buildPoleDetailKeyboard(sess);
}

String TelegramMenus::buildTimerSelectMessage(UserSession &sess) {
  NodeRecord *n = meshMgr.findNode(sess.selectedPole);
  String pName = n ? n->name : sess.selectedPole;
  return "⏱ *SELECT OPERATING TIMER*\nTarget: *" + pName + "* (`" + sess.selectedPole + "`)\n\n"
         "Select duration after which output will automatically turn OFF:";
}

String TelegramMenus::buildTimerSelectKeyboard(UserSession &sess) {
  String pid = sess.selectedPole;
  return "[[{\"text\":\"⏱ 5 Min\",\"callback_data\":\"TMR_SET:" + pid + ":5\"},"
          "{\"text\":\"⏱ 10 Min\",\"callback_data\":\"TMR_SET:" + pid + ":10\"}],"
         "[{\"text\":\"⏱ 15 Min\",\"callback_data\":\"TMR_SET:" + pid + ":15\"},"
          "{\"text\":\"⏱ 30 Min\",\"callback_data\":\"TMR_SET:" + pid + ":30\"}],"
         "[{\"text\":\"⏱ 60 Min\",\"callback_data\":\"TMR_SET:" + pid + ":60\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"P:" + pid + "\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildScheduleMessage(UserSession &sess) {
  String pid = sess.selectedPole;
  return "🌅 *SCHEDULE CONFIGURATION*\nTarget: `" + pid + "`\nConfigure autonomous on-board scheduler:";
}

String TelegramMenus::buildScheduleKeyboard(UserSession &sess) {
  String pid = sess.selectedPole;
  return "[[{\"text\":\"🌅 Morning (6:00 - 10:00)\",\"callback_data\":\"SCHED_SET:" + pid + ":360:600\"}],"
         "[{\"text\":\"🌇 Evening (18:00 - 22:00)\",\"callback_data\":\"SCHED_SET:" + pid + ":1080:1320\"}],"
         "[{\"text\":\"🚫 Disable Schedule\",\"callback_data\":\"SCHED_DIS:" + pid + "\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"P:" + pid + "\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildRegistrationMessage(UserSession &sess) {
  String msg = "📝 *PENDING NODE REGISTRATIONS*\n━━━━━━━━━━━━━━━━━━━━\n";
  int count = 0;
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    PendingRegistration *p = meshMgr.getPendingReg(i);
    if (p && p->valid && p->state == REG_DETECTED) {
      count++;
      msg += "• HW ID: `" + p->hwId + "` | RSSI: `" + String(p->rssi) + " dBm`\n";
    }
  }
  if (count == 0) {
    msg += "No unregistered nodes currently probing.\n";
  }
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenus::buildRegistrationKeyboard(UserSession &sess) {
  String kb = "[";
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    PendingRegistration *p = meshMgr.getPendingReg(i);
    if (p && p->valid && p->state == REG_DETECTED) {
      kb += "[{\"text\":\"✅ Register " + p->hwId + "\",\"callback_data\":\"REG:" + p->hwId + "\"}],";
    }
  }
  kb += "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"REGLIST\"}],"
        "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"HOME\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

String TelegramMenus::buildRegTypeSelectMessage(const String &hwId) {
  return "📝 *SELECT NODE TYPE*\nHardware ID: `" + hwId + "`\nChoose the application profile:";
}

String TelegramMenus::buildRegTypeSelectKeyboard(const String &hwId) {
  return "[[{\"text\":\"💡 Municipal Smart Pole\",\"callback_data\":\"REG_T:SL:" + hwId + "\"}],"
         "[{\"text\":\"🌾 Agricultural Pump Station\",\"callback_data\":\"REG_T:FR:" + hwId + "\"}],"
         "[{\"text\":\"❌ Cancel\",\"callback_data\":\"REGLIST\"}]]";
}

String TelegramMenus::buildResetSelectMessage() {
  return "🔁 *DEVICE RESTART CONTROLS*\n━━━━━━━━━━━━━━━━━━━━\n"
         "Select target device restart action.\nConfiguration & flash memory will be preserved.";
}

String TelegramMenus::buildResetSelectKeyboard() {
  return "[[{\"text\":\"🔁 Reboot Single Pole\",\"callback_data\":\"RST_LST\"}],"
         "[{\"text\":\"⚠️ Reboot All Poles (Mesh Broadcast)\",\"callback_data\":\"RST_ALL_CONF\"}],"
         "[{\"text\":\"⬅️ Back\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildResetPoleListKeyboard() {
  String kb = "[";
  int count = meshMgr.getNodeCount();
  for (int i = 0; i < count; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n) {
      kb += "[{\"text\":\"Reboot " + n->name + " (" + n->logicalId + ")\",\"callback_data\":\"RST_P:" + n->logicalId + "\"}],";
    }
  }
  kb += "[{\"text\":\"⬅️ Back\",\"callback_data\":\"RESET_SEL\"},"
        "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

String TelegramMenus::buildResetConfirmPoleMessage(const String &poleId) {
  NodeRecord *node = meshMgr.findNode(poleId);
  String pName = node ? node->name : poleId;
  return "⚠️ *CONFIRM REBOOT: " + pName + "* (`" + poleId + "`)\n\n"
         "Are you sure you want to reboot this field pole?";
}

String TelegramMenus::buildResetConfirmPoleKeyboard(const String &poleId) {
  return "[[{\"text\":\"✅ Confirm Reboot\",\"callback_data\":\"RST_DO_P:" + poleId + "\"},"
          "{\"text\":\"❌ Cancel\",\"callback_data\":\"RESET_SEL\"}]]";
}

String TelegramMenus::buildResetConfirmNeighMessage(const String &originPoleId, const String &neighId) {
  return "⚠️ *CONFIRM NEIGHBOUR REBOOT: " + neighId + "*\nVia Route: `" + originPoleId + "`";
}

String TelegramMenus::buildResetConfirmNeighKeyboard(const String &originId, const String &neighId) {
  return "[[{\"text\":\"✅ Confirm Reboot\",\"callback_data\":\"RST_DO_N:" + originId + ":" + neighId + "\"},"
          "{\"text\":\"❌ Cancel\",\"callback_data\":\"RESET_SEL\"}]]";
}

String TelegramMenus::buildResetConfirmAllMessage() {
  return "⚠️ *CONFIRM BROADCAST REBOOT ALL POLES*\n━━━━━━━━━━━━━━━━━━━━\n\n"
         "This will transmit a restart command to **ALL field poles** (" + String(meshMgr.getNodeCount()) + " poles).\n\n"
         "Are you sure you want to proceed?";
}

String TelegramMenus::buildResetConfirmAllKeyboard() {
  return "[[{\"text\":\"✅ CONFIRM REBOOT ALL\",\"callback_data\":\"RST_DO_ALL\"}],"
         "[{\"text\":\"❌ CANCEL\",\"callback_data\":\"RESET_SEL\"}]]";
}

String TelegramMenus::buildDeleteManagementMessage(UserSession &sess) {
  return menuMgr.buildDeleteManagementMessage(sess);
}

String TelegramMenus::buildDeleteManagementKeyboard(UserSession &sess) {
  return menuMgr.buildDeleteManagementKeyboard(sess);
}
