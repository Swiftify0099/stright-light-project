/*
 * =====================================================================
 *  TELEGRAMCALLBACKMANAGER.CPP — SECURE INLINE CALLBACK QUERY ROUTER
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#include "TelegramCallbackManager.h"
#include "TelegramSessionManager.h"
#include "TelegramMenuManager.h"
#include "TelegramMenus.h"
#include "TelegramNotificationManager.h"
#include "AlertManager.h"
#include "GwTelegram.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "FeederManager.h"
#include "RevocationManager.h"
#include "GwRegistry.h"
#include "StorageManager.h"
#include <UniversalTelegramBot.h>

TelegramCallbackManager callbackMgr;

TelegramCallbackManager::TelegramCallbackManager() : seenHead(0) {
  for (int i = 0; i < MAX_SEEN_QUERIES; i++) {
    seenQueries[i] = "";
  }
}

void TelegramCallbackManager::init() {
  seenHead = 0;
  Serial.println(F("[GW][CB] Initialized Telegram Callback Manager."));
}

bool TelegramCallbackManager::isDuplicateQuery(const String &queryId) {
  if (queryId.length() == 0) return false;
  for (int i = 0; i < MAX_SEEN_QUERIES; i++) {
    if (seenQueries[i] == queryId) return true;
  }
  return false;
}

void TelegramCallbackManager::recordQuery(const String &queryId) {
  if (queryId.length() == 0) return;
  seenQueries[seenHead] = queryId;
  seenHead = (seenHead + 1) % MAX_SEEN_QUERIES;
}

void TelegramCallbackManager::renderMenu(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, int messageId) {
  String text = "";
  String kb = "";
  String wifiActiveSsid = String(WIFI_DEF_SSID);

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

void TelegramCallbackManager::handleCallback(UniversalTelegramBot &bot,
                                           const String &chatId,
                                           const String &fromId,
                                           const String &queryId,
                                           const String &data,
                                           int messageId) {
  String action = data;
  action.trim();
  Serial.printf("[GW][CB] RX Callback: '%s' (Chat: %s, User: %s, MsgID: %d)\n",
                action.c_str(), chatId.c_str(), fromId.c_str(), messageId);

  // 1. FAIL-CLOSED ADMIN IDENTITY VERIFICATION
  if (!sessionMgr.isAuthorized(chatId, fromId)) {
    Serial.printf("[GW][SECURITY] ⛔ Unauthorized Callback rejected from Chat %s, User %s\n",
                  chatId.c_str(), fromId.c_str());
    if (queryId.length() > 0) {
      bot.answerCallbackQuery(queryId, "⛔ Access Denied: Unauthorized Administrator", true);
    }
    notifMgr.enqueueMessage(chatId, "🔒 *ACCESS DENIED*\n\nIdentity verification failed. Your User ID (`" + fromId + "`) is not in the administrator allowlist.", "Markdown", "", 0, NOTIF_PRIO_HIGH);
    return;
  }

  // 2. IMMEDIATE ACK TO CLEAR BUTTON SPINNER
  if (queryId.length() > 0) {
    if (action.startsWith("CMD:")) {
      bot.answerCallbackQuery(queryId, "⚡ Processing Command...");
    } else if (action == "REFRESH") {
      bot.answerCallbackQuery(queryId, "🔄 Refreshed!");
    } else if (action.startsWith("DEL:")) {
      bot.answerCallbackQuery(queryId, "⚠️ Deletion Action");
    } else {
      bot.answerCallbackQuery(queryId, "");
    }
  }

  // 3. DUPLICATE QUERY / REPLAY SUPPRESSION
  if (isDuplicateQuery(queryId)) {
    Serial.printf("[GW][CB] Duplicate query ignored: %s\n", queryId.c_str());
    return;
  }
  recordQuery(queryId);

  // 4. GET / UPDATE ADMIN SESSION
  UserSession &sess = sessionMgr.getOrCreateSession(chatId, fromId);
  sess.lastMessageId = messageId;
  sess.lastActive = millis();

  // 5. ROUTE BASED ON PREFIX
  if (action == "HOME" || action == "DASHBOARD") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_MAIN_DASHBOARD;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "BACK") {
    sess.currentMenu = sess.previousMenu;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "REFRESH") {
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "GW_STAT") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_GW_STATUS;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "POLES_ALL") {
    sess.previousMenu = sess.currentMenu;
    sess.pageIndex = 0;
    sess.poleFilter = "ALL";
    sess.currentMenu = MENU_ALL_POLES;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "POLES_ON") {
    sess.previousMenu = sess.currentMenu;
    sess.pageIndex = 0;
    sess.poleFilter = "ONLINE";
    sess.currentMenu = MENU_ONLINE_POLES;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "POLES_OFF") {
    sess.previousMenu = sess.currentMenu;
    sess.pageIndex = 0;
    sess.poleFilter = "OFFLINE";
    sess.currentMenu = MENU_OFFLINE_POLES;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "FEEDER_MON") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_FEEDER_MGMT;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "VOLT_MON") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_VOLTAGE_MON;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "V_TREND") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_VOLTAGE_TREND;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "V_COMPARE") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_PHASE_COMPARE;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("V_ALL:")) {
    sess.pageIndex = action.substring(6).toInt();
    sess.currentMenu = MENU_VOLTAGE_MON;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("V_PG:")) {
    sess.pageIndex = action.substring(5).toInt();
    sess.currentMenu = MENU_VOLTAGE_MON;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "CURR_MON") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_CURRENT_MON;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("CURR_M:")) {
    sess.currentMode = action.substring(7);
    sess.currentMenu = MENU_CURRENT_MON;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "ALERTS_LIST") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_FAULT_ALERTS;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "TOPOLOGY") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_TOPOLOGY;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "REGLIST") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_REGISTRATION;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "DEL_MGMT") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_DELETE_MGMT;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("EVT_HIST:")) {
    sess.pageIndex = action.substring(9).toInt();
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_EVENT_HISTORY;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("VIEW_INC:")) {
    sess.selectedEventId = (uint32_t)action.substring(9).toInt();
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_INCIDENT_DETAIL;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("ACK_INC:")) {
    uint32_t eId = (uint32_t)action.substring(8).toInt();
    alertMgr.acknowledgeIncident(eId, fromId.c_str());
    bot.answerCallbackQuery(queryId, "✅ Incident #" + String(eId) + " Acknowledged (Underlying physical state unchanged).", true);
    sess.selectedEventId = eId;
    sess.currentMenu = MENU_INCIDENT_DETAIL;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("RES_INC:")) {
    uint32_t eId = (uint32_t)action.substring(8).toInt();
    alertMgr.markResolved(eId, fromId.c_str());
    bot.answerCallbackQuery(queryId, "🟢 Incident #" + String(eId) + " Marked Resolved.", true);
    sess.selectedEventId = eId;
    sess.currentMenu = MENU_INCIDENT_DETAIL;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("P:")) {
    String pid = action.substring(2);
    sess.selectedPole = pid;
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_POLE_DETAIL;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("FDR_P:") || action.startsWith("FDR_SW:")) {
    String pid = action.startsWith("FDR_P:") ? action.substring(6) : action.substring(7);
    sess.selectedPole = pid;
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_FEEDER_MGMT;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("V_P:")) {
    String pid = action.substring(4);
    sess.selectedPole = pid;
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_VOLT_DETAIL;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("C_P:")) {
    String pid = action.substring(4);
    sess.selectedPole = pid;
    sess.currentMode = "A";
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_CURR_DETAIL;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("PG:P:")) {
    int colon = action.indexOf(':', 5);
    String pageStr = (colon > 0) ? action.substring(5, colon) : action.substring(5);
    String filterStr = (colon > 0) ? action.substring(colon + 1) : "ALL";
    sess.pageIndex = max(0, pageStr.toInt() - 1);
    sess.poleFilter = filterStr;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("PG:N:")) {
    int colon = action.indexOf(':', 5);
    String pageStr = (colon > 0) ? action.substring(5, colon) : action.substring(5);
    String filterStr = (colon > 0) ? action.substring(colon + 1) : "ALL";
    sess.pageIndex = pageStr.toInt() + 1;
    sess.poleFilter = filterStr;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("PON:")) {
    String p = action.substring(4);
    bot.sendMessage(chatId, "⚡ Sending LIGHT ON to `" + p + "`...", "Markdown");
    loraMgr.queueCommand(chatId, p, "LIGHT_ON", PRIO_HIGH);
  }
  else if (action.startsWith("POFF:")) {
    String p = action.substring(5);
    bot.sendMessage(chatId, "🌙 Sending LIGHT OFF to `" + p + "`...", "Markdown");
    loraMgr.queueCommand(chatId, p, "LIGHT_OFF", PRIO_HIGH);
  }
  else if (action == "ALL_ON") {
    gwTelegram.startBatchCommand(chatId, "ALL_ON");
  }
  else if (action == "ALL_OFF") {
    gwTelegram.startBatchCommand(chatId, "ALL_OFF");
  }
  else if (action.startsWith("NAV:") || action.startsWith("PAGE:") || action.startsWith("P_SEL:") ||
           action.startsWith("F_SEL:") || action.startsWith("V_SEL:") || action.startsWith("C_SEL:")) {
    routeNavigation(bot, chatId, sess, action, messageId);
  }
  else if (action.startsWith("CMD:")) {
    routeControl(bot, chatId, sess, action, messageId);
  }
  else if (action.startsWith("BATCH:")) {
    routeBatch(bot, chatId, sess, action, messageId);
  }
  else if (action.startsWith("REG:") || action.startsWith("REG_T:")) {
    routeRegistration(bot, chatId, sess, action, messageId);
  }
  else if (action.startsWith("DEL:") || action.startsWith("FDR:DEACT")) {
    routeDeletion(bot, chatId, sess, action, messageId);
  }
  else if (action.startsWith("WIFI_") || action == "NAV:WIFI") {
    routeWiFi(bot, chatId, sess, action, messageId);
  }
  else {
    Serial.printf("[GW][CB] ⚠️ Unhandled callback action: %s\n", action.c_str());
    renderMenu(bot, chatId, sess, messageId);
  }
}

// ─────────────────────────────────────────────────────────────
//  NAVIGATION ROUTER
// ─────────────────────────────────────────────────────────────
void TelegramCallbackManager::routeNavigation(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId) {
  sess.previousMenu = sess.currentMenu;

  if (action == "NAV:POLE_MGMT") {
    sess.pageIndex = 0;
    sess.currentMenu = MENU_POLE_MGMT;
  }
  else if (action == "NAV:FEEDER_MGMT") {
    sess.currentMenu = MENU_FEEDER_MGMT;
  }
  else if (action == "NAV:POLE_CTRL") {
    sess.currentMenu = MENU_POLE_CTRL;
  }
  else if (action == "NAV:FEEDER_CTRL") {
    sess.currentMenu = MENU_FEEDER_CTRL;
  }
  else if (action == "NAV:GROUP_CTRL") {
    sess.currentMenu = MENU_GROUP_CTRL;
  }
  else if (action == "NAV:ALL_POLES") {
    sess.currentMenu = MENU_ALL_POLES_CTRL;
  }
  else if (action == "NAV:VOLT_MON") {
    sess.pageIndex = 0;
    sess.currentMenu = MENU_VOLTAGE_MON;
  }
  else if (action == "NAV:CURR_MON") {
    sess.currentMenu = MENU_CURRENT_MON;
  }
  else if (action == "NAV:TOPOLOGY") {
    sess.currentMenu = MENU_TOPOLOGY;
  }
  else if (action == "NAV:NODE_STATUS") {
    sess.currentMenu = MENU_NODE_STATUS;
  }
  else if (action == "NAV:FAULTS") {
    sess.currentMenu = MENU_FAULT_ALERTS;
  }
  else if (action == "NAV:REG") {
    sess.currentMenu = MENU_REG_PENDING;
  }
  else if (action == "NAV:DEL_MGMT") {
    sess.pageIndex = 0;
    sess.selectedPole = "";
    sess.currentMenu = MENU_DELETE_MGMT;
  }
  else if (action == "NAV:REVOKED") {
    sess.currentMenu = MENU_REVOKED_LIST;
  }
  else if (action == "NAV:TIME_CFG") {
    sess.currentMenu = MENU_TIME_CONFIG;
  }
  else if (action == "NAV:WIFI") {
    sess.currentMenu = MENU_WIFI_SETTINGS;
  }
  else if (action == "NAV:GW_SETTINGS") {
    sess.currentMenu = MENU_GW_SETTINGS;
  }
  else if (action == "NAV:FW_MGMT") {
    sess.currentMenu = MENU_FW_MGMT;
  }
  else if (action == "NAV:EVENTS") {
    sess.currentMenu = MENU_EVENT_HISTORY;
  }
  else if (action == "NAV:HELP") {
    sess.currentMenu = MENU_HELP_INFO;
  }
  // Pagination
  else if (action == "PAGE:POLE_N") {
    sess.pageIndex++;
    sess.currentMenu = MENU_POLE_MGMT;
  }
  else if (action == "PAGE:POLE_P") {
    if (sess.pageIndex > 0) sess.pageIndex--;
    sess.currentMenu = MENU_POLE_MGMT;
  }
  else if (action == "PAGE:VOLT_N") {
    sess.pageIndex++;
    sess.currentMenu = MENU_VOLTAGE_MON;
  }
  else if (action == "PAGE:VOLT_P") {
    if (sess.pageIndex > 0) sess.pageIndex--;
    sess.currentMenu = MENU_VOLTAGE_MON;
  }
  // Target Selectors (Verify stable logical ID)
  else if (action.startsWith("P_SEL:")) {
    String pId = action.substring(6);
    NodeRecord *n = meshMgr.findNode(pId);
    if (!n) {
      bot.sendMessage(chatId, "⚠️ *Selected Pole is no longer in database.*", "Markdown");
      sess.currentMenu = MENU_POLE_MGMT;
    } else {
      sess.selectedPole = pId;
      sess.currentMenu = MENU_POLE_DETAIL;
    }
  }
  else if (action.startsWith("F_SEL:")) {
    int c1 = action.indexOf(':', 6);
    if (c1 > 0) {
      String pId = action.substring(6, c1);
      uint8_t fdr = (uint8_t)action.substring(c1 + 1).toInt();
      sess.selectedPole = pId;
      sess.selectedFeeder = fdr;
      sess.currentMenu = MENU_FEEDER_DETAIL;
    }
  }
  else if (action.startsWith("V_SEL:")) {
    sess.selectedPole = action.substring(6);
    sess.currentMenu = MENU_VOLT_DETAIL;
  }
  else if (action.startsWith("C_SEL:")) {
    sess.selectedPole = action.substring(6);
    sess.currentMenu = MENU_CURR_DETAIL;
  }

  renderMenu(bot, chatId, sess, messageId);
}

// ─────────────────────────────────────────────────────────────
//  CONTROL COMMAND ROUTER
// ─────────────────────────────────────────────────────────────
void TelegramCallbackManager::routeControl(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId) {
  // POLE MAIN LIGHT ON / OFF
  if (action.startsWith("CMD:P_ON:")) {
    String pId = action.substring(9);
    NodeRecord *n = meshMgr.findNode(pId);
    if (!n) {
      bot.sendMessage(chatId, "⚠️ *Pole not found:* `" + pId + "`", "Markdown");
      return;
    }
    String pName = n->name;
    notifMgr.enqueueMessage(chatId, "⏳ *Dispatching LIGHT ON to* `" + pName + "` (`" + pId + "`)...", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
    loraMgr.queueCommand(chatId, pId, "LIGHT_ON", PRIO_HIGH);
  }
  else if (action.startsWith("CMD:P_OFF:")) {
    String pId = action.substring(10);
    NodeRecord *n = meshMgr.findNode(pId);
    if (!n) {
      bot.sendMessage(chatId, "⚠️ *Pole not found:* `" + pId + "`", "Markdown");
      return;
    }
    String pName = n->name;
    notifMgr.enqueueMessage(chatId, "⏳ *Dispatching LIGHT OFF to* `" + pName + "` (`" + pId + "`)...", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
    loraMgr.queueCommand(chatId, pId, "LIGHT_OFF", PRIO_HIGH);
  }
  // FEEDER INDIVIDUAL ON / OFF
  else if (action.startsWith("CMD:F_ON:")) {
    int c1 = action.indexOf(':', 9);
    if (c1 > 0) {
      String pId = action.substring(9, c1);
      int fdr = action.substring(c1 + 1).toInt();
      notifMgr.enqueueMessage(chatId, "⏳ *Dispatching Feeder " + String(fdr) + " ON to* `" + pId + "`...", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
      loraMgr.queueCommand(chatId, pId, "FDR_ON|" + String(fdr), PRIO_HIGH);
    }
  }
  else if (action.startsWith("CMD:F_OFF:")) {
    int c1 = action.indexOf(':', 10);
    if (c1 > 0) {
      String pId = action.substring(10, c1);
      int fdr = action.substring(c1 + 1).toInt();
      notifMgr.enqueueMessage(chatId, "⏳ *Dispatching Feeder " + String(fdr) + " OFF to* `" + pId + "`...", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
      loraMgr.queueCommand(chatId, pId, "FDR_OFF|" + String(fdr), PRIO_HIGH);
    }
  }
  // TIMER ON
  else if (action.startsWith("CMD:TMR:")) {
    int c1 = action.indexOf(':', 8);
    if (c1 > 0) {
      String pId = action.substring(8, c1);
      int mins = action.substring(c1 + 1).toInt();
      notifMgr.enqueueMessage(chatId, "⏱ *Setting " + String(mins) + "-minute timer on* `" + pId + "`...", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
      loraMgr.queueCommand(chatId, pId, "TIMER_ON|" + String(mins), PRIO_HIGH);
    }
  }
  // STATUS QUERY
  else if (action.startsWith("CMD:PSTAT:")) {
    String pId = action.substring(10);
    notifMgr.enqueueMessage(chatId, "📊 *Requesting live telemetry from* `" + pId + "`...", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
    loraMgr.queueCommand(chatId, pId, "GET_STATUS", PRIO_MEDIUM);
  }
  // SAFE REBOOT
  else if (action.startsWith("CMD:RST:")) {
    String pId = action.substring(8);
    notifMgr.enqueueMessage(chatId, "🔁 *Transmitting safe REBOOT command to* `" + pId + "`...", "Markdown", "", 0, NOTIF_PRIO_HIGH);
    loraMgr.queueCommand(chatId, pId, "RESET", PRIO_HIGH);
  }
  // TIME SYNC BROADCAST
  else if (action == "CMD:TIME_SYNC") {
    time_t raw = time(nullptr);
    if (raw > 1700000000UL) {
      loraMgr.dispatchLoRaPacket("CMD", BROADCAST_ID, BROADCAST_ID, 1, loraMgr.getNextSeq(), "SET_TIME|" + String((unsigned long)raw));
      notifMgr.enqueueMessage(chatId, "✅ *Broadcast Soft RTC Time Sync:* `" + String((unsigned long)raw) + "`", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
    } else {
      notifMgr.enqueueMessage(chatId, "⚠️ *Gateway NTP Time not synchronized yet.*", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
    }
  }
  // GROUP CONTROL TRIGGER
  else if (action.startsWith("CMD:GRP:")) {
    int c1 = action.indexOf(':', 8);
    if (c1 > 0) {
      String grp = action.substring(8, c1);
      String act = action.substring(c1 + 1);
      sess.selectedGroup = grp;
      sess.wizardTargetId = act;
      sessionMgr.generateConfirmationToken(sess, grp, WIZ_GROUP_CONFIRM, TOKEN_TTL_MS);
      sess.currentMenu = MENU_GROUP_CONFIRM;
      renderMenu(bot, chatId, sess, messageId);
    }
  }
  else if (action == "CMD:GRP_CONF:") {
    String grp = sess.selectedGroup;
    String act = sess.wizardTargetId;
    sess.currentMenu = MENU_DASHBOARD;
    sessionMgr.clearWizard(sess);

    int total = meshMgr.getNodeCount();
    int targetCount = 0;
    String cmdStr = (act == "ON") ? "LIGHT_ON" : "LIGHT_OFF";

    for (int i = 0; i < total; i++) {
      NodeRecord *n = meshMgr.getNode(i);
      if (!n) continue;
      bool match = (grp == "STREET" && n->type != "FARMER") ||
                   (grp == "FARMER" && n->type == "FARMER") ||
                   (grp == "POLE_ALL" && n->logicalId == sess.selectedPole);

      if (match) {
        targetCount++;
        loraMgr.queueCommand(chatId, n->logicalId, cmdStr, PRIO_MEDIUM);
      }
    }

    String msg = "🚀 *Group Operation Dispatched*\n• Group: `" + grp + "`\n• Action: `" + act + "`\n• Queued Targets: *" + String(targetCount) + " Poles*\nCommands are staggered to avoid LoRa radio collision.";
    notifMgr.enqueueMessage(chatId, msg, "Markdown", "", 0, NOTIF_PRIO_NORMAL);
  }
}

// ─────────────────────────────────────────────────────────────
//  BATCH COMMAND ROUTER (ALL LIGHTS ON / OFF)
// ─────────────────────────────────────────────────────────────
void TelegramCallbackManager::routeBatch(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId) {
  if (action == "BATCH:ALL_ON_REQ") {
    sess.wizardTargetId = "ALL_ON";
    sessionMgr.generateConfirmationToken(sess, "ALL_ON", WIZ_ALL_CONFIRM, TOKEN_TTL_MS);
    sess.currentMenu = MENU_ALL_CONFIRM;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action == "BATCH:ALL_OFF_REQ") {
    sess.wizardTargetId = "ALL_OFF";
    sessionMgr.generateConfirmationToken(sess, "ALL_OFF", WIZ_ALL_CONFIRM, TOKEN_TTL_MS);
    sess.currentMenu = MENU_ALL_CONFIRM;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("BATCH:EXEC:")) {
    String act = action.substring(11);
    sess.currentMenu = MENU_DASHBOARD;
    sessionMgr.clearWizard(sess);

    int total = meshMgr.getNodeCount();
    String label = (act == "ALL_ON") ? "ALL LIGHTS ON" : "ALL LIGHTS OFF";
    notifMgr.enqueueMessage(chatId, "⏳ *Dispatching " + label + " across entire grid (" + String(total) + " poles)...*", "Markdown", "", 0, NOTIF_PRIO_HIGH);

    String loraCmd = (act == "ALL_ON") ? "LIGHT_ON" : "LIGHT_OFF";
    loraMgr.queueCommand(chatId, "ALL", loraCmd, PRIO_HIGH);
  }
}

// ─────────────────────────────────────────────────────────────
//  REGISTRATION WORKFLOW ROUTER
// ─────────────────────────────────────────────────────────────
void TelegramCallbackManager::routeRegistration(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId) {
  if (action.startsWith("REG:APP:")) {
    String hwId = action.substring(8);
    int pIdx = meshMgr.findPendingRegIndex(hwId);
    if (pIdx >= 0) {
      sess.pendingRegIdx = pIdx;
      sess.wizardTargetId = hwId;
      sess.wizardState = WIZ_REG_NAME;

      String prompt = "💡 *COMMISSIONING NODE: " + hwId + "*\n\n"
                      "Please send the *Pole Friendly Name* in your next text message:\n"
                      "*(Example: `Highway Pole 1` or `North Farm Pump`)*\n\n"
                      "Type `/cancel` at any time to abort registration.";
      bot.sendMessage(chatId, prompt, "Markdown");
    }
  }
  else if (action.startsWith("REG:REJ:")) {
    String hwId = action.substring(8);
    int pIdx = meshMgr.findPendingRegIndex(hwId);
    if (pIdx >= 0) {
      meshMgr.clearPendingReg(pIdx);
      gwRegistry.rejectNode(hwId.c_str(), 1);
      bot.sendMessage(chatId, "❌ *Rejected join request for device:* `" + hwId + "`", "Markdown");
      sess.currentMenu = MENU_REG_PENDING;
      renderMenu(bot, chatId, sess, messageId);
    }
  }
  else if (action.startsWith("REG_T:SL:")) {
    String hwId = action.substring(9);
    int pIdx = meshMgr.findPendingRegIndex(hwId);
    if (pIdx >= 0) {
      PendingRegistration *p = meshMgr.getPendingReg(pIdx);
      if (p) p->type = "STREET";
      sess.pendingRegIdx = pIdx;
      sess.wizardTargetId = hwId;
      sess.wizardState = WIZ_REG_NAME;
      bot.sendMessage(chatId, "💡 *Street Light Selected.* Now enter Pole Name:\n(Or /cancel)", "Markdown");
    }
  }
  else if (action.startsWith("REG_T:FR:")) {
    String hwId = action.substring(9);
    int pIdx = meshMgr.findPendingRegIndex(hwId);
    if (pIdx >= 0) {
      PendingRegistration *p = meshMgr.getPendingReg(pIdx);
      if (p) p->type = "FARMER";
      sess.pendingRegIdx = pIdx;
      sess.wizardTargetId = hwId;
      sess.wizardState = WIZ_REG_NAME;
      bot.sendMessage(chatId, "🌾 *Farmer Station Selected.* Now enter Station / Pump Name:\n(Or /cancel)", "Markdown");
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  DELETION & REVOCATION ROUTER (2-STEP VERIFICATION)
// ─────────────────────────────────────────────────────────────
void TelegramCallbackManager::routeDeletion(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId) {
  if (action.startsWith("DEL:IMPACT:")) {
    String pId = action.substring(11);
    sess.selectedPole = pId;
    sess.currentMenu = MENU_DELETE_MGMT;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("DEL:CONF_REQ:")) {
    String pId = action.substring(13);
    sess.selectedPole = pId;
    sessionMgr.generateConfirmationToken(sess, pId, WIZ_DELETE_CONFIRM, TOKEN_TTL_MS);
    sess.currentMenu = MENU_DELETE_CONFIRM;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("DEL:EXEC:")) {
    int c1 = action.indexOf(':', 9);
    if (c1 > 0) {
      String pId = action.substring(9, c1);
      String token = action.substring(c1 + 1);

      if (!sessionMgr.validateConfirmationToken(sess, token)) {
        bot.sendMessage(chatId, "⚠️ *Confirmation Failed:* Token expired or invalid. Deletion aborted.", "Markdown");
        sess.currentMenu = MENU_DELETE_MGMT;
        renderMenu(bot, chatId, sess, messageId);
        return;
      }

      // Execute Authoritative Revocation
      String outTxId, outConfirmToken, outMsg;
      String adminId = "ADMIN:" + chatId;
      bool reqOk = revocationMgr.requestNodeDeletion(adminId, pId, outTxId, outConfirmToken, outMsg);

      if (reqOk) {
        String resultMsg;
        bool confOk = revocationMgr.confirmNodeDeletion(adminId, outTxId, outConfirmToken, resultMsg);

        if (confOk) {
          bot.sendMessage(chatId, "✅ *POLE PERMANENTLY REVOKED & DELETED*\n\n" + resultMsg, "Markdown");
        } else {
          bot.sendMessage(chatId, "❌ *Deletion Confirmation Failed:* " + resultMsg, "Markdown");
        }
      } else {
        bot.sendMessage(chatId, "❌ *Deletion Request Failed:* " + outMsg, "Markdown");
      }

      sessionMgr.clearWizard(sess);
      sess.selectedPole = "";
      sess.currentMenu = MENU_DELETE_MGMT;
      renderMenu(bot, chatId, sess, 0);
    }
  }
  else if (action.startsWith("DEL:CANCEL:")) {
    sessionMgr.clearWizard(sess);
    sess.selectedPole = "";
    sess.currentMenu = MENU_DELETE_MGMT;
    renderMenu(bot, chatId, sess, messageId);
  }
  // Feeder Deactivation
  else if (action.startsWith("FDR:DEACT_REQ:")) {
    int c1 = action.indexOf(':', 14);
    if (c1 > 0) {
      String pId = action.substring(14, c1);
      uint8_t fdr = (uint8_t)action.substring(c1 + 1).toInt();

      String outTx, outTok, outMsg;
      String adminId = "ADMIN:" + chatId;
      bool ok = revocationMgr.requestFeederDeactivation(adminId, pId, fdr, outTx, outTok, outMsg);
      if (ok) {
        String resMsg;
        revocationMgr.confirmFeederDeactivation(adminId, outTx, outTok, resMsg);
        bot.sendMessage(chatId, "✅ *Feeder Deactivated Successfully*\n\n" + resMsg, "Markdown");
      } else {
        bot.sendMessage(chatId, "❌ *Feeder Deactivation Failed:* " + outMsg, "Markdown");
      }
      sess.currentMenu = MENU_FEEDER_MGMT;
      renderMenu(bot, chatId, sess, messageId);
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  WI-FI CONFIGURATION & RECOVERY ROUTER (PROMPT 17)
// ─────────────────────────────────────────────────────────────
void TelegramCallbackManager::routeWiFi(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId) {
  if (action == "NAV:WIFI" || action == "WIFI_MENU" || action == "WIFI_REFRESH") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_WIFI_SETTINGS;
    renderMenu(bot, chatId, sess, messageId);
  }
  else if (action.startsWith("WIFI_")) {
    bot.sendMessage(chatId, "🔒 *Security Notice:* Wi-Fi credential modification is disabled in this stage (Prompt 16 security policy).", "Markdown");
    sess.currentMenu = MENU_WIFI_SETTINGS;
    renderMenu(bot, chatId, sess, messageId);
  }
}
