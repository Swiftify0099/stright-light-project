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
#include "GwTelegramCert.h"

#define GW_TG_NOTIFY_QUEUE_CAPACITY 64
#define GW_TG_MAX_RETRIES           5
#define GW_TG_DEFAULT_THROTTLE_MS   3000UL
#define GW_TG_GROUP_BATCH_MAX       12

enum TgNotifyEventType : uint8_t {
  TG_EVT_NONE             = 0,
  TG_EVT_SYSTEM_ONLINE    = 1,
  TG_EVT_PENDING_REG      = 2,
  TG_EVT_REG_CONFIRMED    = 3,
  TG_EVT_REG_REJECTED     = 4,
  TG_EVT_REVOKED_REAPPEAR = 5,
  TG_EVT_POLE_OFFLINE     = 6,
  TG_EVT_POLE_UNREACHABLE = 7,
  TG_EVT_POLE_RECOVERED   = 8,
  TG_EVT_CMD_RESULT       = 9,
  TG_EVT_GENERAL_ALERT    = 10
};

enum TgDeliveryState : uint8_t {
  TG_DELIVERY_PENDING           = 0,
  TG_DELIVERY_RETRY_SCHEDULED   = 1,
  TG_DELIVERY_SENT_CONFIRMED    = 2,
  TG_DELIVERY_UNKNOWN_AMBIGUOUS = 3,
  TG_DELIVERY_FAILED_PERMANENT  = 4
};

enum TgTlsStatus : uint8_t {
  TG_TLS_OK                      = 0,
  TG_TLS_ERR_TIME_UNSYNCED       = 1,
  TG_TLS_ERR_CERT_UNAVAILABLE    = 2,
  TG_TLS_ERR_CERT_INVALID        = 3,
  TG_TLS_ERR_HANDSHAKE_FAILED    = 4,
  TG_TLS_ERR_NETWORK_UNAVAILABLE = 5
};

enum TgTransportOutcome : uint8_t {
  TG_TX_OK                = 0,
  TG_TX_PARSE_ERROR       = 1,
  TG_TX_RATE_LIMITED      = 2,
  TG_TX_TRANSIENT_FAILURE = 3,
  TG_TX_AMBIGUOUS_TIMEOUT = 4,
  TG_TX_PERMANENT_ERROR   = 5
};

struct TgNotifyQueueItem {
  bool              active;
  uint32_t          eventId;
  TgNotifyEventType eventType;
  uint8_t           mac[6];
  uint16_t          poleId;
  uint32_t          eventTimestampSec;
  char              reason[48];
  char              dedupKey[48];
  char              text[384];
  TgDeliveryState   deliveryState;
  uint8_t           retryCount;
  uint32_t          nextAttemptMs;
  uint32_t          lastAttemptMs;
};

struct TgBroadcastSummary {
  uint8_t            totalRecipients;
  uint8_t            confirmedCount;
  uint8_t            ambiguousCount;
  uint8_t            transientFailCount;
  uint8_t            permanentFailCount;
  uint32_t           maxRetryAfterSec;
  TgTransportOutcome overallOutcome;
};

class GwTelegramBot {
public:
  GwTelegramBot();

  bool begin();
  void service(); // Non-blocking polling ticker
  void applyTlsSecurity();

  bool isAuthorizedAdmin(int64_t chatId) const;
  bool isAuthorizedSender(int64_t fromUserId, int64_t chatId, bool isGroupChat = false) const;
  TgTlsStatus getTlsStatus() const { return tlsStatus; }
  bool isTlsValid() const { return (tlsStatus == TG_TLS_OK); }
  bool sendMessage(int64_t chatId, const char* text);
  TgTransportOutcome sendMessageWithOutcome(int64_t chatId, const char* text, uint32_t &outRetryAfterSec);
  static TgTransportOutcome parseTelegramApiResponse(const char* jsonResp, uint32_t &outRetryAfterSec);
  void sendMessageWithKeyboard(int64_t chatId, const char* text, const char* keyboardJson);
  void sendReplyKeyboard(int64_t chatId, const char* text);
  bool broadcastAdminAlert(const char* alertText);
  bool broadcastAdminAlertDetailed(const char* alertText, TgBroadcastSummary &outSummary);

  // Bounded Reliable Notification Queue (R3.2.2 P0)
  bool enqueueNotification(TgNotifyEventType type, const char* dedupKey, const char* text,
                           const uint8_t* mac = nullptr, uint16_t poleId = 0,
                           uint32_t tsSec = 0, const char* reason = nullptr);
  void processNotificationQueue(uint32_t nowMs);
  void resetNotificationQueue();
  uint8_t  getNotifyQueueCount() const { return notifyQueueCount; }
  uint32_t getNotifyInsertedCount() const { return notifyInsertedCount; }
  uint32_t getNotifyDeliveredCount() const { return notifyDeliveredCount; }
  uint32_t getNotifyAmbiguousCount() const { return notifyAmbiguousCount; }
  uint32_t getNotifyFailedPermanentCount() const { return notifyFailedPermanentCount; }
  uint32_t getNotifyDedupSuppressedCount() const { return notifyDedupSuppressedCount; }
  uint32_t getNotifyOverflowDroppedCount() const { return notifyOverflowDroppedCount; }
  uint32_t getNotifyRetryCount() const { return notifyRetryCount; }
  uint32_t getRateLimitUntilMs() const { return rateLimitUntilMs; }
  void setAlertThrottleMs(uint32_t ms) { alertThrottleMs = ms; }
  uint32_t getAlertThrottleMs() const { return alertThrottleMs; }
  const TgNotifyQueueItem* getNotifyQueueItem(uint8_t idx) const {
    return (idx < GW_TG_NOTIFY_QUEUE_CAPACITY && notifyQueue[idx].active) ? &notifyQueue[idx] : nullptr;
  }

#if !defined(ARDUINO)
  typedef TgTransportOutcome (*TgMockSendFn)(int64_t chatId, const char* text, const char* parseMode, uint32_t &outRetryAfterSec);
  void setMockSendHook(TgMockSendFn fn) { mockSendHook = fn; }
  void setTestAllowlistOverride(const int64_t* ids, size_t count);
  void clearTestAllowlistOverride();
  void setMockTimeSynced(bool synced) { mockTimeSynced = synced; applyTlsSecurity(); }
  void setMockCertValid(bool valid) { mockCertValid = valid; applyTlsSecurity(); }
  void setMockSendFail(bool fail) { mockSendFail = fail; }
#endif

  // Command & Callback handlers
  void handleCommand(int64_t chatId, const char* cmdText, int64_t fromUserId = 0LL, bool isGroupChat = false);
  void handleCallback(int64_t chatId, const char* callbackData, int64_t fromUserId = 0LL, bool isGroupChat = false);

  // Interactive Quick-Reply Screen Builders
  void showMainMenu(int64_t chatId);
  void showDashboard(int64_t chatId);
  void showPolesMenu(int64_t chatId, uint8_t page = 0);
  void showPoleDetail(int64_t chatId, uint16_t poleId);
  void showPoleFeederList(int64_t chatId, uint16_t poleId, uint8_t page = 0);
  void showFeederControlDetail(int64_t chatId, uint16_t poleId, uint8_t feederId);
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
  void showScheduleFeederSelect(int64_t chatId, uint16_t poleId, uint8_t page = 0);
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
  uint32_t lastAlertBroadcastMs;
  uint32_t alertThrottleMs;
  uint32_t rateLimitUntilMs;
  uint32_t nextEventId;
  uint8_t  notifyQueueCount;
  uint32_t notifyInsertedCount;
  uint32_t notifyDeliveredCount;
  uint32_t notifyAmbiguousCount;
  uint32_t notifyFailedPermanentCount;
  uint32_t notifyDedupSuppressedCount;
  uint32_t notifyOverflowDroppedCount;
  uint32_t notifyRetryCount;
  TgNotifyQueueItem notifyQueue[GW_TG_NOTIFY_QUEUE_CAPACITY];
  TgTlsStatus tlsStatus;
#if !defined(ARDUINO)
  bool mockTimeSynced;
  bool mockCertValid;
  bool mockSendFail;
#endif

#if !defined(ARDUINO)
  TgMockSendFn mockSendHook;
  bool     hasTestAllowlistOverride;
  int64_t  testAllowlist[8];
  size_t   testAllowlistCount;
#endif

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

  const char* getLastSentKeyboardJson() const { return lastSentKeyboardJson; }
  const char* getLastSentMessageText() const { return lastSentMessageText; }
  void clearLastSent() {
    lastSentKeyboardJson[0] = '\0';
    lastSentMessageText[0] = '\0';
  }

private:
  char lastSentKeyboardJson[1024];
  char lastSentMessageText[2048];

private:
#if defined(ARDUINO)
  WiFiClientSecure client;
  UniversalTelegramBot bot;
#endif
};

extern GwTelegramBot gwTelegram;

#endif // GW_TELEGRAM_BOT_H
