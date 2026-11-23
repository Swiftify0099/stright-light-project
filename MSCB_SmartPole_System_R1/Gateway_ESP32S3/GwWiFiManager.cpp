/**
 * @file GwWiFiManager.cpp
 * @brief MSCB Smart Pole Gateway - Wi-Fi Station Manager with Safe Non-Destructive Rollback Implementation
 * @version 1.0.0
 */

#include "GwWiFiManager.h"
#include <string.h>

#if defined(ARDUINO)
  #include <WiFiClientSecure.h>
#endif

GwWiFiManager gwWifi;

GwWiFiManager::GwWiFiManager() :
  state(WIFI_STATE_DISCONNECTED),
  connectStartTimeMs(0),
  lastReconnectAttemptMs(0),
  reconnectFailures(0),
  internetVerified(false)
{
  memset(activeSsid, 0, sizeof(activeSsid));
  memset(activePass, 0, sizeof(activePass));
  memset(stagingSsid, 0, sizeof(stagingSsid));
  memset(stagingPass, 0, sizeof(stagingPass));
}

bool GwWiFiManager::begin() {
#if defined(ARDUINO)
  prefs.begin("mscb_wifi", false);
  String storedSsid = prefs.getString("ssid", MSCB_WIFI_SSID);
  String storedPass = prefs.getString("pass", MSCB_WIFI_PASS);
  prefs.end();

  strncpy(activeSsid, storedSsid.c_str(), sizeof(activeSsid) - 1);
  strncpy(activePass, storedPass.c_str(), sizeof(activePass) - 1);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
#else
  strncpy(activeSsid, MSCB_WIFI_SSID, sizeof(activeSsid) - 1);
  strncpy(activePass, MSCB_WIFI_PASS, sizeof(activePass) - 1);
#endif

  reconnectStation();
  return true;
}

void GwWiFiManager::reconnectStation() {
  if (strlen(activeSsid) == 0 || strcmp(activeSsid, "CONFIGURE_YOUR_SSID_IN_PRIVATE_CONFIG") == 0) {
    state = WIFI_STATE_DISCONNECTED;
    return;
  }

#if defined(ARDUINO)
  WiFi.disconnect();
  WiFi.begin(activeSsid, activePass);
#endif

  state = WIFI_STATE_CONNECTING;
  connectStartTimeMs = millis();
  internetVerified = false;
}

bool GwWiFiManager::requestCredentialUpdate(const char* newSsid, const char* newPassword) {
  if (!newSsid || strlen(newSsid) == 0) return false;
  if (!newPassword || strlen(newPassword) < 8) return false;

  // Staging credentials without erasing current NVS
  strncpy(stagingSsid, newSsid, sizeof(stagingSsid) - 1);
  stagingSsid[sizeof(stagingSsid) - 1] = '\0';
  strncpy(stagingPass, newPassword, sizeof(stagingPass) - 1);
  stagingPass[sizeof(stagingPass) - 1] = '\0';

  state = WIFI_STATE_TESTING_NEW_CRED;
  connectStartTimeMs = millis();

#if defined(ARDUINO)
  WiFi.disconnect();
  WiFi.begin(stagingSsid, stagingPass);
#endif

  return true;
}

bool GwWiFiManager::verifyInternetReachability() {
#if defined(ARDUINO)
  // Check IP assignment
  if (WiFi.status() != WL_CONNECTED) return false;

  // Test DNS resolution for api.telegram.org
  IPAddress telegramIp;
  if (WiFi.hostByName("api.telegram.org", telegramIp) != 1) {
    return false;
  }

  // Test TLS connection using domain name to ensure SNI header is sent
  WiFiClientSecure testClient;
  testClient.setTimeout(4000);
  testClient.setInsecure(); // Test TCP handshake only
  if (testClient.connect("api.telegram.org", 443)) {
    testClient.stop();
    return true;
  }

  // If DNS successfully resolved to a valid non-zero IP, internet is functional
  // (Prevents aggressive mobile hotspot drops due to single TCP timeout)
  if (telegramIp[0] != 0 && telegramIp[0] != 255) {
    return true;
  }
  return false;
#else
  return true;
#endif
}

void GwWiFiManager::commitCredentials(const char* ssid, const char* pass) {
#if defined(ARDUINO)
  prefs.begin("mscb_wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
#endif
  strncpy(activeSsid, ssid, sizeof(activeSsid) - 1);
  strncpy(activePass, pass, sizeof(activePass) - 1);
  reconnectFailures = 0;
}

void GwWiFiManager::startFallbackAp() {
#if defined(ARDUINO)
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("MSCB_GATEWAY_RECOVERY", "MscbRecovery2026!");
#endif
  state = WIFI_STATE_FALLBACK_AP;
}

void GwWiFiManager::service() {
  uint32_t now = millis();

  switch (state) {
    case WIFI_STATE_CONNECTED: {
#if defined(ARDUINO)
      if (WiFi.status() != WL_CONNECTED) {
        state = WIFI_STATE_CONNECTING;
        connectStartTimeMs = now;
        internetVerified = false;
      }
#endif
      break;
    }

    case WIFI_STATE_CONNECTING: {
#if defined(ARDUINO)
      if (WiFi.status() == WL_CONNECTED) {
        if (verifyInternetReachability()) {
          state = WIFI_STATE_CONNECTED;
          internetVerified = true;
          reconnectFailures = 0;
        }
      } else if (now - connectStartTimeMs > 20000) {
        // Connection timeout
        reconnectFailures++;
        if (reconnectFailures >= 5) {
          startFallbackAp();
        } else {
          state = WIFI_STATE_DISCONNECTED;
          lastReconnectAttemptMs = now;
        }
      }
#else
      state = WIFI_STATE_CONNECTED;
      internetVerified = true;
#endif
      break;
    }

    case WIFI_STATE_TESTING_NEW_CRED: {
#if defined(ARDUINO)
      if (WiFi.status() == WL_CONNECTED) {
        if (verifyInternetReachability()) {
          // Success! Commit new credentials
          commitCredentials(stagingSsid, stagingPass);
          state = WIFI_STATE_CONNECTED;
          internetVerified = true;
        }
      } else if (now - connectStartTimeMs > 18000) {
        // Validation failed! ROLLBACK TO PREVIOUS KNOWN-GOOD CREDENTIALS
        state = WIFI_STATE_ROLLING_BACK;
        WiFi.disconnect();
        WiFi.begin(activeSsid, activePass);
        connectStartTimeMs = now;
      }
#else
      commitCredentials(stagingSsid, stagingPass);
      state = WIFI_STATE_CONNECTED;
      internetVerified = true;
#endif
      break;
    }

    case WIFI_STATE_ROLLING_BACK: {
#if defined(ARDUINO)
      if (WiFi.status() == WL_CONNECTED) {
        state = WIFI_STATE_CONNECTED;
        internetVerified = verifyInternetReachability();
      } else if (now - connectStartTimeMs > 20000) {
        startFallbackAp();
      }
#else
      state = WIFI_STATE_CONNECTED;
#endif
      break;
    }

    case WIFI_STATE_DISCONNECTED: {
      if (now - lastReconnectAttemptMs > GW_WIFI_RECONNECT_INTERVAL_MS) {
        reconnectStation();
      }
      break;
    }

    case WIFI_STATE_FALLBACK_AP:
      break;
  }
}

bool GwWiFiManager::isConnected() const {
#if defined(ARDUINO)
  return (WiFi.status() == WL_CONNECTED);
#else
  return (state == WIFI_STATE_CONNECTED);
#endif
}

int8_t GwWiFiManager::getRssi() const {
#if defined(ARDUINO)
  return (WiFi.status() == WL_CONNECTED) ? (int8_t)WiFi.RSSI() : -127;
#else
  return -65;
#endif
}

const char* GwWiFiManager::getLocalIp() const {
#if defined(ARDUINO)
  static char ipStr[16];
  snprintf(ipStr, sizeof(ipStr), "%s", WiFi.localIP().toString().c_str());
  return ipStr;
#else
  return "192.168.1.100";
#endif
}
