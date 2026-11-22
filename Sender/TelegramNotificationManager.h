/*
 * =====================================================================
 *  TELEGRAMNOTIFICATIONMANAGER.H — MESSAGE QUEUE, RATE LIMITING & ALERT DEDUP
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#ifndef TELEGRAM_NOTIFICATION_MANAGER_H
#define TELEGRAM_NOTIFICATION_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

// Priority Levels
#define NOTIF_PRIO_LOW      0   // Routine telemetry summaries
#define NOTIF_PRIO_NORMAL   1   // Command feedback, menu responses
#define NOTIF_PRIO_HIGH     2   // Critical safety faults, emergency alerts

// Rate Limiting Constants
#define MAX_NOTIF_QUEUE              16
#define TG_MIN_SEND_INTERVAL_MS      60UL     // Minimum 60ms between consecutive Telegram API calls
#define TG_PER_CHAT_INTERVAL_MS      500UL    // Minimum 500ms per chat to prevent 429 Too Many Requests
#define ALERT_SUPPRESSION_WINDOW_MS  120000UL // 2 minutes alert deduplication cooldown
#define MAX_ALERT_CACHE              16
#define MAX_RETRIES_PER_MSG          2

struct QueuedMessage {
  bool          active;
  String        chatId;
  String        text;
  String        parseMode;
  String        keyboard;
  int           messageId;
  uint8_t       priority;
  uint8_t       retries;
  unsigned long enqueuedMs;
};

struct AlertCacheEntry {
  char          poleId[16];
  char          faultType[32];
  unsigned long lastSentMs;
};

class UniversalTelegramBot;

class TelegramNotificationManager {
public:
  TelegramNotificationManager();
  void init();

  // Queue Dispatcher & Poller (Invoked periodically in loop)
  void processOutgoingQueue(UniversalTelegramBot &bot, bool isOnline);

  // Message Enqueue API
  bool enqueueMessage(const String &chatId, const String &text,
                      const String &parseMode = "Markdown",
                      const String &keyboard = "",
                      int messageId = 0,
                      uint8_t priority = NOTIF_PRIO_NORMAL);

  // High-Priority Direct Send (Non-blocking queue bypass if budget permits)
  bool sendImmediate(UniversalTelegramBot &bot, const String &chatId,
                     const String &text, const String &parseMode = "Markdown",
                     const String &keyboard = "", int messageId = 0);

  // Duplicate Alert Suppression Engine
  bool shouldSuppressAlert(const String &poleId, const String &faultType);
  void recordAlertSent(const String &poleId, const String &faultType);
  void clearAlert(const String &poleId);

  // Formatted Notification Generators
  void notifyFault(UniversalTelegramBot &bot, const String &targetChat,
                   const String &poleId, const String &poleName,
                   const String &faultType, const String &details);

  void notifyRecovery(UniversalTelegramBot &bot, const String &targetChat,
                      const String &poleId, const String &poleName,
                      const String &details);

  void notifyOffline(UniversalTelegramBot &bot, const String &targetChat,
                     const String &poleId, const String &poleName,
                     unsigned long elapsedMs);

  // Helpers
  String safeTruncate(const String &text, size_t maxLen = 4000);
  int  getQueueLength() const;

private:
  QueuedMessage queue[MAX_NOTIF_QUEUE];
  AlertCacheEntry alertCache[MAX_ALERT_CACHE];
  uint8_t alertCacheHead;

  unsigned long lastGlobalSendMs;
  unsigned long lastChatSendMs;
  String        lastChatId;
};

extern TelegramNotificationManager notifMgr;

#endif // TELEGRAM_NOTIFICATION_MANAGER_H
