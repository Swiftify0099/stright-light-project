/**
 * @file GwTelegramBot.h
 * @brief MSCB Smart Pole Gateway - Telegram Admin Interface
 * @version 1.0.0
 * 
 * Enforces:
 * - Admin authentication using strict explicit allowlist.
 * - Non-blocking HTTPS polling.
 * - Two-step deletion confirmation with revocation.
 * - Feeder command validation & ACK progression (no false-early success return).
 * - Safe Wi-Fi credential updates without echoing secrets.
 */

#ifndef GW_TELEGRAM_BOT_H
#define GW_TELEGRAM_BOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <WiFiClientSecure.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "GwConfig.h"
#include "GwNodeRegistry.h"
#include "GwMeshCoordinator.h"
#include "GwHeartbeatMonitor.h"
#include "GwWiFiManager.h"
#include "GwFirmwareManager.h"

class GwTelegramBot {
public:
  GwTelegramBot();

  bool begin();
  void service(); // Non-blocking polling ticker

  bool isAuthorizedAdmin(int64_t chatId) const;
  void sendMessage(int64_t chatId, const char* text);
  void broadcastAdminAlert(const char* alertText);

  // Command handlers
  void handleCommand(int64_t chatId, const char* cmdText);
  void handleCallback(int64_t chatId, const char* callbackData);

private:
  void cmdStatus(int64_t chatId);
  void cmdPoles(int64_t chatId, uint8_t page = 0);
  void cmdPending(int64_t chatId);
  void cmdPoleDetail(int64_t chatId, uint16_t poleId);
  void cmdFeeder(int64_t chatId, uint16_t poleId, uint8_t feederId, uint8_t action);
  void cmdGroup(int64_t chatId, uint16_t poleId, uint8_t groupMask, uint8_t action);
  void cmdAll(int64_t chatId, uint16_t poleId, uint8_t action);
  void cmdDeletePrompt(int64_t chatId, uint16_t poleId);
  void cmdDeleteConfirm(int64_t chatId, uint16_t poleId);
  void cmdConfig(int64_t chatId, const char* args);
  void cmdWifi(int64_t chatId, const char* args);
  void cmdOta(int64_t chatId);
  void cmdAlarms(int64_t chatId);

  uint32_t lastPollMs;
  int32_t  lastUpdateId;
  char     botToken[64];
  int64_t  primaryAdminChatId;

#if defined(ARDUINO)
  WiFiClientSecure client;
#endif
};

extern GwTelegramBot gwTelegram;

#endif // GW_TELEGRAM_BOT_H
