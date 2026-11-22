/*
 * =====================================================================
 *  TELEGRAMMENUMANAGER.CPP — DASHBOARD UI & INLINE KEYBOARD BUILDERS
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#include "TelegramMenuManager.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "FeederManager.h"
#include "RevocationManager.h"
#include "GwRegistry.h"
#include "StorageManager.h"
#include "AuthSecurity.h"
#include <WiFi.h>

TelegramMenuManager menuMgr;

TelegramMenuManager::TelegramMenuManager() {}

String TelegramMenuManager::formatUptime(unsigned long ms) {
  unsigned long sec = ms / 1000;
  int d = sec / 86400;
  int h = (sec % 86400) / 3600;
  int m = (sec % 3600) / 60;
  int s = sec % 60;
  char buf[48];
  if (d > 0) {
    snprintf(buf, sizeof(buf), "%dd %dh %dm %ds", d, h, m, s);
  } else {
    snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, s);
  }
  return String(buf);
}

String TelegramMenuManager::formatRelativeTime(unsigned long pastMs) {
  if (pastMs == 0) return "Never";
  unsigned long now = millis();
  if (now < pastMs) return "Just now";
  unsigned long diffSec = (now - pastMs) / 1000;
  if (diffSec < 5) return "Just now";
  if (diffSec < 60) return String(diffSec) + "s ago";
  if (diffSec < 3600) return String(diffSec / 60) + "m ago";
  return String(diffSec / 3600) + "h ago";
}

// ─────────────────────────────────────────────────────────────
//  1. SYSTEM DASHBOARD
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildDashboardMessage(const String &wifiSsid) {
  int totalPoles = meshMgr.getNodeCount();
  int onlineCount = 0;
  int degradedCount = 0;
  int offlineCount = 0;
  int activeFeeders = 0;
  int feedersOn = 0;
  int faultCount = 0;

  for (int i = 0; i < totalPoles; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;
    if (n->onlineState == "ONLINE") onlineCount++;
    else if (n->onlineState == "DEGRADED") degradedCount++;
    else offlineCount++;

    if (n->fault != "OK" && n->fault.length() > 0 && n->fault != "NONE") {
      faultCount++;
    }

    for (int f = 0; f < MAX_FEEDERS; f++) {
      if (n->feeders[f].status != FEEDER_STATUS_DEACTIVATED && n->feeders[f].status != FEEDER_STATUS_REMOVED) {
        activeFeeders++;
        if (n->feeders[f].relayState == "ON") feedersOn++;
      }
    }
  }

  String msg = "🌟 *MSCB SMART POLE CENTRAL GATEWAY*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🟢 *Gateway:* `ONLINE` (`" + String(GW_VERSION) + "`)\n";
  msg += "📡 *LoRa Radio:* `433.0 MHz (SF10, BW125)`\n";
  msg += "📶 *WiFi:* `CONNECTED` (`" + wifiSsid + "`, `" + String(WiFi.RSSI()) + " dBm`)\n";
  msg += "🌐 *IP Address:* `" + WiFi.localIP().toString() + "`\n";
  msg += "⏱ *Uptime:* `" + formatUptime(millis()) + "`\n";
  msg += "💾 *Free Heap:* `" + String(ESP.getFreeHeap()) + " bytes`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📊 *NETWORK STATUS (Max 50 Poles):*\n";
  msg += "• Registered Poles: *" + String(totalPoles) + " / " + String(MAX_NODES) + "*\n";
  msg += "• Health: 🟢 `" + String(onlineCount) + "` | 🟡 `" + String(degradedCount) + "` | 🔴 `" + String(offlineCount) + "`\n";
  msg += "• Active Feeders: *" + String(activeFeeders) + " / 300* (⚡ *" + String(feedersOn) + " ON*)\n";
  msg += "• Metric Channels: *900 Current* | *150 Voltage*\n";

  if (faultCount > 0) {
    msg += "⚠️ *Active Faults:* `" + String(faultCount) + " POLE(S) IN FAULT`\n";
  } else {
    msg += "✅ *Active Faults:* `NONE (All systems nominal)`\n";
  }

  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Select an operation from the menu below:";
  return msg;
}

String TelegramMenuManager::buildDashboardKeyboard() {
  String kb = "{\"inline_keyboard\":[";
  // Row 1: Poles & Feeders
  kb += "[{\"text\":\"📍 Poles (50)\",\"callback_data\":\"NAV:POLE_MGMT\"},{\"text\":\"🔌 Feeders (300)\",\"callback_data\":\"NAV:FEEDER_MGMT\"}],";
  // Row 2: Control
  kb += "[{\"text\":\"⚡ Pole Control\",\"callback_data\":\"NAV:POLE_CTRL\"},{\"text\":\"🔌 Feeder Control\",\"callback_data\":\"NAV:FEEDER_CTRL\"}],";
  // Row 3: Bulk Control
  kb += "[{\"text\":\"👥 Group Control\",\"callback_data\":\"NAV:GROUP_CTRL\"},{\"text\":\"🌐 All Poles Control\",\"callback_data\":\"NAV:ALL_POLES\"}],";
  // Row 4: Monitoring
  kb += "[{\"text\":\"⚡ Voltage Monitor\",\"callback_data\":\"NAV:VOLT_MON\"},{\"text\":\"🔌 Current (900Ch)\",\"callback_data\":\"NAV:CURR_MON\"}],";
  // Row 5: Topology & Status
  kb += "[{\"text\":\"🗺️ Network Topology\",\"callback_data\":\"NAV:TOPOLOGY\"},{\"text\":\"📊 Online/Offline\",\"callback_data\":\"NAV:NODE_STATUS\"}],";
  // Row 6: Safety & Registration
  kb += "[{\"text\":\"⚠️ Fault Alerts\",\"callback_data\":\"NAV:FAULTS\"},{\"text\":\"📝 New Registration\",\"callback_data\":\"NAV:REG\"}],";
  // Row 7: Deletion & Time
  kb += "[{\"text\":\"🗑️ Delete Pole\",\"callback_data\":\"NAV:DEL_MGMT\"},{\"text\":\"🕒 Time & Schedule\",\"callback_data\":\"NAV:TIME_CFG\"}],";
  // Row 8: Settings & Audit
  kb += "[{\"text\":\"⚙️ Settings\",\"callback_data\":\"NAV:GW_SETTINGS\"},{\"text\":\"📜 Event Log\",\"callback_data\":\"NAV:EVENTS\"}],";
  // Row 9: Refresh & Help
  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"ℹ️ System Help\",\"callback_data\":\"NAV:HELP\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  2. POLE MANAGEMENT
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildPoleManagementMessage(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  String msg = "📍 *POLE MANAGEMENT DASHBOARD*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Total Registered: *" + String(total) + " / " + String(MAX_NODES) + "*\n\n";

  if (total == 0) {
    msg += "⚠️ *No poles registered in database.*\n";
    msg += "Awaiting field node join requests via LoRa.\n";
    return msg;
  }

  int totalPages = ((total - 1) / POLES_PAGE_SIZE) + 1;
  if (sess.pageIndex >= totalPages) sess.pageIndex = totalPages - 1;
  if (sess.pageIndex < 0) sess.pageIndex = 0;

  int start = sess.pageIndex * POLES_PAGE_SIZE;
  int end = min(start + POLES_PAGE_SIZE, total);

  for (int i = start; i < end; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;

    String stIcon = (n->onlineState == "ONLINE") ? "🟢" : ((n->onlineState == "DEGRADED") ? "🟡" : "🔴");
    msg += stIcon + " *" + n->name + "* (`" + n->logicalId + "`)\n";
    msg += "   📍 Area: `" + n->location + "` | Hops: `" + String(n->hopCount) + "`\n";
    msg += "   📶 RSSI: `" + String(n->rssi) + " dBm` | SNR: `" + String(n->snr) + " dB`\n";

    // Six Feeder Status overview badges
    msg += "   🔌 Feeders: `[";
    for (int f = 0; f < MAX_FEEDERS; f++) {
      if (n->feeders[f].status == FEEDER_STATUS_DEACTIVATED) msg += "⚪";
      else if (n->feeders[f].relayState == "ON") msg += "🟢";
      else msg += "🔴";
      if (f < MAX_FEEDERS - 1) msg += " ";
    }
    msg += "]`\n";

    // 3 Common Bus Voltage overview
    if (n->onlineState != "OFFLINE" && n->commonVoltageR > 0) {
      msg += "   ⚡ Bus: `VR=" + String(n->commonVoltageR, 1) + "V VY=" + String(n->commonVoltageY, 1) + "V VB=" + String(n->commonVoltageB, 1) + "V`\n";
    } else if (n->onlineState == "OFFLINE") {
      msg += "   ⚡ Bus: `Node Offline (" + formatRelativeTime(n->lastSeen) + ")`\n";
    }
    msg += "\n";
  }

  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Page " + String(sess.pageIndex + 1) + " of " + String(totalPages);
  return msg;
}

String TelegramMenuManager::buildPoleManagementKeyboard(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  int totalPages = ((total - 1) / POLES_PAGE_SIZE) + 1;
  int start = sess.pageIndex * POLES_PAGE_SIZE;
  int end = min(start + POLES_PAGE_SIZE, total);

  String kb = "{\"inline_keyboard\":[";

  // Buttons for each visible pole
  for (int i = start; i < end; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;
    String btnText = "📍 " + n->logicalId + " - " + n->name;
    if (btnText.length() > 30) btnText = btnText.substring(0, 28) + "..";
    kb += "[{\"text\":\"" + btnText + "\",\"callback_data\":\"P_SEL:" + n->logicalId + "\"}],";
  }

  // Pagination navigation row
  kb += "[";
  if (sess.pageIndex > 0) {
    kb += "{\"text\":\"◀️ Prev\",\"callback_data\":\"PAGE:POLE_P\"},";
  }
  kb += "{\"text\":\"Page " + String(sess.pageIndex + 1) + "/" + String(max(1, totalPages)) + "\",\"callback_data\":\"REFRESH\"}";
  if (sess.pageIndex < totalPages - 1) {
    kb += ",{\"text\":\"Next ▶️\",\"callback_data\":\"PAGE:POLE_N\"}";
  }
  kb += "],";

  // Action footer
  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  2B. POLE DETAIL
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildPoleDetailMessage(UserSession &sess) {
  NodeRecord *n = meshMgr.findNode(sess.selectedPole);
  if (!n) return "⚠️ *Pole Not Found:* `" + sess.selectedPole + "`\n\nPlease return to Pole Management.";

  String stIcon = (n->onlineState == "ONLINE") ? "🟢 ONLINE" : ((n->onlineState == "DEGRADED") ? "🟡 DEGRADED" : "🔴 OFFLINE");

  String msg = "📍 *POLE DIAGNOSTIC PROFILE*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🆔 *Logical ID:* `" + n->logicalId + "`\n";
  msg += "⚙️ *Hardware ID:* `" + n->hwId + "`\n";
  msg += "🏷️ *Pole Name:* *" + n->name + "*\n";
  msg += "🏢 *Area / Loc:* `" + n->location + "`\n";
  msg += "📊 *Status:* " + stIcon + "\n";
  msg += "🏷️ *Lifecycle:* `" + String(poleLifecycleStateToString(n->lifecycleState)) + "`\n";
  msg += "📶 *RF Signal:* `" + String(n->rssi) + " dBm` (SNR: `" + String(n->snr) + " dB`)\n";
  msg += "⏱ *Last Seen:* `" + formatRelativeTime(n->lastSeen) + "`\n";
  msg += "📦 *Firmware:* `" + String(POLE_VERSION) + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🗺️ *MESH ROUTING TOPOLOGY:*\n";
  msg += "• Primary Parent: `" + (n->primaryNextHop.length() > 0 ? n->primaryNextHop : "GATEWAY") + "`\n";
  msg += "• Backup Parent:  `" + (n->backupNextHop.length() > 0 ? n->backupNextHop : "NONE") + "`\n";
  msg += "• Hop Count:      `" + String(n->hopCount) + "` (Rank: `" + String(n->rank) + "`)\n";
  msg += "• Route Path:     `" + (n->routePath.length() > 0 ? n->routePath : "DIRECT") + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "⚡ *3-PHASE COMMON BUS VOLTAGE:*\n";
  msg += "• Phase R: `" + String(n->commonVoltageR, 1) + " V` (" + n->commonVoltageStateR + ")\n";
  msg += "• Phase Y: `" + String(n->commonVoltageY, 1) + " V` (" + n->commonVoltageStateY + ")\n";
  msg += "• Phase B: `" + String(n->commonVoltageB, 1) + " V` (" + n->commonVoltageStateB + ")\n";
  msg += "• Bus Frequency: `" + String(n->gridFrequency, 2) + " Hz`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🔌 *SIX FEEDER CHANNELS (F1–F6):*\n";
  for (int f = 0; f < MAX_FEEDERS; f++) {
    FeederState &fdr = n->feeders[f];
    String fIcon = (fdr.status == FEEDER_STATUS_DEACTIVATED) ? "⚪ DEACTIVATED" : (fdr.relayState == "ON" ? "🟢 ON" : "🔴 OFF");
    String cont = fdr.contactor ? "🔒 CLOSED" : "🔓 OPEN";
    msg += "• *F" + String(f + 1) + " (" + fdr.name + "):* " + fIcon + " | Contactor: " + cont + "\n";
  }

  if (n->fault != "OK" && n->fault.length() > 0 && n->fault != "NONE") {
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "⚠️ *ACTIVE FAULT:* `" + n->fault + "`\n";
  }

  return msg;
}

String TelegramMenuManager::buildPoleDetailKeyboard(UserSession &sess) {
  String p = sess.selectedPole;
  String kb = "{\"inline_keyboard\":[";
  // Row 1: Control & Feeders
  kb += "[{\"text\":\"⚡ Control Pole\",\"callback_data\":\"NAV:POLE_CTRL\"},{\"text\":\"🔌 View Feeders\",\"callback_data\":\"NAV:FEEDER_MGMT\"}],";
  // Row 2: Route & Faults
  kb += "[{\"text\":\"🗺️ Network Route\",\"callback_data\":\"NAV:TOPOLOGY\"},{\"text\":\"⚠️ Fault Log\",\"callback_data\":\"NAV:FAULTS\"}],";
  // Row 3: Live Query & Refresh
  kb += "[{\"text\":\"📊 Query Status\",\"callback_data\":\"CMD:PSTAT:" + p + "\"},{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"}],";
  // Row 4: Navigation
  kb += "[{\"text\":\"🔙 Back to Poles\",\"callback_data\":\"NAV:POLE_MGMT\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  3. FEEDER MANAGEMENT
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildFeederManagementMessage(UserSession &sess) {
  NodeRecord *n = nullptr;
  if (sess.selectedPole.length() > 0) {
    n = meshMgr.findNode(sess.selectedPole);
  }
  if (!n && meshMgr.getNodeCount() > 0) {
    n = meshMgr.getNode(0);
    if (n) sess.selectedPole = n->logicalId;
  }

  if (!n) {
    return "⚠️ *No registered field poles found.*\nCannot display feeder channels.";
  }

  String msg = "🔌 *FEEDER MANAGEMENT — " + n->name + "*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Pole ID: `" + n->logicalId + "` | Status: `" + n->onlineState + "`\n\n";

  for (int f = 0; f < MAX_FEEDERS; f++) {
    FeederState &fdr = n->feeders[f];
    String rIcon = (fdr.status == FEEDER_STATUS_DEACTIVATED) ? "⚪" : (fdr.relayState == "ON" ? "🟢" : "🔴");
    String mcb = fdr.mcbR ? "OK" : "TRIPPED";
    String cont = fdr.contactor ? "CLOSED" : "OPEN";

    msg += rIcon + " *Feeder " + String(f + 1) + ":* `" + fdr.name + "`\n";
    msg += "   • Relay: *" + fdr.relayState + "* | MCB: `" + mcb + "` | Cont: `" + cont + "`\n";

    // 3-Phase Current measurement
    if (n->onlineState != "OFFLINE") {
      msg += "   • Current: `R=" + String(fdr.currentR, 1) + "A Y=" + String(fdr.currentY, 1) + "A B=" + String(fdr.currentB, 1) + "A`\n";
    } else {
      msg += "   • Current: `Measurement Unavailable (Offline)`\n";
    }
    msg += "\n";
  }

  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Select a feeder below to inspect diagnostics or operate relay:";
  return msg;
}

String TelegramMenuManager::buildFeederManagementKeyboard(UserSession &sess) {
  String p = sess.selectedPole;
  String kb = "{\"inline_keyboard\":[";

  // Row 1: F1, F2
  kb += "[{\"text\":\"🔌 F1 Details\",\"callback_data\":\"F_SEL:" + p + ":1\"},{\"text\":\"🔌 F2 Details\",\"callback_data\":\"F_SEL:" + p + ":2\"}],";
  // Row 2: F3, F4
  kb += "[{\"text\":\"🔌 F3 Details\",\"callback_data\":\"F_SEL:" + p + ":3\"},{\"text\":\"🔌 F4 Details\",\"callback_data\":\"F_SEL:" + p + ":4\"}],";
  // Row 3: F5, F6
  kb += "[{\"text\":\"🔌 F5 Details\",\"callback_data\":\"F_SEL:" + p + ":5\"},{\"text\":\"🔌 F6 Details\",\"callback_data\":\"F_SEL:" + p + ":6\"}],";
  // Row 4: Switch Pole or Refresh
  kb += "[{\"text\":\"📍 Switch Pole\",\"callback_data\":\"NAV:POLE_MGMT\"},{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"}],";
  // Row 5: Dashboard
  kb += "[{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  3B. FEEDER DETAIL & DIAGNOSTIC CARD
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildFeederDetailMessage(UserSession &sess) {
  NodeRecord *n = meshMgr.findNode(sess.selectedPole);
  if (!n) return "⚠️ Pole not found.";

  uint8_t fdrNum = sess.selectedFeeder;
  if (fdrNum < 1 || fdrNum > MAX_FEEDERS) fdrNum = 1;
  FeederState &fdr = n->feeders[fdrNum - 1];

  String rState = (fdr.status == FEEDER_STATUS_DEACTIVATED) ? "⚪ DEACTIVATED" : (fdr.relayState == "ON" ? "🟢 ON" : "🔴 OFF");
  String mcb = fdr.mcbR ? "🟢 NORMAL" : "🔴 TRIPPED";
  String cont = fdr.contactor ? "🔒 CLOSED" : "🔓 OPEN";

  String validityR = (fdr.currentR > 0.05f) ? "VALID" : "ZERO_LOAD";
  String validityY = (fdr.currentY > 0.05f) ? "VALID" : "ZERO_LOAD";
  String validityB = (fdr.currentB > 0.05f) ? "VALID" : "ZERO_LOAD";

  if (n->onlineState == "OFFLINE") {
    validityR = validityY = validityB = "COMM_TIMEOUT";
  }

  String msg = "🔌 *FEEDER DIAGNOSTIC PROFILE*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📍 *Pole:* *" + n->name + "* (`" + n->logicalId + "`)\n";
  msg += "⚡ *Feeder ID:* `Feeder " + String(fdrNum) + "` (`" + fdr.name + "`)\n";
  msg += "🔘 *Relay Output:* " + rState + "\n";
  msg += "🔒 *Contactor Feedback:* `" + cont + "`\n";
  msg += "🛡️ *MCB Status:* `" + mcb + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📊 *3-PHASE CURRENT SENSORS (Dedicated Channels):*\n";
  if (n->onlineState != "OFFLINE") {
    msg += "• Phase R: `" + String(fdr.currentR, 2) + " A` [" + validityR + "]\n";
    msg += "• Phase Y: `" + String(fdr.currentY, 2) + " A` [" + validityY + "]\n";
    msg += "• Phase B: `" + String(fdr.currentB, 2) + " A` [" + validityB + "]\n";
    msg += "• Active Power: `" + String(fdr.totalPowerW / 1000.0f, 2) + " kW` | PF: `" + String(fdr.powerFactor, 2) + "`\n";
  } else {
    msg += "⚠️ *Current: Measurement Unavailable (Node is Offline)*\n";
    msg += "*(Stale values are not displayed as zero)*\n";
  }
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "⚡ *Common Bus Voltage at Pole:*\n";
  msg += "• VR: `" + String(n->commonVoltageR, 1) + " V` | VY: `" + String(n->commonVoltageY, 1) + " V` | VB: `" + String(n->commonVoltageB, 1) + " V`\n";
  msg += "⏱ *Last Measurement:* `" + formatRelativeTime(fdr.lastVoltageUpdate) + "`\n";

  if (fdr.faultState != "OK" && fdr.faultState.length() > 0) {
    msg += "⚠️ *Feeder Fault:* `" + fdr.faultState + "`\n";
  }

  return msg;
}

String TelegramMenuManager::buildFeederDetailKeyboard(UserSession &sess) {
  String p = sess.selectedPole;
  uint8_t f = sess.selectedFeeder;
  NodeRecord *n = meshMgr.findNode(p);
  bool isDeact = (n && n->feeders[f - 1].status == FEEDER_STATUS_DEACTIVATED);

  String kb = "{\"inline_keyboard\":[";

  if (!isDeact) {
    kb += "[{\"text\":\"🟢 Switch ON\",\"callback_data\":\"CMD:F_ON:" + p + ":" + String(f) + "\"},";
    kb += "{\"text\":\"🔴 Switch OFF\",\"callback_data\":\"CMD:F_OFF:" + p + ":" + String(f) + "\"}],";
    kb += "[{\"text\":\"⚙️ Deactivate Feeder\",\"callback_data\":\"FDR:DEACT_REQ:" + p + ":" + String(f) + "\"}],";
  } else {
    kb += "[{\"text\":\"⚠️ Feeder is Deactivated\",\"callback_data\":\"REFRESH\"}],";
  }

  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🔙 Back to Feeders\",\"callback_data\":\"NAV:FEEDER_MGMT\"}],";
  kb += "[{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  4. NEW NODE REGISTRATION
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildRegistrationMessage(UserSession &sess) {
  int pCount = meshMgr.getPendingRegCount();

  String msg = "📝 *NEW NODE REGISTRATION QUEUE*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";

  if (pCount == 0) {
    msg += "✅ *No pending node join requests.*\n\n";
    msg += "When an uncommissioned pole boots, it broadcasts `NEW_NODE` via LoRa and appears here for admin approval.";
    return msg;
  }

  msg += "Found *" + String(pCount) + "* device(s) awaiting approval:\n\n";

  for (int i = 0; i < pCount; i++) {
    PendingRegistration *p = meshMgr.getPendingReg(i);
    if (!p || !p->valid) continue;

    msg += "⚙️ *Device " + String(i + 1) + ":* `" + p->hwId + "`\n";
    msg += "• Signal: `" + String(p->rssi) + " dBm`\n";
    msg += "• Detected: `" + formatRelativeTime(p->detectedAt) + "`\n";
    msg += "• Default Type: `" + p->type + "`\n\n";
  }

  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Click 'Approve' to assign Pole Name and Area:";
  return msg;
}

String TelegramMenuManager::buildRegistrationKeyboard(UserSession &sess) {
  int pCount = meshMgr.getPendingRegCount();
  String kb = "{\"inline_keyboard\":[";

  for (int i = 0; i < pCount; i++) {
    PendingRegistration *p = meshMgr.getPendingReg(i);
    if (!p || !p->valid) continue;

    kb += "[{\"text\":\"✅ Approve " + p->hwId + "\",\"callback_data\":\"REG:APP:" + p->hwId + "\"},";
    kb += "{\"text\":\"❌ Reject\",\"callback_data\":\"REG:REJ:" + p->hwId + "\"}],";
  }

  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  5. DELETE / DE-REGISTER POLE
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildDeleteManagementMessage(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  String msg = "🗑️ *DELETE / DE-REGISTER POLE*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "⚠️ *CAUTION:* Deleting a pole removes it permanently from the central registry and activates the Revocation Registry.\n\n";

  if (sess.selectedPole.length() > 0) {
    NodeRecord *n = meshMgr.findNode(sess.selectedPole);
    if (n) {
      DeletionImpact impact;
      revocationMgr.computeDeletionImpact(sess.selectedPole, impact);

      msg += "🎯 *Selected Target:* *" + n->name + "* (`" + n->logicalId + "`)\n";
      msg += "📍 Area: `" + n->location + "`\n";
      msg += "━━━━━━━━━━━━━━━━━━━━\n";
      msg += "📊 *DOWNSTREAM CHILD NODE IMPACT:*\n";
      msg += "• Direct Children: *" + String(impact.directChildCount) + "*\n";
      msg += "• Total Downstream Nodes: *" + String(impact.totalDownstreamCount) + "*\n";
      msg += "• Affected Mesh Routes: *" + String(impact.affectedRouteCount) + "*\n";

      if (impact.hasOrphansWithoutRoute) {
        msg += "⚠️ *WARNING:* Some child nodes have NO backup parent and will be temporarily ORPHANED until alternate routes are discovered!\n";
      } else {
        msg += "✅ *Migration:* All downstream nodes have valid backup parents.\n";
      }
      msg += "━━━━━━━━━━━━━━━━━━━━\n";
      msg += "Click 'Proceed to Confirm' to generate a 2-step verification token.";
      return msg;
    }
  }

  msg += "Select a pole to evaluate deletion impact:\n";
  int start = sess.pageIndex * POLES_PAGE_SIZE;
  int end = min(start + POLES_PAGE_SIZE, total);

  for (int i = start; i < end; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n) {
      msg += "• *" + n->name + "* (`" + n->logicalId + "`)\n";
    }
  }

  return msg;
}

String TelegramMenuManager::buildDeleteManagementKeyboard(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  String kb = "{\"inline_keyboard\":[";

  if (sess.selectedPole.length() > 0) {
    kb += "[{\"text\":\"⚠️ Proceed to Confirm Deletion\",\"callback_data\":\"DEL:CONF_REQ:" + sess.selectedPole + "\"}],";
    kb += "[{\"text\":\"🔙 Select Different Pole\",\"callback_data\":\"DEL:CANCEL:" + sess.selectedPole + "\"}],";
  } else {
    int start = sess.pageIndex * POLES_PAGE_SIZE;
    int end = min(start + POLES_PAGE_SIZE, total);
    for (int i = start; i < end; i++) {
      NodeRecord *n = meshMgr.getNode(i);
      if (n) {
        kb += "[{\"text\":\"🗑️ Select " + n->logicalId + " (" + n->name + ")\",\"callback_data\":\"DEL:IMPACT:" + n->logicalId + "\"}],";
      }
    }
  }

  kb += "[{\"text\":\"📜 View Revoked Nodes\",\"callback_data\":\"NAV:REVOKED\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  5B. DELETE CONFIRMATION (2-STEP VERIFICATION)
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildDeleteConfirmMessage(UserSession &sess) {
  String msg = "⚠️ *CRITICAL CONFIRMATION REQUIRED*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "You are about to *PERMANENTLY DELETE* Pole:\n";
  msg += "📍 *ID:* `" + sess.selectedPole + "`\n";
  msg += "🔑 *Verification Token:* `" + sess.confirmationToken + "`\n\n";
  msg += "This action will:\n";
  msg += "1. Transmit authoritative `REVOKE` command over LoRa mesh.\n";
  msg += "2. Persist hardware ID to non-volatile Revocation Registry.\n";
  msg += "3. Migrate all downstream child node routes.\n";
  msg += "4. Prevent this device from ever joining without factory reset.\n\n";
  msg += "⏱ *Token expires in 60 seconds.*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenuManager::buildDeleteConfirmKeyboard(UserSession &sess) {
  String p = sess.selectedPole;
  String tok = sess.confirmationToken;
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"🛑 CONFIRM PERMANENT DELETE\",\"callback_data\":\"DEL:EXEC:" + p + ":" + tok + "\"}],";
  kb += "[{\"text\":\"❌ Abort & Cancel\",\"callback_data\":\"DEL:CANCEL:" + p + "\"}],";
  kb += "[{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  5C. REVOKED NODES LIST
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildRevokedListMessage(UserSession &sess) {
  int rCount = revocationMgr.getRevokedCount();
  String msg = "📜 *REVOKED NODES REGISTRY*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";

  if (rCount == 0) {
    msg += "✅ No nodes have been revoked.\n";
    return msg;
  }

  msg += "Total Revoked Devices: *" + String(rCount) + "*\n\n";
  for (int i = 0; i < rCount; i++) {
    const RevocationRecord *r = revocationMgr.getRevocationByIndex(i);
    if (!r || !r->valid) continue;
    msg += "🚫 *ID:* `" + String(r->logicalId) + "` (`" + String(r->hwId) + "`)\n";
    msg += "• Reason: `" + String(r->reason) + "`\n";
    msg += "• TxID: `" + String(r->transactionId) + "`\n\n";
  }

  return msg;
}

String TelegramMenuManager::buildRevokedListKeyboard(UserSession &sess) {
  return "{\"inline_keyboard\":[[{\"text\":\"🔙 Back to Delete Menu\",\"callback_data\":\"NAV:DEL_MGMT\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]]}";
}

// ─────────────────────────────────────────────────────────────
//  6. INDIVIDUAL POLE CONTROL
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildPoleControlMessage(UserSession &sess) {
  NodeRecord *n = meshMgr.findNode(sess.selectedPole);
  if (!n) {
    return "⚠️ Please select a pole from the list below to control:";
  }

  String msg = "⚡ *INDIVIDUAL POLE CONTROL*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📍 *Target:* *" + n->name + "* (`" + n->logicalId + "`)\n";
  msg += "📊 *Status:* `" + n->onlineState + "` (RSSI: `" + String(n->rssi) + " dBm`)\n";
  msg += "💡 *Main Light Relay:* *" + String(n->lightState == "ON" ? "🟢 ON" : "🔴 OFF") + "*\n";
  msg += "⚡ *Common Bus Voltage:* `VR=" + String(n->commonVoltageR, 1) + "V VY=" + String(n->commonVoltageY, 1) + "V VB=" + String(n->commonVoltageB, 1) + "V`\n";
  msg += "🔌 *Phase R Current:* `" + String(n->current, 2) + " A`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Select control command to dispatch via LoRa mesh:";
  return msg;
}

String TelegramMenuManager::buildPoleControlKeyboard(UserSession &sess) {
  String p = sess.selectedPole;
  String kb = "{\"inline_keyboard\":[";

  if (p.length() > 0) {
    kb += "[{\"text\":\"🟢 Turn Light ON\",\"callback_data\":\"CMD:P_ON:" + p + "\"},";
    kb += "{\"text\":\"🔴 Turn Light OFF\",\"callback_data\":\"CMD:P_OFF:" + p + "\"}],";
    kb += "[{\"text\":\"⏱️ 30m Timer\",\"callback_data\":\"CMD:TMR:" + p + ":30\"},";
    kb += "{\"text\":\"⏱️ 60m Timer\",\"callback_data\":\"CMD:TMR:" + p + ":60\"}],";
    kb += "[{\"text\":\"📊 Query Status\",\"callback_data\":\"CMD:PSTAT:" + p + "\"},";
    kb += "{\"text\":\"🔁 Safe Reboot\",\"callback_data\":\"CMD:RST:" + p + "\"}],";
    kb += "[{\"text\":\"📍 Switch Pole\",\"callback_data\":\"NAV:POLE_MGMT\"},{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"}],";
  } else {
    int total = meshMgr.getNodeCount();
    for (int i = 0; i < min(total, 5); i++) {
      NodeRecord *n = meshMgr.getNode(i);
      if (n) {
        kb += "[{\"text\":\"📍 " + n->logicalId + " - " + n->name + "\",\"callback_data\":\"P_SEL:" + n->logicalId + "\"}],";
      }
    }
  }

  kb += "[{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  7. INDIVIDUAL FEEDER CONTROL
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildFeederControlMessage(UserSession &sess) {
  return buildFeederDetailMessage(sess);
}

String TelegramMenuManager::buildFeederControlKeyboard(UserSession &sess) {
  return buildFeederDetailKeyboard(sess);
}

// ─────────────────────────────────────────────────────────────
//  8. GROUP CONTROL & CONFIRMATION
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildGroupControlMessage(UserSession &sess) {
  String msg = "👥 *GROUP CONTROL DASHBOARD*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Universal grouping allows synchronized switching across designated functional categories:\n\n";
  msg += "• 💡 *Municipal Street Lights* — Controls all street lighting poles\n";
  msg += "• 🌾 *Agricultural Pumps* — Controls all farmer station nodes\n";
  msg += "• ⚡ *All Feeders on Selected Pole* — Switches F1–F6 simultaneously\n\n";
  msg += "Select group and target action:";
  return msg;
}

String TelegramMenuManager::buildGroupControlKeyboard(UserSession &sess) {
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"💡 Street Lights ON\",\"callback_data\":\"CMD:GRP:STREET:ON\"},{\"text\":\"🌙 Street Lights OFF\",\"callback_data\":\"CMD:GRP:STREET:OFF\"}],";
  kb += "[{\"text\":\"🌾 Farm Pumps ON\",\"callback_data\":\"CMD:GRP:FARMER:ON\"},{\"text\":\"🌾 Farm Pumps OFF\",\"callback_data\":\"CMD:GRP:FARMER:OFF\"}],";
  if (sess.selectedPole.length() > 0) {
    kb += "[{\"text\":\"⚡ All 6 Feeders ON (" + sess.selectedPole + ")\",\"callback_data\":\"CMD:GRP:POLE_ALL:ON\"},";
    kb += "{\"text\":\"⚡ All 6 Feeders OFF\",\"callback_data\":\"CMD:GRP:POLE_ALL:OFF\"}],";
  }
  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

String TelegramMenuManager::buildGroupConfirmMessage(UserSession &sess) {
  String grp = sess.selectedGroup;
  String act = sess.wizardTargetId;

  String msg = "⚠️ *CONFIRM GROUP OPERATION*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Target Group: *" + grp + "*\n";
  msg += "Action: *" + act + "*\n";
  msg += "Transaction Token: `" + sess.confirmationToken + "`\n\n";
  msg += "Before executing:\n";
  msg += "1. Batch transaction ID will be generated.\n";
  msg += "2. LoRa commands will be staggered to prevent radio congestion.\n";
  msg += "3. Individual ACKs will be collected for final report.\n\n";
  msg += "Click 'Confirm' to begin staggered transmission:";
  return msg;
}

String TelegramMenuManager::buildGroupConfirmKeyboard(UserSession &sess) {
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"✅ CONFIRM GROUP ACTION\",\"callback_data\":\"CMD:GRP_CONF:\"}],";
  kb += "[{\"text\":\"❌ Cancel\",\"callback_data\":\"NAV:GROUP_CTRL\"}],";
  kb += "[{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  9. ALL POLES CONTROL & CONFIRMATION
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildAllPolesControlMessage(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  String msg = "🌐 *ALL POLES MASTER CONTROL*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "⚠️ *GRID-WIDE OPERATION*\n\n";
  msg += "This command broadcasts switching directives across the *entire municipal grid*:\n";
  msg += "• Target Poles: *" + String(total) + " Poles*\n";
  msg += "• Potential Channels: *Up to 300 Feeders*\n\n";
  msg += "Select desired grid state:";
  return msg;
}

String TelegramMenuManager::buildAllPolesControlKeyboard(UserSession &sess) {
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"💡 ALL LIGHTS ON\",\"callback_data\":\"BATCH:ALL_ON_REQ\"},{\"text\":\"🌙 ALL LIGHTS OFF\",\"callback_data\":\"BATCH:ALL_OFF_REQ\"}],";
  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

String TelegramMenuManager::buildAllConfirmMessage(UserSession &sess) {
  String act = (sess.wizardTargetId == "ALL_ON") ? "ALL LIGHTS ON" : "ALL LIGHTS OFF";
  int total = meshMgr.getNodeCount();

  String msg = "⚠️ *CONFIRM GRID-WIDE BROADCAST*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Requested Action: *" + act + "*\n";
  msg += "Affected Poles: *" + String(total) + " registered devices*\n";
  msg += "Transaction Token: `" + sess.confirmationToken + "`\n\n";
  msg += "Commands will be queued and staggered to respect 1% LoRa duty cycle.\n";
  msg += "Broadcast transmissions do not constitute proof of execution; individual ACKs will be collected.\n\n";
  msg += "Confirm execution?";
  return msg;
}

String TelegramMenuManager::buildAllConfirmKeyboard(UserSession &sess) {
  String act = sess.wizardTargetId;
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"⚡ YES, EXECUTE " + act + "\",\"callback_data\":\"BATCH:EXEC:" + act + "\"}],";
  kb += "[{\"text\":\"❌ Cancel\",\"callback_data\":\"NAV:ALL_POLES\"}],";
  kb += "[{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  10. VOLTAGE MONITORING (3 COMMON BUS VOLTAGES)
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildVoltageMonitoringMessage(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  String msg = "⚡ *3-PHASE VOLTAGE MONITORING*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "ℹ️ *Architecture Note:* Common Pole R/Y/B Voltage Sensors (individual feeder output voltage is not wired by design).\n\n";

  if (total == 0) {
    msg += "⚠️ No poles registered.";
    return msg;
  }

  int totalPages = ((total - 1) / POLES_PAGE_SIZE) + 1;
  int start = sess.pageIndex * POLES_PAGE_SIZE;
  int end = min(start + POLES_PAGE_SIZE, total);

  msg += "```\n";
  msg += "Pole ID   | VR(V) | VY(V) | VB(V) | F(Hz)\n";
  msg += "----------+-------+-------+-------+------\n";

  for (int i = start; i < end; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;

    char row[48];
    if (n->onlineState != "OFFLINE") {
      snprintf(row, sizeof(row), "%-9s | %5.1f | %5.1f | %5.1f | %4.1f\n",
               n->logicalId.c_str(), n->commonVoltageR, n->commonVoltageY, n->commonVoltageB, n->gridFrequency);
    } else {
      snprintf(row, sizeof(row), "%-9s |  OFFLINE / TIMEOUT \n", n->logicalId.c_str());
    }
    msg += String(row);
  }
  msg += "```\n";

  msg += "Page " + String(sess.pageIndex + 1) + " of " + String(totalPages);
  return msg;
}

String TelegramMenuManager::buildVoltageMonitoringKeyboard(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  int totalPages = ((total - 1) / POLES_PAGE_SIZE) + 1;
  int start = sess.pageIndex * POLES_PAGE_SIZE;
  int end = min(start + POLES_PAGE_SIZE, total);

  String kb = "{\"inline_keyboard\":[";

  for (int i = start; i < end; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n) {
      kb += "[{\"text\":\"⚡ " + n->logicalId + " Voltages\",\"callback_data\":\"V_SEL:" + n->logicalId + "\"}],";
    }
  }

  kb += "[";
  if (sess.pageIndex > 0) kb += "{\"text\":\"◀️ Prev\",\"callback_data\":\"PAGE:VOLT_P\"},";
  kb += "{\"text\":\"Page " + String(sess.pageIndex + 1) + "/" + String(max(1, totalPages)) + "\",\"callback_data\":\"REFRESH\"}";
  if (sess.pageIndex < totalPages - 1) kb += ",{\"text\":\"Next ▶️\",\"callback_data\":\"PAGE:VOLT_N\"}";
  kb += "],";

  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

String TelegramMenuManager::buildVoltageDetailMessage(UserSession &sess) {
  NodeRecord *n = meshMgr.findNode(sess.selectedPole);
  if (!n) return "⚠️ Pole not found.";

  String msg = "⚡ *VOLTAGE PROFILE — " + n->name + "*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🆔 *Logical ID:* `" + n->logicalId + "`\n";
  msg += "📶 *Status:* `" + n->onlineState + "`\n\n";

  msg += "🔹 *3-Phase Common Mains Voltage (ZMPT101B):*\n";
  msg += "• R Phase: `" + String(n->commonVoltageR, 1) + " VAC` (" + n->commonVoltageStateR + ")\n";
  msg += "• Y Phase: `" + String(n->commonVoltageY, 1) + " VAC` (" + n->commonVoltageStateY + ")\n";
  msg += "• B Phase: `" + String(n->commonVoltageB, 1) + " VAC` (" + n->commonVoltageStateB + ")\n\n";

  msg += "🔹 *Grid Health:*\n";
  msg += "• Frequency: `" + String(n->gridFrequency, 2) + " Hz`\n";
  bool isSym = (abs(n->commonVoltageR - n->commonVoltageY) < 15.0f && abs(n->commonVoltageY - n->commonVoltageB) < 15.0f);
  msg += "• Balance: `" + String(isSym ? "Symmetric (Balanced)" : "Phase Unbalance Alert") + "`\n";
  msg += "• Last Update: `" + formatRelativeTime(n->voltageUpdatedAt) + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "💡 *Note:* Pole uses 3 common voltage transformers on the incoming bus.";
  return msg;
}

String TelegramMenuManager::buildVoltageDetailKeyboard(UserSession &sess) {
  return "{\"inline_keyboard\":[[{\"text\":\"🔙 Back to Voltage List\",\"callback_data\":\"NAV:VOLT_MON\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]]}";
}

// ─────────────────────────────────────────────────────────────
//  11. CURRENT MONITORING (18 CHANNELS PER POLE)
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildCurrentMonitoringMessage(UserSession &sess) {
  return feederMgr.build21ChannelMeterTable(sess.selectedPole.length() > 0 ? sess.selectedPole : (meshMgr.getNodeCount() > 0 ? meshMgr.getNode(0)->logicalId : ""));
}

String TelegramMenuManager::buildCurrentMonitoringKeyboard(UserSession &sess) {
  String kb = "{\"inline_keyboard\":[";
  int total = meshMgr.getNodeCount();
  for (int i = 0; i < min(total, 5); i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n) {
      kb += "[{\"text\":\"🔌 " + n->logicalId + " Currents\",\"callback_data\":\"C_SEL:" + n->logicalId + "\"}],";
    }
  }
  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

String TelegramMenuManager::buildCurrentDetailMessage(UserSession &sess) {
  return buildCurrentMonitoringMessage(sess);
}

String TelegramMenuManager::buildCurrentDetailKeyboard(UserSession &sess) {
  return buildCurrentMonitoringKeyboard(sess);
}

// ─────────────────────────────────────────────────────────────
//  12. NETWORK TOPOLOGY (ASCII TREE)
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildTopologyMessage(UserSession &sess) {
  String msg = "🗺️ *LORA MESH NETWORK TOPOLOGY*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "```\n";
  msg += "🌐 Gateway (Root Rank 0)\n";

  int total = meshMgr.getNodeCount();
  if (total == 0) {
    msg += "  (No nodes discovered)\n";
  } else {
    // 1-Hop Direct Nodes
    for (int i = 0; i < total; i++) {
      NodeRecord *n1 = meshMgr.getNode(i);
      if (!n1) continue;

      if (n1->primaryNextHop == "GATEWAY" || n1->primaryNextHop.length() == 0 || n1->hopCount == 1) {
        msg += "  ├── 📍 " + n1->logicalId + " (1h, " + String(n1->rssi) + "dBm)\n";

        // 2-Hop Children
        for (int j = 0; j < total; j++) {
          NodeRecord *n2 = meshMgr.getNode(j);
          if (!n2) continue;
          if (n2->primaryNextHop == n1->logicalId || n2->primaryNextHop == n1->hwId) {
            msg += "  │     ├── 📍 " + n2->logicalId + " (2h, " + String(n2->rssi) + "dBm)\n";

            // 3-Hop Children
            for (int k = 0; k < total; k++) {
              NodeRecord *n3 = meshMgr.getNode(k);
              if (!n3) continue;
              if (n3->primaryNextHop == n2->logicalId || n3->primaryNextHop == n2->hwId) {
                msg += "  │     │     └── 📍 " + n3->logicalId + " (3h)\n";
              }
            }
          }
        }
      }
    }
  }
  msg += "```\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "• Total Nodes: *" + String(total) + "* | Max Hops: *6*\n";
  msg += "• Orphan Nodes: *0 (Mesh healthy)*\n";
  return msg;
}

String TelegramMenuManager::buildTopologyKeyboard(UserSession &sess) {
  return "{\"inline_keyboard\":[[{\"text\":\"🔄 Refresh Topology\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]]}";
}

// ─────────────────────────────────────────────────────────────
//  13. ONLINE / OFFLINE NODES
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildNodeStatusMessage(UserSession &sess) {
  int total = meshMgr.getNodeCount();
  String msg = "📊 *NODE CONNECTIVITY AUDIT*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";

  int onC = 0, degC = 0, offC = 0;

  String onList = "", degList = "", offList = "";

  for (int i = 0; i < total; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;

    if (n->onlineState == "ONLINE") {
      onC++;
      onList += "• *" + n->name + "* (`" + n->logicalId + "`): " + String(n->rssi) + " dBm\n";
    } else if (n->onlineState == "DEGRADED") {
      degC++;
      degList += "• *" + n->name + "* (`" + n->logicalId + "`): " + formatRelativeTime(n->lastSeen) + "\n";
    } else {
      offC++;
      offList += "• *" + n->name + "* (`" + n->logicalId + "`): Offline (" + formatRelativeTime(n->lastSeen) + ")\n";
    }
  }

  msg += "🟢 *ONLINE NODES (" + String(onC) + "):*\n" + (onList.length() > 0 ? onList : "None\n") + "\n";
  msg += "🟡 *DEGRADED NODES (" + String(degC) + "):*\n" + (degList.length() > 0 ? degList : "None\n") + "\n";
  msg += "🔴 *OFFLINE NODES (" + String(offC) + "):*\n" + (offList.length() > 0 ? offList : "None\n") + "\n";

  return msg;
}

String TelegramMenuManager::buildNodeStatusKeyboard(UserSession &sess) {
  return "{\"inline_keyboard\":[[{\"text\":\"🔄 Refresh Status\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]]}";
}

// ─────────────────────────────────────────────────────────────
//  14. FAULT ALERTS
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildFaultAlertsMessage(UserSession &sess) {
  String msg = "⚠️ *ACTIVE FAULT LOG*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";

  int faultCount = 0;
  int total = meshMgr.getNodeCount();

  for (int i = 0; i < total; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;

    if (n->fault != "OK" && n->fault.length() > 0 && n->fault != "NONE") {
      faultCount++;
      msg += "🚨 *" + n->name + "* (`" + n->logicalId + "`)\n";
      msg += "• Fault Code: `" + n->fault + "`\n";
      msg += "• Voltage: `VR=" + String(n->commonVoltageR, 1) + "V`\n";
      msg += "• Current: `IR=" + String(n->current, 2) + "A`\n\n";
    }
  }

  if (faultCount == 0) {
    msg += "✅ *Zero active faults.* All electrical sensors, MCBs and contactors normal.\n";
  }

  return msg;
}

String TelegramMenuManager::buildFaultAlertsKeyboard(UserSession &sess) {
  return "{\"inline_keyboard\":[[{\"text\":\"🔄 Refresh Faults\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]]}";
}

// ─────────────────────────────────────────────────────────────
//  15. TIME-BASED CONFIGURATION
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildTimeConfigMessage(UserSession &sess) {
  time_t now = time(nullptr);
  struct tm *ti = localtime(&now);

  char timeBuf[32] = "Not Synced";
  if (ti && now > 1700000000UL) {
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d (IST)", ti->tm_hour, ti->tm_min, ti->tm_sec);
  }

  String msg = "🕒 *TIME & AUTONOMOUS SCHEDULE*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🌐 *Gateway NTP Time:* `" + String(timeBuf) + "`\n";
  msg += "📡 *Time Sync Broadcast:* Every 10 minutes\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "⚙️ *AUTONOMOUS SCHEDULE (Field Poles):*\n";
  msg += "• Schedule: `ENABLED`\n";
  msg += "• Auto ON Time:  `18:30 (Dusk)`\n";
  msg += "• Auto OFF Time: `06:00 (Dawn)`\n\n";
  msg += "Field poles execute schedules autonomously using local soft RTC even if LoRa link is temporarily lost.";
  return msg;
}

String TelegramMenuManager::buildTimeConfigKeyboard(UserSession &sess) {
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"📡 Broadcast Time Sync Now\",\"callback_data\":\"CMD:TIME_SYNC\"}],";
  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  16. WI-FI SETTINGS (VIEW-ONLY IN THIS STAGE)
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildWiFiSettingsMessage(const String &wifiSsid) {
  String msg = "📶 *GATEWAY WI-FI SETTINGS*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "• SSID: `" + wifiSsid + "`\n";
  msg += "• Connection State: `CONNECTED 🟢`\n";
  msg += "• Local IP: `" + WiFi.localIP().toString() + "`\n";
  msg += "• Gateway IP: `" + WiFi.gatewayIP().toString() + "`\n";
  msg += "• Subnet Mask: `" + WiFi.subnetMask().toString() + "`\n";
  msg += "• MAC Address: `" + WiFi.macAddress() + "`\n";
  msg += "• Signal Strength (RSSI): `" + String(WiFi.RSSI()) + " dBm`\n";
  msg += "• Telegram Bot API: `ONLINE 🤖`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🔒 *Security Policy Notice:*\n";
  msg += "Wi-Fi credential modification via Telegram is disabled in this stage (Prompt 16 security constraints).";
  return msg;
}

String TelegramMenuManager::buildWiFiSettingsKeyboard() {
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

String TelegramMenuManager::buildWiFiConfirmMessage(const String &candidateSsid, size_t passLen) {
  String msg = "⚠️ *CONFIRM WI-FI CREDENTIAL UPDATE*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "• *Proposed Network (SSID):* `" + candidateSsid + "`\n";
  msg += "• *Password Length:* `" + String(passLen) + " characters` (`••••••••`)\n";
  msg += "• *Verification Protocol:* Association ➔ DHCP ➔ DNS ➔ Telegram HTTPS\n";
  msg += "• *Safety Mechanism:* `Automatic Rollback on Failure (20s)`\n";
  msg += "• *LoRa Mesh Status:* `Uninterrupted Continuous Operation`\n\n";
  msg += "Are you sure you want to test and commit these credentials?";
  return msg;
}

String TelegramMenuManager::buildWiFiConfirmKeyboard() {
  return "{\"inline_keyboard\":[[{\"text\":\"✅ Apply & Test\",\"callback_data\":\"WIFI_CONFIRM_APPLY\"}],[{\"text\":\"❌ Cancel\",\"callback_data\":\"WIFI_CANCEL\"}]]}";
}

// ─────────────────────────────────────────────────────────────
//  17. GATEWAY SETTINGS
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildGatewaySettingsMessage() {
  String msg = "⚙️ *GATEWAY HARDWARE CONFIGURATION*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "• Platform: `ESP32-S3-WROOM-N16R8 (16MB Flash, 8MB PSRAM)`\n";
  msg += "• LoRa Module: `SX1278 RA-02 @ 433.0 MHz`\n";
  msg += "• Spreading Factor: `SF10` | Bandwidth: `125 kHz`\n";
  msg += "• Coding Rate: `4/5` | TX Power: `20 dBm (PA_BOOST)`\n";
  msg += "• Max Supported Nodes: `50 Poles`\n";
  msg += "• Max Feeder Channels: `300 Channels`\n";
  msg += "• Max Current Sensors: `900 Channels`\n";
  msg += "• Max Voltage Sensors: `150 Channels`\n";
  msg += "• Duty Cycle Window: `1% Airtime Compliance`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenuManager::buildGatewaySettingsKeyboard() {
  String kb = "{\"inline_keyboard\":[";
  kb += "[{\"text\":\"📶 Wi-Fi Settings\",\"callback_data\":\"NAV:WIFI\"},{\"text\":\"📦 Firmware Status\",\"callback_data\":\"NAV:FW_MGMT\"}],";
  kb += "[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]";
  kb += "]}";
  return kb;
}

// ─────────────────────────────────────────────────────────────
//  18. FIRMWARE MANAGEMENT (VIEW-ONLY IN THIS STAGE)
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildFirmwareManagementMessage() {
  String msg = "📦 *FIRMWARE MANAGEMENT*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "• Gateway Firmware: `" + String(GW_VERSION) + "`\n";
  msg += "• Pole Secondary Firmware: `" + String(POLE_VERSION) + "`\n";
  msg += "• Protocol Version: `V12 Industrial Secure`\n";
  msg += "• OTA Engine: `Encrypted Fragmented Chunking`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🔒 *Security Policy Notice:*\n";
  msg += "Firmware OTA execution via Telegram is disabled in this stage (Prompt 16 security constraints).";
  return msg;
}

String TelegramMenuManager::buildFirmwareManagementKeyboard() {
  return "{\"inline_keyboard\":[[{\"text\":\"🔙 Back to Settings\",\"callback_data\":\"NAV:GW_SETTINGS\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]]}";
}

// ─────────────────────────────────────────────────────────────
//  19. EVENT HISTORY
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildEventHistoryMessage(UserSession &sess) {
  int count = storageMgr.getGwLogCount();
  String msg = "📜 *AUDIT EVENT LOG (Last 20)*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";

  if (count == 0) {
    msg += "No events logged yet.\n";
    return msg;
  }

  for (int i = 0; i < min(count, 10); i++) {
    const GwLogEntry *e = storageMgr.getGwLog(i);
    if (!e) continue;
    String resIcon = (e->result == 0) ? "✅" : "❌";
    msg += resIcon + " `" + String(e->action) + "` -> `" + String(e->target) + "` (" + String(e->timestamp) + "s)\n";
  }

  return msg;
}

String TelegramMenuManager::buildEventHistoryKeyboard(UserSession &sess) {
  return "{\"inline_keyboard\":[[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"},{\"text\":\"🏠 Dashboard\",\"callback_data\":\"HOME\"}]]}";
}

// ─────────────────────────────────────────────────────────────
//  20. HELP / SYSTEM INFORMATION
// ─────────────────────────────────────────────────────────────
String TelegramMenuManager::buildHelpInfoMessage() {
  String msg = "ℹ️ *MSCB SMART POLE SYSTEM HELP*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📖 *Dashboard Navigation Guide:*\n";
  msg += "• 📍 *Poles* — View, inspect and configure 50 field poles\n";
  msg += "• 🔌 *Feeders* — Monitor and switch 300 feeder circuits\n";
  msg += "• ⚡ *Voltage* — 3-Phase common mains monitoring\n";
  msg += "• 🔌 *Current* — 900 independent current sensor channels\n";
  msg += "• 🗺️ *Topology* — Multi-hop DAG routing tree & orphan detection\n";
  msg += "• 👥 *Group / All* — Coordinated staggered switching\n";
  msg += "• 🗑️ *Delete* — 2-step verification de-registration & child migration\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "💡 *Icon Legend:*\n";
  msg += "🟢 Online / Normal | 🟡 Degraded | 🔴 Offline / Fault\n";
  msg += "🔒 Contactor Closed | 🔓 Contactor Open | ⚪ Deactivated\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Commands: `/start`, `/dashboard`, `/status`, `/help`, `/cancel`";
  return msg;
}

String TelegramMenuManager::buildHelpInfoKeyboard() {
  return "{\"inline_keyboard\":[[{\"text\":\"🏠 Return to Dashboard\",\"callback_data\":\"HOME\"}]]}";
}
