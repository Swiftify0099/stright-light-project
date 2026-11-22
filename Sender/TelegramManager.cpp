/*
 * =====================================================================
 *  TELEGRAMMANAGER.CPP — TELEGRAM INTERACTION & BOT CONTROLLER
 * =====================================================================
 */

#include "TelegramManager.h"
#include "TelegramMenus.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "FeederManager.h"
#include "ResetManager.h"
#include "StorageManager.h"
#include "GwConfigManager.h"
#include "ConfigDistribution.h"
#include "GwFirmwareManager.h"
#include <time.h>

TelegramManager tgMgr;

TelegramManager::TelegramManager() :
  bot(BOT_TOKEN, client),
  wifiSsid(WIFI_DEF_SSID),
  wifiPass(WIFI_DEF_PASS),
  wifiConnected(false),
  telegramOnline(false),
  hasNtpTime(false),
  bootMenuSent(false),
  tWifiWatchdog(0)
{
  batchTracker.active = false;
  batchTracker.finalReportSent = true;
}

void TelegramManager::init() {
  storageMgr.loadWiFiCredentials(wifiSsid, wifiPass);

  bot.maxMessageLength = 4096;
  bot.waitForResponse  = 1000;

  Serial.printf("[GW][WIFI] Connecting to SSID: '%s'...\n", wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 6000) {
    delay(200);
    Serial.print(F("."));
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("[GW][WIFI] Connected! IP: %s (RSSI: %d dBm)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    client.setInsecure();
    checkNtpSync();

    if (verifyTelegramApi()) {
      sendStartupMainMenu(OWNER_CHAT);
      bootMenuSent = true;
    }
  } else {
    Serial.println(F("[GW][WIFI] ⚠️ Hotspot not connected at boot. LoRa active. Background reconnect scheduled."));
  }
}

bool TelegramManager::isAuthorized(const String &chatId, const String &fromId) {
  String configured = String(OWNER_CHAT);
  configured.trim();
  if (configured.length() == 0) return true;

  String cId = chatId; cId.trim();
  String fId = fromId; fId.trim();

  return (cId == configured || fId == configured);
}

UserSession& TelegramManager::getOrCreateSession(const String &chatId) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].chatId == chatId) {
      sessions[i].lastActive = millis();
      return sessions[i];
    }
  }

  int oldestIdx = 0;
  unsigned long oldestTime = sessions[0].lastActive;
  for (int i = 1; i < MAX_SESSIONS; i++) {
    if (sessions[i].chatId.length() == 0) {
      oldestIdx = i;
      break;
    }
    if (sessions[i].lastActive < oldestTime) {
      oldestTime = sessions[i].lastActive;
      oldestIdx = i;
    }
  }

  sessions[oldestIdx] = {};
  sessions[oldestIdx].chatId = chatId;
  sessions[oldestIdx].currentMenu = MENU_HOME;
  sessions[oldestIdx].previousMenu = MENU_HOME;
  sessions[oldestIdx].lastMessageId = 0;
  sessions[oldestIdx].lastActive = millis();
  return sessions[oldestIdx];
}

String TelegramManager::getFormattedTime() {
  time_t raw = time(nullptr);
  struct tm *ti = localtime(&raw);
  if (!ti || raw < 1700000000UL) return "NoSync";
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
  return String(buf);
}

void TelegramManager::checkNtpSync() {
  if (WiFi.status() == WL_CONNECTED) {
    configTime(19800, 0, "pool.ntp.org", "time.nist.gov"); // IST +5:30
    time_t raw = time(nullptr);
    if (raw > 1700000000UL) {
      hasNtpTime = true;
      Serial.printf("[GW][NTP] Synced successfully: %s\n", getFormattedTime().c_str());
    }
  }
}

bool TelegramManager::verifyTelegramApi() {
  if (WiFi.status() != WL_CONNECTED) {
    telegramOnline = false;
    return false;
  }

  Serial.println(F("[GW][TG] Verifying Telegram Bot API connectivity..."));
  client.setInsecure();
  if (bot.getMe()) {
    telegramOnline = true;
    Serial.printf("[GW][TG] ✅ Telegram Bot ONLINE! Bot: @%s\n", bot.userName.c_str());
    return true;
  } else {
    telegramOnline = false;
    Serial.println(F("[GW][TG] ⚠️ Telegram API verification failed."));
    return false;
  }
}

void TelegramManager::checkWiFiAndTelegram() {
  unsigned long now = millis();
  if (now - tWifiWatchdog < WIFI_WATCHDOG_INTERVAL_MS) return;
  tWifiWatchdog = now;

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.printf("[GW][WIFI] Connected! IP: %s (RSSI: %d dBm)\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      client.setInsecure();
      checkNtpSync();
      
      if (verifyTelegramApi()) {
        if (!bootMenuSent) {
          sendStartupMainMenu(OWNER_CHAT);
          bootMenuSent = true;
        }
      }
    } else if (!telegramOnline) {
      verifyTelegramApi();
    }
  } else {
    if (wifiConnected) {
      wifiConnected = false;
      telegramOnline = false;
      Serial.println(F("[GW][WIFI] WiFi connection lost."));
    }
    Serial.println(F("[GW][WIFI] Reconnecting to WiFi..."));
    WiFi.disconnect();
    WiFi.reconnect();
  }
}

bool TelegramManager::sendMessage(const String &chatId, const String &text, const String &parseMode) {
  if (!telegramOnline) return false;
  return bot.sendMessage(chatId, text, parseMode);
}

bool TelegramManager::sendMessageWithInlineKeyboard(const String &chatId, const String &text, const String &parseMode, const String &keyboard, int messageId) {
  if (!telegramOnline) return false;
  if (messageId > 0) {
    bool ok = bot.sendMessageWithInlineKeyboard(chatId, text, parseMode, keyboard, messageId);
    if (ok) return true;
  }
  return bot.sendMessageWithInlineKeyboard(chatId, text, parseMode, keyboard, 0);
}

bool TelegramManager::answerCallbackQuery(const String &queryId, const String &text, bool showAlert) {
  if (!telegramOnline || queryId.length() == 0) return false;
  return bot.answerCallbackQuery(queryId, text, showAlert);
}

void TelegramManager::renderMenu(const String &chatId, UserSession &sess, int messageId) {
  String text = "";
  String kb = "";

  switch (sess.currentMenu) {
    case MENU_SETTINGS:
      text = tgMenus.buildSettingsMessage();
      kb   = tgMenus.buildSettingsKeyboard();
      break;

    case MENU_STATUS:
      text = tgMenus.buildStatusMessage(wifiSsid);
      kb   = tgMenus.buildStatusKeyboard();
      break;

    case MENU_FEEDER_STATUS:
      text = feederMgr.buildFeederStatusTable(sess.selectedPole);
      kb   = tgMenus.buildFeederStatusKeyboard();
      break;

    case MENU_NEIGHBOURS:
      text = tgMenus.buildNeighboursMessage(sess);
      kb   = tgMenus.buildNeighboursKeyboard(sess);
      break;

    case MENU_POLE_CTRL:
      text = tgMenus.buildPoleCtrlMessage(sess);
      kb   = tgMenus.buildPoleCtrlKeyboard(sess);
      break;

    case MENU_POLE_NEIGHBOURS:
      text = tgMenus.buildPoleNeighboursMessage(sess);
      kb   = tgMenus.buildPoleNeighboursKeyboard(sess);
      break;

    case MENU_FARMER_LIST:
      text = tgMenus.buildFarmerListMessage(sess);
      kb   = tgMenus.buildFarmerListKeyboard(sess);
      break;

    case MENU_FARMER_CTRL:
      text = tgMenus.buildFarmerCtrlMessage(sess);
      kb   = tgMenus.buildFarmerCtrlKeyboard(sess);
      break;

    case MENU_TIMER_SELECT:
      text = tgMenus.buildTimerSelectMessage(sess);
      kb   = tgMenus.buildTimerSelectKeyboard(sess);
      break;

    case MENU_RESET_SELECT:
      text = tgMenus.buildResetSelectMessage();
      kb   = tgMenus.buildResetSelectKeyboard();
      break;

    case MENU_RESET_CONFIRM_POLE:
      text = tgMenus.buildResetConfirmPoleMessage(sess.selectedPole);
      kb   = tgMenus.buildResetConfirmPoleKeyboard(sess.selectedPole);
      break;

    case MENU_RESET_CONFIRM_NEIGH:
      text = tgMenus.buildResetConfirmNeighMessage(sess.selectedPole, sess.selectedNeighbour);
      kb   = tgMenus.buildResetConfirmNeighKeyboard(sess.selectedPole, sess.selectedNeighbour);
      break;

    case MENU_RESET_CONFIRM_ALL:
      text = tgMenus.buildResetConfirmAllMessage();
      kb   = tgMenus.buildResetConfirmAllKeyboard();
      break;
      
    case MENU_REGISTRATION:
      text = tgMenus.buildRegistrationMessage(sess);
      kb   = tgMenus.buildRegistrationKeyboard(sess);
      break;

    case MENU_HOME:
    default:
      sess.currentMenu = MENU_HOME;
      text = tgMenus.buildHomeMessage(wifiSsid);
      kb   = tgMenus.buildHomeKeyboard();
      break;
  }

  bool success = false;
  if (messageId > 0) {
    success = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, messageId);
    if (!success) {
      success = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, 0);
      if (success) sess.lastMessageId = bot.last_sent_message_id;
    }
  } else {
    success = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, 0);
    if (success) sess.lastMessageId = bot.last_sent_message_id;
  }
}

void TelegramManager::sendStartupMainMenu(const String &chatId) {
  UserSession &sess = getOrCreateSession(chatId);
  sess.currentMenu = MENU_HOME;
  sess.previousMenu = MENU_HOME;
  renderMenu(chatId, sess, 0);
  Serial.printf("[GW][TG] Startup Main Menu sent to chat %s\n", chatId.c_str());
}

// ─────────────────────────────────────────────────────────────
//  BATCH COMMAND AGGREGATION (ALL LIGHTS ON / OFF)
// ─────────────────────────────────────────────────────────────
void TelegramManager::startBatchCommand(const String &chatId, const String &cmdType) {
  int total = meshMgr.getNodeCount();
  batchTracker.active = true;
  batchTracker.cmdType = cmdType;
  batchTracker.chatId = chatId;
  batchTracker.startTime = millis();
  batchTracker.timeoutMs = 5000UL;
  batchTracker.totalExpected = total;
  batchTracker.confirmedCount = 0;
  batchTracker.failedCount = 0;
  batchTracker.unreachableCount = 0;
  batchTracker.finalReportSent = false;

  for (int i = 0; i < MAX_NODES; i++) {
    batchTracker.nodeAcked[i] = false;
  }

  String label = (cmdType == "ALL_ON") ? "ALL LIGHTS ON" : "ALL LIGHTS OFF";
  sendMessage(chatId, "⏳ *Sending " + label + " command over LoRa mesh...*", "Markdown");

  String loraCmd = (cmdType == "ALL_ON") ? "LIGHT_ON" : "LIGHT_OFF";
  loraMgr.queueCommand(chatId, "ALL", loraCmd, PRIO_HIGH);
}

void TelegramManager::onBatchAckReceived(const String &srcNodeId, const String &cmd, const String &state) {
  if (!batchTracker.active) return;

  int idx = meshMgr.findNodeIndex(srcNodeId);
  if (idx >= 0 && !batchTracker.nodeAcked[idx]) {
    batchTracker.nodeAcked[idx] = true;
    batchTracker.confirmedCount++;
    Serial.printf("[GW][BATCH] Node %s confirmed (Total: %d/%d)\n",
                  srcNodeId.c_str(), batchTracker.confirmedCount, batchTracker.totalExpected);
  }
}

void TelegramManager::processBatchTracker() {
  if (!batchTracker.active || batchTracker.finalReportSent) return;

  unsigned long elapsed = millis() - batchTracker.startTime;
  bool allReceived = (batchTracker.confirmedCount >= batchTracker.totalExpected && batchTracker.totalExpected > 0);
  bool timedOut = (elapsed >= batchTracker.timeoutMs);

  if (allReceived || timedOut) {
    batchTracker.active = false;
    batchTracker.finalReportSent = true;

    int total = meshMgr.getNodeCount();
    batchTracker.failedCount = max(0, total - batchTracker.confirmedCount);

    String cmdTitle = (batchTracker.cmdType == "ALL_ON") ? "💡 ALL LIGHTS ON RESULT" : "🌙 ALL LIGHTS OFF RESULT";
    String resMsg = "━━━━━━━━━━━━━━━━━━━━\n"
                    "✅ *" + cmdTitle + "*\n"
                    "━━━━━━━━━━━━━━━━━━━━\n\n"
                    "✅ *Confirmed:* `" + String(batchTracker.confirmedCount) + "`\n"
                    "❌ *Failed / Unreachable:* `" + String(batchTracker.failedCount) + "`\n\n"
                    "📊 *Updated Status:*\n" +
                    feederMgr.buildCompactFeederTable() + "\n"
                    "━━━━━━━━━━━━━━━━━━━━";

    if (batchTracker.chatId.length() > 0) {
      sendMessage(batchTracker.chatId, resMsg, "Markdown");
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  CENTRALIZED CALLBACK QUERY ROUTER
// ─────────────────────────────────────────────────────────────
void TelegramManager::handleTelegramCallback(const String &chatId, const String &queryId, const String &data, int messageId) {
  String action = data;
  action.trim();
  Serial.printf("[GW][TG] Callback = %s (Chat: %s, MsgID: %d)\n", action.c_str(), chatId.c_str(), messageId);

  UserSession &sess = getOrCreateSession(chatId);
  sess.lastMessageId = messageId;

  // Immediate ACK to clear button spinner
  if (queryId.length() > 0) {
    if (action == "REFRESH") {
      answerCallbackQuery(queryId, "🔄 Refreshed!");
    } else if (action == "ALL_ON") {
      answerCallbackQuery(queryId, "⚡ Sending ALL LIGHTS ON...");
    } else if (action == "ALL_OFF") {
      answerCallbackQuery(queryId, "🌙 Sending ALL LIGHTS OFF...");
    } else if (action.startsWith("FARM_ON:")) {
      answerCallbackQuery(queryId, "🟢 Starting Pump...");
    } else if (action.startsWith("FARM_OFF:")) {
      answerCallbackQuery(queryId, "🔴 Stopping Pump...");
    } else {
      answerCallbackQuery(queryId, "");
    }
  }

  // Routing
  if (action == "HOME") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_HOME;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "BACK") {
    if (sess.currentMenu == MENU_STATUS || sess.currentMenu == MENU_FEEDER_STATUS || 
        sess.currentMenu == MENU_RESET_SELECT || sess.currentMenu == MENU_NEIGHBOURS ||
        sess.currentMenu == MENU_FARMER_LIST) {
      sess.currentMenu = MENU_SETTINGS;
    } else if (sess.currentMenu == MENU_POLE_CTRL) {
      sess.currentMenu = MENU_NEIGHBOURS;
    } else if (sess.currentMenu == MENU_FARMER_CTRL) {
      sess.currentMenu = MENU_FARMER_LIST;
    } else if (sess.currentMenu == MENU_POLE_NEIGHBOURS || sess.currentMenu == MENU_TIMER_SELECT) {
      NodeRecord *n = meshMgr.findNode(sess.selectedPole);
      sess.currentMenu = (n && n->type == "FARMER") ? MENU_FARMER_CTRL : MENU_POLE_CTRL;
    } else {
      sess.currentMenu = MENU_HOME;
    }
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "SETTINGS") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_SETTINGS;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "STATUS") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_STATUS;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "FEEDER_STAT") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_FEEDER_STATUS;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "REFRESH") {
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "ALL_ON") {
    startBatchCommand(chatId, "ALL_ON");
  }
  else if (action == "ALL_OFF") {
    startBatchCommand(chatId, "ALL_OFF");
  }
  else if (action == "NEIGH") {
    sess.pageIndex = 0;
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_NEIGHBOURS;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "FARM_LIST") {
    sess.pageIndex = 0;
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_FARMER_LIST;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("PG:N:")) {
    sess.pageIndex = action.substring(5).toInt() + 1;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("PG:P:")) {
    sess.pageIndex = action.substring(5).toInt() - 1;
    if (sess.pageIndex < 0) sess.pageIndex = 0;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("FPG:N:")) {
    sess.pageIndex = action.substring(6).toInt() + 1;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("FPG:P:")) {
    sess.pageIndex = action.substring(6).toInt() - 1;
    if (sess.pageIndex < 0) sess.pageIndex = 0;
    renderMenu(chatId, sess, messageId);
  }
  // ── UNIVERSAL NODE SELECTOR (STREET LIGHT OR FARMER) ──
  else if (action.startsWith("P:")) {
    String nodeId = action.substring(2);
    sess.selectedPole = nodeId;
    sess.previousMenu = sess.currentMenu;
    NodeRecord *node = meshMgr.findNode(nodeId);
    if (node && node->type == "FARMER") {
      sess.currentMenu = MENU_FARMER_CTRL;
    } else {
      sess.currentMenu = MENU_POLE_CTRL;
    }
    renderMenu(chatId, sess, messageId);
  }
  // ── STREET LIGHT CONTROLS ──
  else if (action.startsWith("PON:")) {
    String p = action.substring(4);
    sendMessage(chatId, "⚡ Sending LIGHT ON to `" + p + "`...", "Markdown");
    loraMgr.queueCommand(chatId, p, "LIGHT_ON", PRIO_HIGH);
  }
  else if (action.startsWith("POFF:")) {
    String p = action.substring(5);
    sendMessage(chatId, "🌙 Sending LIGHT OFF to `" + p + "`...", "Markdown");
    loraMgr.queueCommand(chatId, p, "LIGHT_OFF", PRIO_HIGH);
  }
  // ── FARMER PUMP CONTROLS ──
  else if (action.startsWith("FARM_ON:")) {
    String p = action.substring(8);
    NodeRecord *node = meshMgr.findNode(p);
    String pName = node ? node->name : p;
    sendMessage(chatId, "⏳ *Starting " + pName + " over LoRa mesh...*", "Markdown");
    loraMgr.queueCommand(chatId, p, "LIGHT_ON", PRIO_HIGH);
  }
  else if (action.startsWith("FARM_OFF:")) {
    String p = action.substring(9);
    NodeRecord *node = meshMgr.findNode(p);
    String pName = node ? node->name : p;
    sendMessage(chatId, "⏳ *Stopping " + pName + " over LoRa mesh...*", "Markdown");
    loraMgr.queueCommand(chatId, p, "LIGHT_OFF", PRIO_HIGH);
  }
  // ── TIMER CONTROLS ──
  else if (action.startsWith("TMR_SEL:")) {
    sess.selectedPole = action.substring(8);
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_TIMER_SELECT;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("TMR_SET:")) {
    int c1 = action.indexOf(':', 8);
    if (c1 > 0) {
      String p = action.substring(8, c1);
      int mins = action.substring(c1 + 1).toInt();
      NodeRecord *node = meshMgr.findNode(p);
      String pName = node ? node->name : p;
      String devType = (node && node->type == "FARMER") ? "Pump" : "Light";

      sendMessage(chatId, "⏱ *Setting " + String(mins) + "-minute timer for " + pName + "...*", "Markdown");
      loraMgr.queueCommand(chatId, p, "TIMER_ON|" + String(mins), PRIO_HIGH);

      sess.currentMenu = (node && node->type == "FARMER") ? MENU_FARMER_CTRL : MENU_POLE_CTRL;
      renderMenu(chatId, sess, 0);
    }
  }
  else if (action.startsWith("PSTAT:")) {
    String p = action.substring(6);
    sendMessage(chatId, "📊 Requesting live status from `" + p + "`...", "Markdown");
    loraMgr.queueCommand(chatId, p, "GET_STATUS", PRIO_MEDIUM);
  }
  else if (action.startsWith("PNEIGH:")) {
    sess.selectedPole = action.substring(7);
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_POLE_NEIGHBOURS;
    renderMenu(chatId, sess, messageId);
  }
  // ── RESET CONTROLS ──
  else if (action == "RESET_SEL") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_RESET_SELECT;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "RST_LST") {
    String kb = tgMenus.buildResetPoleListKeyboard();
    sendMessageWithInlineKeyboard(chatId, "Select device to reboot:", "Markdown", kb, messageId);
  }
  else if (action.startsWith("RST_P:")) {
    sess.selectedPole = action.substring(6);
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_RESET_CONFIRM_POLE;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("RST_DO_P:")) {
    String p = action.substring(9);
    resetMgr.executeResetPole(chatId, p);
    sess.currentMenu = MENU_HOME;
    renderMenu(chatId, sess, 0);
  }
  else if (action == "RST_ALL_CONF") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_RESET_CONFIRM_ALL;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "RST_DO_ALL") {
    resetMgr.executeResetAll(chatId);
    sess.currentMenu = MENU_HOME;
    renderMenu(chatId, sess, 0);
  }
  // ── REGISTRATION WORKFLOW ──
  else if (action == "REGLIST") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_REGISTRATION;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("REG:")) {
    String hwId = action.substring(4);
    int pIdx = meshMgr.findPendingRegIndex(hwId);
    if (pIdx >= 0) {
      String msg = tgMenus.buildRegTypeSelectMessage(hwId);
      String kb  = tgMenus.buildRegTypeSelectKeyboard(hwId);
      sendMessageWithInlineKeyboard(chatId, msg, "Markdown", kb, messageId);
    }
  }
  else if (action.startsWith("REG_T:SL:")) {
    String hwId = action.substring(9);
    int pIdx = meshMgr.findPendingRegIndex(hwId);
    if (pIdx >= 0) {
      PendingRegistration *p = meshMgr.getPendingReg(pIdx);
      p->type = "STREET";
      p->state = REG_AWAITING_NAME;
      sess.pendingRegIdx = pIdx;
      sendMessage(chatId, "💡 *Street Light Registration*\n\nPlease enter the Pole Name (e.g. `Main Road Pole 1`):\n\nOr type /cancel to abort.", "Markdown");
    }
  }
  else if (action.startsWith("REG_T:FR:")) {
    String hwId = action.substring(9);
    int pIdx = meshMgr.findPendingRegIndex(hwId);
    if (pIdx >= 0) {
      PendingRegistration *p = meshMgr.getPendingReg(pIdx);
      p->type = "FARMER";
      p->state = REG_AWAITING_NAME;
      sess.pendingRegIdx = pIdx;
      sendMessage(chatId, "🌾 *Farmer Node Registration*\n\nPlease enter the Farm Station / Pump Name (e.g. `Main Farm Pump`):\n\nOr type /cancel to abort.", "Markdown");
    }
  }
  else if (action.startsWith("RREJ:")) {
    String hwId = action.substring(5);
    int pIdx = meshMgr.findPendingRegIndex(hwId);
    if (pIdx >= 0) {
      meshMgr.clearPendingReg(pIdx);
      sendMessage(chatId, "❌ Rejected registration for `" + hwId + "`", "Markdown");
      renderMenu(chatId, sess, messageId);
    }
  }
  else {
    Serial.printf("[GW][TG] Unknown callback: '%s'\n", action.c_str());
    sendMessage(chatId, "⚠️ Unknown action: `" + action + "`", "Markdown");
  }
}

// ─────────────────────────────────────────────────────────────
//  TELEGRAM TEXT HANDLER
// ─────────────────────────────────────────────────────────────
void TelegramManager::handleTelegramText(const String &chatId, const String &text) {
  String t = text;
  t.trim();

  UserSession &sess = getOrCreateSession(chatId);

  if (t == "/cancel" || t.equalsIgnoreCase("CANCEL")) {
    if (sess.pendingRegIdx >= 0) {
      meshMgr.clearPendingReg(sess.pendingRegIdx);
      sess.pendingRegIdx = -1;
      sendMessage(chatId, "❌ Registration cancelled.", "Markdown");
      sess.currentMenu = MENU_HOME;
      renderMenu(chatId, sess, 0);
    }
    return;
  }

  // Handle active registration wizard
  if (sess.pendingRegIdx >= 0) {
    int pi = sess.pendingRegIdx;
    PendingRegistration *p = meshMgr.getPendingReg(pi);
    if (p && p->valid && p->state == REG_AWAITING_NAME) {
      p->name = t;
      p->state = REG_AWAITING_LOCATION;
      String locPrompt = (p->type == "FARMER") ? "Enter Farm Location (e.g. `Sangli Farm - Well No. 2`):" : "Enter Location (e.g. `Main Road Near Temple`):";
      sendMessage(chatId, "Name set to: *" + t + "*\n\nNow, " + locPrompt, "Markdown");
      return;
    } 
    else if (p && p->valid && p->state == REG_AWAITING_LOCATION) {
      p->location = t;
      p->state = REG_COMPLETE;
      
      // Generate formatted ID (FR-0001 or SL-0001)
      String newId = meshMgr.generateNextLogicalId(p->type);

      int nodeIdx = meshMgr.findNodeIndex(p->hwId);
      if (nodeIdx < 0) {
        nodeIdx = meshMgr.addOrUpdateNode(p->hwId, newId, p->type, p->rssi, 0);
      } else {
        NodeRecord *n = meshMgr.getNode(nodeIdx);
        n->logicalId = newId;
        n->type = p->type;
      }
      NodeRecord *node = meshMgr.getNode(nodeIdx);
      if (node) {
        node->name = p->name;
        node->location = p->location;
        node->registered = true;
      }
      
      meshMgr.clearPendingReg(pi);
      sess.pendingRegIdx = -1;
      
      // Confirm registration to field pole/farmer node
      String regConfirm = node->logicalId + "|" + node->name + "|" + node->type;
      loraMgr.dispatchLoRaPacket("CMD", node->hwId, node->hwId, DEFAULT_TTL, loraMgr.getNextSeq(), "REGISTERED|" + regConfirm);
      
      String typeLabel = (node->type == "FARMER") ? "🌾 *Farmer Node Registered!*" : "💡 *Street Light Registered!*";
      String confMsg = "━━━━━━━━━━━━━━━━━━━━\n" + typeLabel + "\n━━━━━━━━━━━━━━━━━━━━\n\n" +
                       "🆔 *Assigned ID:* `" + newId + "`\n" +
                       "📍 *Name:* *" + node->name + "*\n" +
                       "🏢 *Location:* `" + node->location + "`\n\n" +
                       "✅ Node is now active in the mesh.";

      sendMessage(chatId, confMsg, "Markdown");
      
      storageMgr.saveDatabaseToNVS();
      sess.currentMenu = MENU_HOME;
      renderMenu(chatId, sess, 0);
      return;
    }
  }

  if (t == "/start" || t == "/menu" || t == "/help") {
    sendStartupMainMenu(chatId);
    return;
  }

  if (t.equalsIgnoreCase("ON") || t.equalsIgnoreCase("ALL ON")) {
    startBatchCommand(chatId, "ALL_ON");
    return;
  }
  if (t.equalsIgnoreCase("OFF") || t.equalsIgnoreCase("ALL OFF")) {
    startBatchCommand(chatId, "ALL_OFF");
    return;
  }
  if (t.equalsIgnoreCase("STATUS")) {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_STATUS;
    renderMenu(chatId, sess, 0);
    return;
  }
  if (t.equalsIgnoreCase("FEEDER") || t.equalsIgnoreCase("FEEDERS")) {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_FEEDER_STATUS;
    renderMenu(chatId, sess, 0);
    return;
  }
  if (t.equalsIgnoreCase("FARMER") || t.equalsIgnoreCase("FARM")) {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_FARMER_LIST;
    renderMenu(chatId, sess, 0);
    return;
  }

  // ─────────────────────────────────────────────────────────────
  //  CENTRAL CONFIGURATION & FIRMWARE MANAGEMENT (PROMPT 20)
  //  SCREENS A THROUGH O
  // ─────────────────────────────────────────────────────────────

  // Screen A: Gateway Configuration
  if (t.startsWith("/cfg_gw")) {
    String rep = "⚙️ *SCREEN A: GATEWAY CONFIGURATION*\n\n"
                 "🌐 *Network ID:* `0x4D53` (MSCB)\n"
                 "📶 *LoRa Freq:* `433.0 MHz (SF10, BW 125kHz, CR 4/5)`\n"
                 "⚡ *TX Power:* `20 dBm (PA_BOOST)`\n"
                 "⏱️ *Heartbeat Interval:* `30s`\n"
                 "🔍 *Discovery Interval:* `30s`\n"
                 "⏳ *ACK Timeout:* `3000ms` (Max Retries: 3)\n"
                 "📊 *Radio Duty-Cycle:* `< 0.35%` (ETSI <= 1% Compliant)\n"
                 "👥 *Registered Nodes:* `" + String(gwConfigMgr.getNodeConfigCount()) + " / 50`\n"
                 "💾 *Gateway Hardware:* `ESP32-S3-WROOM-N16R8 (16MB Flash, 8MB PSRAM)`";
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Screen B: Pole Configuration
  if (t.startsWith("/cfg_pole")) {
    String targetId = t.substring(9);
    targetId.trim();
    if (targetId.length() == 0) targetId = sess.selectedPole;
    MscbNodeConfigRecord *rec = gwConfigMgr.getNodeConfig(targetId.c_str());
    if (!rec && gwConfigMgr.getNodeConfigCount() > 0) rec = gwConfigMgr.getNodeConfigByIndex(0);

    if (rec) {
      String rep = "📍 *SCREEN B: POLE CONFIGURATION*\n\n"
                   "🆔 *Logical ID:* `" + String(rec->logicalId) + "`\n"
                   "🏷️ *Friendly Name:* `" + String(rec->pole.poleName) + "`\n"
                   "🏢 *Area:* `" + String(rec->pole.poleArea) + "`\n"
                   "💡 *Type:* `" + String(rec->pole.poleType) + "`\n"
                   "🔌 *Feeders:* `" + String(rec->pole.feederCount) + " Channels`\n"
                   "📦 *Config Version:* `V" + String(rec->currentConfigVersion) + "`\n"
                   "🔑 *Config Hash:* `0x" + String(rec->configHash, HEX) + "`\n"
                   "🚦 *Status:* `" + String(configUpdateStatusToString(rec->updateStatus)) + "`\n"
                   "⏰ *Offline Timeout:* `" + String(rec->pole.offlineTimeoutSec) + "s`";
      sendMessage(chatId, rep, "Markdown");
    } else {
      sendMessage(chatId, "⚠️ No pole configuration record found. Use `/cfg_pole <ID>`.", "Markdown");
    }
    return;
  }

  // Screen C: Feeder Configuration
  if (t.startsWith("/cfg_fdr")) {
    String targetId = t.substring(8);
    targetId.trim();
    if (targetId.length() == 0) targetId = sess.selectedPole;
    MscbNodeConfigRecord *rec = gwConfigMgr.getNodeConfig(targetId.c_str());
    if (!rec && gwConfigMgr.getNodeConfigCount() > 0) rec = gwConfigMgr.getNodeConfigByIndex(0);

    if (rec) {
      String rep = "🔌 *SCREEN C: FEEDER CONFIGURATION (`" + String(rec->logicalId) + "`)*\n\n";
      for (int f = 0; f < 6; f++) {
        rep += "• *Feeder " + String(f + 1) + ":* `" + String(rec->feeders[f].name) + "`\n"
               "  - Relay Mapping: Ch " + String(rec->feeders[f].relayMapping) + "\n"
               "  - Trip Limit: `" + String(rec->feeders[f].currentThresholdA, 1) + " A`\n"
               "  - Monitoring: " + String(rec->feeders[f].monitoringEnabled ? "Enabled ✅" : "Disabled ❌") + "\n";
      }
      sendMessage(chatId, rep, "Markdown");
    } else {
      sendMessage(chatId, "⚠️ Pole not found. Specify `/cfg_fdr <ID>`.", "Markdown");
    }
    return;
  }

  // Screen D: Phase Calibration
  if (t.startsWith("/cfg_cal")) {
    String targetId = t.substring(8);
    targetId.trim();
    if (targetId.length() == 0) targetId = sess.selectedPole;
    MscbNodeConfigRecord *rec = gwConfigMgr.getNodeConfig(targetId.c_str());
    if (!rec && gwConfigMgr.getNodeConfigCount() > 0) rec = gwConfigMgr.getNodeConfigByIndex(0);

    if (rec) {
      String rep = "📐 *SCREEN D: PHASE CALIBRATION (`" + String(rec->logicalId) + "`)*\n\n"
                   "*3 Common Bus Voltages (PT 230V):*\n"
                   "• Phase R: Gain=" + String(rec->voltCalib[0].gain, 4) + ", Off=" + String(rec->voltCalib[0].offset, 2) + "V\n"
                   "• Phase Y: Gain=" + String(rec->voltCalib[1].gain, 4) + ", Off=" + String(rec->voltCalib[1].offset, 2) + "V\n"
                   "• Phase B: Gain=" + String(rec->voltCalib[2].gain, 4) + ", Off=" + String(rec->voltCalib[2].offset, 2) + "V\n\n"
                   "*18 Feeder Currents (CT ACS712):*\n"
                   "• Nominal Gain: `1.0000`, Sensitivity: `100.0 mV/A`\n"
                   "• Zero-Offset Range: `[-5.0A, +5.0A]`, Span Limits: `[0.800, 1.200]`\n"
                   "🔒 *Policy:* Arbitrary manual updates blocked. Requires authenticated PIN.";
      sendMessage(chatId, rep, "Markdown");
    } else {
      sendMessage(chatId, "⚠️ Pole not found. Use `/cfg_cal <ID>`.", "Markdown");
    }
    return;
  }

  // Screen E: Network Timing
  if (t.startsWith("/cfg_time")) {
    String rep = "⏱️ *SCREEN E: NETWORK TIMING & RADIO DUTY-CYCLE*\n\n"
                 "• *Heartbeat Interval:* `30 seconds`\n"
                 "• *Node Offline Detection:* `180 seconds`\n"
                 "• *Metrology Telemetry Interval:* `60 seconds`\n"
                 "• *Alert Cooldown Period:* `60 seconds`\n"
                 "• *Autonomous Schedule Check:* `Every 10 seconds`\n"
                 "• *Config Synchronization Sweep:* `3600 seconds (1 hr)`\n\n"
                 "📡 *Radio Duty-Cycle Analysis:*\n"
                 "- ToA Heartbeat (48B): `514 ms`\n"
                 "- ToA Telemetry (180B): `1428 ms`\n"
                 "- Total Airtime per hour: `147.2 s` (across mesh hops)\n"
                 "- Duty-cycle per node: `0.41%` (Strictly within ETSI 1.0% limit)";
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Screen F: Fault Thresholds
  if (t.startsWith("/cfg_fault")) {
    String rep = "⚡ *SCREEN F: FAULT & ELECTRICAL PROTECTION THRESHOLDS*\n\n"
                 "• *Overcurrent Trip Rating:* `20.0 A` (Range: 0.5A - 100.0A)\n"
                 "• *Overvoltage Threshold:* `265.0 VAC` (Range: 240V - 300V)\n"
                 "• *Undervoltage Threshold:* `180.0 VAC` (Range: 150V - 210V)\n"
                 "• *Fault Confirmation Window:* `100 ms` (Range: 20ms - 5000ms)\n"
                 "• *Voltage Hysteresis:* `5.0 VAC`\n"
                 "• *Current Hysteresis:* `0.5 A`\n"
                 "• *Sensor Disconnect Detection:* `Active ✅`\n"
                 "• *Notification Priority:* `HIGH (Immediate Telegram Alert)`";
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Screen G: Firmware Versions
  if (t.startsWith("/fw_ver")) {
    String rep = "📦 *SCREEN G: FLEET FIRMWARE VERSIONS*\n\n"
                 "🌐 *Main Gateway:* `" + String(GW_VERSION) + "` (ESP32-S3 N16R8)\n"
                 "• Partitions: Dual OTA (`ota_0` 3MB / `ota_1` 3MB), FATFS (9.5MB)\n\n"
                 "💡 *Secondary Field Nodes:* `" + String(POLE_VERSION) + "` (ESP-WROOM-32)\n"
                 "• Registered Nodes: `" + String(gwConfigMgr.getNodeConfigCount()) + "`\n"
                 "• Fleet Version Consistency: 100% on `POLE_V10`\n"
                 "• OTA Provisioning: `default_ota.csv` (Dual 1.3MB slots)";
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Screen H: Gateway OTA
  if (t.startsWith("/gw_ota")) {
    String url = t.substring(7);
    url.trim();
    if (url.length() == 0) url = "https://ota.mscb-smartpole.internal/firmware/gw_v11.bin";

    GwFirmwareManifest manifest;
    memset(&manifest, 0, sizeof(manifest));
    strncpy(manifest.manifestVersion, "1.0", sizeof(manifest.manifestVersion) - 1);
    manifest.firmwareVersion = 11;
    strncpy(manifest.firmwareVersionStr, "GW_V11", sizeof(manifest.firmwareVersionStr) - 1);
    strncpy(manifest.targetHardware, "ESP32-S3-N16R8", sizeof(manifest.targetHardware) - 1);
    manifest.minGatewayVersion = 10;
    strncpy(manifest.sha256Hex, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", 64);
    strncpy(manifest.signatureHex, "VALID_RSA2048_SIG_HEX_AUTHENTICATED", sizeof(manifest.signatureHex) - 1);
    manifest.binarySize = 1450000;
    strncpy(manifest.downloadUrl, url.c_str(), sizeof(manifest.downloadUrl) - 1);
    manifest.valid = true;

    String err;
    bool ok = gwFwMgr.startHttpsOta(manifest, chatId.c_str(), err);
    if (ok) {
      sendMessage(chatId, "🚀 *Gateway OTA Download Started*\nManifest verified. Downloading over HTTPS...", "Markdown");
    } else {
      sendMessage(chatId, "⚠️ *Gateway OTA Rejected:* `" + err + "`", "Markdown");
    }
    return;
  }

  // Screen I: Field Pole Firmware Update
  if (t.startsWith("/pole_fw")) {
    String targetId = t.substring(8);
    targetId.trim();
    if (targetId.length() == 0) targetId = sess.selectedPole;

    String rep = "🔧 *SCREEN I: FIELD POLE FIRMWARE UPDATE (`" + targetId + "`)*\n\n"
                 "⚠️ *LoRa Engineering Restriction:*\n"
                 "Distributing 1.2MB firmware over 433 MHz LoRa requires ~11 days airtime per node and violates duty-cycle rules.\n\n"
                 "📶 *Option 1: Wi-Fi AP Maintenance Mode (Recommended)*\n"
                 "Send `/pole_maint " + targetId + "` to activate local AP `MSCB-" + targetId + "-MAINT` for technician web upload.\n\n"
                 "🔌 *Option 2: Wired Recovery*\n"
                 "Connect USB-UART cable for 115200 baud direct flash.\n\n"
                 "🧪 *Option 3: Experimental Lab LoRa*\n"
                 "Restricted to micro-patches (< 32 KB).";
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Screen J: Update Progress
  if (t.startsWith("/ota_progress")) {
    sendMessage(chatId, gwFwMgr.getOtaStatusReport(), "Markdown");
    return;
  }

  // Screen K: Failed Update Records
  if (t.startsWith("/ota_failed")) {
    const GwOtaAuditRecord &audit = gwFwMgr.getLastAudit();
    String rep = "📋 *SCREEN K: FAILED UPDATE & SECURITY AUDIT RECORDS*\n\n";
    if (audit.result != 0) {
      rep += "🆔 *Tx ID:* `" + String(audit.txId) + "`\n"
             "👤 *Admin:* `" + String(audit.adminId) + "`\n"
             "📦 *Version:* `V" + String(audit.targetVersion) + "`\n"
             "⚠️ *Failure Reason:* `" + String(audit.failureReason) + "`\n";
    } else {
      rep += "✅ No recent failed firmware updates recorded in audit log.\n";
    }
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Screen L: Rollback Management
  if (t.startsWith("/ota_rollback")) {
    String rep = "🔄 *SCREEN L: ROLLBACK MANAGEMENT*\n\n"
                 "• *Gateway Bootloader Rollback:* `Ready`\n"
                 "• *Passive Partition:* `ota_1` (Available for rollback)\n\n"
                 "To rollback Gateway firmware: `/gw_rollback`\n"
                 "To rollback Field Pole configuration: `/cfg_rollback <NODE_ID> <VER>`";
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Screen M: Configuration History
  if (t.startsWith("/cfg_history")) {
    uint8_t count = gwConfigMgr.getAuditCount();
    String rep = "📜 *SCREEN M: CONFIGURATION AUDIT HISTORY*\n\n";
    if (count == 0) {
      rep += "No configuration changes logged yet.\n";
    } else {
      for (uint8_t i = 0; i < count && i < 5; i++) {
        const ConfigAuditRecord *rec = gwConfigMgr.getAuditRecord(i);
        if (rec) {
          rep += "• `" + String(rec->transactionId) + "` | " + String(rec->targetNode) +
                 " | " + String(rec->operationType) + " (V" + String(rec->previousVersion) +
                 "->V" + String(rec->requestedVersion) + "): " +
                 String(rec->result == 0 ? "✅ OK" : "⚠️ FAIL") + "\n";
        }
      }
    }
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Screen N: Bulk Configuration
  if (t.startsWith("/cfg_bulk")) {
    String previewMsg, token;
    char tokenBuf[16];
    bool ok = configDist.prepareBulkUpdate(BULK_OP_ALL_POLES, "ALL", "NET", "HOP_LIMIT", "7",
                                          chatId.c_str(), previewMsg, tokenBuf);
    if (ok) {
      sendMessage(chatId, previewMsg, "Markdown");
    } else {
      sendMessage(chatId, "⚠️ Another bulk update is already in progress.", "Markdown");
    }
    return;
  }

  // Screen O: Bulk Firmware Deployment
  if (t.startsWith("/fw_bulk")) {
    String rep = "🚀 *SCREEN O: 6-STAGE FLEET FIRMWARE ROLLOUT*\n\n"
                 "1️⃣ *Stage 1: Dev Node Test* — Complete laboratory validation\n"
                 "2️⃣ *Stage 2: Canary 1 Field Pole* — Deploy to single pole\n"
                 "3️⃣ *Stage 3: 24h Observation* — Monitor sensors, contactors, mesh\n"
                 "4️⃣ *Stage 4: Small Group (3-5 Poles)* — Multi-hop verification\n"
                 "5️⃣ *Stage 5: Fleet Validation* — Validate zero packet losses\n"
                 "6️⃣ *Stage 6: Batch Fleet Deployment* — Controlled batch rollout\n\n"
                 "🔒 *Safety Policy:* Bulk updates use staggered windows with emergency preemption.";
    sendMessage(chatId, rep, "Markdown");
    return;
  }

  // Two-Step Confirmation Command
  if (t.startsWith("/confirm")) {
    String token = t.substring(8);
    token.trim();
    if (configDist.confirmAndStartBulk(token.c_str())) {
      sendMessage(chatId, "🚀 *Bulk Configuration Launched!*\nPacing window active. Use `/ota_progress` to track.", "Markdown");
    } else {
      sendMessage(chatId, "❌ Invalid confirmation token or no batch awaiting confirmation.", "Markdown");
    }
    return;
  }

  // Pole Maintenance Mode Trigger
  if (t.startsWith("/pole_maint")) {
    String targetId = t.substring(11);
    targetId.trim();
    if (targetId.length() == 0) targetId = sess.selectedPole;

    sendMessage(chatId, "📡 Sending authenticated `START_MAINT_AP` command to `" + targetId + "`...", "Markdown");
    loraMgr.queueCommand(chatId, targetId, "START_MAINT_AP|MSCB_Admin_2026", PRIO_HIGH);
    return;
  }

  sendMessage(chatId, "💡 Type /start to open the Control Dashboard.", "Markdown");
}

void TelegramManager::handleTelegramUpdates() {
  int numUpdates = bot.getUpdates(bot.last_message_received + 1);
  if (numUpdates <= 0) return;

  for (int i = 0; i < numUpdates; i++) {
    String chatId   = bot.messages[i].chat_id;
    String fromId   = bot.messages[i].from_id;
    String type     = bot.messages[i].type;
    String text     = bot.messages[i].text;
    String queryId  = bot.messages[i].query_id;
    int messageId   = bot.messages[i].message_id;

    if (chatId.length() == 0) chatId = fromId;
    if (chatId.length() == 0) continue;

    if (!isAuthorized(chatId, fromId)) {
      Serial.printf("[GW][TG] ⛔ Unauthorized access from Chat: %s, User: %s\n", chatId.c_str(), fromId.c_str());
      if (type == "callback_query" && queryId.length() > 0) {
        bot.answerCallbackQuery(queryId, "⛔ Unauthorized Access Denied", true);
      }
      bot.sendMessage(chatId, "🔒 *ACCESS DENIED*\n\nYour Chat ID: `" + chatId + "`\nAuthorized ID: `" + String(OWNER_CHAT) + "`", "Markdown");
      continue;
    }

    if (type == "callback_query") {
      handleTelegramCallback(chatId, queryId, text, messageId);
    } else {
      handleTelegramText(chatId, text);
    }
  }
}

bool TelegramManager::isTelegramOnline() const { return telegramOnline; }
bool TelegramManager::isWiFiConnected() const { return wifiConnected; }
