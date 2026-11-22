/*
 * =====================================================================
 *  WIFICONFIGMANAGER.H — SECURE WI-FI CONFIGURATION & RECOVERY
 *  MSCB Smart Pole Gateway — Prompt 17
 *  Platform: ESP32-S3 + WiFi + NVS Secure Storage
 * =====================================================================
 */

#ifndef WIFI_CONFIG_MANAGER_H
#define WIFI_CONFIG_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "Types.h"

// Wi-Fi Configuration State Machine
enum WiFiUpdateState {
  WIFI_UPDATE_IDLE = 0,
  WIFI_UPDATE_AWAITING_SSID,
  WIFI_UPDATE_AWAITING_PASS,
  WIFI_UPDATE_CONFIRM_REQUIRED,
  WIFI_UPDATE_CONNECTING,
  WIFI_UPDATE_VERIFYING_IP,
  WIFI_UPDATE_VERIFYING_DNS,
  WIFI_UPDATE_VERIFYING_TELEGRAM,
  WIFI_UPDATE_COMMITTING,
  WIFI_UPDATE_SUCCESS,
  WIFI_UPDATE_ROLLING_BACK,
  WIFI_UPDATE_ROLLBACK_DONE,
  WIFI_UPDATE_FAILED,
  WIFI_UPDATE_AP_RECOVERY
};

enum WiFiFailReason {
  FAIL_NONE = 0,
  FAIL_USER_CANCELLED,
  FAIL_SESSION_TIMEOUT,
  FAIL_INVALID_SSID,
  FAIL_INVALID_PASSWORD,
  FAIL_DUPLICATE_REQUEST,
  FAIL_UNAUTHORIZED,
  FAIL_GROUP_CHAT_REJECTED,
  FAIL_ASSOCIATION_TIMEOUT,
  FAIL_WRONG_PASSWORD,
  FAIL_NO_IP_ASSIGNED,
  FAIL_DNS_RESOLUTION,
  FAIL_TELEGRAM_API,
  FAIL_POWER_LOSS_RECOVERY
};

inline const char* wifiUpdateStateToString(WiFiUpdateState st) {
  switch (st) {
    case WIFI_UPDATE_IDLE:               return "IDLE";
    case WIFI_UPDATE_AWAITING_SSID:      return "AWAITING_SSID";
    case WIFI_UPDATE_AWAITING_PASS:      return "AWAITING_PASS";
    case WIFI_UPDATE_CONFIRM_REQUIRED:   return "CONFIRM_REQUIRED";
    case WIFI_UPDATE_CONNECTING:         return "CONNECTING";
    case WIFI_UPDATE_VERIFYING_IP:       return "VERIFYING_IP";
    case WIFI_UPDATE_VERIFYING_DNS:      return "VERIFYING_DNS";
    case WIFI_UPDATE_VERIFYING_TELEGRAM: return "VERIFYING_TELEGRAM";
    case WIFI_UPDATE_COMMITTING:         return "COMMITTING";
    case WIFI_UPDATE_SUCCESS:            return "SUCCESS";
    case WIFI_UPDATE_ROLLING_BACK:       return "ROLLING_BACK";
    case WIFI_UPDATE_ROLLBACK_DONE:      return "ROLLBACK_DONE";
    case WIFI_UPDATE_FAILED:             return "FAILED";
    case WIFI_UPDATE_AP_RECOVERY:        return "AP_RECOVERY";
    default:                             return "UNKNOWN";
  }
}

inline const char* wifiFailReasonToString(WiFiFailReason fr) {
  switch (fr) {
    case FAIL_NONE:                 return "None";
    case FAIL_USER_CANCELLED:       return "User Cancelled";
    case FAIL_SESSION_TIMEOUT:      return "Session Inactivity Timeout";
    case FAIL_INVALID_SSID:         return "Invalid SSID Format/Length";
    case FAIL_INVALID_PASSWORD:     return "Invalid Password Format/Length";
    case FAIL_DUPLICATE_REQUEST:    return "Concurrent Request In Progress";
    case FAIL_UNAUTHORIZED:         return "Unauthorized Admin ID";
    case FAIL_GROUP_CHAT_REJECTED:  return "Group Chat Submission Rejected";
    case FAIL_ASSOCIATION_TIMEOUT:  return "Network Association Timeout";
    case FAIL_WRONG_PASSWORD:       return "Authentication Failed (Wrong Password)";
    case FAIL_NO_IP_ASSIGNED:       return "DHCP Lease Failure (No IP)";
    case FAIL_DNS_RESOLUTION:       return "DNS Resolution Failed";
    case FAIL_TELEGRAM_API:         return "Telegram Bot API Unreachable";
    case FAIL_POWER_LOSS_RECOVERY:  return "Power Interruption During Test (Auto-Restored)";
    default:                        return "Unknown Failure";
  }
}

class WiFiConfigManager {
public:
  WiFiConfigManager();
  void init();
  void update();
  void processSerialCommands();

  // Diagnostics & Status Getters (Never exposes password!)
  bool isConnected() const;
  bool isInternetOk() const;
  bool isDnsOk() const;
  bool isTelegramOk() const;
  String getActiveSsid() const;
  String getLocalIpStr() const;
  String getGatewayIpStr() const;
  String getSubnetMaskStr() const;
  String getDnsIpStr() const;
  int getRssi() const;
  unsigned long getLastConnectedTime() const;
  unsigned long getLastReconnectAttempt() const;
  uint32_t getReconnectCount() const;
  WiFiUpdateState getUpdateState() const;
  WiFiFailReason getLastFailReason() const;
  bool isApRecoveryActive() const;

  // Telegram Workflow Handlers
  bool startCredentialChangeSession(const String &chatId, const String &fromId, bool isPrivateChat, String &outError);
  bool handleSsidInput(const String &chatId, const String &ssid, String &outPrompt, String &outError);
  bool handlePasswordInput(const String &chatId, const String &pass, size_t &outMaskedLen, String &outPrompt, String &outError);
  bool confirmAndApply(const String &chatId, String &outMsg);
  void cancelSession(const String &chatId, const String &reason = "User cancelled");
  bool triggerManualRollback(const String &chatId, String &outMsg);
  bool runDiagnosticTest(String &outReport);

  // Recovery & Local Maintenance
  void startRecoveryAP();
  void stopRecoveryAP();
  void factoryResetCredentials();
  void applyLocalCredentials(const String &ssid, const String &pass);

  // Session query helpers
  bool isAwaitingSsid() const { return updateState == WIFI_UPDATE_AWAITING_SSID; }
  bool isAwaitingPass() const { return updateState == WIFI_UPDATE_AWAITING_PASS; }
  bool isConfirmRequired() const { return updateState == WIFI_UPDATE_CONFIRM_REQUIRED; }
  bool isTesting() const {
    return (updateState >= WIFI_UPDATE_CONNECTING && updateState <= WIFI_UPDATE_COMMITTING) ||
           (updateState == WIFI_UPDATE_ROLLING_BACK);
  }
  String getSessionChatId() const { return sessionChatId; }
  String getCandidateSsid() const { return candidateSsid; }
  size_t getCandidatePassLen() const { return candidatePassLen; }

private:
  // Active, candidate, and previous credentials (Passwords are protected)
  String activeSsid;
  char   activePass[65];

  String previousSsid;
  char   previousPass[65];

  String candidateSsid;
  char   candidatePass[65];
  size_t candidatePassLen;

  // Session & State Machine
  WiFiUpdateState updateState;
  WiFiFailReason  lastFailReason;
  String          sessionChatId;
  unsigned long   sessionStartTime;
  unsigned long   stateStartTime;
  unsigned long   lastConnectedTime;
  unsigned long   lastReconnectAttempt;
  uint32_t        reconnectCount;
  uint32_t        consecutiveFailures;
  bool            apModeActive;
  bool            dnsVerified;
  bool            internetVerified;
  bool            telegramVerified;
  unsigned long   tWatchdog;

  // Secure memory wipe helper
  void wipeCandidateCredentials();
  void wipeBuffer(char* buf, size_t len);

  // Low-level connection helpers
  void initiateStaConnection(const String &ssid, const char* pass);
  void executeRollback(WiFiFailReason reason);
  void commitCredentials();
  void checkWatchdog();
};

extern WiFiConfigManager wifiCfgMgr;

#endif // WIFI_CONFIG_MANAGER_H
