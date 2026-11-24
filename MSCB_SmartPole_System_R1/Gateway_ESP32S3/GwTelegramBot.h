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
  #include <UniversalTelegramBot.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "GwConfig.h"
#include "GwNodeRegistry.h"
#include "GwMeshCoordinator.h"
#include "GwHeartbeatMonitor.h"
#include "GwWiFiManager.h"
#include "GwFirmwareManager.h"
#include "GwScheduleManager.h"

class GwTelegramBot {
public:
  GwTelegramBot();

  bool begin();
  void service(); // Non-blocking polling ticker

  bool isAuthorizedAdmin(int64_t chatId) const;
  void sendMessage(int64_t chatId, const char* text);
  void sendMessageWithKeyboard(int64_t chatId, const char* text, const char* keyboardJson);
  void sendReplyKeyboard(int64_t chatId, const char* text);
  void broadcastAdminAlert(const char* alertText);

  // Command & Callback handlers
  void handleCommand(int64_t chatId, const char* cmdText);
  void handleCallback(int64_t chatId, const char* callbackData);

  // Interactive Quick-Reply Screen Builders
  void showMainMenu(int64_t chatId);
  void showDashboard(int64_t chatId);
  void showPolesMenu(int64_t chatId, uint8_t page = 0);
  void showPoleDetail(int64_t chatId, uint16_t poleId);
  void showVoltageDashboard(int64_t chatId, uint16_t poleId = 0);
  void showFeederPoleSelectMenu(int64_t chatId, uint8_t page = 0);
  void showFeederDashboard(int64_t chatId, uint16_t poleId = 0);
  void showRegistrationsMenu(int64_t chatId, uint8_t page = 0);
  void showWizardPrompt(int64_t chatId, const uint8_t mac[6]);
  void showDeleteMenu(int64_t chatId, uint8_t page = 0);
  void showDeleteConfirm(int64_t chatId, uint16_t poleId);
  void showConfirmAllPoles(int64_t chatId, uint8_t action);
  void showFeederScheduleMenu(int64_t chatId, uint8_t page = 0);
  void showSchedulePoleSelect(int64_t chatId, uint8_t page = 0);
  void showScheduleFeederSelect(int64_t chatId, uint16_t poleId);
  void showScheduleActionSelect(int64_t chatId, uint16_t poleId, uint8_t feederId);
  void showMeshTimeSync(int64_t chatId);
  void showWifiSettings(int64_t chatId);
  void showFirmwareOta(int64_t chatId);
  void showAlarmLog(int64_t chatId);
  void showSettingsMenu(int64_t chatId);

  // Feeder Naming & Persistence (50 Poles)
  const char* getFeederName(uint16_t poleId, uint8_t feederId) const;
  void setFeederName(uint16_t poleId, uint8_t feederId, const char* name);
  bool saveFeederNames();
  bool loadFeederNames();

  // Network-wide Execution
  void executeAllPolesNetworkWide(int64_t chatId, uint8_t action);

  bool isOnline() const { return telegramOnline; }

private:
  void checkOnlineAndNotify();
  void pollUpdates();
  void serviceTrackedCommands();

  void cmdStart(int64_t chatId);
  void cmdStatus(int64_t chatId);
  void cmdPoles(int64_t chatId, uint8_t page = 0);
  void cmdPending(int64_t chatId);
  void cmdPending(int64_t chatId, uint8_t page);
  void cmdPoleDetail(int64_t chatId, uint16_t poleId);
  void cmdFeeder(int64_t chatId, uint16_t poleId, uint8_t feederId, uint8_t action);
  void cmdGroup(int64_t chatId, uint16_t poleId, uint8_t groupMask, uint8_t action);
  void cmdAll(int64_t chatId, uint16_t poleId, uint8_t action);
  void cmdDeletePrompt(int64_t chatId, uint16_t poleId);
  void cmdDeleteConfirm(int64_t chatId, uint16_t poleId);
  void cmdRevoked(int64_t chatId);
  void cmdRestorePrompt(int64_t chatId, const uint8_t mac[6]);
  void cmdRestoreConfirm(int64_t chatId, const uint8_t mac[6]);
  void cmdConfig(int64_t chatId, const char* args);
  void cmdWifi(int64_t chatId, const char* args);
  void cmdOta(int64_t chatId);
  void cmdAlarms(int64_t chatId);

  uint32_t lastPollMs;
  int32_t  lastUpdateId;
  char     botToken[64];
  int64_t  primaryAdminChatId;
  bool     telegramOnline;
  bool     startupNotified;
  uint32_t lastVerifyAttemptMs;

  struct ActiveRegistrationWizard {
    bool     active;
    int64_t  adminChatId;
    uint8_t  mac[6];
    uint32_t startMs;
    uint8_t  currentStep; // 1 = Name, 2 = Location, 3 = Area/Landmark, 4 = Review
  };

  ActiveRegistrationWizard activeWizard;

public:
  const ActiveRegistrationWizard& getActiveWizard() const { return activeWizard; }
  void resetActiveWizard() { memset(&activeWizard, 0, sizeof(activeWizard)); }

private:
#if defined(ARDUINO)
  WiFiClientSecure client;
  UniversalTelegramBot bot;
#endif
};

extern GwTelegramBot gwTelegram;

#endif // GW_TELEGRAM_BOT_H
