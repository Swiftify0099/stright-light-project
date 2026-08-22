/*
 * =====================================================================
 *  TELEGRAMMANAGER.H — TELEGRAM INTERACTION & BOT CONTROLLER
 * =====================================================================
 */

#ifndef TELEGRAM_MANAGER_H
#define TELEGRAM_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "config.h"
#include "Types.h"

class TelegramManager {
public:
  TelegramManager();
  void init();

  // Polling & Task Handlers
  void checkWiFiAndTelegram();
  void handleTelegramUpdates();
  void processBatchTracker();

  // Batch command tracking (ALL ON / ALL OFF)
  void startBatchCommand(const String &chatId, const String &cmdType);
  void onBatchAckReceived(const String &srcNodeId, const String &cmd, const String &state);

  // Message & Keyboard sending
  bool sendMessage(const String &chatId, const String &text, const String &parseMode = "Markdown");
  bool sendMessageWithInlineKeyboard(const String &chatId, const String &text, const String &parseMode, const String &keyboard, int messageId = 0);
  bool answerCallbackQuery(const String &queryId, const String &text = "", bool showAlert = false);

  // Menu navigation
  void renderMenu(const String &chatId, UserSession &sess, int messageId = 0);
  void sendStartupMainMenu(const String &chatId);

  // Status & Time helpers
  bool isAuthorized(const String &chatId, const String &fromId = "");
  UserSession& getOrCreateSession(const String &chatId);
  String getFormattedTime();
  void checkNtpSync();
  bool isTelegramOnline() const;
  bool isWiFiConnected() const;

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
  UserSession sessions[MAX_SESSIONS];
  BatchCmdTracker batchTracker;

  bool verifyTelegramApi();
  void handleTelegramCallback(const String &chatId, const String &queryId, const String &data, int messageId);
  void handleTelegramText(const String &chatId, const String &text);
};

extern TelegramManager tgMgr;

#endif // TELEGRAM_MANAGER_H
