/*
 * =====================================================================
 *  TELEGRAMMENUS.CPP — TELEGRAM DASHBOARD UI & KEYBOARD BUILDERS
 * =====================================================================
 */

#include "TelegramMenus.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "FeederManager.h"
#include "TelegramManager.h"
#include <WiFi.h>

TelegramMenus tgMenus;

TelegramMenus::TelegramMenus() {}

String TelegramMenus::formatUptime(unsigned long ms) {
  unsigned long sec = ms / 1000;
  int h = sec / 3600;
  int m = (sec % 3600) / 60;
  int s = sec % 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, s);
  return String(buf);
}

// ─────────────────────────────────────────────────────────────
//  MESSAGE BUILDERS
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildHomeMessage(const String &wifiSsid) {
  String lightSummary = "OFF 🌙";
  String pumpSummary  = "OFF 🔴";
  int count = meshMgr.getNodeCount();
  
  for (int i = 0; i < count; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n) {
      if (n->type == "FARMER" && n->lightState == "ON") pumpSummary = "RUNNING 🟢";
      if (n->type != "FARMER" && n->lightState == "ON") lightSummary = "ON 💡";
    }
  }

  String msg = "💡 *SMART STREET LIGHT & FARMER MESH*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🟢 *Gateway:* `ONLINE`\n";
  msg += "📡 *LoRa Mesh:* `READY (433MHz SF10)`\n";
  msg += "📶 *WiFi:* `CONNECTED` (`" + wifiSsid + "`)\n";
  msg += "🌐 *IP:* `" + WiFi.localIP().toString() + "`\n";
  msg += "⏱ *Uptime:* `" + formatUptime(millis()) + "`\n";
  msg += "💡 *Street Lights:* *" + lightSummary + "*\n";
  msg += "🌾 *Farm Pumps:* *" + pumpSummary + "*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Select an action below:";
  return msg;
}

String TelegramMenus::buildSettingsMessage() {
  String msg = "⚙️ *SETTINGS & CONTROLS*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Universal Field Network Control\n\n";
  msg += "• 💡 *Street Lights* — Municipal lighting nodes\n";
  msg += "• 🌾 *Farmer Nodes* — Agriculture pumps & stations\n";
  msg += "• 📊 *Feeder Status* — Live F1–F6 multi-phase state\n";
  msg += "• 🔁 *Reset Controls* — Safe device restarts\n";
  msg += "• 📝 *Registration* — Node provisioning\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenus::buildStatusMessage(const String &wifiSsid) {
  String lastPkt = loraMgr.getLastPacketInfo();

  String msg = "📊 *GATEWAY STATUS*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🟢 *Gateway:* `ONLINE`\n";
  msg += "📡 *LoRa:* `READY (433.0 MHz SF10)`\n";
  msg += "📶 *WiFi:* `CONNECTED` (`" + wifiSsid + "`)\n";
  msg += "🌐 *IP:* `" + WiFi.localIP().toString() + "`\n";
  msg += "📶 *WiFi Signal:* `" + String(WiFi.RSSI()) + " dBm`\n";
  msg += "⏱ *Uptime:* `" + formatUptime(millis()) + "`\n";
  msg += "🕒 *Time:* `" + tgMgr.getFormattedTime() + "`\n";
  msg += "📨 *Telegram:* `ONLINE`\n";
  msg += "📡 *Last LoRa:* `" + lastPkt + "`\n";
  msg += "💾 *Free Heap:* `" + String(ESP.getFreeHeap()) + " bytes`\n";
  msg += "💡 *Street Lights in DB:* `" + String(meshMgr.getNodeCountByType("STREET")) + "`\n";
  msg += "🌾 *Farmer Nodes in DB:* `" + String(meshMgr.getNodeCountByType("FARMER")) + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String TelegramMenus::buildNeighboursMessage(UserSession &sess) {
  String msg = "💡 *STREET LIGHT NODES*\n━━━━━━━━━━━━━━━━━━━━\n";
  int total = meshMgr.getNodeCountByType("STREET");
  if (total == 0) {
    msg += "No street light poles registered yet.\n";
  } else {
    int start = sess.pageIndex * POLES_PER_PAGE;
    int end = min(start + POLES_PER_PAGE, total);
    for (int i = start; i < end; i++) {
      NodeRecord *node = meshMgr.getNodeByTypeIndex("STREET", i);
      if (!node) continue;
      String icon = (node->onlineState == "ONLINE") ? "🟢" : ((node->onlineState == "DEGRADED") ? "🟡" : "🔴");
      msg += "📍 *" + node->name + "* (`" + node->logicalId + "`)\n";
      msg += "   " + icon + " | 💡 " + node->lightState + " | RSSI: `" + String(node->rssi) + " dBm`\n";
      if (node->onlineState != "OFFLINE") {
        if (node->voltage > 0) {
          msg += "   ⚡ `⚡ " + String(node->voltage, 1) + " V`\n";
        } else {
          msg += "   ⚡ `⚡ 0.0 V`\n";
        }
      } else {
        msg += "   ⚡ `Offline`\n";
      }
    }
    msg += "\nPage " + String((start / POLES_PER_PAGE) + 1) + " of " + String(((total - 1) / POLES_PER_PAGE) + 1);
  }
  return msg;
}

String TelegramMenus::buildFarmerListMessage(UserSession &sess) {
  String msg = "🌾 *FARMER PUMP STATIONS*\n━━━━━━━━━━━━━━━━━━━━\n";
  int total = meshMgr.getNodeCountByType("FARMER");
  if (total == 0) {
    msg += "No farmer pump stations registered yet.\n";
  } else {
    int start = sess.pageIndex * POLES_PER_PAGE;
    int end = min(start + POLES_PER_PAGE, total);
    for (int i = start; i < end; i++) {
      NodeRecord *node = meshMgr.getNodeByTypeIndex("FARMER", i);
      if (!node) continue;
      String icon = (node->onlineState == "ONLINE") ? "🟢" : ((node->onlineState == "DEGRADED") ? "🟡" : "🔴");
      String pumpState = (node->lightState == "ON") ? "🟢 RUNNING" : "🔴 OFF";
      msg += "🌾 *" + node->name + "* (`" + node->logicalId + "`)\n";
      msg += "   " + icon + " | ⚡ " + pumpState + " | RSSI: `" + String(node->rssi) + " dBm`\n";
      if (node->onlineState != "OFFLINE") {
        String vStr = (node->voltage > 0) ? (String(node->voltage, 1) + " V") : "0.0 V";
        msg += "   ⚡ `" + vStr + "` | 🔌 `" + String(node->current, 2) + " A`\n";
      } else {
        msg += "   ⚡ `Offline`\n";
      }
    }
    msg += "\nPage " + String((start / POLES_PER_PAGE) + 1) + " of " + String(((total - 1) / POLES_PER_PAGE) + 1);
  }
  return msg;
}

String TelegramMenus::buildPoleNeighboursMessage(UserSession &sess) {
  NodeRecord *node = meshMgr.findNode(sess.selectedPole);
  String pName = node ? node->name : sess.selectedPole;

  String msg = "📡 *NEIGHBOURS OF " + pName + "*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";

  int count = meshMgr.getNeighborSummaryCount();
  bool found = false;

  for (int i = 0; i < count; i++) {
    NeighborSummary *nb = meshMgr.getNeighborSummary(i);
    if (nb && nb->valid && (nb->reportingNode == sess.selectedPole || (node && nb->reportingNode == node->hwId))) {
      NodeRecord *targetNode = meshMgr.findNode(nb->neighborId);
      String icon = (targetNode && targetNode->type == "FARMER") ? "🌾" : "💡";
      String tName = targetNode ? targetNode->name : nb->neighborId;
      String stIcon = nb->responded ? "🟢" : "🔴";
      msg += icon + " *" + tName + "* (`" + nb->neighborId + "`)\n";
      msg += "   " + stIcon + " RSSI: `" + String(nb->rssi) + " dBm`\n\n";
      found = true;
    }
  }

  if (!found) {
    msg += "No direct mesh neighbours currently reported by this node.\n";
  }
  return msg;
}

String TelegramMenus::buildPoleCtrlMessage(UserSession &sess) {
  NodeRecord *node = meshMgr.findNode(sess.selectedPole);
  if (!node) return "⚠️ Pole not found.";
  
  String msg = "💡 *POLE CONTROL PANEL*\n━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🆔 *ID:* `" + node->logicalId + "`\n";
  msg += "📍 *Name:* *" + node->name + "*\n";
  msg += "🏢 *Loc:* `" + node->location + "`\n";
  String statusIcon = (node->onlineState == "ONLINE") ? "🟢" : ((node->onlineState == "DEGRADED") ? "🟡" : "🔴");
  msg += statusIcon + " *Status:* " + node->onlineState + "\n";
  msg += "📶 *RSSI:* `" + String(node->rssi) + " dBm`\n";
  msg += "🗺️ *Next Hop:* `" + node->primaryNextHop + "`\n";
  msg += "💡 *Light Relay:* *" + String(node->lightState == "ON" ? "🟢 ON" : "🔴 OFF") + "*\n";

  if (node->onlineState != "OFFLINE") {
    if (node->voltage > 0) {
      msg += "⚡ *Voltage (ZMPT101B):* `⚡ " + String(node->voltage, 1) + " V`\n";
    } else {
      msg += "⚡ *Voltage (ZMPT101B):* `⚡ 0.0 V (Mains OFF)`\n";
    }
  } else {
    msg += "⚡ *Voltage:* `⚠️ Node Offline`\n";
  }
  msg += "🔌 *Current:* `" + String(node->current > 0 ? node->current : 0.00, 2) + " A`\n";
  msg += "💡 *Power:* `" + String(node->power > 0 ? node->power : 0.0, 1) + " W`\n";
  return msg;
}

String TelegramMenus::buildFarmerCtrlMessage(UserSession &sess) {
  NodeRecord *node = meshMgr.findNode(sess.selectedPole);
  if (!node) return "⚠️ Farmer station not found.";
  
  String msg = "🌾 *FARMER CONTROL PANEL*\n━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🆔 *Farmer ID:* `" + node->logicalId + "`\n";
  msg += "📍 *Name:* *" + node->name + "*\n";
  msg += "🏢 *Location:* `" + node->location + "`\n";
  String statusIcon = (node->onlineState == "ONLINE") ? "🟢" : ((node->onlineState == "DEGRADED") ? "🟡" : "🔴");
  msg += statusIcon + " *Status:* " + node->onlineState + "\n";
  msg += "⏱ *Uptime:* `" + formatUptime((unsigned long)node->uptimeSec * 1000) + "`\n";
  msg += "📶 *RSSI:* `" + String(node->rssi) + " dBm`\n";
  msg += "🗺️ *Route:* `Gateway → " + node->primaryNextHop + "`\n";
  msg += "⚡ *Pump State:* *" + String(node->lightState == "ON" ? "🟢 RUNNING" : "🔴 OFF") + "*\n";

  if (node->onlineState != "OFFLINE") {
    if (node->voltage > 0) {
      msg += "⚡ *Voltage (ZMPT101B):* `⚡ " + String(node->voltage, 1) + " V`\n";
    } else {
      msg += "⚡ *Voltage (ZMPT101B):* `⚡ 0.0 V (Mains OFF)`\n";
    }
  } else {
    msg += "⚡ *Voltage:* `⚠️ Station Offline`\n";
  }
  msg += "🔌 *Motor Current:* `" + String(node->current > 0 ? node->current : 0.00, 2) + " A`\n";
  msg += "💡 *Active Power:* `" + String(node->power > 0 ? node->power : 0.0, 1) + " W`\n";
  return msg;
}

String TelegramMenus::buildTimerSelectMessage(UserSession &sess) {
  NodeRecord *node = meshMgr.findNode(sess.selectedPole);
  String pName = node ? node->name : sess.selectedPole;
  String devType = (node && node->type == "FARMER") ? "Pump" : "Light";

  String msg = "⏱ *AUTONOMOUS COUNTDOWN TIMER*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "Target: *" + pName + "* (`" + sess.selectedPole + "`)\n\n";
  msg += "Select running duration. The " + devType + " will turn ON immediately and turn OFF automatically when the timer finishes.\n\n";
  msg += "💡 *Note:* The timer runs locally on the node even if internet drops.";
  return msg;
}

String TelegramMenus::buildScheduleMessage(UserSession &sess) {
  NodeRecord *node = meshMgr.findNode(sess.selectedPole);
  String pName = node ? node->name : sess.selectedPole;

  String msg = "⏰ *AUTONOMOUS LOCAL SCHEDULE*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "Target: *" + pName + "* (`" + sess.selectedPole + "`)\n\n";
  if (node && node->schedEnabled && node->schedOnMin >= 0 && node->schedOffMin >= 0) {
    char buf[48];
    snprintf(buf, sizeof(buf), "🟢 Active: ON at %02d:%02d | OFF at %02d:%02d",
             node->schedOnMin / 60, node->schedOnMin % 60,
             node->schedOffMin / 60, node->schedOffMin % 60);
    msg += String(buf) + "\n\n";
  } else {
    msg += "⚠️ No schedule currently enabled.\n\n";
  }
  msg += "Select an action below:";
  return msg;
}

String TelegramMenus::buildRegistrationMessage(UserSession &sess) {
  String msg = "📝 *PENDING REGISTRATIONS*\n━━━━━━━━━━━━━━━━━━━━\n";
  bool found = false;
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    PendingRegistration *p = meshMgr.getPendingReg(i);
    if (p && p->valid && p->state == REG_DETECTED) {
      msg += "⚙️ *HW ID:* `" + p->hwId + "`\n";
      msg += "📶 *RSSI:* `" + String(p->rssi) + " dBm`\n\n";
      found = true;
    }
  }
  if (!found) msg += "No pending detections.\n";
  return msg;
}

String TelegramMenus::buildRegTypeSelectMessage(const String &hwId) {
  String msg = "🆕 *SELECT NODE TYPE*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "Hardware ID: `" + hwId + "`\n\n";
  msg += "Please select the application type for this field node:";
  return msg;
}

String TelegramMenus::buildResetSelectMessage() {
  String msg = "🔁 *SYSTEM RESET CONTROLS*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Select target device to reboot:\n\n";
  msg += "• 🔁 *Reset Node* — Reboot a specific pole or pump station\n";
  msg += "• ⚠️ *Reset All* — Restart all field nodes\n\n";
  msg += "💡 *Safety:* Registration and configuration will remain safe.";
  return msg;
}

String TelegramMenus::buildResetConfirmPoleMessage(const String &poleId) {
  NodeRecord *node = meshMgr.findNode(poleId);
  String pName = node ? node->name : poleId;
  String msg = "⚠️ *CONFIRM DEVICE REBOOT*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "Target: *" + pName + "* (`" + poleId + "`)\n\n";
  msg += "Are you sure you want to reboot this device?\n";
  msg += "Registration & persistent configuration will be preserved.";
  return msg;
}

String TelegramMenus::buildResetConfirmNeighMessage(const String &originPoleId, const String &neighId) {
  String msg = "⚠️ *CONFIRM NEIGHBOUR REBOOT*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "Neighbour: `" + neighId + "`\n";
  msg += "Via Route: `" + originPoleId + "`\n\n";
  msg += "Are you sure you want to reboot this neighbour?";
  return msg;
}

String TelegramMenus::buildResetConfirmAllMessage() {
  String msg = "⚠️ *RESET ALL NODES?*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n\n";
  msg += "This will transmit a restart command to **ALL reachable field nodes** (" + String(meshMgr.getNodeCount()) + " nodes).\n\n";
  msg += "Are you sure you want to proceed?";
  return msg;
}

// ─────────────────────────────────────────────────────────────
//  KEYBOARD BUILDERS
// ─────────────────────────────────────────────────────────────
String TelegramMenus::buildHomeKeyboard() {
  return "[[{\"text\":\"💡 ALL LIGHTS ON\",\"callback_data\":\"ALL_ON\"}],"
         "[{\"text\":\"🌙 ALL LIGHTS OFF\",\"callback_data\":\"ALL_OFF\"}],"
         "[{\"text\":\"⚙️ SETTINGS & NODES\",\"callback_data\":\"SETTINGS\"}]]";
}

String TelegramMenus::buildSettingsKeyboard() {
  return "[[{\"text\":\"💡 Street Lights\",\"callback_data\":\"NEIGH\"},"
          "{\"text\":\"🌾 Farmer Nodes\",\"callback_data\":\"FARM_LIST\"}],"
         "[{\"text\":\"📊 Feeder Status\",\"callback_data\":\"FEEDER_STAT\"},"
          "{\"text\":\"🔁 Reset Controls\",\"callback_data\":\"RESET_SEL\"}],"
         "[{\"text\":\"📝 Registration\",\"callback_data\":\"REGLIST\"},"
          "{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"}],"
         "[{\"text\":\"⬅️ Back\",\"callback_data\":\"HOME\"},"
          "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildStatusKeyboard() {
  return "[[{\"text\":\"🔄 Refresh\",\"callback_data\":\"REFRESH\"}],"
         "[{\"text\":\"⬅️ Back\",\"callback_data\":\"SETTINGS\"},"
          "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildFeederStatusKeyboard() {
  return "[[{\"text\":\"🔄 Refresh\",\"callback_data\":\"FEEDER_STAT\"}],"
         "[{\"text\":\"⬅️ Back\",\"callback_data\":\"SETTINGS\"},"
          "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildNeighboursKeyboard(UserSession &sess) {
  String kb = "[";
  int total = meshMgr.getNodeCountByType("STREET");
  int start = sess.pageIndex * POLES_PER_PAGE;
  int end = min(start + POLES_PER_PAGE, total);
  
  for (int i = start; i < end; i++) {
    NodeRecord *node = meshMgr.getNodeByTypeIndex("STREET", i);
    if (node) {
      kb += "[{\"text\":\"📍 " + node->name + "\",\"callback_data\":\"P:" + node->logicalId + "\"}],";
    }
  }
  
  String nav = "[";
  if (sess.pageIndex > 0) nav += "{\"text\":\"◀️ Prev\",\"callback_data\":\"PG:P:" + String(sess.pageIndex) + "\"},";
  if (end < total) nav += "{\"text\":\"Next ▶️\",\"callback_data\":\"PG:N:" + String(sess.pageIndex) + "\"},";
  if (nav.endsWith(",")) nav = nav.substring(0, nav.length() - 1);
  nav += "]";
  
  if (nav != "[]") kb += nav + ",";
  kb += "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"NEIGH\"}],"
        "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"SETTINGS\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

String TelegramMenus::buildFarmerListKeyboard(UserSession &sess) {
  String kb = "[";
  int total = meshMgr.getNodeCountByType("FARMER");
  int start = sess.pageIndex * POLES_PER_PAGE;
  int end = min(start + POLES_PER_PAGE, total);
  
  for (int i = start; i < end; i++) {
    NodeRecord *node = meshMgr.getNodeByTypeIndex("FARMER", i);
    if (node) {
      kb += "[{\"text\":\"🌾 " + node->name + "\",\"callback_data\":\"P:" + node->logicalId + "\"}],";
    }
  }
  
  String nav = "[";
  if (sess.pageIndex > 0) nav += "{\"text\":\"◀️ Prev\",\"callback_data\":\"FPG:P:" + String(sess.pageIndex) + "\"},";
  if (end < total) nav += "{\"text\":\"Next ▶️\",\"callback_data\":\"FPG:N:" + String(sess.pageIndex) + "\"},";
  if (nav.endsWith(",")) nav = nav.substring(0, nav.length() - 1);
  nav += "]";
  
  if (nav != "[]") kb += nav + ",";
  kb += "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"FARM_LIST\"}],"
        "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"SETTINGS\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

String TelegramMenus::buildPoleNeighboursKeyboard(UserSession &sess) {
  NodeRecord *node = meshMgr.findNode(sess.selectedPole);
  String kb = "[";
  int count = meshMgr.getNeighborSummaryCount();

  for (int i = 0; i < count; i++) {
    NeighborSummary *nb = meshMgr.getNeighborSummary(i);
    if (nb && nb->valid && (nb->reportingNode == sess.selectedPole || (node && nb->reportingNode == node->hwId))) {
      NodeRecord *targetNode = meshMgr.findNode(nb->neighborId);
      String icon = (targetNode && targetNode->type == "FARMER") ? "🌾" : "💡";
      String tName = targetNode ? targetNode->name : nb->neighborId;
      kb += "[{\"text\":\"" + icon + " " + tName + "\",\"callback_data\":\"P:" + nb->neighborId + "\"}],";
    }
  }

  kb += "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"PNEIGH:" + sess.selectedPole + "\"}],"
        "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"P:" + sess.selectedPole + "\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

String TelegramMenus::buildPoleCtrlKeyboard(UserSession &sess) {
  String pid = sess.selectedPole;
  return "[[{\"text\":\"💡 ON\",\"callback_data\":\"PON:" + pid + "\"},"
          "{\"text\":\"🌙 OFF\",\"callback_data\":\"POFF:" + pid + "\"}],"
         "[{\"text\":\"📊 STATUS\",\"callback_data\":\"PSTAT:" + pid + "\"},"
          "{\"text\":\"⏱ TIMER\",\"callback_data\":\"TMR_SEL:" + pid + "\"}],"
         "[{\"text\":\"📡 NEIGHBOURS\",\"callback_data\":\"PNEIGH:" + pid + "\"},"
          "{\"text\":\"🔁 RESET\",\"callback_data\":\"RST_P:" + pid + "\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"NEIGH\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildFarmerCtrlKeyboard(UserSession &sess) {
  String pid = sess.selectedPole;
  return "[[{\"text\":\"🟢 PUMP ON\",\"callback_data\":\"FARM_ON:" + pid + "\"},"
          "{\"text\":\"🔴 PUMP OFF\",\"callback_data\":\"FARM_OFF:" + pid + "\"}],"
         "[{\"text\":\"📊 STATUS\",\"callback_data\":\"PSTAT:" + pid + "\"},"
          "{\"text\":\"⏱ TIMER\",\"callback_data\":\"TMR_SEL:" + pid + "\"}],"
         "[{\"text\":\"📡 NEIGHBOURS\",\"callback_data\":\"PNEIGH:" + pid + "\"},"
          "{\"text\":\"🔁 RESET\",\"callback_data\":\"RST_P:" + pid + "\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"FARM_LIST\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
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

String TelegramMenus::buildScheduleKeyboard(UserSession &sess) {
  String pid = sess.selectedPole;
  return "[[{\"text\":\"🌅 Morning (6:00 - 10:00)\",\"callback_data\":\"SCHED_SET:" + pid + ":360:600\"}],"
         "[{\"text\":\"🌇 Evening (18:00 - 22:00)\",\"callback_data\":\"SCHED_SET:" + pid + ":1080:1320\"}],"
         "[{\"text\":\"🚫 Disable Schedule\",\"callback_data\":\"SCHED_DIS:" + pid + "\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"P:" + pid + "\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
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
        "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"SETTINGS\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

String TelegramMenus::buildRegTypeSelectKeyboard(const String &hwId) {
  return "[[{\"text\":\"💡 STREET LIGHT\",\"callback_data\":\"REG_T:SL:" + hwId + "\"}],"
         "[{\"text\":\"🌾 FARMER PUMP\",\"callback_data\":\"REG_T:FR:" + hwId + "\"}],"
         "[{\"text\":\"❌ CANCEL\",\"callback_data\":\"REGLIST\"}]]";
}

String TelegramMenus::buildResetSelectKeyboard() {
  return "[[{\"text\":\"🔁 Reset Single Node\",\"callback_data\":\"RST_LST\"}],"
         "[{\"text\":\"⚠️ Reset All Nodes\",\"callback_data\":\"RST_ALL_CONF\"}],"
         "[{\"text\":\"⬅️ Back\",\"callback_data\":\"SETTINGS\"},"
          "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
}

String TelegramMenus::buildResetPoleListKeyboard() {
  String kb = "[";
  int count = meshMgr.getNodeCount();
  for (int i = 0; i < count; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (n) {
      String icon = (n->type == "FARMER") ? "🌾" : "💡";
      kb += "[{\"text\":\"" + icon + " Reset " + n->name + " (" + n->logicalId + ")\",\"callback_data\":\"RST_P:" + n->logicalId + "\"}],";
    }
  }
  kb += "[{\"text\":\"⬅️ Back\",\"callback_data\":\"RESET_SEL\"},"
        "{\"text\":\"🏠 Home\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

String TelegramMenus::buildResetConfirmPoleKeyboard(const String &poleId) {
  return "[[{\"text\":\"✅ Confirm Reset\",\"callback_data\":\"RST_DO_P:" + poleId + "\"},"
          "{\"text\":\"❌ Cancel\",\"callback_data\":\"RESET_SEL\"}]]";
}

String TelegramMenus::buildResetConfirmNeighKeyboard(const String &originId, const String &neighId) {
  return "[[{\"text\":\"✅ Confirm Reset\",\"callback_data\":\"RST_DO_N:" + originId + ":" + neighId + "\"},"
          "{\"text\":\"❌ Cancel\",\"callback_data\":\"RESET_SEL\"}]]";
}

String TelegramMenus::buildResetConfirmAllKeyboard() {
  return "[[{\"text\":\"✅ CONFIRM RESET ALL\",\"callback_data\":\"RST_DO_ALL\"}],"
         "[{\"text\":\"❌ CANCEL\",\"callback_data\":\"RESET_SEL\"}]]";
}
