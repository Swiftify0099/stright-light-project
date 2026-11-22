/*
 * =====================================================================
 *  TELEGRAMNOTIFICATIONMANAGER.CPP — MESSAGE QUEUE, RATE LIMITING & ALERT DEDUP
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#include "TelegramNotificationManager.h"
#include <UniversalTelegramBot.h>

TelegramNotificationManager notifMgr;

TelegramNotificationManager::TelegramNotificationManager() :
  alertCacheHead(0),
  lastGlobalSendMs(0),
  lastChatSendMs(0),
  lastChatId("")
{
  for (int i = 0; i < MAX_NOTIF_QUEUE; i++) {
    queue[i].active = false;
  }
  for (int i = 0; i < MAX_ALERT_CACHE; i++) {
    alertCache[i].poleId[0] = '\0';
    alertCache[i].faultType[0] = '\0';
    alertCache[i].lastSentMs = 0;
  }
}

void TelegramNotificationManager::init() {
  lastGlobalSendMs = 0;
  lastChatSendMs = 0;
  lastChatId = "";
  alertCacheHead = 0;
  Serial.println(F("[GW][NOTIF] Initialized Telegram Notification Manager & Rate Limiter."));
}

String TelegramNotificationManager::safeTruncate(const String &text, size_t maxLen) {
  if (text.length() <= maxLen) return text;

  String truncated = text.substring(0, maxLen - 64);
  truncated += "\n\n⚠️ *[Message truncated due to Telegram size limit]*";

  // Balance unclosed backticks to avoid markdown parsing error
  int backtickCount = 0;
  for (size_t i = 0; i < truncated.length(); i++) {
    if (truncated[i] == '`') backtickCount++;
  }
  if (backtickCount % 2 != 0) {
    truncated += "`";
  }

  return truncated;
}

bool TelegramNotificationManager::enqueueMessage(const String &chatId, const String &text,
                                                const String &parseMode, const String &keyboard,
                                                int messageId, uint8_t priority) {
  if (chatId.length() == 0 || text.length() == 0) return false;

  String safeText = safeTruncate(text);

  // Search for available slot
  int emptyIdx = -1;
  for (int i = 0; i < MAX_NOTIF_QUEUE; i++) {
    if (!queue[i].active) {
      emptyIdx = i;
      break;
    }
  }

  if (emptyIdx < 0) {
    Serial.println(F("[GW][NOTIF] ⚠️ Notification Queue Full! Dropping oldest LOW priority message."));
    int lowestPrioIdx = -1;
    uint8_t minPrio = 255;
    unsigned long oldestTime = 0xFFFFFFFFUL;

    for (int i = 0; i < MAX_NOTIF_QUEUE; i++) {
      if (queue[i].priority < minPrio || (queue[i].priority == minPrio && queue[i].enqueuedMs < oldestTime)) {
        minPrio = queue[i].priority;
        oldestTime = queue[i].enqueuedMs;
        lowestPrioIdx = i;
      }
    }

    if (lowestPrioIdx >= 0 && queue[lowestPrioIdx].priority < priority) {
      emptyIdx = lowestPrioIdx;
    } else {
      Serial.println(F("[GW][NOTIF] ❌ Cannot enqueue: Queue has higher priority messages."));
      return false;
    }
  }

  queue[emptyIdx].active = true;
  queue[emptyIdx].chatId = chatId;
  queue[emptyIdx].text = safeText;
  queue[emptyIdx].parseMode = parseMode;
  queue[emptyIdx].keyboard = keyboard;
  queue[emptyIdx].messageId = messageId;
  queue[emptyIdx].priority = priority;
  queue[emptyIdx].retries = 0;
  queue[emptyIdx].enqueuedMs = millis();

  return true;
}

void TelegramNotificationManager::processOutgoingQueue(UniversalTelegramBot &bot, bool isOnline) {
  if (!isOnline) return;

  unsigned long now = millis();

  // Enforce global rate limit
  if (now - lastGlobalSendMs < TG_MIN_SEND_INTERVAL_MS) {
    return;
  }

  // Find highest priority queued message
  int bestIdx = -1;
  int highestPrio = -1;
  unsigned long oldestEnqueue = 0xFFFFFFFFUL;

  for (int i = 0; i < MAX_NOTIF_QUEUE; i++) {
    if (queue[i].active) {
      // Per-chat cooldown check
      if (queue[i].chatId == lastChatId && (now - lastChatSendMs < TG_PER_CHAT_INTERVAL_MS)) {
        continue;
      }

      if ((int)queue[i].priority > highestPrio ||
          ((int)queue[i].priority == highestPrio && queue[i].enqueuedMs < oldestEnqueue)) {
        highestPrio = (int)queue[i].priority;
        oldestEnqueue = queue[i].enqueuedMs;
        bestIdx = i;
      }
    }
  }

  if (bestIdx < 0) return;

  // Dispatch message
  bool success = false;
  QueuedMessage &msg = queue[bestIdx];

  if (msg.keyboard.length() > 0) {
    if (msg.messageId > 0) {
      success = bot.sendMessageWithInlineKeyboard(msg.chatId, msg.text, msg.parseMode, msg.keyboard, msg.messageId);
      if (!success) {
        // Fallback to sending fresh message if edit failed
        success = bot.sendMessageWithInlineKeyboard(msg.chatId, msg.text, msg.parseMode, msg.keyboard, 0);
      }
    } else {
      success = bot.sendMessageWithInlineKeyboard(msg.chatId, msg.text, msg.parseMode, msg.keyboard, 0);
    }
  } else {
    success = bot.sendMessage(msg.chatId, msg.text, msg.parseMode);
  }

  lastGlobalSendMs = now;
  lastChatSendMs = now;
  lastChatId = msg.chatId;

  if (success) {
    msg.active = false;
  } else {
    msg.retries++;
    if (msg.retries >= MAX_RETRIES_PER_MSG) {
      Serial.printf("[GW][NOTIF] ❌ Message to %s failed after %d retries. Dropped.\n",
                    msg.chatId.c_str(), msg.retries);
      msg.active = false;
    } else {
      Serial.printf("[GW][NOTIF] ⚠️ Send failed. Retry scheduled (%d/%d).\n",
                    msg.retries, MAX_RETRIES_PER_MSG);
    }
  }
}

bool TelegramNotificationManager::sendImmediate(UniversalTelegramBot &bot, const String &chatId,
                                                const String &text, const String &parseMode,
                                                const String &keyboard, int messageId) {
  unsigned long now = millis();
  String safeText = safeTruncate(text);

  // Check rate limit
  if (now - lastGlobalSendMs < TG_MIN_SEND_INTERVAL_MS) {
    return enqueueMessage(chatId, safeText, parseMode, keyboard, messageId, NOTIF_PRIO_HIGH);
  }

  bool success = false;
  if (keyboard.length() > 0) {
    if (messageId > 0) {
      success = bot.sendMessageWithInlineKeyboard(chatId, safeText, parseMode, keyboard, messageId);
      if (!success) {
        success = bot.sendMessageWithInlineKeyboard(chatId, safeText, parseMode, keyboard, 0);
      }
    } else {
      success = bot.sendMessageWithInlineKeyboard(chatId, safeText, parseMode, keyboard, 0);
    }
  } else {
    success = bot.sendMessage(chatId, safeText, parseMode);
  }

  lastGlobalSendMs = millis();
  lastChatSendMs = millis();
  lastChatId = chatId;

  if (!success) {
    // If immediate failed, push to queue with high priority
    return enqueueMessage(chatId, safeText, parseMode, keyboard, messageId, NOTIF_PRIO_HIGH);
  }

  return true;
}

// ─────────────────────────────────────────────────────────────
//  DUPLICATE ALERT SUPPRESSION ENGINE
// ─────────────────────────────────────────────────────────────
bool TelegramNotificationManager::shouldSuppressAlert(const String &poleId, const String &faultType) {
  unsigned long now = millis();

  for (int i = 0; i < MAX_ALERT_CACHE; i++) {
    if (strcmp(alertCache[i].poleId, poleId.c_str()) == 0 &&
        strcmp(alertCache[i].faultType, faultType.c_str()) == 0) {
      if (now - alertCache[i].lastSentMs < ALERT_SUPPRESSION_WINDOW_MS) {
        return true; // Suppress duplicate alert within window
      }
    }
  }
  return false;
}

void TelegramNotificationManager::recordAlertSent(const String &poleId, const String &faultType) {
  unsigned long now = millis();

  for (int i = 0; i < MAX_ALERT_CACHE; i++) {
    if (strcmp(alertCache[i].poleId, poleId.c_str()) == 0 &&
        strcmp(alertCache[i].faultType, faultType.c_str()) == 0) {
      alertCache[i].lastSentMs = now;
      return;
    }
  }

  // Insert into circular cache
  strncpy(alertCache[alertCacheHead].poleId, poleId.c_str(), sizeof(alertCache[alertCacheHead].poleId) - 1);
  alertCache[alertCacheHead].poleId[sizeof(alertCache[alertCacheHead].poleId) - 1] = '\0';

  strncpy(alertCache[alertCacheHead].faultType, faultType.c_str(), sizeof(alertCache[alertCacheHead].faultType) - 1);
  alertCache[alertCacheHead].faultType[sizeof(alertCache[alertCacheHead].faultType) - 1] = '\0';

  alertCache[alertCacheHead].lastSentMs = now;
  alertCacheHead = (alertCacheHead + 1) % MAX_ALERT_CACHE;
}

void TelegramNotificationManager::clearAlert(const String &poleId) {
  for (int i = 0; i < MAX_ALERT_CACHE; i++) {
    if (strcmp(alertCache[i].poleId, poleId.c_str()) == 0) {
      alertCache[i].poleId[0] = '\0';
      alertCache[i].faultType[0] = '\0';
      alertCache[i].lastSentMs = 0;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  FORMATTED NOTIFICATION GENERATORS
// ─────────────────────────────────────────────────────────────
void TelegramNotificationManager::notifyFault(UniversalTelegramBot &bot, const String &targetChat,
                                             const String &poleId, const String &poleName,
                                             const String &faultType, const String &details) {
  if (shouldSuppressAlert(poleId, faultType)) {
    Serial.printf("[GW][NOTIF] Suppressed duplicate fault alert '%s' for %s\n",
                  faultType.c_str(), poleId.c_str());
    return;
  }

  recordAlertSent(poleId, faultType);

  String msg = "🚨 *CRITICAL SAFETY ALERT*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📍 *Pole:* *" + poleName + "* (`" + poleId + "`)\n";
  msg += "⚠️ *Fault:* `" + faultType + "`\n";
  if (details.length() > 0) {
    msg += "📋 *Details:*\n" + details + "\n";
  }
  msg += "🕒 *Timestamp:* `" + String((unsigned long)(millis() / 1000)) + "s uptime`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "💡 *Action Required:* Inspect pole electrical parameters or disconnect feeder.";

  enqueueMessage(targetChat, msg, "Markdown", "", 0, NOTIF_PRIO_HIGH);
}

void TelegramNotificationManager::notifyRecovery(UniversalTelegramBot &bot, const String &targetChat,
                                                const String &poleId, const String &poleName,
                                                const String &details) {
  clearAlert(poleId);

  String msg = "🟢 *POLE COMM RECOVERED*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📍 *Pole:* *" + poleName + "* (`" + poleId + "`)\n";
  msg += "✅ *Status:* Back ONLINE via LoRa mesh\n";
  if (details.length() > 0) {
    msg += "📋 *Link:* " + details + "\n";
  }
  msg += "━━━━━━━━━━━━━━━━━━━━";

  enqueueMessage(targetChat, msg, "Markdown", "", 0, NOTIF_PRIO_NORMAL);
}

void TelegramNotificationManager::notifyOffline(UniversalTelegramBot &bot, const String &targetChat,
                                               const String &poleId, const String &poleName,
                                               unsigned long elapsedMs) {
  if (shouldSuppressAlert(poleId, "OFFLINE")) {
    return;
  }

  recordAlertSent(poleId, "OFFLINE");

  String msg = "🔴 *NODE OFFLINE TIMEOUT*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "📍 *Pole:* *" + poleName + "* (`" + poleId + "`)\n";
  msg += "⚠️ *Status:* No packet received for `" + String(elapsedMs / 1000) + " seconds`.\n";
  msg += "🗺️ *Topology:* Node marked OFFLINE. Children will initiate DAG parent failover.\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";

  enqueueMessage(targetChat, msg, "Markdown", "", 0, NOTIF_PRIO_HIGH);
}

int TelegramNotificationManager::getQueueLength() const {
  int count = 0;
  for (int i = 0; i < MAX_NOTIF_QUEUE; i++) {
    if (queue[i].active) count++;
  }
  return count;
}
