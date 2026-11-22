/*
 * =====================================================================
 *  TELEGRAMCALLBACKMANAGER.H — SECURE INLINE CALLBACK QUERY ROUTER
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#ifndef TELEGRAM_CALLBACK_MANAGER_H
#define TELEGRAM_CALLBACK_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

#define MAX_SEEN_QUERIES 16

class UniversalTelegramBot;

class TelegramCallbackManager {
public:
  TelegramCallbackManager();
  void init();

  // Central Callback Dispatcher
  void handleCallback(UniversalTelegramBot &bot,
                      const String &chatId,
                      const String &fromId,
                      const String &queryId,
                      const String &data,
                      int messageId);

  // Anti-Replay / Deduplication
  bool isDuplicateQuery(const String &queryId);
  void recordQuery(const String &queryId);

private:
  String seenQueries[MAX_SEEN_QUERIES];
  uint8_t seenHead;

  void routeNavigation(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId);
  void routeControl(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId);
  void routeRegistration(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId);
  void routeDeletion(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId);
  void routeBatch(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId);
  void routeWiFi(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, const String &action, int messageId);

  void renderMenu(UniversalTelegramBot &bot, const String &chatId, UserSession &sess, int messageId);
};

extern TelegramCallbackManager callbackMgr;

#endif // TELEGRAM_CALLBACK_MANAGER_H
