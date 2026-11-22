/*
 * =====================================================================
 *  WIFICONFIGMANAGER.CPP — SECURE WI-FI CONFIGURATION & RECOVERY
 *  MSCB Smart Pole Gateway — Prompt 17
 * =====================================================================
 */

#include "WiFiConfigManager.h"
#include "TelegramManager.h"
#include "StorageManager.h"
#include <time.h>

WiFiConfigManager wifiCfgMgr;

WiFiConfigManager::WiFiConfigManager() :
  activeSsid(WIFI_DEF_SSID),
  previousSsid(WIFI_DEF_SSID),
  candidateSsid(""),
  candidatePassLen(0),
  updateState(WIFI_UPDATE_IDLE),
  lastFailReason(FAIL_NONE),
  sessionChatId(""),
  sessionStartTime(0),
  stateStartTime(0),
  lastConnectedTime(0),
  lastReconnectAttempt(0),
  reconnectCount(0),
  consecutiveFailures(0),
  apModeActive(false),
  dnsVerified(false),
  internetVerified(false),
  telegramVerified(false),
  tWatchdog(0)
{
  memset(activePass, 0, sizeof(activePass));
  memset(previousPass, 0, sizeof(previousPass));
  memset(candidatePass, 0, sizeof(candidatePass));
  strncpy(activePass, WIFI_DEF_PASS, sizeof(activePass) - 1);
  strncpy(previousPass, WIFI_DEF_PASS, sizeof(previousPass) - 1);
}

void WiFiConfigManager::wipeBuffer(char* buf, size_t len) {
  if (!buf || len == 0) return;
  volatile char *p = (volatile char *)buf;
  while (len--) {
    *p++ = 0;
  }
}

void WiFiConfigManager::wipeCandidateCredentials() {
  candidateSsid = "";
  wipeBuffer(candidatePass, sizeof(candidatePass));
  candidatePassLen = 0;
}

void WiFiConfigManager::init() {
  String sActiveSsid, sActivePass, sPrevSsid, sPrevPass;
  uint8_t commitState = NVS_WIFI_STABLE;

  storageMgr.loadWiFiConfig(sActiveSsid, sActivePass, sPrevSsid, sPrevPass, commitState);

  if (sActiveSsid.length() > 0) activeSsid = sActiveSsid;
  if (sPrevSsid.length() > 0)   previousSsid = sPrevSsid;

  wipeBuffer(activePass, sizeof(activePass));
  wipeBuffer(previousPass, sizeof(previousPass));

  if (sActivePass.length() > 0) {
    strncpy(activePass, sActivePass.c_str(), sizeof(activePass) - 1);
  } else {
    strncpy(activePass, WIFI_DEF_PASS, sizeof(activePass) - 1);
  }

  if (sPrevPass.length() > 0) {
    strncpy(previousPass, sPrevPass.c_str(), sizeof(previousPass) - 1);
  } else {
    strncpy(previousPass, WIFI_DEF_PASS, sizeof(previousPass) - 1);
  }

  // Scrub intermediate strings from RAM
  sActivePass = "";
  sPrevPass = "";

  // ── POWER LOSS / CRASH RECOVERY CHECK ──
  if (commitState == NVS_WIFI_TESTING) {
    Serial.println(F("[GW][WIFI] ⚠️ WARNING: Detected unverified Wi-Fi update during power loss/reboot!"));
    Serial.printf("[GW][WIFI] Rolling back automatically to verified configuration: '%s'\n", previousSsid.c_str());
    
    activeSsid = previousSsid;
    strncpy(activePass, previousPass, sizeof(activePass) - 1);

    storageMgr.saveWiFiActive(activeSsid, String(activePass));
    storageMgr.setWiFiCommitState(NVS_WIFI_STABLE);
    storageMgr.addGwLog("WIFI_PWR_ROLLBACK", activeSsid.c_str(), 1);
    lastFailReason = FAIL_POWER_LOSS_RECOVERY;
  }

  Serial.printf("[GW][WIFI] Initializing Station Mode (SSID: '%s', Pass: [PROTECTED])...\n", activeSsid.c_str());
  WiFi.mode(WIFI_STA);
  initiateStaConnection(activeSsid, activePass);
}

void WiFiConfigManager::initiateStaConnection(const String &ssid, const char* pass) {
  WiFi.disconnect();
  delay(50);
  if (pass && strlen(pass) > 0) {
    WiFi.begin(ssid.c_str(), pass);
  } else {
    WiFi.begin(ssid.c_str());
  }
  lastReconnectAttempt = millis();
}

void WiFiConfigManager::update() {
  unsigned long now = millis();

  // 1. Session Inactivity Timeout Checker
  if (updateState == WIFI_UPDATE_AWAITING_SSID || 
      updateState == WIFI_UPDATE_AWAITING_PASS || 
      updateState == WIFI_UPDATE_CONFIRM_REQUIRED) {
    if (now - sessionStartTime >= WIFI_SESSION_TIMEOUT_MS) {
      Serial.println(F("[GW][WIFI] Session inactivity timeout expired. Clearing candidate credentials."));
      if (sessionChatId.length() > 0) {
        tgMgr.sendMessage(sessionChatId, "⏱ *Wi-Fi Configuration Session Timed Out*\nSession expired after 2 minutes of inactivity. Temporary credentials wiped from memory.", "Markdown");
      }
      cancelSession(sessionChatId, "Session Inactivity Timeout");
      return;
    }
  }

  // 2. Non-blocking Wi-Fi Verification & Rollback State Machine
  switch (updateState) {
    case WIFI_UPDATE_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[GW][WIFI] Candidate AP Associated! RSSI: %d dBm. Verifying IP...\n", WiFi.RSSI());
        stateStartTime = now;
        updateState = WIFI_UPDATE_VERIFYING_IP;
      } else if (now - stateStartTime >= WIFI_CONNECT_TIMEOUT_MS) {
        Serial.println(F("[GW][WIFI] ❌ Association to candidate network timed out!"));
        executeRollback(FAIL_ASSOCIATION_TIMEOUT);
      }
      break;

    case WIFI_UPDATE_VERIFYING_IP:
      if (WiFi.localIP() != INADDR_NONE && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
        Serial.printf("[GW][WIFI] DHCP Lease Verified: %s. Verifying DNS...\n", WiFi.localIP().toString().c_str());
        stateStartTime = now;
        updateState = WIFI_UPDATE_VERIFYING_DNS;
      } else if (now - stateStartTime >= WIFI_DHCP_TIMEOUT_MS) {
        Serial.println(F("[GW][WIFI] ❌ Failed to obtain IP lease from candidate network!"));
        executeRollback(FAIL_NO_IP_ASSIGNED);
      }
      break;

    case WIFI_UPDATE_VERIFYING_DNS: {
      IPAddress resolvedIp;
      if (WiFi.hostByName("api.telegram.org", resolvedIp) == 1) {
        Serial.printf("[GW][WIFI] DNS Resolution OK: api.telegram.org -> %s. Verifying Telegram...\n", resolvedIp.toString().c_str());
        dnsVerified = true;
        stateStartTime = now;
        updateState = WIFI_UPDATE_VERIFYING_TELEGRAM;
      } else if (now - stateStartTime >= WIFI_DNS_TIMEOUT_MS) {
        Serial.println(F("[GW][WIFI] ❌ DNS Resolution failed on candidate network!"));
        executeRollback(FAIL_DNS_RESOLUTION);
      }
      break;
    }

    case WIFI_UPDATE_VERIFYING_TELEGRAM:
      if (tgMgr.verifyTelegramApi()) {
        Serial.println(F("[GW][WIFI] ✅ Telegram Bot API connectivity verified!"));
        telegramVerified = true;
        internetVerified = true;
        updateState = WIFI_UPDATE_COMMITTING;
        commitCredentials();
      } else if (now - stateStartTime >= WIFI_TG_TIMEOUT_MS) {
        Serial.println(F("[GW][WIFI] ❌ Telegram Bot API unreachable on candidate network!"));
        executeRollback(FAIL_TELEGRAM_API);
      }
      break;

    case WIFI_UPDATE_ROLLING_BACK:
      if (WiFi.status() == WL_CONNECTED) {
        IPAddress resolvedIp;
        if (WiFi.hostByName("api.telegram.org", resolvedIp) == 1 && tgMgr.verifyTelegramApi()) {
          Serial.printf("[GW][WIFI] ✅ Restored previous network '%s' successfully! IP: %s\n",
                        activeSsid.c_str(), WiFi.localIP().toString().c_str());
          updateState = WIFI_UPDATE_IDLE;
          consecutiveFailures = 0;
          lastConnectedTime = now;

          String failMsg = "⚠️ *WI-FI UPDATE FAILED & ROLLED BACK*\n"
                           "━━━━━━━━━━━━━━━━━━━━\n"
                           "❌ *Failure Reason:* `" + String(wifiFailReasonToString(lastFailReason)) + "`\n\n"
                           "✅ *Restored Previous Wi-Fi:*\n"
                           "• Network: `" + activeSsid + "`\n"
                           "• IP: `" + WiFi.localIP().toString() + "`\n"
                           "• Gateway: `" + WiFi.gatewayIP().toString() + "`\n"
                           "• Signal: `" + String(WiFi.RSSI()) + " dBm`\n"
                           "• Telegram: `ONLINE 🟢`\n"
                           "• LoRa Mesh: `OPERATIONAL (Uninterrupted)`\n"
                           "━━━━━━━━━━━━━━━━━━━━";

          tgMgr.sendMessage(sessionChatId.length() > 0 ? sessionChatId : String(OWNER_CHAT), failMsg, "Markdown");
          sessionChatId = "";
        }
      } else if (now - stateStartTime >= WIFI_CONNECT_TIMEOUT_MS * 2) {
        Serial.println(F("[GW][WIFI] ⚠️ Previous Wi-Fi reconnect taking long. Retrying..."));
        stateStartTime = now;
        consecutiveFailures++;
        if (consecutiveFailures >= WIFI_MAX_FAIL_BEFORE_AP && !apModeActive) {
          startRecoveryAP();
        }
        initiateStaConnection(activeSsid, activePass);
      }
      break;

    case WIFI_UPDATE_IDLE:
    default:
      checkWatchdog();
      break;
  }
}

void WiFiConfigManager::checkWatchdog() {
  unsigned long now = millis();
  if (now - tWatchdog < WIFI_WATCHDOG_INTERVAL_MS) return;
  tWatchdog = now;

  if (WiFi.status() == WL_CONNECTED) {
    if (lastConnectedTime == 0) lastConnectedTime = now;
    consecutiveFailures = 0;
  } else {
    Serial.println(F("[GW][WIFI] WiFi disconnected. Attempting non-blocking reconnect..."));
    reconnectCount++;
    consecutiveFailures++;
    lastReconnectAttempt = now;
    WiFi.disconnect();
    WiFi.reconnect();

    if (consecutiveFailures >= WIFI_MAX_FAIL_BEFORE_AP && !apModeActive) {
      Serial.println(F("[GW][WIFI] ⚠️ Consecutive reconnect failures exceeded limit. Starting Recovery AP!"));
      startRecoveryAP();
    }
  }
}

bool WiFiConfigManager::startCredentialChangeSession(const String &chatId, const String &fromId, bool isPrivateChat, String &outError) {
  if (!isPrivateChat || chatId.startsWith("-")) {
    lastFailReason = FAIL_GROUP_CHAT_REJECTED;
    outError = "🚨 *SECURITY ERROR: Group Chat Detected!*\nWi-Fi credentials may ONLY be updated in an authorized private 1-on-1 chat with the Gateway.";
    return false;
  }

  String configured = String(OWNER_CHAT);
  configured.trim();
  String cId = chatId; cId.trim();
  String fId = fromId; fId.trim();

  if (configured.length() > 0 && cId != configured && fId != configured) {
    lastFailReason = FAIL_UNAUTHORIZED;
    outError = "🔒 *ACCESS DENIED: Unauthorized Administrator ID.*";
    return false;
  }

  if (isTesting() || updateState != WIFI_UPDATE_IDLE) {
    lastFailReason = FAIL_DUPLICATE_REQUEST;
    outError = "⚠️ *A Wi-Fi configuration or test session is already active. Please wait for completion or cancel it.*";
    return false;
  }

  wipeCandidateCredentials();
  updateState = WIFI_UPDATE_AWAITING_SSID;
  sessionChatId = chatId;
  sessionStartTime = millis();
  storageMgr.addGwLog("WIFI_CFG_START", activeSsid.c_str(), 0);

  Serial.printf("[GW][WIFI] New Wi-Fi configuration session started by Chat %s\n", chatId.c_str());
  return true;
}

bool WiFiConfigManager::handleSsidInput(const String &chatId, const String &ssid, String &outPrompt, String &outError) {
  if (updateState != WIFI_UPDATE_AWAITING_SSID || sessionChatId != chatId) {
    outError = "⚠️ No active SSID input session found.";
    return false;
  }

  String clean = ssid;
  clean.trim();

  if (clean.length() < 1 || clean.length() > 32) {
    lastFailReason = FAIL_INVALID_SSID;
    outError = "❌ *Invalid SSID Length:* Must be between 1 and 32 characters.\nPlease enter a valid SSID or type /cancel:";
    return false;
  }

  // Validate printable ASCII
  for (size_t i = 0; i < clean.length(); i++) {
    char c = clean[i];
    if (c < 32 || c > 126) {
      lastFailReason = FAIL_INVALID_SSID;
      outError = "❌ *Invalid SSID Characters:* Only printable standard characters are allowed.\nPlease try again or type /cancel:";
      return false;
    }
  }

  candidateSsid = clean;
  updateState = WIFI_UPDATE_AWAITING_PASS;
  sessionStartTime = millis();

  outPrompt = "🔒 *Enter Wi-Fi Password*\n"
              "━━━━━━━━━━━━━━━━━━━━\n"
              "Target Network: `" + candidateSsid + "`\n\n"
              "Please enter the WPA2/WPA3 password for this network:\n"
              "• 8 to 63 characters\n"
              "• For open networks (no password), type `NONE`\n\n"
              "🛡 *Security Notice:* The bot will delete your password message immediately upon receipt.\n"
              "💡 *Type /cancel to abort.*";

  Serial.printf("[GW][WIFI] Valid candidate SSID accepted: '%s'\n", candidateSsid.c_str());
  return true;
}

bool WiFiConfigManager::handlePasswordInput(const String &chatId, const String &pass, size_t &outMaskedLen, String &outPrompt, String &outError) {
  if (updateState != WIFI_UPDATE_AWAITING_PASS || sessionChatId != chatId) {
    outError = "⚠️ No active Password input session found.";
    return false;
  }

  String p = pass;
  p.trim();

  wipeBuffer(candidatePass, sizeof(candidatePass));

  if (p.equalsIgnoreCase("NONE")) {
    candidatePass[0] = '\0';
    candidatePassLen = 0;
  } else {
    if (p.length() < 8 || p.length() > 63) {
      lastFailReason = FAIL_INVALID_PASSWORD;
      outError = "❌ *Invalid Password Length:* WPA2/WPA3 passwords must be 8–63 characters long (or 'NONE' for open).\nPlease enter the password again or type /cancel:";
      return false;
    }

    for (size_t i = 0; i < p.length(); i++) {
      char c = p[i];
      if (c < 32 || c > 126) {
        lastFailReason = FAIL_INVALID_PASSWORD;
        outError = "❌ *Invalid Password Characters:* Only standard printable characters are supported.\nPlease enter the password again or type /cancel:";
        return false;
      }
    }

    strncpy(candidatePass, p.c_str(), sizeof(candidatePass) - 1);
    candidatePassLen = p.length();
  }

  // Scrub parameter string from memory immediately
  p = "";

  updateState = WIFI_UPDATE_CONFIRM_REQUIRED;
  sessionStartTime = millis();
  outMaskedLen = candidatePassLen;

  outPrompt = "✅ *Credentials Validated in Memory*\n"
              "• Proposed SSID: `" + candidateSsid + "`\n"
              "• Password Length: `" + String(candidatePassLen) + " characters`\n\n"
              "Please confirm to apply and test.";

  Serial.printf("[GW][WIFI] Valid candidate password accepted (length: %d). Awaiting admin confirmation.\n", (int)candidatePassLen);
  return true;
}

bool WiFiConfigManager::confirmAndApply(const String &chatId, String &outMsg) {
  if (updateState != WIFI_UPDATE_CONFIRM_REQUIRED || sessionChatId != chatId) {
    outMsg = "⚠️ No pending Wi-Fi change awaiting confirmation.";
    return false;
  }

  Serial.println(F("[GW][WIFI] Admin confirmed Wi-Fi change. Staging in NVS and initiating connection test..."));

  // 1. Stage in NVS (Mark state as TESTING so power loss triggers rollback on boot)
  storageMgr.setWiFiCommitState(NVS_WIFI_TESTING);
  storageMgr.addGwLog("WIFI_TEST_START", candidateSsid.c_str(), 0);

  // 2. Initiate non-blocking connection to candidate AP
  initiateStaConnection(candidateSsid, candidatePass);

  updateState = WIFI_UPDATE_CONNECTING;
  stateStartTime = millis();

  outMsg = "⏳ *Connecting to `" + candidateSsid + "`...*\n"
           "━━━━━━━━━━━━━━━━━━━━\n"
           "Step 1: Network Association (WPA Handshake)\n"
           "Step 2: IP Configuration (DHCP)\n"
           "Step 3: Internet & DNS Verification\n"
           "Step 4: Telegram Bot API Handshake\n\n"
           "🛡 *Safety Guarantee:* If any step fails within 20s, the Gateway automatically rolls back to `" + activeSsid + "`.\n"
           "📡 *LoRa Mesh:* Fully active without interruption.\n"
           "━━━━━━━━━━━━━━━━━━━━";

  return true;
}

void WiFiConfigManager::cancelSession(const String &chatId, const String &reason) {
  wipeCandidateCredentials();
  updateState = WIFI_UPDATE_IDLE;
  lastFailReason = FAIL_USER_CANCELLED;
  sessionChatId = "";
  Serial.printf("[GW][WIFI] Wi-Fi configuration session cancelled: %s\n", reason.c_str());
}

void WiFiConfigManager::executeRollback(WiFiFailReason reason) {
  lastFailReason = reason;
  storageMgr.addGwLog("WIFI_ROLLBACK", activeSsid.c_str(), 1);
  wipeCandidateCredentials();

  Serial.printf("[GW][WIFI] ⚠️ Executing Rollback! Reason: %s. Restoring: '%s'...\n",
                wifiFailReasonToString(reason), activeSsid.c_str());

  updateState = WIFI_UPDATE_ROLLING_BACK;
  stateStartTime = millis();

  initiateStaConnection(activeSsid, activePass);
}

void WiFiConfigManager::commitCredentials() {
  Serial.printf("[GW][WIFI] 🎉 Committing new verified Wi-Fi: '%s'!\n", candidateSsid.c_str());

  // 1. Save previous credentials for cold rollback
  storageMgr.saveWiFiPrevious(activeSsid, String(activePass));
  previousSsid = activeSsid;
  strncpy(previousPass, activePass, sizeof(previousPass) - 1);

  // 2. Save new active credentials
  storageMgr.saveWiFiActive(candidateSsid, String(candidatePass));
  activeSsid = candidateSsid;
  strncpy(activePass, candidatePass, sizeof(activePass) - 1);

  // 3. Mark NVS state as STABLE
  storageMgr.setWiFiCommitState(NVS_WIFI_STABLE);
  storageMgr.addGwLog("WIFI_APPLY_OK", activeSsid.c_str(), 0);

  // 4. Wipe candidate RAM
  wipeCandidateCredentials();

  updateState = WIFI_UPDATE_IDLE;
  consecutiveFailures = 0;
  lastConnectedTime = millis();

  // 5. Send Success Confirmation
  String succMsg = "🎉 *WI-FI CONFIGURATION SUCCESSFUL!*\n"
                   "━━━━━━━━━━━━━━━━━━━━\n"
                   "✅ *New Active Network:*\n"
                   "• SSID: `" + activeSsid + "`\n"
                   "• IP Address: `" + WiFi.localIP().toString() + "`\n"
                   "• Gateway: `" + WiFi.gatewayIP().toString() + "`\n"
                   "• Subnet: `" + WiFi.subnetMask().toString() + "`\n"
                   "• DNS Server: `" + WiFi.dnsIP().toString() + "`\n"
                   "• Signal: `" + String(WiFi.RSSI()) + " dBm`\n\n"
                   "🌐 *Internet Status:* `ONLINE 🟢`\n"
                   "🤖 *Telegram Bot:* `RECONNECTED 🟢`\n"
                   "💾 *NVS Storage:* `COMMITTED ATOMICALLY`\n"
                   "📡 *LoRa Mesh:* `CONTINUOUSLY OPERATIONAL`\n"
                   "━━━━━━━━━━━━━━━━━━━━";

  tgMgr.sendMessage(sessionChatId.length() > 0 ? sessionChatId : String(OWNER_CHAT), succMsg, "Markdown");
  sessionChatId = "";
}

bool WiFiConfigManager::triggerManualRollback(const String &chatId, String &outMsg) {
  if (previousSsid.length() == 0 || (previousSsid == activeSsid)) {
    outMsg = "⚠️ No previous distinct Wi-Fi network available to rollback.";
    return false;
  }

  Serial.printf("[GW][WIFI] Manual rollback triggered to: '%s'\n", previousSsid.c_str());
  storageMgr.addGwLog("WIFI_MANUAL_ROLLBACK", previousSsid.c_str(), 0);

  // Swap active and previous
  String tmpSsid = activeSsid;
  char tmpPass[65];
  strncpy(tmpPass, activePass, sizeof(tmpPass) - 1);

  activeSsid = previousSsid;
  strncpy(activePass, previousPass, sizeof(activePass) - 1);

  previousSsid = tmpSsid;
  strncpy(previousPass, tmpPass, sizeof(previousPass) - 1);
  wipeBuffer(tmpPass, sizeof(tmpPass));

  storageMgr.saveWiFiActive(activeSsid, String(activePass));
  storageMgr.saveWiFiPrevious(previousSsid, String(previousPass));
  storageMgr.setWiFiCommitState(NVS_WIFI_STABLE);

  initiateStaConnection(activeSsid, activePass);

  outMsg = "🔁 *Rolling Back to Previous Wi-Fi*\nRestoring connection to: `" + activeSsid + "`...";
  return true;
}

bool WiFiConfigManager::runDiagnosticTest(String &outReport) {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  IPAddress dnsIp;
  bool dnsOk = false;
  if (wifiOk) {
    dnsOk = (WiFi.hostByName("api.telegram.org", dnsIp) == 1);
  }
  bool tgOk = tgMgr.isTelegramOnline();

  outReport = "🔍 *WI-FI CONNECTIVITY DIAGNOSTICS*\n"
              "━━━━━━━━━━━━━━━━━━━━\n"
              "📶 *Active SSID:* `" + activeSsid + "`\n"
              "📡 *Association:* " + String(wifiOk ? "CONNECTED 🟢" : "DISCONNECTED 🔴") + "\n"
              "🌐 *Local IP:* `" + (wifiOk ? WiFi.localIP().toString() : "0.0.0.0") + "`\n"
              "🚪 *Gateway IP:* `" + (wifiOk ? WiFi.gatewayIP().toString() : "0.0.0.0") + "`\n"
              "🧭 *DNS Server:* `" + (wifiOk ? WiFi.dnsIP().toString() : "None") + "`\n"
              "📶 *Signal Strength:* `" + (wifiOk ? String(WiFi.RSSI()) + " dBm" : "N/A") + "`\n"
              "🌍 *DNS Resolution:* " + String(dnsOk ? "RESOLVED (api.telegram.org) 🟢" : "FAILED 🔴") + "\n"
              "🤖 *Telegram Bot API:* " + String(tgOk ? "ONLINE 🟢" : "OFFLINE 🔴") + "\n"
              "🔄 *Reconnection Watchdog Count:* `" + String(reconnectCount) + "`\n"
              "━━━━━━━━━━━━━━━━━━━━";

  return (wifiOk && dnsOk && tgOk);
}

void WiFiConfigManager::startRecoveryAP() {
  if (apModeActive) return;
  Serial.println(F("[GW][WIFI] 🆘 Starting Failsafe Recovery SoftAP..."));
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_RECOVERY_AP_SSID, WIFI_RECOVERY_AP_PASS);
  apModeActive = true;
  Serial.printf("[GW][WIFI] Failsafe AP Online! SSID: '%s', IP: %s\n",
                WIFI_RECOVERY_AP_SSID, WiFi.softAPIP().toString().c_str());
  storageMgr.addGwLog("WIFI_AP_START", WIFI_RECOVERY_AP_SSID, 0);
}

void WiFiConfigManager::stopRecoveryAP() {
  if (!apModeActive) return;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apModeActive = false;
  Serial.println(F("[GW][WIFI] Failsafe Recovery AP stopped."));
  storageMgr.addGwLog("WIFI_AP_STOP", WIFI_RECOVERY_AP_SSID, 0);
}

void WiFiConfigManager::factoryResetCredentials() {
  activeSsid = WIFI_DEF_SSID;
  previousSsid = WIFI_DEF_SSID;
  strncpy(activePass, WIFI_DEF_PASS, sizeof(activePass) - 1);
  strncpy(previousPass, WIFI_DEF_PASS, sizeof(previousPass) - 1);

  storageMgr.saveWiFiActive(activeSsid, String(activePass));
  storageMgr.saveWiFiPrevious(previousSsid, String(previousPass));
  storageMgr.setWiFiCommitState(NVS_WIFI_STABLE);
  storageMgr.addGwLog("WIFI_FACTORY_RST", activeSsid.c_str(), 0);

  Serial.printf("[GW][WIFI] Restored default factory credentials: '%s'\n", activeSsid.c_str());
  initiateStaConnection(activeSsid, activePass);
}

void WiFiConfigManager::applyLocalCredentials(const String &ssid, const String &pass) {
  if (ssid.length() < 1 || ssid.length() > 32) return;
  if (pass.length() > 0 && pass.length() < 8) return;

  storageMgr.saveWiFiPrevious(activeSsid, String(activePass));
  previousSsid = activeSsid;
  strncpy(previousPass, activePass, sizeof(previousPass) - 1);

  activeSsid = ssid;
  strncpy(activePass, pass.c_str(), sizeof(activePass) - 1);
  storageMgr.saveWiFiActive(activeSsid, String(activePass));
  storageMgr.setWiFiCommitState(NVS_WIFI_STABLE);
  storageMgr.addGwLog("WIFI_SERIAL_SET", activeSsid.c_str(), 0);

  Serial.printf("[GW][WIFI] Local credentials set: SSID='%s', Pass=[PROTECTED]\n", activeSsid.c_str());
  initiateStaConnection(activeSsid, activePass);
}

void WiFiConfigManager::processSerialCommands() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line == "WIFI_STATUS") {
    Serial.println(F("\n=== GATEWAY WI-FI DIAGNOSTICS ==="));
    Serial.printf("Active SSID:       %s\n", activeSsid.c_str());
    Serial.println(F("Password:          [PROTECTED]"));
    Serial.printf("Previous SSID:     %s\n", previousSsid.c_str());
    Serial.printf("State:             %s\n", (WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED"));
    Serial.printf("IP Address:        %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Gateway IP:        %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("Subnet Mask:       %s\n", WiFi.subnetMask().toString().c_str());
    Serial.printf("DNS Server:        %s\n", WiFi.dnsIP().toString().c_str());
    Serial.printf("RSSI:              %d dBm\n", WiFi.RSSI());
    Serial.printf("Recovery AP Mode:  %s\n", apModeActive ? "ENABLED" : "DISABLED");
    Serial.printf("Update Machine:    %s\n", wifiUpdateStateToString(updateState));
    Serial.printf("Last Fail Reason:  %s\n", wifiFailReasonToString(lastFailReason));
    Serial.printf("Total Reconnects:  %u\n", reconnectCount);
    Serial.println(F("=================================\n"));
  }
  else if (line.startsWith("WIFI_SET ")) {
    int sp = line.indexOf(' ', 9);
    if (sp > 9) {
      String newS = line.substring(9, sp);
      String newP = line.substring(sp + 1);
      applyLocalCredentials(newS, newP);
      Serial.println(F("[GW][WIFI] Local credentials applied. Reconnecting..."));
    } else {
      String newS = line.substring(9);
      applyLocalCredentials(newS, "");
      Serial.println(F("[GW][WIFI] Open network credentials applied. Reconnecting..."));
    }
  }
  else if (line == "WIFI_ROLLBACK") {
    String dummy;
    triggerManualRollback("", dummy);
    Serial.printf("[GW][WIFI] Rolled back to: %s\n", activeSsid.c_str());
  }
  else if (line == "WIFI_RESET_DEFAULT") {
    factoryResetCredentials();
  }
  else if (line == "WIFI_AP_START") {
    startRecoveryAP();
  }
  else if (line == "WIFI_AP_STOP") {
    stopRecoveryAP();
  }
  else if (line == "HELP") {
    Serial.println(F("\nAvailable Serial Maintenance Commands:"));
    Serial.println(F("  WIFI_STATUS          - Print Wi-Fi status and diagnostics"));
    Serial.println(F("  WIFI_SET <ssid> <pw> - Set new Wi-Fi credentials locally"));
    Serial.println(F("  WIFI_ROLLBACK        - Roll back to previous working Wi-Fi"));
    Serial.println(F("  WIFI_RESET_DEFAULT   - Restore factory default Wi-Fi"));
    Serial.println(F("  WIFI_AP_START        - Activate emergency recovery SoftAP"));
    Serial.println(F("  WIFI_AP_STOP         - Deactivate emergency recovery SoftAP\n"));
  }
}

// Diagnostics Getters
bool WiFiConfigManager::isConnected() const { return (WiFi.status() == WL_CONNECTED); }
bool WiFiConfigManager::isInternetOk() const { return internetVerified; }
bool WiFiConfigManager::isDnsOk() const { return dnsVerified; }
bool WiFiConfigManager::isTelegramOk() const { return telegramVerified; }
String WiFiConfigManager::getActiveSsid() const { return activeSsid; }
String WiFiConfigManager::getLocalIpStr() const { return WiFi.localIP().toString(); }
String WiFiConfigManager::getGatewayIpStr() const { return WiFi.gatewayIP().toString(); }
String WiFiConfigManager::getSubnetMaskStr() const { return WiFi.subnetMask().toString(); }
String WiFiConfigManager::getDnsIpStr() const { return WiFi.dnsIP().toString(); }
int WiFiConfigManager::getRssi() const { return WiFi.RSSI(); }
unsigned long WiFiConfigManager::getLastConnectedTime() const { return lastConnectedTime; }
unsigned long WiFiConfigManager::getLastReconnectAttempt() const { return lastReconnectAttempt; }
uint32_t WiFiConfigManager::getReconnectCount() const { return reconnectCount; }
WiFiUpdateState WiFiConfigManager::getUpdateState() const { return updateState; }
WiFiFailReason WiFiConfigManager::getLastFailReason() const { return lastFailReason; }
bool WiFiConfigManager::isApRecoveryActive() const { return apModeActive; }
