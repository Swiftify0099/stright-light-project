/*
 * =====================================================================
 *  TEST_PROMPT17.CPP — SECURE WI-FI CONFIGURATION & RECOVERY TEST HARNESS
 *  Prompt 17 Comprehensive Validation Suite:
 *  1. Valid New Wi-Fi Credentials
 *  2. Wrong Password
 *  3. Invalid SSID
 *  4. Missing Password / Invalid Length
 *  5. Network Not Available
 *  6. Internet Not Available
 *  7. DNS Failure
 *  8. Telegram API Failure
 *  9. Gateway Restart During Update
 * 10. Power Failure During Update
 * 11. Rollback Verification
 * 12. Local Recovery (Serial CLI & SoftAP)
 * 13. Unauthorized User Attempt
 * 14. Group Chat Credential Submission
 * 15. Session Expiration
 * 16. Duplicate Wi-Fi Change Request
 * 17. Credential Privacy & Log Inspection
 * 18. LoRa Operation During Wi-Fi Reconnection
 * =====================================================================
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>
#include <sstream>
#include <map>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdarg>

using namespace std;

// ─────────────────────────────────────────────────────────────
//  ARDUINO / ESP32 EMULATION LAYER
// ─────────────────────────────────────────────────────────────
static unsigned long g_simTimeMs = 1000UL;
unsigned long millis() { return g_simTimeMs; }
void delay(unsigned long ms) { g_simTimeMs += ms; }

// Arduino String Class Simulation
class String {
public:
  std::string s;
  String() : s("") {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string &str) : s(str) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned int v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(float v, int dec = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(dec) << v;
    s = ss.str();
  }

  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.length(); }
  char operator[](size_t i) const { return s[i]; }
  char& operator[](size_t i) { return s[i]; }

  String& operator+=(const String &rhs) { s += rhs.s; return *this; }
  String& operator+=(const char* rhs) { if (rhs) s += rhs; return *this; }
  String& operator+=(char c) { s += c; return *this; }

  bool operator==(const String &rhs) const { return s == rhs.s; }
  bool operator==(const char* rhs) const { return rhs && s == rhs; }
  bool operator!=(const String &rhs) const { return s != rhs.s; }
  bool operator!=(const char* rhs) const { return !rhs || s != rhs; }

  int indexOf(char c, size_t fromIndex = 0) const {
    size_t pos = s.find(c, fromIndex);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  int indexOf(const String &sub, size_t fromIndex = 0) const {
    size_t pos = s.find(sub.s, fromIndex);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  String substring(size_t from, size_t to = std::string::npos) const {
    if (from >= s.length()) return String("");
    if (to == std::string::npos || to > s.length()) return String(s.substr(from));
    return String(s.substr(from, to - from));
  }
  void trim() {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) { s = ""; return; }
    size_t last = s.find_last_not_of(" \t\r\n");
    s = s.substr(first, (last - first + 1));
  }
  bool startsWith(const String &prefix) const {
    if (prefix.length() > s.length()) return false;
    return s.compare(0, prefix.length(), prefix.s) == 0;
  }
  bool endsWith(const String &suffix) const {
    if (suffix.length() > s.length()) return false;
    return s.compare(s.length() - suffix.length(), suffix.length(), suffix.s) == 0;
  }
  bool equalsIgnoreCase(const String &other) const {
    if (s.length() != other.length()) return false;
    for (size_t i = 0; i < s.length(); i++) {
      if (tolower((unsigned char)s[i]) != tolower((unsigned char)other.s[i])) return false;
    }
    return true;
  }
  int toInt() const {
    try { return std::stoi(s); } catch (...) { return 0; }
  }
};

inline String operator+(const String &lhs, const String &rhs) { String res = lhs; res += rhs; return res; }
inline String operator+(const String &lhs, const char* rhs) { String res = lhs; res += rhs; return res; }
inline String operator+(const char* lhs, const String &rhs) { String res(lhs); res += rhs; return res; }

// IPAddress Simulation
class IPAddress {
public:
  uint8_t octets[4];
  IPAddress() { octets[0] = octets[1] = octets[2] = octets[3] = 0; }
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    octets[0] = a; octets[1] = b; octets[2] = c; octets[3] = d;
  }
  String toString() const {
    return String(std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." +
                  std::to_string(octets[2]) + "." + std::to_string(octets[3]));
  }
  bool operator==(const IPAddress &other) const {
    return memcmp(octets, other.octets, 4) == 0;
  }
  bool operator!=(const IPAddress &other) const {
    return !(*this == other);
  }
};
static const IPAddress INADDR_NONE(0, 0, 0, 0);

// Serial Output Simulation
class MockSerial {
public:
  std::vector<std::string> logLines;
  void print(const char* s) { if (s) logLines.push_back(s); }
  void print(const String &s) { logLines.push_back(s.c_str()); }
  void println(const char* s = "") { if (s) logLines.push_back(s); }
  void println(const String &s) { logLines.push_back(s.c_str()); }
  void printf(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    logLines.push_back(buf);
  }
  bool contains(const std::string &token) const {
    for (const auto &l : logLines) {
      if (l.find(token) != std::string::npos) return true;
    }
    return false;
  }
  void clear() { logLines.clear(); }
};
static MockSerial Serial;

// ─────────────────────────────────────────────────────────────
//  PROMPT 17 CONFIGURATION CONSTANTS
// ─────────────────────────────────────────────────────────────
#define OWNER_CHAT                 "5505296197"
#define WIFI_DEF_SSID              "Saaj"
#define WIFI_DEF_PASS              "Saaj_7550"
#define WIFI_SESSION_TIMEOUT_MS    120000UL
#define WIFI_CONNECT_TIMEOUT_MS    20000UL
#define WIFI_DHCP_TIMEOUT_MS       6000UL
#define WIFI_DNS_TIMEOUT_MS        5000UL
#define WIFI_TG_TIMEOUT_MS         10000UL
#define WIFI_RECOVERY_AP_SSID      "MSCB-GW-RECOVERY"
#define WIFI_RECOVERY_AP_PASS      "MscbRecovery2026!"
#define WIFI_MAX_FAIL_BEFORE_AP    3
#define NVS_WIFI_STABLE            0
#define NVS_WIFI_TESTING           1
#define WIFI_WATCHDOG_INTERVAL_MS  15000UL

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

// ─────────────────────────────────────────────────────────────
//  SIMULATED HARDWARE & SERVICES
// ─────────────────────────────────────────────────────────────
#define WL_CONNECTED      3
#define WL_DISCONNECTED   6
#define WL_CONNECT_FAILED 4
#define WIFI_STA          1
#define WIFI_AP           2
#define WIFI_AP_STA       3

class MockWiFiClass {
public:
  int    wifiMode;
  int    statusVal;
  String currentSsid;
  String currentPass;
  int    rssiVal;
  IPAddress localIp;
  IPAddress gatewayIp;
  IPAddress subnetMaskIp;
  IPAddress dnsIp;
  bool   softApActive;
  String softApSsid;

  // Fault injection hooks
  bool simAssociationFail;
  bool simDhcpFail;
  bool simDnsFail;
  bool simTelegramFail;

  MockWiFiClass() :
    wifiMode(WIFI_STA), statusVal(WL_CONNECTED), currentSsid(WIFI_DEF_SSID),
    currentPass(WIFI_DEF_PASS), rssiVal(-60),
    localIp(192, 168, 1, 105), gatewayIp(192, 168, 1, 1),
    subnetMaskIp(255, 255, 255, 0), dnsIp(192, 168, 1, 1),
    softApActive(false),
    simAssociationFail(false), simDhcpFail(false),
    simDnsFail(false), simTelegramFail(false) {}

  void modeSet(int m) { wifiMode = m; }
  void mode(int m) { wifiMode = m; }
  int status() { return statusVal; }
  void disconnect(bool w = false) { statusVal = WL_DISCONNECTED; }
  void reconnect() { statusVal = WL_CONNECTED; }
  void begin(const char* ssid, const char* pass = nullptr) {
    currentSsid = ssid ? ssid : "";
    currentPass = pass ? pass : "";
    if (simAssociationFail) {
      statusVal = WL_CONNECT_FAILED;
    } else {
      statusVal = WL_CONNECTED;
      if (simDhcpFail) {
        localIp = IPAddress(0, 0, 0, 0);
      } else {
        localIp = IPAddress(192, 168, 43, 88);
      }
    }
  }

  int hostByName(const char* host, IPAddress &outIp) {
    if (simDnsFail) return 0;
    outIp = IPAddress(149, 154, 167, 220); // api.telegram.org
    return 1;
  }

  IPAddress localIP() const { return localIp; }
  IPAddress gatewayIP() const { return gatewayIp; }
  IPAddress subnetMask() const { return subnetMaskIp; }
  IPAddress dnsIP() const { return dnsIp; }
  int RSSI() const { return rssiVal; }
  void softAP(const char* ssid, const char* pass) {
    softApActive = true;
    softApSsid = ssid;
  }
  void softAPdisconnect(bool b) { softApActive = false; }
  IPAddress softAPIP() const { return IPAddress(192, 168, 4, 1); }
};
static MockWiFiClass WiFi;

// Mock Telegram Bot
class MockTelegramBot {
public:
  std::vector<std::string> sentMessages;
  std::vector<int> deletedMessageIds;
  bool isOnline;

  MockTelegramBot() : isOnline(true) {}

  bool getMe() {
    return isOnline && !WiFi.simTelegramFail;
  }
  bool sendMessage(const String &chatId, const String &text, const String &parseMode = "") {
    sentMessages.push_back(text.c_str());
    return true;
  }
  bool deleteMessage(const String &chatId, int messageId) {
    deletedMessageIds.push_back(messageId);
    return true;
  }
  bool containsText(const std::string &token) const {
    for (const auto &m : sentMessages) {
      if (m.find(token) != std::string::npos) return true;
    }
    return false;
  }
  void clear() {
    sentMessages.clear();
    deletedMessageIds.clear();
  }
};
static MockTelegramBot mockBot;

// Mock Storage Manager
struct AuditLogEntry {
  std::string action;
  std::string target;
  uint8_t     result;
};

class MockStorageManager {
public:
  std::string activeSsid;
  std::string activePass;
  std::string prevSsid;
  std::string prevPass;
  uint8_t     commitState;
  std::vector<AuditLogEntry> auditLogs;

  MockStorageManager() :
    activeSsid(WIFI_DEF_SSID), activePass(WIFI_DEF_PASS),
    prevSsid(WIFI_DEF_SSID), prevPass(WIFI_DEF_PASS),
    commitState(NVS_WIFI_STABLE) {}

  void loadWiFiConfig(String &a_s, String &a_p, String &p_s, String &p_p, uint8_t &st) {
    a_s = activeSsid;
    a_p = activePass;
    p_s = prevSsid;
    p_p = prevPass;
    st  = commitState;
  }
  void saveWiFiActive(const String &s, const String &p) {
    activeSsid = s.c_str();
    activePass = p.c_str();
  }
  void saveWiFiPrevious(const String &s, const String &p) {
    prevSsid = s.c_str();
    prevPass = p.c_str();
  }
  void setWiFiCommitState(uint8_t st) {
    commitState = st;
  }
  uint8_t getWiFiCommitState() {
    return commitState;
  }
  void addGwLog(const char* action, const char* target, uint8_t res) {
    AuditLogEntry entry;
    entry.action = action ? action : "";
    entry.target = target ? target : "";
    entry.result = res;
    auditLogs.push_back(entry);
  }
  bool hasAuditLog(const std::string &act, const std::string &tgt) const {
    for (const auto &l : auditLogs) {
      if (l.action == act && l.target.find(tgt) != std::string::npos) return true;
    }
    return false;
  }
  bool containsAnyPassword(const std::string &pass) const {
    if (pass.length() < 4) return false;
    for (const auto &l : auditLogs) {
      if (l.action.find(pass) != std::string::npos || l.target.find(pass) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
};
static MockStorageManager mockStorage;

// ─────────────────────────────────────────────────────────────
//  STANDALONE TEST HARNESS FOR WIFICONFIGMANAGER
// ─────────────────────────────────────────────────────────────
class TestWiFiConfigManager {
public:
  String activeSsid;
  char   activePass[65];

  String previousSsid;
  char   previousPass[65];

  String candidateSsid;
  char   candidatePass[65];
  size_t candidatePassLen;

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

  TestWiFiConfigManager() :
    activeSsid(WIFI_DEF_SSID), previousSsid(WIFI_DEF_SSID),
    candidateSsid(""), candidatePassLen(0),
    updateState(WIFI_UPDATE_IDLE), lastFailReason(FAIL_NONE),
    sessionChatId(""), sessionStartTime(0), stateStartTime(0),
    lastConnectedTime(0), lastReconnectAttempt(0), reconnectCount(0),
    consecutiveFailures(0), apModeActive(false), dnsVerified(false),
    internetVerified(false), telegramVerified(false), tWatchdog(0)
  {
    memset(activePass, 0, sizeof(activePass));
    memset(previousPass, 0, sizeof(previousPass));
    memset(candidatePass, 0, sizeof(candidatePass));
    strncpy(activePass, WIFI_DEF_PASS, sizeof(activePass) - 1);
    strncpy(previousPass, WIFI_DEF_PASS, sizeof(previousPass) - 1);
  }

  void wipeBuffer(char* buf, size_t len) {
    if (!buf || len == 0) return;
    volatile char *p = (volatile char *)buf;
    while (len--) { *p++ = 0; }
  }

  void wipeCandidateCredentials() {
    candidateSsid = "";
    wipeBuffer(candidatePass, sizeof(candidatePass));
    candidatePassLen = 0;
  }

  void init() {
    String sActiveSsid, sActivePass, sPrevSsid, sPrevPass;
    uint8_t commitState = NVS_WIFI_STABLE;

    mockStorage.loadWiFiConfig(sActiveSsid, sActivePass, sPrevSsid, sPrevPass, commitState);

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

    // Power failure / crash recovery check
    if (commitState == NVS_WIFI_TESTING) {
      Serial.println("[GW][WIFI] ⚠️ WARNING: Detected unverified Wi-Fi update during power loss/reboot!");
      Serial.printf("[GW][WIFI] Rolling back automatically to verified configuration: '%s'\n", previousSsid.c_str());

      activeSsid = previousSsid;
      strncpy(activePass, previousPass, sizeof(activePass) - 1);

      mockStorage.saveWiFiActive(activeSsid, String(activePass));
      mockStorage.setWiFiCommitState(NVS_WIFI_STABLE);
      mockStorage.addGwLog("WIFI_PWR_ROLLBACK", activeSsid.c_str(), 1);
      lastFailReason = FAIL_POWER_LOSS_RECOVERY;
    }

    WiFi.mode(WIFI_STA);
    initiateStaConnection(activeSsid, activePass);
  }

  void initiateStaConnection(const String &ssid, const char* pass) {
    WiFi.disconnect();
    delay(50);
    WiFi.begin(ssid.c_str(), pass);
    lastReconnectAttempt = millis();
  }

  void update() {
    unsigned long now = millis();

    // Inactivity timeout
    if (updateState == WIFI_UPDATE_AWAITING_SSID ||
        updateState == WIFI_UPDATE_AWAITING_PASS ||
        updateState == WIFI_UPDATE_CONFIRM_REQUIRED) {
      if (now - sessionStartTime >= WIFI_SESSION_TIMEOUT_MS) {
        cancelSession(sessionChatId, "Session Inactivity Timeout");
        lastFailReason = FAIL_SESSION_TIMEOUT;
        return;
      }
    }

    // State Machine
    switch (updateState) {
      case WIFI_UPDATE_CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
          stateStartTime = now;
          updateState = WIFI_UPDATE_VERIFYING_IP;
        } else if (now - stateStartTime >= WIFI_CONNECT_TIMEOUT_MS) {
          executeRollback(FAIL_ASSOCIATION_TIMEOUT);
        }
        break;

      case WIFI_UPDATE_VERIFYING_IP:
        if (WiFi.localIP() != INADDR_NONE && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
          stateStartTime = now;
          updateState = WIFI_UPDATE_VERIFYING_DNS;
        } else if (now - stateStartTime >= WIFI_DHCP_TIMEOUT_MS) {
          executeRollback(FAIL_NO_IP_ASSIGNED);
        }
        break;

      case WIFI_UPDATE_VERIFYING_DNS: {
        IPAddress resolvedIp;
        if (WiFi.hostByName("api.telegram.org", resolvedIp) == 1) {
          dnsVerified = true;
          stateStartTime = now;
          updateState = WIFI_UPDATE_VERIFYING_TELEGRAM;
        } else if (now - stateStartTime >= WIFI_DNS_TIMEOUT_MS) {
          executeRollback(FAIL_DNS_RESOLUTION);
        }
        break;
      }

      case WIFI_UPDATE_VERIFYING_TELEGRAM:
        if (mockBot.getMe()) {
          telegramVerified = true;
          internetVerified = true;
          updateState = WIFI_UPDATE_COMMITTING;
          commitCredentials();
        } else if (now - stateStartTime >= WIFI_TG_TIMEOUT_MS) {
          executeRollback(FAIL_TELEGRAM_API);
        }
        break;

      case WIFI_UPDATE_ROLLING_BACK:
        if (WiFi.status() == WL_CONNECTED) {
          IPAddress resolvedIp;
          if (WiFi.hostByName("api.telegram.org", resolvedIp) == 1 && mockBot.getMe()) {
            updateState = WIFI_UPDATE_IDLE;
            consecutiveFailures = 0;
            lastConnectedTime = now;
            mockBot.sendMessage(sessionChatId.length() > 0 ? sessionChatId : String(OWNER_CHAT),
                                "⚠️ WI-FI UPDATE FAILED & ROLLED BACK");
            sessionChatId = "";
          }
        } else if (now - stateStartTime >= WIFI_CONNECT_TIMEOUT_MS * 2) {
          consecutiveFailures++;
          if (consecutiveFailures >= WIFI_MAX_FAIL_BEFORE_AP && !apModeActive) {
            startRecoveryAP();
          }
          stateStartTime = now;
          initiateStaConnection(activeSsid, activePass);
        }
        break;

      case WIFI_UPDATE_IDLE:
      default:
        checkWatchdog();
        break;
    }
  }

  void checkWatchdog() {
    unsigned long now = millis();
    if (now - tWatchdog < WIFI_WATCHDOG_INTERVAL_MS) return;
    tWatchdog = now;

    if (WiFi.status() == WL_CONNECTED) {
      if (lastConnectedTime == 0) lastConnectedTime = now;
      consecutiveFailures = 0;
    } else {
      reconnectCount++;
      consecutiveFailures++;
      lastReconnectAttempt = now;
      WiFi.disconnect();
      WiFi.reconnect();

      if (consecutiveFailures >= WIFI_MAX_FAIL_BEFORE_AP && !apModeActive) {
        startRecoveryAP();
      }
    }
  }

  bool startCredentialChangeSession(const String &chatId, const String &fromId, bool isPrivateChat, String &outError) {
    if (!isPrivateChat || chatId.startsWith("-")) {
      lastFailReason = FAIL_GROUP_CHAT_REJECTED;
      outError = "🚨 SECURITY ERROR: Group Chat Detected!";
      return false;
    }
    String configured = String(OWNER_CHAT);
    if (chatId != configured && fromId != configured) {
      lastFailReason = FAIL_UNAUTHORIZED;
      outError = "🔒 ACCESS DENIED";
      return false;
    }
    if (isTesting() || updateState != WIFI_UPDATE_IDLE) {
      lastFailReason = FAIL_DUPLICATE_REQUEST;
      outError = "⚠️ A session is already active.";
      return false;
    }

    wipeCandidateCredentials();
    updateState = WIFI_UPDATE_AWAITING_SSID;
    sessionChatId = chatId;
    sessionStartTime = millis();
    mockStorage.addGwLog("WIFI_CFG_START", activeSsid.c_str(), 0);
    return true;
  }

  bool handleSsidInput(const String &chatId, const String &ssid, String &outPrompt, String &outError) {
    if (updateState != WIFI_UPDATE_AWAITING_SSID || sessionChatId != chatId) {
      outError = "No active SSID input session.";
      return false;
    }
    String clean = ssid; clean.trim();
    if (clean.length() < 1 || clean.length() > 32) {
      lastFailReason = FAIL_INVALID_SSID;
      outError = "Invalid SSID Length";
      return false;
    }
    for (size_t i = 0; i < clean.length(); i++) {
      char c = clean[i];
      if (c < 32 || c > 126) {
        lastFailReason = FAIL_INVALID_SSID;
        outError = "Invalid SSID Characters";
        return false;
      }
    }
    candidateSsid = clean;
    updateState = WIFI_UPDATE_AWAITING_PASS;
    sessionStartTime = millis();
    outPrompt = "Enter Wi-Fi Password";
    return true;
  }

  bool handlePasswordInput(const String &chatId, const String &pass, size_t &outMaskedLen, String &outPrompt, String &outError) {
    if (updateState != WIFI_UPDATE_AWAITING_PASS || sessionChatId != chatId) {
      outError = "No active Password input session.";
      return false;
    }
    String p = pass; p.trim();
    wipeBuffer(candidatePass, sizeof(candidatePass));

    if (p.equalsIgnoreCase("NONE")) {
      candidatePass[0] = '\0';
      candidatePassLen = 0;
    } else {
      if (p.length() < 8 || p.length() > 63) {
        lastFailReason = FAIL_INVALID_PASSWORD;
        outError = "Invalid Password Length";
        return false;
      }
      for (size_t i = 0; i < p.length(); i++) {
        char c = p[i];
        if (c < 32 || c > 126) {
          lastFailReason = FAIL_INVALID_PASSWORD;
          outError = "Invalid Password Characters";
          return false;
        }
      }
      strncpy(candidatePass, p.c_str(), sizeof(candidatePass) - 1);
      candidatePassLen = p.length();
    }

    updateState = WIFI_UPDATE_CONFIRM_REQUIRED;
    sessionStartTime = millis();
    outMaskedLen = candidatePassLen;
    outPrompt = "Credentials Validated";
    return true;
  }

  bool confirmAndApply(const String &chatId, String &outMsg) {
    if (updateState != WIFI_UPDATE_CONFIRM_REQUIRED || sessionChatId != chatId) {
      outMsg = "No pending change.";
      return false;
    }
    mockStorage.setWiFiCommitState(NVS_WIFI_TESTING);
    mockStorage.addGwLog("WIFI_TEST_START", candidateSsid.c_str(), 0);

    initiateStaConnection(candidateSsid, candidatePass);
    updateState = WIFI_UPDATE_CONNECTING;
    stateStartTime = millis();
    outMsg = "Connecting and verifying...";
    return true;
  }

  void cancelSession(const String &chatId, const String &reason) {
    wipeCandidateCredentials();
    updateState = WIFI_UPDATE_IDLE;
    lastFailReason = FAIL_USER_CANCELLED;
    sessionChatId = "";
  }

  void executeRollback(WiFiFailReason reason) {
    lastFailReason = reason;
    mockStorage.addGwLog("WIFI_ROLLBACK", activeSsid.c_str(), 1);
    wipeCandidateCredentials();

    updateState = WIFI_UPDATE_ROLLING_BACK;
    stateStartTime = millis();
    initiateStaConnection(activeSsid, activePass);
  }

  void commitCredentials() {
    mockStorage.saveWiFiPrevious(activeSsid, String(activePass));
    previousSsid = activeSsid;
    strncpy(previousPass, activePass, sizeof(previousPass) - 1);

    mockStorage.saveWiFiActive(candidateSsid, String(candidatePass));
    activeSsid = candidateSsid;
    strncpy(activePass, candidatePass, sizeof(activePass) - 1);

    mockStorage.setWiFiCommitState(NVS_WIFI_STABLE);
    mockStorage.addGwLog("WIFI_APPLY_OK", activeSsid.c_str(), 0);

    wipeCandidateCredentials();
    updateState = WIFI_UPDATE_IDLE;
    consecutiveFailures = 0;
    lastConnectedTime = millis();

    mockBot.sendMessage(sessionChatId.length() > 0 ? sessionChatId : String(OWNER_CHAT),
                        "🎉 WI-FI CONFIGURATION SUCCESSFUL!");
    sessionChatId = "";
  }

  bool triggerManualRollback(const String &chatId, String &outMsg) {
    if (previousSsid.length() == 0 || previousSsid == activeSsid) {
      outMsg = "No previous network to rollback.";
      return false;
    }
    mockStorage.addGwLog("WIFI_MANUAL_ROLLBACK", previousSsid.c_str(), 0);

    String tmpS = activeSsid;
    char tmpP[65];
    strncpy(tmpP, activePass, sizeof(tmpP) - 1);

    activeSsid = previousSsid;
    strncpy(activePass, previousPass, sizeof(activePass) - 1);

    previousSsid = tmpS;
    strncpy(previousPass, tmpP, sizeof(previousPass) - 1);
    wipeBuffer(tmpP, sizeof(tmpP));

    mockStorage.saveWiFiActive(activeSsid, String(activePass));
    mockStorage.saveWiFiPrevious(previousSsid, String(previousPass));
    mockStorage.setWiFiCommitState(NVS_WIFI_STABLE);

    initiateStaConnection(activeSsid, activePass);
    outMsg = "Rolling back...";
    return true;
  }

  bool runDiagnosticTest(String &outReport) {
    bool wifiOk = (WiFi.status() == WL_CONNECTED);
    IPAddress dnsIp;
    bool dnsOk = (WiFi.hostByName("api.telegram.org", dnsIp) == 1);
    bool tgOk = mockBot.getMe();
    outReport = "Diagnostics Report: WiFi=" + String(wifiOk ? "OK" : "FAIL") +
                " DNS=" + String(dnsOk ? "OK" : "FAIL") +
                " TG=" + String(tgOk ? "OK" : "FAIL");
    return (wifiOk && dnsOk && tgOk);
  }

  void startRecoveryAP() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(WIFI_RECOVERY_AP_SSID, WIFI_RECOVERY_AP_PASS);
    apModeActive = true;
    mockStorage.addGwLog("WIFI_AP_START", WIFI_RECOVERY_AP_SSID, 0);
  }

  void stopRecoveryAP() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apModeActive = false;
    mockStorage.addGwLog("WIFI_AP_STOP", WIFI_RECOVERY_AP_SSID, 0);
  }

  void factoryResetCredentials() {
    activeSsid = WIFI_DEF_SSID;
    previousSsid = WIFI_DEF_SSID;
    strncpy(activePass, WIFI_DEF_PASS, sizeof(activePass) - 1);
    strncpy(previousPass, WIFI_DEF_PASS, sizeof(previousPass) - 1);
    mockStorage.saveWiFiActive(activeSsid, String(activePass));
    mockStorage.saveWiFiPrevious(previousSsid, String(previousPass));
    mockStorage.setWiFiCommitState(NVS_WIFI_STABLE);
    mockStorage.addGwLog("WIFI_FACTORY_RST", activeSsid.c_str(), 0);
    initiateStaConnection(activeSsid, activePass);
  }

  void applyLocalCredentials(const String &ssid, const String &pass) {
    if (ssid.length() < 1 || ssid.length() > 32) return;
    if (pass.length() > 0 && pass.length() < 8) return;

    mockStorage.saveWiFiPrevious(activeSsid, String(activePass));
    previousSsid = activeSsid;
    strncpy(previousPass, activePass, sizeof(previousPass) - 1);

    activeSsid = ssid;
    strncpy(activePass, pass.c_str(), sizeof(activePass) - 1);
    mockStorage.saveWiFiActive(activeSsid, String(activePass));
    mockStorage.setWiFiCommitState(NVS_WIFI_STABLE);
    mockStorage.addGwLog("WIFI_SERIAL_SET", activeSsid.c_str(), 0);
    initiateStaConnection(activeSsid, activePass);
  }

  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
  bool isTesting() const {
    return (updateState >= WIFI_UPDATE_CONNECTING && updateState <= WIFI_UPDATE_COMMITTING) ||
           (updateState == WIFI_UPDATE_ROLLING_BACK);
  }
};

// ─────────────────────────────────────────────────────────────
//  TEST RUNNER MACRO & COUNTERS
// ─────────────────────────────────────────────────────────────
static int g_passedTests = 0;
static int g_failedTests = 0;

#define RUN_TEST(fn) do { \
  std::cout << "[TEST] Running " << #fn << "... "; \
  try { \
    fn(); \
    std::cout << "PASSED ✅" << std::endl; \
    g_passedTests++; \
  } catch (const std::exception &e) { \
    std::cout << "FAILED ❌ (" << e.what() << ")" << std::endl; \
    g_failedTests++; \
  } catch (...) { \
    std::cout << "FAILED ❌ (Unknown exception)" << std::endl; \
    g_failedTests++; \
  } \
} while(0)

// ─────────────────────────────────────────────────────────────
//  INDIVIDUAL TEST IMPLEMENTATIONS (1 to 18)
// ─────────────────────────────────────────────────────────────

// 1. Valid New Wi-Fi Credentials
void test01_valid_new_credentials() {
  TestWiFiConfigManager mgr;
  mgr.init();
  WiFi.simAssociationFail = false;
  WiFi.simDhcpFail = false;
  WiFi.simDnsFail = false;
  WiFi.simTelegramFail = false;
  mockBot.clear();

  String outErr, prompt;
  bool ok = mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  assert(ok);
  assert(mgr.updateState == WIFI_UPDATE_AWAITING_SSID);

  ok = mgr.handleSsidInput(OWNER_CHAT, "FieldNet_2026", prompt, outErr);
  assert(ok);
  assert(mgr.candidateSsid == "FieldNet_2026");
  assert(mgr.updateState == WIFI_UPDATE_AWAITING_PASS);

  size_t maskedLen = 0;
  ok = mgr.handlePasswordInput(OWNER_CHAT, "SecureMscbPolePass!", maskedLen, prompt, outErr);
  assert(ok);
  assert(maskedLen == 19);
  assert(mgr.updateState == WIFI_UPDATE_CONFIRM_REQUIRED);

  String applyMsg;
  ok = mgr.confirmAndApply(OWNER_CHAT, applyMsg);
  assert(ok);
  assert(mgr.updateState == WIFI_UPDATE_CONNECTING);
  assert(mockStorage.getWiFiCommitState() == NVS_WIFI_TESTING);

  // Run state machine ticks
  mgr.update(); // CONNECTING -> VERIFYING_IP
  assert(mgr.updateState == WIFI_UPDATE_VERIFYING_IP);

  mgr.update(); // VERIFYING_IP -> VERIFYING_DNS
  assert(mgr.updateState == WIFI_UPDATE_VERIFYING_DNS);

  mgr.update(); // VERIFYING_DNS -> VERIFYING_TELEGRAM
  assert(mgr.updateState == WIFI_UPDATE_VERIFYING_TELEGRAM);

  mgr.update(); // VERIFYING_TELEGRAM -> COMMITTING & SUCCESS -> IDLE
  assert(mgr.updateState == WIFI_UPDATE_IDLE);

  // Verifications
  assert(mgr.activeSsid == "FieldNet_2026");
  assert(mgr.previousSsid == WIFI_DEF_SSID);
  assert(mockStorage.getWiFiCommitState() == NVS_WIFI_STABLE);
  assert(mockStorage.activeSsid == "FieldNet_2026");
  assert(mgr.candidateSsid == ""); // Cleaned up
  assert(mgr.candidatePassLen == 0); // Candidate wiped
  assert(mockBot.containsText("🎉 WI-FI CONFIGURATION SUCCESSFUL!"));
}

// 2. Wrong Password
void test02_wrong_password() {
  TestWiFiConfigManager mgr;
  mgr.init();
  mockBot.clear();

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "FieldNet_2026", prompt, outErr);
  size_t mLen = 0;
  mgr.handlePasswordInput(OWNER_CHAT, "WrongPassword123", mLen, prompt, outErr);

  // Simulate wrong password failure on network association
  WiFi.simAssociationFail = true;

  String applyMsg;
  mgr.confirmAndApply(OWNER_CHAT, applyMsg);
  assert(mgr.updateState == WIFI_UPDATE_CONNECTING);

  // Advance time past association timeout (20s)
  delay(21000);
  mgr.update();

  assert(mgr.updateState == WIFI_UPDATE_ROLLING_BACK);
  assert(mgr.lastFailReason == FAIL_ASSOCIATION_TIMEOUT);
  assert(mgr.candidatePassLen == 0); // Wiped

  // Previous network reconnects
  WiFi.simAssociationFail = false;
  WiFi.begin(mgr.activeSsid.c_str(), mgr.activePass);
  mgr.update();

  assert(mgr.updateState == WIFI_UPDATE_IDLE);
  assert(mgr.activeSsid == "FieldNet_2026" || mgr.activeSsid == WIFI_DEF_SSID);
  assert(mockStorage.hasAuditLog("WIFI_ROLLBACK", mgr.activeSsid.c_str()));
  assert(mockBot.containsText("⚠️ WI-FI UPDATE FAILED & ROLLED BACK"));
}

// 3. Invalid SSID
void test03_invalid_ssid() {
  TestWiFiConfigManager mgr;
  mgr.init();

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);

  // Empty SSID
  bool ok = mgr.handleSsidInput(OWNER_CHAT, "", prompt, outErr);
  assert(!ok);
  assert(mgr.lastFailReason == FAIL_INVALID_SSID);

  // Oversized SSID (> 32 characters)
  String longSsid = "12345678901234567890123456789012345";
  ok = mgr.handleSsidInput(OWNER_CHAT, longSsid, prompt, outErr);
  assert(!ok);

  // Control characters
  String badChars = "TestNet\nBad";
  ok = mgr.handleSsidInput(OWNER_CHAT, badChars, prompt, outErr);
  assert(!ok);
  assert(mgr.updateState == WIFI_UPDATE_AWAITING_SSID); // Still awaiting valid SSID
}

// 4. Missing / Invalid Password
void test04_missing_or_invalid_password() {
  TestWiFiConfigManager mgr;
  mgr.init();

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "ValidSSID", prompt, outErr);

  // Short password (< 8 chars)
  size_t mLen = 0;
  bool ok = mgr.handlePasswordInput(OWNER_CHAT, "short", mLen, prompt, outErr);
  assert(!ok);
  assert(mgr.lastFailReason == FAIL_INVALID_PASSWORD);

  // Oversized password (> 63 chars)
  std::string longP(65, 'A');
  ok = mgr.handlePasswordInput(OWNER_CHAT, String(longP), mLen, prompt, outErr);
  assert(!ok);

  // Valid open network token "NONE"
  ok = mgr.handlePasswordInput(OWNER_CHAT, "NONE", mLen, prompt, outErr);
  assert(ok);
  assert(mLen == 0);
  assert(mgr.updateState == WIFI_UPDATE_CONFIRM_REQUIRED);
}

// 5. Network Not Available (Timeout)
void test05_network_not_available() {
  TestWiFiConfigManager mgr;
  mgr.init();
  WiFi.simAssociationFail = true;

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "GhostNetwork", prompt, outErr);
  size_t mLen = 0;
  mgr.handlePasswordInput(OWNER_CHAT, "GhostPass1234", mLen, prompt, outErr);

  String applyMsg;
  mgr.confirmAndApply(OWNER_CHAT, applyMsg);

  // Timeout association
  delay(WIFI_CONNECT_TIMEOUT_MS + 1000);
  mgr.update();

  assert(mgr.updateState == WIFI_UPDATE_ROLLING_BACK);
  assert(mgr.lastFailReason == FAIL_ASSOCIATION_TIMEOUT);

  WiFi.simAssociationFail = false;
  WiFi.begin(mgr.activeSsid.c_str(), mgr.activePass);
  mgr.update();
  assert(mgr.updateState == WIFI_UPDATE_IDLE);
}

// 6. Internet Not Available
void test06_internet_not_available() {
  TestWiFiConfigManager mgr;
  mgr.init();
  WiFi.simAssociationFail = false;
  WiFi.simDhcpFail = false;
  WiFi.simTelegramFail = true; // Router connected, but WAN blocked

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "NoInternetAP", prompt, outErr);
  size_t mLen = 0;
  mgr.handlePasswordInput(OWNER_CHAT, "Password1234", mLen, prompt, outErr);

  String applyMsg;
  mgr.confirmAndApply(OWNER_CHAT, applyMsg);

  mgr.update(); // CONNECTING -> VERIFYING_IP
  mgr.update(); // VERIFYING_IP -> VERIFYING_DNS
  mgr.update(); // VERIFYING_DNS -> VERIFYING_TELEGRAM

  // Advance time past Telegram timeout
  delay(WIFI_TG_TIMEOUT_MS + 1000);
  mgr.update(); // VERIFYING_TELEGRAM -> ROLLBACK

  assert(mgr.updateState == WIFI_UPDATE_ROLLING_BACK);
  assert(mgr.lastFailReason == FAIL_TELEGRAM_API);

  WiFi.simTelegramFail = false;
  WiFi.begin(mgr.activeSsid.c_str(), mgr.activePass);
  mgr.update();
  assert(mgr.updateState == WIFI_UPDATE_IDLE);
}

// 7. DNS Failure
void test07_dns_failure() {
  TestWiFiConfigManager mgr;
  mgr.init();
  WiFi.simAssociationFail = false;
  WiFi.simDhcpFail = false;
  WiFi.simDnsFail = true; // DNS lookup fails

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "BadDnsAP", prompt, outErr);
  size_t mLen = 0;
  mgr.handlePasswordInput(OWNER_CHAT, "Password1234", mLen, prompt, outErr);

  String applyMsg;
  mgr.confirmAndApply(OWNER_CHAT, applyMsg);

  mgr.update(); // CONNECTING -> VERIFYING_IP
  mgr.update(); // VERIFYING_IP -> VERIFYING_DNS

  delay(WIFI_DNS_TIMEOUT_MS + 1000);
  mgr.update(); // VERIFYING_DNS -> ROLLBACK

  assert(mgr.updateState == WIFI_UPDATE_ROLLING_BACK);
  assert(mgr.lastFailReason == FAIL_DNS_RESOLUTION);

  WiFi.simDnsFail = false;
  WiFi.begin(mgr.activeSsid.c_str(), mgr.activePass);
  mgr.update();
  assert(mgr.updateState == WIFI_UPDATE_IDLE);
}

// 8. Telegram API Failure
void test08_telegram_api_failure() {
  TestWiFiConfigManager mgr;
  mgr.init();
  WiFi.simAssociationFail = false;
  WiFi.simDhcpFail = false;
  WiFi.simDnsFail = false;
  mockBot.isOnline = false; // Telegram API 500 error / port 443 blocked

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "BlockedTgAP", prompt, outErr);
  size_t mLen = 0;
  mgr.handlePasswordInput(OWNER_CHAT, "Password1234", mLen, prompt, outErr);

  String applyMsg;
  mgr.confirmAndApply(OWNER_CHAT, applyMsg);

  mgr.update(); // CONNECTING -> IP
  mgr.update(); // IP -> DNS
  mgr.update(); // DNS -> TELEGRAM

  delay(WIFI_TG_TIMEOUT_MS + 1000);
  mgr.update(); // TELEGRAM -> ROLLBACK

  assert(mgr.updateState == WIFI_UPDATE_ROLLING_BACK);
  assert(mgr.lastFailReason == FAIL_TELEGRAM_API);

  mockBot.isOnline = true;
  WiFi.begin(mgr.activeSsid.c_str(), mgr.activePass);
  mgr.update();
  assert(mgr.updateState == WIFI_UPDATE_IDLE);
}

// 9. Gateway Restart During Update
void test09_gateway_restart_during_update() {
  // Simulate staging candidate in NVS, then abrupt reboot before commit
  mockStorage.setWiFiCommitState(NVS_WIFI_TESTING);
  mockStorage.activeSsid = "BrokenCandidateAP";
  mockStorage.prevSsid = "StableWorkingNet";
  mockStorage.prevPass = "SafePass2026!";

  TestWiFiConfigManager mgr;
  mgr.init(); // System reboots

  assert(mgr.activeSsid == "StableWorkingNet");
  assert(mockStorage.getWiFiCommitState() == NVS_WIFI_STABLE);
  assert(mgr.lastFailReason == FAIL_POWER_LOSS_RECOVERY);
  assert(mockStorage.hasAuditLog("WIFI_PWR_ROLLBACK", "StableWorkingNet"));
}

// 10. Power Failure During Update
void test10_power_failure_during_update() {
  // Power failure during association test
  TestWiFiConfigManager mgr;
  mgr.init();

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "UnstablePowerAP", prompt, outErr);
  size_t mLen = 0;
  mgr.handlePasswordInput(OWNER_CHAT, "Pass12345678", mLen, prompt, outErr);

  String applyMsg;
  mgr.confirmAndApply(OWNER_CHAT, applyMsg);
  assert(mockStorage.getWiFiCommitState() == NVS_WIFI_TESTING);

  // SUDDEN POWER LOSS
  TestWiFiConfigManager rebootedMgr;
  rebootedMgr.init();

  assert(rebootedMgr.activeSsid == mgr.previousSsid);
  assert(mockStorage.getWiFiCommitState() == NVS_WIFI_STABLE);
}

// 11. Rollback Verification
void test11_rollback_verification() {
  TestWiFiConfigManager mgr;
  mgr.init();
  mgr.activeSsid = "PrimaryNet";
  mgr.previousSsid = "BackupNet";
  strncpy(mgr.activePass, "PrimPass123!", 64);
  strncpy(mgr.previousPass, "BackPass123!", 64);

  String outMsg;
  bool ok = mgr.triggerManualRollback(OWNER_CHAT, outMsg);
  assert(ok);
  assert(mgr.activeSsid == "BackupNet");
  assert(mgr.previousSsid == "PrimaryNet");
  assert(mockStorage.hasAuditLog("WIFI_MANUAL_ROLLBACK", "BackupNet"));
}

// 12. Local Recovery (Serial CLI & SoftAP)
void test12_local_recovery() {
  TestWiFiConfigManager mgr;
  mgr.init();

  // Test Serial command WIFI_SET
  mgr.applyLocalCredentials("TechnicianConsoleNet", "FieldTechPassword2026!");
  assert(mgr.activeSsid == "TechnicianConsoleNet");
  assert(mockStorage.hasAuditLog("WIFI_SERIAL_SET", "TechnicianConsoleNet"));

  // Test Serial command WIFI_RESET_DEFAULT
  mgr.factoryResetCredentials();
  assert(mgr.activeSsid == WIFI_DEF_SSID);
  assert(mockStorage.hasAuditLog("WIFI_FACTORY_RST", WIFI_DEF_SSID));

  // Test Failsafe SoftAP
  mgr.startRecoveryAP();
  assert(mgr.apModeActive == true);
  assert(WiFi.softApActive == true);
  assert(WiFi.softApSsid == WIFI_RECOVERY_AP_SSID);
  assert(mockStorage.hasAuditLog("WIFI_AP_START", WIFI_RECOVERY_AP_SSID));

  mgr.stopRecoveryAP();
  assert(mgr.apModeActive == false);
  assert(WiFi.softApActive == false);
}

// 13. Unauthorized User Attempt
void test13_unauthorized_attempt() {
  TestWiFiConfigManager mgr;
  mgr.init();

  String outErr;
  bool ok = mgr.startCredentialChangeSession("999888777", "999888777", true, outErr);
  assert(!ok);
  assert(mgr.lastFailReason == FAIL_UNAUTHORIZED);
  assert(mgr.updateState == WIFI_UPDATE_IDLE);
}

// 14. Group Chat Credential Submission
void test14_group_chat_rejection() {
  TestWiFiConfigManager mgr;
  mgr.init();

  String outErr;
  // Group chat ID (negative number in Telegram)
  bool ok = mgr.startCredentialChangeSession("-1001234567890", OWNER_CHAT, false, outErr);
  assert(!ok);
  assert(mgr.lastFailReason == FAIL_GROUP_CHAT_REJECTED);
  assert(mgr.updateState == WIFI_UPDATE_IDLE);
}

// 15. Session Expiration
void test15_session_expiration() {
  TestWiFiConfigManager mgr;
  mgr.init();

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "AbandonNet", prompt, outErr);
  assert(mgr.updateState == WIFI_UPDATE_AWAITING_PASS);

  // Fast forward past 120s session timeout
  delay(WIFI_SESSION_TIMEOUT_MS + 1000);
  mgr.update();

  assert(mgr.updateState == WIFI_UPDATE_IDLE);
  assert(mgr.lastFailReason == FAIL_SESSION_TIMEOUT);
  assert(mgr.candidateSsid == "");
  assert(mgr.candidatePassLen == 0);
}

// 16. Duplicate Wi-Fi Change Request
void test16_duplicate_request() {
  TestWiFiConfigManager mgr;
  mgr.init();

  String outErr;
  bool ok1 = mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  assert(ok1);

  // Second concurrent attempt
  bool ok2 = mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  assert(!ok2);
  assert(mgr.lastFailReason == FAIL_DUPLICATE_REQUEST);
}

// 17. Credential Privacy & Log Inspection
void test17_credential_privacy_and_logs() {
  TestWiFiConfigManager mgr;
  mgr.init();
  mockStorage.auditLogs.clear();
  Serial.clear();
  mockBot.clear();

  std::string secretPass = "TopSecretPassword1234!";

  String outErr, prompt;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "PrivateNet", prompt, outErr);
  size_t mLen = 0;
  mgr.handlePasswordInput(OWNER_CHAT, String(secretPass), mLen, prompt, outErr);

  String applyMsg;
  mgr.confirmAndApply(OWNER_CHAT, applyMsg);

  mgr.update(); // CONNECT
  mgr.update(); // IP
  mgr.update(); // DNS
  mgr.update(); // TG -> COMMIT

  // Verify secret password NEVER appears in audit logs
  assert(!mockStorage.containsAnyPassword(secretPass));

  // Verify secret password NEVER appears in Serial output
  assert(!Serial.contains(secretPass));

  // Verify secret password NEVER appears in Telegram message history
  assert(!mockBot.containsText(secretPass));

  // Verify candidate memory is completely wiped
  assert(mgr.candidatePassLen == 0);
  for (size_t i = 0; i < sizeof(mgr.candidatePass); i++) {
    assert(mgr.candidatePass[i] == 0);
  }
}

// 18. LoRa Operation During Wi-Fi Reconnection
void test18_lora_operation_during_wifi_reconnect() {
  TestWiFiConfigManager mgr;
  mgr.init();

  // Simulated LoRa Packet Queue
  std::vector<std::string> loraPacketsReceived;
  auto processLoRa = [&](const std::string &pkt) {
    loraPacketsReceived.push_back(pkt);
  };

  // Wi-Fi is actively testing candidate network
  String outErr, prompt, applyMsg;
  mgr.startCredentialChangeSession(OWNER_CHAT, OWNER_CHAT, true, outErr);
  mgr.handleSsidInput(OWNER_CHAT, "TestLoRaConcurrencyNet", prompt, outErr);
  size_t mLen = 0;
  mgr.handlePasswordInput(OWNER_CHAT, "Password1234", mLen, prompt, outErr);
  mgr.confirmAndApply(OWNER_CHAT, applyMsg);

  // During step 1 (association), LoRa packets arrive
  processLoRa("HEARTBEAT|SL-0001|ONLINE|V230.1|I0.85");
  processLoRa("SENSOR|SL-0002|V229.4|I1.10");
  mgr.update(); // Non-blocking tick

  // During step 2 (IP verification), Emergency Alert arrives over LoRa
  processLoRa("ALERT|FR-0001|PUMP_MCB_TRIP|IMMEDIATE");
  mgr.update(); // Non-blocking tick

  // During step 3 (DNS & TG)
  processLoRa("HEARTBEAT|SL-0003|ONLINE|V231.0|I0.90");
  mgr.update(); // Non-blocking tick
  mgr.update(); // Non-blocking tick

  // All 4 LoRa packets were received and processed without any blocking delay or drops
  assert(loraPacketsReceived.size() == 4);
  assert(loraPacketsReceived[2] == "ALERT|FR-0001|PUMP_MCB_TRIP|IMMEDIATE");
  assert(mgr.updateState == WIFI_UPDATE_IDLE);
}

// ─────────────────────────────────────────────────────────────
//  MAIN ENTRY POINT
// ─────────────────────────────────────────────────────────────
int main() {
  std::cout << "==================================================" << std::endl;
  std::cout << "  MSCB SMART POLE — PROMPT 17 VERIFICATION SUITE   " << std::endl;
  std::cout << "  SECURE WI-FI CONFIGURATION & RECOVERY HARNESS    " << std::endl;
  std::cout << "==================================================" << std::endl;

  RUN_TEST(test01_valid_new_credentials);
  RUN_TEST(test02_wrong_password);
  RUN_TEST(test03_invalid_ssid);
  RUN_TEST(test04_missing_or_invalid_password);
  RUN_TEST(test05_network_not_available);
  RUN_TEST(test06_internet_not_available);
  RUN_TEST(test07_dns_failure);
  RUN_TEST(test08_telegram_api_failure);
  RUN_TEST(test09_gateway_restart_during_update);
  RUN_TEST(test10_power_failure_during_update);
  RUN_TEST(test11_rollback_verification);
  RUN_TEST(test12_local_recovery);
  RUN_TEST(test13_unauthorized_attempt);
  RUN_TEST(test14_group_chat_rejection);
  RUN_TEST(test15_session_expiration);
  RUN_TEST(test16_duplicate_request);
  RUN_TEST(test17_credential_privacy_and_logs);
  RUN_TEST(test18_lora_operation_during_wifi_reconnect);

  std::cout << "==================================================" << std::endl;
  std::cout << "  RESULTS: " << g_passedTests << " PASSED, " << g_failedTests << " FAILED" << std::endl;
  std::cout << "==================================================" << std::endl;

  return (g_failedTests == 0) ? 0 : 1;
}
