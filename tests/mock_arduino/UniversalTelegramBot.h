#ifndef UNIVERSAL_TELEGRAM_BOT_H
#define UNIVERSAL_TELEGRAM_BOT_H

#include <Arduino.h>

struct telegramMessage {
  String chat_id;
  String from_id;
  String type;
  String text;
  String query_id;
  int message_id;
};

class UniversalTelegramBot {
public:
  template<typename T>
  UniversalTelegramBot(const String &token, T &client) :
    userName("MSCB_CentralGateway_Bot"),
    last_sent_message_id(1001),
    last_message_received(0),
    maxMessageLength(4096),
    waitForResponse(1000)
  {}

  UniversalTelegramBot() :
    userName("MSCB_CentralGateway_Bot"),
    last_sent_message_id(1001),
    last_message_received(0),
    maxMessageLength(4096),
    waitForResponse(1000)
  {}

  String userName;
  int last_sent_message_id;
  int last_message_received;
  int maxMessageLength;
  int waitForResponse;

  bool sendMessage(const String &chat_id, const String &text, const String &parse_mode = "") { return true; }
  bool sendMessageWithInlineKeyboard(const String &chat_id, const String &text, const String &parse_mode, const String &keyboard) { return true; }
  bool answerCallbackQuery(const String &query_id, const String &text = "", bool alert = false) { return true; }
  bool editMessageText(const String &chat_id, int message_id, const String &text, const String &parse_mode = "") { return true; }
  int getUpdates(int offset) { return 0; }
  telegramMessage messages[10];
};

#endif // UNIVERSAL_TELEGRAM_BOT_H
