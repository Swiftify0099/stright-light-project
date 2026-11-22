/*
 * =====================================================================
 *  GWTELEGRAM.H — MASTER CENTRAL GATEWAY TELEGRAM BOT CONTROLLER
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#ifndef GW_TELEGRAM_H
#define GW_TELEGRAM_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "config.h"
#include "Types.h"
#include "TelegramSessionManager.h"
#include "TelegramNotificationManager.h"
#include "TelegramMenuManager.h"
#include "TelegramCallbackManager.h"

class GwTelegram {
public:
  GwTelegram();
  void init();

  // Polling & Lifecycle Tasks
  void checkWiFiAndTelegram();
  void handleTelegramUpdates();
  void checkNtpSync();
  String getFormattedTime();

  // Batch command tracking (Grid-wide operations)
  void startBatchCommand(const String &chatId, const String &cmdType);
  void onBatchAckReceived(const String &srcNodeId, const String &cmd, const String &state);
  void processBatchTracker();

  // Core Messaging APIs
  bool sendMessage(const String &chatId, const String &text, const String &parseMode = "Markdown");
  bool sendMessageWithInlineKeyboard(const String &chatId, const String &text, const String &parseMode, const String &keyboard, int messageId = 0);
  bool answerCallbackQuery(const String &queryId, const String &text = "", bool showAlert = false);
  bool deleteMessage(const String &chatId, int messageId);

  // Status & Connectivity
  bool isAuthorized(const String &chatId, const String &fromId = "");
  bool isTelegramOnline() const;
  bool isWiFiConnected() const;
  bool verifyTelegramApi();
  String getWifiSsid() const;

  // Menu presentation
  void renderMenu(const String &chatId, UserSession &sess, int messageId = 0);
  void sendStartupMainMenu(const String &chatId);

  // Direct bot instance access for sub-managers
  UniversalTelegramBot& getBot() { return bot; }

private:
  WiFiClientSecure client;
  UniversalTelegramBot bot;

  String wifiSsid;
  String wifiPass;
  bool   wifiConnected;
  bool   telegramOnline;
  bool   hasNtpTime;
  bool   bootMenuSent;

  unsigned long tWifiWatchdog;
  BatchCmdTracker batchTracker;

  void handleTelegramText(const String &chatId, const String &fromId, const String &text, int messageId, bool isPrivateChat);
};

extern GwTelegram gwTelegram;

// Backward-compatibility alias for existing codebase
#define tgMgr gwTelegram

#endif // GW_TELEGRAM_H
