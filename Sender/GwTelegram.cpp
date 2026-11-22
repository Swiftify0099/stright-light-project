/*
 * =====================================================================
 *  GWTELEGRAM.CPP — MASTER CENTRAL GATEWAY TELEGRAM BOT CONTROLLER
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#include "GwTelegram.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "FeederManager.h"
#include "RevocationManager.h"
#include "GwRegistry.h"
#include "StorageManager.h"
#include "TelegramMenus.h"
#include "AlertManager.h"
#include <time.h>

GwTelegram gwTelegram;

GwTelegram::GwTelegram() :
  bot(BOT_TOKEN, client),
  wifiConnected(false),
  telegramOnline(false),
  hasNtpTime(false),
  bootMenuSent(false),
  tWifiWatchdog(0)
{
  batchTracker.active = false;
  batchTracker.finalReportSent = true;
}

void GwTelegram::init() {
  // 1. Initialize Sub-Managers
  sessionMgr.init();
  notifMgr.init();
  callbackMgr.init();

  bot.maxMessageLength = 4096;
  bot.waitForResponse  = 1000;
  client.setInsecure();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    checkNtpSync();

    if (verifyTelegramApi()) {
      sendStartupMainMenu(String(OWNER_CHAT));
      bootMenuSent = true;
    }
  } else {
    Serial.println(F("[GW][WIFI] ⚠️ Hotspot not connected at boot. LoRa active. Background reconnect scheduled."));
  }
}

bool GwTelegram::verifyTelegramApi() {
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

void GwTelegram::checkWiFiAndTelegram() {
  unsigned long now = millis();
  if (now - tWifiWatchdog < WIFI_WATCHDOG_INTERVAL_MS) return;
  tWifiWatchdog = now;

  bool nowWifi = (WiFi.status() == WL_CONNECTED);

  if (nowWifi) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.printf("[GW][WIFI] Connected! IP: %s (RSSI: %d dBm)\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      client.setInsecure();
      checkNtpSync();

      if (verifyTelegramApi()) {
        if (!bootMenuSent) {
          sendStartupMainMenu(String(OWNER_CHAT));
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
      Serial.println(F("[GW][WIFI] WiFi connection dropped."));
    }
  }
}

bool GwTelegram::deleteMessage(const String &chatId, int messageId) {
  if (messageId <= 0 || chatId.length() == 0 || !telegramOnline) return false;
  String cmd = bot.buildCommand("deleteMessage?chat_id=" + chatId + "&message_id=" + String(messageId));
  String response = bot.sendGetToTelegram(cmd);
  return bot.checkForOkResponse(response);
}

String GwTelegram::getWifiSsid() const {
  return String(WIFI_DEF_SSID);
}

void GwTelegram::checkNtpSync() {
  if (WiFi.status() == WL_CONNECTED) {
    configTime(19800, 0, "pool.ntp.org", "time.nist.gov"); // IST +5:30
    time_t raw = time(nullptr);
    if (raw > 1700000000UL) {
      hasNtpTime = true;
      Serial.printf("[GW][NTP] Synced successfully: %s\n", getFormattedTime().c_str());
    }
  }
}

String GwTelegram::getFormattedTime() {
  time_t raw = time(nullptr);
  struct tm *ti = localtime(&raw);
  if (!ti || raw < 1700000000UL) return "NoSync";
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
  return String(buf);
}

bool GwTelegram::sendMessage(const String &chatId, const String &text, const String &parseMode) {
  if (!telegramOnline) return false;
  return notifMgr.enqueueMessage(chatId, text, parseMode, "", 0, NOTIF_PRIO_NORMAL);
}

bool GwTelegram::sendMessageWithInlineKeyboard(const String &chatId, const String &text, const String &parseMode, const String &keyboard, int messageId) {
  if (!telegramOnline) return false;
  return notifMgr.sendImmediate(bot, chatId, text, parseMode, keyboard, messageId);
}

bool GwTelegram::answerCallbackQuery(const String &queryId, const String &text, bool showAlert) {
  if (!telegramOnline || queryId.length() == 0) return false;
  return bot.answerCallbackQuery(queryId, text, showAlert);
}

bool GwTelegram::isAuthorized(const String &chatId, const String &fromId) {
  return sessionMgr.isAuthorized(chatId, fromId);
}

void GwTelegram::renderMenu(const String &chatId, UserSession &sess, int messageId) {
  String text = "";
  String kb = "";
  String wifiActiveSsid = getWifiSsid();

  switch (sess.currentMenu) {
    case MENU_MAIN_DASHBOARD:
    case MENU_DASHBOARD:
      text = menuMgr.buildDashboardMessage(wifiActiveSsid);
      kb   = menuMgr.buildDashboardKeyboard();
      break;

    case MENU_GW_STATUS:
    case MENU_STATUS:
    case MENU_GW_SETTINGS:
    case MENU_SETTINGS:
    case MENU_RESET_SELECT:
    case MENU_RESET_CONFIRM_POLE:
    case MENU_RESET_CONFIRM_NEIGH:
    case MENU_RESET_CONFIRM_ALL:
      text = menuMgr.buildGatewaySettingsMessage();
      kb   = menuMgr.buildGatewaySettingsKeyboard();
      break;

    case MENU_POLE_MGMT:
    case MENU_ALL_POLES:
    case MENU_ONLINE_POLES:
    case MENU_OFFLINE_POLES:
    case MENU_NEIGHBOURS:
    case MENU_FARMER_LIST:
      text = menuMgr.buildPoleManagementMessage(sess);
      kb   = menuMgr.buildPoleManagementKeyboard(sess);
      break;

    case MENU_POLE_DETAIL:
      text = menuMgr.buildPoleDetailMessage(sess);
      kb   = menuMgr.buildPoleDetailKeyboard(sess);
      break;

    case MENU_FEEDER_MGMT:
    case MENU_FEEDER_STATUS:
      text = menuMgr.buildFeederManagementMessage(sess);
      kb   = menuMgr.buildFeederManagementKeyboard(sess);
      break;

    case MENU_FEEDER_DETAIL:
      text = menuMgr.buildFeederDetailMessage(sess);
      kb   = menuMgr.buildFeederDetailKeyboard(sess);
      break;

    case MENU_REG_PENDING:
    case MENU_REGISTRATION:
      text = menuMgr.buildRegistrationMessage(sess);
      kb   = menuMgr.buildRegistrationKeyboard(sess);
      break;

    case MENU_DELETE_MGMT:
      text = menuMgr.buildDeleteManagementMessage(sess);
      kb   = menuMgr.buildDeleteManagementKeyboard(sess);
      break;

    case MENU_DELETE_CONFIRM:
      text = menuMgr.buildDeleteConfirmMessage(sess);
      kb   = menuMgr.buildDeleteConfirmKeyboard(sess);
      break;

    case MENU_REVOKED_LIST:
      text = menuMgr.buildRevokedListMessage(sess);
      kb   = menuMgr.buildRevokedListKeyboard(sess);
      break;

    case MENU_POLE_CTRL:
    case MENU_FARMER_CTRL:
      text = menuMgr.buildPoleControlMessage(sess);
      kb   = menuMgr.buildPoleControlKeyboard(sess);
      break;

    case MENU_FEEDER_CTRL:
      text = menuMgr.buildFeederControlMessage(sess);
      kb   = menuMgr.buildFeederControlKeyboard(sess);
      break;

    case MENU_GROUP_CTRL:
      text = menuMgr.buildGroupControlMessage(sess);
      kb   = menuMgr.buildGroupControlKeyboard(sess);
      break;

    case MENU_GROUP_CONFIRM:
      text = menuMgr.buildGroupConfirmMessage(sess);
      kb   = menuMgr.buildGroupConfirmKeyboard(sess);
      break;

    case MENU_ALL_POLES_CTRL:
      text = menuMgr.buildAllPolesControlMessage(sess);
      kb   = menuMgr.buildAllPolesControlKeyboard(sess);
      break;

    case MENU_ALL_CONFIRM:
      text = menuMgr.buildAllConfirmMessage(sess);
      kb   = menuMgr.buildAllConfirmKeyboard(sess);
      break;

    case MENU_VOLTAGE_MON:
      text = menuMgr.buildVoltageMonitoringMessage(sess);
      kb   = menuMgr.buildVoltageMonitoringKeyboard(sess);
      break;

    case MENU_VOLT_DETAIL:
    case MENU_VOLTAGE_TREND:
    case MENU_PHASE_COMPARE:
      text = menuMgr.buildVoltageDetailMessage(sess);
      kb   = menuMgr.buildVoltageDetailKeyboard(sess);
      break;

    case MENU_CURRENT_MON:
      text = menuMgr.buildCurrentMonitoringMessage(sess);
      kb   = menuMgr.buildCurrentMonitoringKeyboard(sess);
      break;

    case MENU_CURR_DETAIL:
      text = menuMgr.buildCurrentDetailMessage(sess);
      kb   = menuMgr.buildCurrentDetailKeyboard(sess);
      break;

    case MENU_TOPOLOGY:
    case MENU_POLE_NEIGHBOURS:
      text = menuMgr.buildTopologyMessage(sess);
      kb   = menuMgr.buildTopologyKeyboard(sess);
      break;

    case MENU_NODE_STATUS:
      text = menuMgr.buildNodeStatusMessage(sess);
      kb   = menuMgr.buildNodeStatusKeyboard(sess);
      break;

    case MENU_FAULT_ALERTS:
    case MENU_INCIDENT_DETAIL:
      text = menuMgr.buildFaultAlertsMessage(sess);
      kb   = menuMgr.buildFaultAlertsKeyboard(sess);
      break;

    case MENU_TIME_CONFIG:
    case MENU_TIMER_SELECT:
      text = menuMgr.buildTimeConfigMessage(sess);
      kb   = menuMgr.buildTimeConfigKeyboard(sess);
      break;

    case MENU_WIFI_SETTINGS:
    case MENU_WIFI_CONFIRM:
      text = menuMgr.buildWiFiSettingsMessage(wifiActiveSsid);
      kb   = menuMgr.buildWiFiSettingsKeyboard();
      break;

    case MENU_FW_MGMT:
      text = menuMgr.buildFirmwareManagementMessage();
      kb   = menuMgr.buildFirmwareManagementKeyboard();
      break;

    case MENU_EVENT_HISTORY:
      text = menuMgr.buildEventHistoryMessage(sess);
      kb   = menuMgr.buildEventHistoryKeyboard(sess);
      break;

    case MENU_HELP_INFO:
      text = menuMgr.buildHelpInfoMessage();
      kb   = menuMgr.buildHelpInfoKeyboard();
      break;

    default:
      sess.currentMenu = MENU_MAIN_DASHBOARD;
      text = menuMgr.buildDashboardMessage(wifiActiveSsid);
      kb   = menuMgr.buildDashboardKeyboard();
      break;
  }

  bool ok = false;
  if (messageId > 0) {
    ok = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, messageId);
    if (!ok) {
      ok = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, 0);
      if (ok) sess.lastMessageId = bot.last_sent_message_id;
    }
  } else {
    ok = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, 0);
    if (ok) sess.lastMessageId = bot.last_sent_message_id;
  }
}

void GwTelegram::sendStartupMainMenu(const String &chatId) {
  UserSession &sess = sessionMgr.getOrCreateSession(chatId);
  sess.currentMenu = MENU_DASHBOARD;
  sess.previousMenu = MENU_DASHBOARD;
  renderMenu(chatId, sess, 0);
  Serial.printf("[GW][TG] Startup Main Dashboard sent to Chat: %s\n", chatId.c_str());
}

// ─────────────────────────────────────────────────────────────
//  BATCH COMMAND TRACKER (GRID-WIDE OPERATIONS)
// ─────────────────────────────────────────────────────────────
void GwTelegram::startBatchCommand(const String &chatId, const String &cmdType) {
  int total = meshMgr.getNodeCount();
  batchTracker.active = true;
  batchTracker.cmdType = cmdType;
  batchTracker.chatId = chatId;
  batchTracker.startTime = millis();
  batchTracker.timeoutMs = 8000UL;
  batchTracker.totalExpected = total;
  batchTracker.confirmedCount = 0;
  batchTracker.failedCount = 0;
  batchTracker.unreachableCount = 0;
  batchTracker.finalReportSent = false;

  for (int i = 0; i < MAX_NODES; i++) {
    batchTracker.nodeAcked[i] = false;
  }

  String label = (cmdType == "ALL_ON") ? "ALL LIGHTS ON" : "ALL LIGHTS OFF";
  sendMessage(chatId, "⏳ *Staggering " + label + " across LoRa mesh grid...*", "Markdown");

  String loraCmd = (cmdType == "ALL_ON") ? "LIGHT_ON" : "LIGHT_OFF";
  loraMgr.queueCommand(chatId, "ALL", loraCmd, PRIO_HIGH);
}

void GwTelegram::onBatchAckReceived(const String &srcNodeId, const String &cmd, const String &state) {
  if (!batchTracker.active) return;

  int idx = meshMgr.findNodeIndex(srcNodeId);
  if (idx >= 0 && !batchTracker.nodeAcked[idx]) {
    batchTracker.nodeAcked[idx] = true;
    batchTracker.confirmedCount++;
    Serial.printf("[GW][BATCH] Pole %s confirmed (Progress: %d/%d)\n",
                  srcNodeId.c_str(), batchTracker.confirmedCount, batchTracker.totalExpected);
  }
}

void GwTelegram::processBatchTracker() {
  if (!batchTracker.active || batchTracker.finalReportSent) return;

  unsigned long elapsed = millis() - batchTracker.startTime;
  bool allReceived = (batchTracker.confirmedCount >= batchTracker.totalExpected && batchTracker.totalExpected > 0);
  bool timedOut = (elapsed >= batchTracker.timeoutMs);

  if (allReceived || timedOut) {
    batchTracker.active = false;
    batchTracker.finalReportSent = true;

    int total = meshMgr.getNodeCount();
    batchTracker.failedCount = max(0, total - batchTracker.confirmedCount);

    String cmdTitle = (batchTracker.cmdType == "ALL_ON") ? "💡 ALL LIGHTS ON REPORT" : "🌙 ALL LIGHTS OFF REPORT";
    String resMsg = "━━━━━━━━━━━━━━━━━━━━\n"
                    "✅ *" + cmdTitle + "*\n"
                    "━━━━━━━━━━━━━━━━━━━━\n\n"
                    "✅ *Confirmed Poles:* `" + String(batchTracker.confirmedCount) + " / " + String(total) + "`\n"
                    "❌ *Unreachable / Timeout:* `" + String(batchTracker.failedCount) + "`\n\n"
                    "📊 *Grid Health:*\n" +
                    feederMgr.buildCompactFeederTable() + "\n"
                    "━━━━━━━━━━━━━━━━━━━━";

    if (batchTracker.chatId.length() > 0) {
      sendMessage(batchTracker.chatId, resMsg, "Markdown");
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  TEXT MESSAGE & WIZARD INPUT HANDLER
// ─────────────────────────────────────────────────────────────
void GwTelegram::handleTelegramText(const String &chatId, const String &fromId, const String &text, int messageId, bool isPrivateChat) {
  String t = text;
  t.trim();

  // Fail-closed Admin Authorization Check
  if (!sessionMgr.isAuthorized(chatId, fromId)) {
    Serial.printf("[GW][SECURITY] ⛔ Unauthorized Text Message dropped from Chat: %s, User: %s\n",
                  chatId.c_str(), fromId.c_str());
    bot.sendMessage(chatId, "🔒 *ACCESS DENIED*\n\nIdentity verification failed. Your User ID (`" + fromId + "`) is not in the administrator allowlist.", "Markdown");
    return;
  }

  UserSession &sess = sessionMgr.getOrCreateSession(chatId, fromId);

  // Abort wizard
  if (t == "/cancel" || t.equalsIgnoreCase("CANCEL")) {
    if (sess.wizardState != WIZ_NONE || sess.pendingRegIdx >= 0) {
      sessionMgr.clearWizard(sess);
      sess.pendingRegIdx = -1;
      bot.sendMessage(chatId, "❌ *Operation cancelled.* Returning to Dashboard.", "Markdown");
      sess.currentMenu = MENU_DASHBOARD;
      renderMenu(chatId, sess, 0);
    } else {
      bot.sendMessage(chatId, "ℹ️ No active operation to cancel.", "Markdown");
    }
    return;
  }

  // Handle Commissioning Wizard Text Inputs
  if (sess.wizardState == WIZ_REG_NAME && sess.pendingRegIdx >= 0) {
    sess.pendingPoleName = t;
    sess.wizardState = WIZ_REG_AREA;

    String prompt = "✅ *Name set to:* `" + t + "`\n\n"
                    "Now enter the *Installation Area / Location*:\n"
                    "*(Example: `South Highway Sector 4`)*\n\n"
                    "Or type `/cancel` to abort.";
    bot.sendMessage(chatId, prompt, "Markdown");
    return;
  }
  else if (sess.wizardState == WIZ_REG_AREA && sess.pendingRegIdx >= 0) {
    sess.pendingPoleArea = t;
    int pIdx = sess.pendingRegIdx;
    PendingRegistration *p = meshMgr.getPendingReg(pIdx);

    if (p && p->valid) {
      // Generate Unique Logical ID
      String newLogicalId = meshMgr.generateNextLogicalId(p->type);

      int nodeIdx = meshMgr.findNodeIndex(p->hwId);
      if (nodeIdx < 0) {
        nodeIdx = meshMgr.addOrUpdateNode(p->hwId, newLogicalId, p->type, p->rssi, 0);
      }
      NodeRecord *node = meshMgr.getNode(nodeIdx);
      if (node) {
        node->logicalId = newLogicalId;
        node->name = sess.pendingPoleName;
        node->location = sess.pendingPoleArea;
        node->registered = true;
        node->lifecycleState = POLE_STATE_ACTIVE;
      }

      // Authoritative Registry Approval
      gwRegistry.approveNode(p->hwId.c_str(), newLogicalId.c_str(),
                             sess.pendingPoleName.c_str(), sess.pendingPoleArea.c_str(),
                             "COMMISSIONED_VIA_TELEGRAM", "GATEWAY", "NONE");

      // Confirm over LoRa Mesh
      String regConfirm = newLogicalId + "|" + sess.pendingPoleName + "|" + p->type;
      loraMgr.dispatchLoRaPacket("CMD", p->hwId, p->hwId, DEFAULT_TTL, loraMgr.getNextSeq(), "REGISTERED|" + regConfirm);

      meshMgr.clearPendingReg(pIdx);
      sessionMgr.clearWizard(sess);
      sess.pendingRegIdx = -1;

      String confMsg = "🎉 *NODE SUCCESSFULLY COMMISSIONED*\n"
                       "━━━━━━━━━━━━━━━━━━━━\n"
                       "🆔 *Logical ID:* `" + newLogicalId + "`\n"
                       "⚙️ *Hardware ID:* `" + p->hwId + "`\n"
                       "🏷️ *Pole Name:* *" + sess.pendingPoleName + "*\n"
                       "📍 *Area:* `" + sess.pendingPoleArea + "`\n"
                       "📡 *Status:* Active in Mesh Database\n"
                       "━━━━━━━━━━━━━━━━━━━━";
      bot.sendMessage(chatId, confMsg, "Markdown");

      storageMgr.saveDatabaseToNVS();
      sess.currentMenu = MENU_POLE_MGMT;
      renderMenu(chatId, sess, 0);
      return;
    }
  }

  // Standard Slash Commands
  if (t == "/start" || t == "/menu" || t == "/dashboard") {
    sendStartupMainMenu(chatId);
    return;
  }
  if (t == "/status") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_NODE_STATUS;
    renderMenu(chatId, sess, 0);
    return;
  }
  if (t == "/help") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_HELP_INFO;
    renderMenu(chatId, sess, 0);
    return;
  }
  if (t.equalsIgnoreCase("WIFI") || t.equalsIgnoreCase("/wifi")) {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_WIFI_SETTINGS;
    renderMenu(chatId, sess, 0);
    return;
  }

  // Quick text command triggers
  if (t.equalsIgnoreCase("ON") || t.equalsIgnoreCase("ALL ON")) {
    startBatchCommand(chatId, "ALL_ON");
    return;
  }
  if (t.equalsIgnoreCase("OFF") || t.equalsIgnoreCase("ALL OFF")) {
    startBatchCommand(chatId, "ALL_OFF");
    return;
  }

  bot.sendMessage(chatId, "💡 Type `/menu` or `/start` to open the MSCB Smart Pole Dashboard.", "Markdown");
}

// ─────────────────────────────────────────────────────────────
//  UPDATE POLLING CYCLE (COOPERATIVE SCHEDULER)
// ─────────────────────────────────────────────────────────────
void GwTelegram::handleTelegramUpdates() {
  if (!wifiConnected || !telegramOnline) return;

  // 1. Process outgoing message queue first
  notifMgr.processOutgoingQueue(bot, telegramOnline);

  // 2. Poll for incoming Telegram updates
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

    bool isPrivateChat = !chatId.startsWith("-") && (type != "group" && type != "supergroup");

    if (type == "callback_query") {
      callbackMgr.handleCallback(bot, chatId, fromId, queryId, text, messageId);
    } else {
      handleTelegramText(chatId, fromId, text, messageId, isPrivateChat);
    }
  }
}

bool GwTelegram::isTelegramOnline() const { return telegramOnline; }
bool GwTelegram::isWiFiConnected() const { return (WiFi.status() == WL_CONNECTED); }
