#line 1 "C:\\Users\\panka\\OneDrive\\Desktop\\PS\\MSCB_SmartPole_System_R1\\Gateway_ESP32S3\\GwWiFiManager.h"
/**
 * @file GwWiFiManager.h
 * @brief MSCB Smart Pole Gateway - Wi-Fi Station Manager with Safe Non-Destructive Rollback
 * @version 1.0.0
 * 
 * Enforces:
 * - Persistent storage of primary valid credentials in NVS.
 * - Non-destructive testing with temporary credentials.
 * - DNS & Telegram API reachability verification before committing.
 * - Automatic rollback to previous valid network on failure.
 * - Emergency Local SoftAP Fallback if all station connections fail.
 * - Continuous servicing of background LoRa mesh during connection attempts.
 * - Zero plaintext password leakage in logs or responses.
 */

#ifndef GW_WIFI_MANAGER_H
#define GW_WIFI_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <WiFi.h>
  #include <Preferences.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "GwConfig.h"

enum WiFiManagerState : uint8_t {
  WIFI_STATE_DISCONNECTED     = 0,
  WIFI_STATE_CONNECTING       = 1,
  WIFI_STATE_CONNECTED        = 2,
  WIFI_STATE_TESTING_NEW_CRED = 3,
  WIFI_STATE_ROLLING_BACK     = 4,
  WIFI_STATE_FALLBACK_AP      = 5
};

inline const char* wifiStateToString(WiFiManagerState st) {
  switch (st) {
    case WIFI_STATE_DISCONNECTED:     return "DISCONNECTED";
    case WIFI_STATE_CONNECTING:       return "CONNECTING";
    case WIFI_STATE_CONNECTED:        return "CONNECTED";
    case WIFI_STATE_TESTING_NEW_CRED: return "TESTING_NEW_CREDENTIALS";
    case WIFI_STATE_ROLLING_BACK:     return "ROLLING_BACK";
    case WIFI_STATE_FALLBACK_AP:      return "FALLBACK_AP_ACTIVE";
    default:                          return "UNKNOWN";
  }
}

class GwWiFiManager {
public:
  GwWiFiManager();

  bool begin();
  void service(); // Non-blocking state machine ticker

  // Safe Non-Destructive Update API
  bool requestCredentialUpdate(const char* newSsid, const char* newPassword);

  // Status API
  bool isConnected() const;
  bool isInternetVerified() const { return internetVerified; }
  WiFiManagerState getState() const { return state; }
  const char* getActiveSsid() const { return activeSsid; }
  int8_t getRssi() const;
  const char* getLocalIp() const;

  // Manual Trigger
  void reconnectStation();

private:
  bool verifyInternetReachability();
  void commitCredentials(const char* ssid, const char* pass);
  void startFallbackAp();

  WiFiManagerState state;
  char             activeSsid[33];
  char             activePass[65];

  // Temporary staging buffer during validation
  char             stagingSsid[33];
  char             stagingPass[65];

  uint32_t         connectStartTimeMs;
  uint32_t         lastReconnectAttemptMs;
  uint8_t          reconnectFailures;
  bool             internetVerified;

#if defined(ARDUINO)
  Preferences      prefs;
#endif
};

extern GwWiFiManager gwWifi;

#endif // GW_WIFI_MANAGER_H
