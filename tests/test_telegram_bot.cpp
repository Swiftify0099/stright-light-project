/*
 * =====================================================================
 *  TEST_TELEGRAM_BOT.CPP — MSCB SMART POLE TELEGRAM BOT INTEGRATION TEST
 *  Prompt 16 Validation: Complete Central Gateway Telegram Management System
 * =====================================================================
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cassert>

using namespace std;

#define ARDUINO_H
#define UNIVERSAL_TELEGRAM_BOT_H
#define WIFI_H
#define WIFI_CLIENT_SECURE_H

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_STA 1

struct WiFiClientSecure {
  void setInsecure() {}
};

inline void configTime(long gmtOffset_sec, int daylightOffset_sec, const char* server1, const char* server2 = nullptr, const char* server3 = nullptr) {}

// ─────────────────────────────────────────────────────────────
//  ARDUINO & ESP32 SIMULATION LAYER
// ─────────────────────────────────────────────────────────────
class String : public std::string {
public:
  String() : std::string() {}
  String(const char *s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned int v) : std::string(std::to_string(v)) {}
  String(long v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  String(float v, int dec = 2) {
    char b[32];
    snprintf(b, sizeof(b), "%.*f", dec, v);
    *this = b;
  }
  String(double v, int dec = 2) {
    char b[32];
    snprintf(b, sizeof(b), "%.*f", dec, v);
    *this = b;
  }

  void trim() {
    size_t p = find_first_not_of(" \t\r\n");
    if (p == npos) { clear(); return; }
    size_t q = find_last_not_of(" \t\r\n");
    *this = substr(p, q - p + 1);
  }

  bool startsWith(const String &prefix) const {
    if (prefix.length() > length()) return false;
    return substr(0, prefix.length()) == prefix;
  }

  bool endsWith(const String &suffix) const {
    if (suffix.length() > length()) return false;
    return substr(length() - suffix.length()) == suffix;
  }

  int indexOf(char c, size_t from = 0) const {
    size_t pos = find(c, from);
    return (pos == npos) ? -1 : (int)pos;
  }

  int indexOf(const String &str, size_t from = 0) const {
    size_t pos = find(str, from);
    return (pos == npos) ? -1 : (int)pos;
  }

  String substring(size_t start, size_t end = npos) const {
    if (start >= length()) return "";
    if (end == npos || end > length()) return substr(start);
    return substr(start, end - start);
  }

  int toInt() const {
    try { return std::stoi(*this); } catch (...) { return 0; }
  }

  float toFloat() const {
    try { return std::stof(*this); } catch (...) { return 0.0f; }
  }

  bool equalsIgnoreCase(const String &other) const {
    if (length() != other.length()) return false;
    for (size_t i = 0; i < length(); i++) {
      if (tolower((*this)[i]) != tolower(other[i])) return false;
    }
    return true;
  }
};

static unsigned long g_simTimeMs = 100000;
unsigned long millis() { return g_simTimeMs; }
void delay(unsigned long ms) { g_simTimeMs += ms; }

long random(long minVal, long maxVal) {
  if (minVal >= maxVal) return minVal;
  return minVal + (rand() % (maxVal - minVal + 1));
}

struct SerialMock {
  void print(const String &s) {}
  void println(const String &s = "") {}
  template<typename... Args>
  void printf(const char *fmt, Args... args) {}
} Serial;

#define F(s) (s)

struct IPAddress {
  String toString() const { return "192.168.43.100"; }
};

struct WiFiMock {
  int status() { return 3; } // WL_CONNECTED = 3
  IPAddress localIP() { return IPAddress(); }
  IPAddress subnetMask() { return IPAddress(); }
  IPAddress gatewayIP() { return IPAddress(); }
  IPAddress dnsIP() { return IPAddress(); }
  String macAddress() { return "A4:CF:12:88:99:AA"; }
  int RSSI() { return -55; }
  void mode(int) {}
  void begin(const char*, const char*) {}
  void disconnect() {}
  void reconnect() {}
} WiFi;

struct ESPMock {
  uint32_t getFreeHeap() { return 248192; }
} ESP;

// ─────────────────────────────────────────────────────────────
//  UNIVERSAL TELEGRAM BOT MOCK
// ─────────────────────────────────────────────────────────────
struct telegramMessage {
  String chat_id;
  String from_id;
  String type;
  String text;
  String query_id;
  int message_id;
};

class UniversalTelegramBotMock {
public:
  template<typename T>
  UniversalTelegramBotMock(const String &token, T &dummy) :
    userName("MSCB_CentralGateway_Bot"),
    last_sent_message_id(1001),
    last_message_received(0),
    maxMessageLength(4096),
    waitForResponse(1000),
    apiOnline(true),
    lastAnswerQuery(""),
    lastAnswerText(""),
    lastAnswerAlert(false)
  {}

  String userName;
  int last_sent_message_id;
  int last_message_received;
  int maxMessageLength;
  int waitForResponse;
  bool apiOnline;

  vector<telegramMessage> messages;
  vector<telegramMessage> sentHistory;

  String lastAnswerQuery;
  String lastAnswerText;
  bool lastAnswerAlert;

  bool getMe() {
    return apiOnline;
  }

  int getUpdates(int offset) {
    if (!apiOnline) return 0;
    int count = (int)messages.size();
    if (count > 0) {
      last_message_received += count;
    }
    return count;
  }

  bool sendMessage(const String &chatId, const String &text, const String &parseMode = "") {
    if (!apiOnline) return false;
    telegramMessage m;
    m.chat_id = chatId;
    m.text = text;
    m.message_id = ++last_sent_message_id;
    sentHistory.push_back(m);
    return true;
  }

  bool sendMessageWithInlineKeyboard(const String &chatId, const String &text, const String &parseMode, const String &kb, int msgId = 0) {
    if (!apiOnline) return false;
    telegramMessage m;
    m.chat_id = chatId;
    m.text = text;
    m.type = kb; // Store keyboard JSON in type field for inspection
    m.message_id = (msgId > 0) ? msgId : (++last_sent_message_id);
    sentHistory.push_back(m);
    return true;
  }

  bool answerCallbackQuery(const String &queryId, const String &text = "", bool alert = false) {
    if (!apiOnline) return false;
    lastAnswerQuery = queryId;
    lastAnswerText = text;
    lastAnswerAlert = alert;
    return true;
  }

  String buildCommand(const String &cmd) { return cmd; }
  String sendGetToTelegram(const String &cmd) { return "{\"ok\":true}"; }
  bool checkForOkResponse(const String &resp) { return true; }

  void injectIncomingCallback(const String &chatId, const String &fromId, const String &queryId, const String &data, int msgId = 500) {
    messages.clear();
    telegramMessage m;
    m.chat_id = chatId;
    m.from_id = fromId;
    m.query_id = queryId;
    m.type = "callback_query";
    m.text = data;
    m.message_id = msgId;
    messages.push_back(m);
  }

  void injectIncomingText(const String &chatId, const String &fromId, const String &text, int msgId = 501) {
    messages.clear();
    telegramMessage m;
    m.chat_id = chatId;
    m.from_id = fromId;
    m.type = "text";
    m.text = text;
    m.message_id = msgId;
    messages.push_back(m);
  }
};

#define UniversalTelegramBot UniversalTelegramBotMock

// Test Admin Credentials for Simulated QA Environment
#define BOT_TOKEN "123456789:ABCdefGHIjklMNOpqrSTUvwxYZ"
#define OWNER_CHAT "5505296197"
#define ADMIN_ROLE_ID "5505296197"

// Include Core Types & Config
#include "../Sender/config.h"
#include "../Sender/Types.h"
#include "../Sender/TelegramSessionManager.h"
#include "../Sender/TelegramNotificationManager.h"
#include "../Sender/TelegramMenuManager.h"
#include "../Sender/TelegramCallbackManager.h"

// Define Include Guards to inject Mock Subsystems
#define MESH_MANAGER_H
#define LORA_MANAGER_H
#define FEEDER_MANAGER_H
#define REVOCATION_MANAGER_H
#define GW_REGISTRY_H
#define STORAGE_MANAGER_H
#define AUTH_SECURITY_H

// ─────────────────────────────────────────────────────────────
//  MOCK BACKEND SUBSYSTEMS
// ─────────────────────────────────────────────────────────────
class MeshManager {
public:
  vector<NodeRecord> nodes;
  vector<PendingRegistration> pending;

  int getNodeCount() const { return (int)nodes.size(); }
  NodeRecord* getNode(int idx) {
    if (idx >= 0 && idx < (int)nodes.size()) return &nodes[idx];
    return nullptr;
  }
  NodeRecord* findNode(const String &id) {
    for (auto &n : nodes) {
      if (n.logicalId == id || n.hwId == id) return &n;
    }
    return nullptr;
  }
  int findNodeIndex(const String &id) {
    for (size_t i = 0; i < nodes.size(); i++) {
      if (nodes[i].logicalId == id || nodes[i].hwId == id) return (int)i;
    }
    return -1;
  }

  int getPendingRegCount() const { return (int)pending.size(); }
  PendingRegistration* getPendingReg(int idx) {
    if (idx >= 0 && idx < (int)pending.size()) return &pending[idx];
    return nullptr;
  }
  int findPendingRegIndex(const String &hwId) {
    for (size_t i = 0; i < pending.size(); i++) {
      if (pending[i].hwId == hwId && pending[i].valid) return (int)i;
    }
    return -1;
  }
  void clearPendingReg(int idx) {
    if (idx >= 0 && idx < (int)pending.size()) pending[idx].valid = false;
  }
  String generateNextLogicalId(const String &type) {
    char buf[16];
    snprintf(buf, sizeof(buf), "POLE-%04d", (int)nodes.size() + 1);
    return String(buf);
  }
  int addOrUpdateNode(const String &hwId, const String &logId, const String &type, int rssi, int snr) {
    NodeRecord n;
    n.hwId = hwId;
    n.logicalId = logId;
    n.type = type;
    n.name = "Test Pole " + logId;
    n.location = "Main Road Sector 1";
    n.onlineState = "ONLINE";
    n.lifecycleState = POLE_STATE_ACTIVE;
    n.rssi = rssi;
    n.snr = snr;
    n.hopCount = 1;
    n.primaryNextHop = "GATEWAY";
    n.lastSeen = millis();
    n.registered = true;
    for (int f = 0; f < MAX_FEEDERS; f++) {
      n.feeders[f].feederId = f + 1;
      n.feeders[f].name = "Feeder " + String(f + 1);
      n.feeders[f].relayState = "OFF";
      n.feeders[f].mcbR = true;
      n.feeders[f].contactor = false;
      n.feeders[f].status = FEEDER_STATUS_ACTIVE;
      n.feeders[f].currentR = 0.0f;
      n.feeders[f].currentY = 0.0f;
      n.feeders[f].currentB = 0.0f;
    }
    nodes.push_back(n);
    return (int)nodes.size() - 1;
  }
} meshMgr;

class LoRaManager {
public:
  vector<String> queuedCommands;
  vector<String> txPackets;
  uint16_t masterSeq = 100;

  uint16_t getNextSeq() { return ++masterSeq; }
  void queueCommand(const String &chatId, const String &nodeId, const String &cmd, uint8_t prio = 1) {
    queuedCommands.push_back(nodeId + ":" + cmd);
  }
  void dispatchLoRaPacket(const String &type, const String &src, const String &dst, int ttl, uint16_t seq, const String &data) {
    txPackets.push_back(type + "|" + dst + "|" + data);
  }
} loraMgr;

class FeederManager {
public:
  String buildCompactFeederTable(const String &nodeId = "") {
    return "[F1: OFF | F2: OFF | F3: OFF | F4: OFF | F5: OFF | F6: OFF]";
  }
  String build21ChannelMeterTable(const String &nodeId = "") {
    NodeRecord *n = meshMgr.findNode(nodeId);
    if (!n) return "Node not found";
    String out = "⚡ *MSCB SMART POLE METROLOGY*\n";
    out += "📍 *Pole:* `" + n->name + "` (`" + n->logicalId + "`)\n";
    out += "🔹 *3-Phase Common Bus Voltage:*\n";
    out += "• VR: `" + String(n->commonVoltageR, 1) + " V` | VY: `" + String(n->commonVoltageY, 1) + " V` | VB: `" + String(n->commonVoltageB, 1) + " V`\n";
    out += "🔹 *18 Dedicated Current Channels (6 Feeders x 3 Phases):*\n";
    for (int f = 0; f < MAX_FEEDERS; f++) {
      out += "F" + String(f + 1) + ": R=" + String(n->feeders[f].currentR, 1) + "A Y=" + String(n->feeders[f].currentY, 1) + "A B=" + String(n->feeders[f].currentB, 1) + "A\n";
    }
    return out;
  }
} feederMgr;

class RevocationManager {
public:
  int revokedCount = 0;
  bool isRevoked(const String &id) { return false; }
  int getRevokedCount() const { return revokedCount; }
  const RevocationRecord* getRevocationByIndex(int i) const { return nullptr; }
  bool computeDeletionImpact(const String &poleId, DeletionImpact &impact) {
    impact.directChildCount = 2;
    impact.totalDownstreamCount = 3;
    impact.affectedRouteCount = 3;
    impact.hasOrphansWithoutRoute = false;
    return true;
  }
  bool requestNodeDeletion(const String &admin, const String &id, String &tx, String &tok, String &msg) {
    tx = "DEL-TX-001";
    tok = "99AABB";
    msg = "Node deletion prepared.";
    return true;
  }
  bool confirmNodeDeletion(const String &admin, const String &tx, const String &tok, String &res) {
    revokedCount++;
    res = "Node successfully revoked in NVS.";
    return true;
  }
  bool requestFeederDeactivation(const String &admin, const String &id, uint8_t fdr, String &tx, String &tok, String &msg) {
    tx = "FDR-TX-001";
    tok = "112233";
    msg = "Feeder deactivation prepared.";
    return true;
  }
  bool confirmFeederDeactivation(const String &admin, const String &tx, const String &tok, String &res) {
    res = "Feeder successfully deactivated.";
    return true;
  }
} revocationMgr;

class GwRegistry {
public:
  bool approveNode(const char *hw, const char *log, const char *nm, const char *ar, const char *inf, const char *pri, const char *bkp) {
    return true;
  }
  bool rejectNode(const char *hw, uint8_t reason) { return true; }
} gwRegistry;

class StorageManager {
public:
  vector<GwLogEntry> logs;
  void loadWiFiCredentials(String &s, String &p) { s = "Saaj"; p = "Saaj_7550"; }
  void saveDatabaseToNVS() {}
  int getGwLogCount() const { return (int)logs.size(); }
  const GwLogEntry* getGwLog(int idx) const {
    if (idx >= 0 && idx < (int)logs.size()) return &logs[idx];
    return nullptr;
  }
} storageMgr;

#define ALERT_MANAGER_H
class AlertManager {
public:
  uint32_t getActiveFaultCount() const { return 0; }
  bool acknowledgeIncident(uint32_t eventId, const char* adminId) { return true; }
  bool markResolved(uint32_t eventId, const char* adminId) { return true; }
} alertMgr;

// Include Module Implementations
#include "../Sender/TelegramSessionManager.cpp"
#include "../Sender/TelegramNotificationManager.cpp"
#include "../Sender/TelegramMenuManager.cpp"
#include "../Sender/TelegramCallbackManager.cpp"
#include "../Sender/GwTelegram.cpp"

// ─────────────────────────────────────────────────────────────
//  TEST RUNNER MACROS
// ─────────────────────────────────────────────────────────────
static int g_passCount = 0;
static int g_failCount = 0;

void assertTest(bool condition, const char *testName) {
  if (condition) {
    std::cout << "  [PASS] " << testName << std::endl;
    g_passCount++;
  } else {
    std::cout << "  [FAIL] " << testName << " <--- FAILED!" << std::endl;
    g_failCount++;
  }
}

// ─────────────────────────────────────────────────────────────
//  MAIN TEST EXECUTION (22 TEST SUITES)
// ─────────────────────────────────────────────────────────────
int main() {
  std::cout << "========================================================" << std::endl;
  std::cout << "  MSCB SMART POLE — PROMPT 16 TELEGRAM BOT VALIDATION   " << std::endl;
  std::cout << "========================================================" << std::endl;

  gwTelegram.init();

  // Populate Mock Network: 10 Poles with 6 Feeders each
  for (int i = 1; i <= 10; i++) {
    char hw[16], log[16], name[32];
    snprintf(hw, sizeof(hw), "A4CF12E8%02d", i);
    snprintf(log, sizeof(log), "POLE-%04d", i);
    snprintf(name, sizeof(name), "Main Road Pole %d", i);
    int idx = meshMgr.addOrUpdateNode(hw, log, (i == 5 ? "FARMER" : "STREET"), -65 - i, 8);
    NodeRecord *n = meshMgr.getNode(idx);
    n->commonVoltageR = 230.0f + (float)(i % 5) * 0.5f;
    n->commonVoltageY = 229.5f + (float)(i % 3) * 0.4f;
    n->commonVoltageB = 231.0f - (float)(i % 4) * 0.3f;
    n->commonVoltageStateR = "VALID";
    n->commonVoltageStateY = "VALID";
    n->commonVoltageStateB = "VALID";
    n->gridFrequency = 50.0f;
    n->hopCount = (i <= 2) ? 1 : ((i <= 6) ? 2 : 3);
    n->primaryNextHop = (i <= 2) ? "GATEWAY" : ((i <= 6) ? "POLE-0001" : "POLE-0003");

    // 18 Current Channels per pole
    for (int f = 0; f < MAX_FEEDERS; f++) {
      n->feeders[f].currentR = 10.0f + (float)f;
      n->feeders[f].currentY = 9.5f + (float)f;
      n->feeders[f].currentB = 10.2f + (float)f;
      n->feeders[f].totalPowerW = 6800.0f;
      n->feeders[f].powerFactor = 0.92f;
      n->feeders[f].relayState = (f == 0 && i % 2 == 0) ? "ON" : "OFF";
      n->feeders[f].contactor = (n->feeders[f].relayState == "ON");
    }
  }

  // ── TEST 1: Gateway Telegram Connection ──
  std::cout << "\n[TEST 1] Gateway Telegram Connection" << std::endl;
  assertTest(gwTelegram.isTelegramOnline() == true, "Telegram Bot API reported ONLINE via getMe()");
  assertTest(gwTelegram.isWiFiConnected() == true, "Gateway Wi-Fi connected with valid local IP");

  // ── TEST 2: Authorized Admin Login ──
  std::cout << "\n[TEST 2] Authorized Admin Login" << std::endl;
  assertTest(sessionMgr.isAuthorized("5505296197", "5505296197") == true, "Primary Owner Chat ID authorized");
  assertTest(sessionMgr.isAuthorized("5505296197", "") == true, "Chat ID check valid");
  assertTest(sessionMgr.getAdminCount() >= 1, "Admin allowlist loaded properly");

  // ── TEST 3: Unauthorized User Rejection ──
  std::cout << "\n[TEST 3] Unauthorized User Rejection (Fail-Closed)" << std::endl;
  assertTest(sessionMgr.isAuthorized("999999999", "999999999") == false, "Unknown User ID strictly rejected");
  assertTest(sessionMgr.isAuthorized("", "") == false, "Empty User & Chat ID rejected fail-closed");

  // Callback simulation from unauthorized user
  gwTelegram.getBot().sentHistory.clear();
  gwTelegram.getBot().injectIncomingCallback("999999999", "999999999", "Q_UNAUTH_1", "NAV:POLE_CTRL");
  gwTelegram.handleTelegramUpdates();
  assertTest(gwTelegram.getBot().lastAnswerAlert == true, "Unauthorized user received Access Denied alert");
  assertTest(loraMgr.queuedCommands.empty(), "Zero hardware actions executed for unauthorized user");

  // ── TEST 4: Dashboard Rendering ──
  std::cout << "\n[TEST 4] Dashboard Rendering" << std::endl;
  UserSession &sess = sessionMgr.getOrCreateSession("5505296197");
  String dashMsg = menuMgr.buildDashboardMessage("Saaj");
  String dashKb = menuMgr.buildDashboardKeyboard();
  assertTest(dashMsg.indexOf("MSCB SMART POLE CENTRAL GATEWAY") >= 0, "Dashboard title rendered");
  assertTest(dashMsg.indexOf("Registered Poles:") >= 0, "Pole count displayed");
  assertTest(dashMsg.indexOf("900 Current") >= 0 && dashMsg.indexOf("150 Voltage") >= 0, "Metric channel capacity listed");
  assertTest(dashKb.indexOf("NAV:POLE_MGMT") >= 0 && dashKb.indexOf("NAV:FEEDER_MGMT") >= 0, "Dashboard navigation buttons present");

  // ── TEST 5: Pole Pagination ──
  std::cout << "\n[TEST 5] Pole Pagination" << std::endl;
  sess.pageIndex = 0;
  String p1Msg = menuMgr.buildPoleManagementMessage(sess);
  String p1Kb = menuMgr.buildPoleManagementKeyboard(sess);
  assertTest(p1Msg.indexOf("POLE-0001") >= 0 && p1Msg.indexOf("POLE-0005") >= 0, "Page 1 displays first 5 poles");
  assertTest(p1Msg.indexOf("POLE-0008") < 0, "Page 1 does not spill into Page 2");
  assertTest(p1Kb.indexOf("PAGE:POLE_N") >= 0, "Next Page navigation button rendered");

  sess.pageIndex = 1;
  String p2Msg = menuMgr.buildPoleManagementMessage(sess);
  assertTest(p2Msg.indexOf("POLE-0006") >= 0 && p2Msg.indexOf("POLE-0010") >= 0, "Page 2 displays next 5 poles");

  // ── TEST 6: Feeder Pagination & Status ──
  std::cout << "\n[TEST 6] Feeder Management & Status" << std::endl;
  sess.selectedPole = "POLE-0001";
  String fdrMsg = menuMgr.buildFeederManagementMessage(sess);
  String fdrKb = menuMgr.buildFeederManagementKeyboard(sess);
  assertTest(fdrMsg.indexOf("FEEDER MANAGEMENT") >= 0, "Feeder screen header rendered");
  assertTest(fdrMsg.indexOf("Feeder 1:") >= 0 && fdrMsg.indexOf("Feeder 6:") >= 0, "All 6 feeders rendered");
  assertTest(fdrKb.indexOf("F_SEL:POLE-0001:1") >= 0, "Individual feeder selector button rendered");

  // ── TEST 7: Individual Pole Control ──
  std::cout << "\n[TEST 7] Individual Pole Control" << std::endl;
  loraMgr.queuedCommands.clear();
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_CTRL_1", "CMD:P_ON:POLE-0001");
  gwTelegram.handleTelegramUpdates();
  assertTest(loraMgr.queuedCommands.size() == 1, "LoRa command queued for Pole 1");
  assertTest(loraMgr.queuedCommands[0] == "POLE-0001:LIGHT_ON", "LIGHT_ON dispatched asynchronously");

  // ── TEST 8: Individual Feeder Control & 4-Step Feedback ──
  std::cout << "\n[TEST 8] Individual Feeder Control" << std::endl;
  loraMgr.queuedCommands.clear();
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_FDR_1", "CMD:F_ON:POLE-0002:3");
  gwTelegram.handleTelegramUpdates();
  assertTest(loraMgr.queuedCommands.size() == 1, "LoRa command queued for Feeder 3");
  assertTest(loraMgr.queuedCommands[0] == "POLE-0002:FDR_ON|3", "FDR_ON|3 dispatched to Pole 2");

  // Feeder diagnostic profile
  sess.selectedPole = "POLE-0002";
  sess.selectedFeeder = 3;
  String fdrDet = menuMgr.buildFeederDetailMessage(sess);
  assertTest(fdrDet.indexOf("FEEDER DIAGNOSTIC PROFILE") >= 0, "Diagnostic card rendered");
  assertTest(fdrDet.indexOf("Contactor Feedback") >= 0, "Contactor state shown");

  // ── TEST 9: Group Control ──
  std::cout << "\n[TEST 9] Group Control & Staggered Dispatch" << std::endl;
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_GRP_1", "CMD:GRP:STREET:ON");
  gwTelegram.handleTelegramUpdates();
  assertTest(sess.wizardState == WIZ_GROUP_CONFIRM, "Group control entered confirmation stage");
  assertTest(sess.confirmationToken.length() == 6, "Batch transaction token generated");

  loraMgr.queuedCommands.clear();
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_GRP_CONF", "CMD:GRP_CONF:");
  gwTelegram.handleTelegramUpdates();
  assertTest(loraMgr.queuedCommands.size() >= 5, "Staggered commands queued for all street light poles");

  // ── TEST 10: All Pole Control ──
  std::cout << "\n[TEST 10] All Pole Master Control" << std::endl;
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_ALL_1", "BATCH:ALL_ON_REQ");
  gwTelegram.handleTelegramUpdates();
  assertTest(sess.wizardState == WIZ_ALL_CONFIRM, "Entered All Poles Confirmation screen");

  loraMgr.queuedCommands.clear();
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_ALL_EXEC", "BATCH:EXEC:ALL_ON");
  gwTelegram.handleTelegramUpdates();
  assertTest(loraMgr.queuedCommands.size() == 1 && loraMgr.queuedCommands[0] == "ALL:LIGHT_ON", "Grid-wide ALL:LIGHT_ON queued");

  // ── TEST 11: Command Confirmation Token TTL Expiry ──
  std::cout << "\n[TEST 11] Confirmation Token TTL Expiry" << std::endl;
  String tok = sessionMgr.generateConfirmationToken(sess, "POLE-0003", WIZ_DELETE_CONFIRM, 1000); // 1 sec TTL
  assertTest(sessionMgr.validateConfirmationToken(sess, tok) == true, "Token valid immediately");
  delay(1500); // Wait for expiration
  assertTest(sessionMgr.validateConfirmationToken(sess, tok) == false, "Token rejected after TTL expiration");

  // ── TEST 12: Stale Callback Rejection ──
  std::cout << "\n[TEST 12] Stale Callback Rejection" << std::endl;
  gwTelegram.getBot().sentHistory.clear();
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_STALE_1", "P_SEL:POLE-9999");
  gwTelegram.handleTelegramUpdates();
  bool foundStaleNotice = false;
  for (const auto &m : gwTelegram.getBot().sentHistory) {
    if (m.text.indexOf("Selected Pole is no longer in database") >= 0) {
      foundStaleNotice = true;
      break;
    }
  }
  assertTest(foundStaleNotice,
             "Stale button click safely handled without index out-of-bounds");

  // ── TEST 13: Registration Management ──
  std::cout << "\n[TEST 13] Node Registration Management Wizard" << std::endl;
  PendingRegistration pendingNode;
  pendingNode.valid = true;
  pendingNode.hwId = "B4CF99887766";
  pendingNode.type = "STREET";
  pendingNode.rssi = -64;
  pendingNode.detectedAt = millis();
  meshMgr.pending.push_back(pendingNode);

  assertTest(meshMgr.getPendingRegCount() == 1, "Pending node detected in queue");

  // 1. Approve
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_REG_APP", "REG:APP:B4CF99887766");
  gwTelegram.handleTelegramUpdates();
  assertTest(sess.wizardState == WIZ_REG_NAME, "Prompted for Pole Name");

  // 2. Name text
  gwTelegram.getBot().injectIncomingText("5505296197", "5505296197", "East Bypass Pole 11");
  gwTelegram.handleTelegramUpdates();
  assertTest(sess.wizardState == WIZ_REG_AREA, "Prompted for Pole Area");

  // 3. Area text & LoRa confirmation
  loraMgr.txPackets.clear();
  gwTelegram.getBot().injectIncomingText("5505296197", "5505296197", "Industrial Area Sector 5");
  gwTelegram.handleTelegramUpdates();
  assertTest(sess.wizardState == WIZ_NONE, "Wizard completed and cleared");
  assertTest(loraMgr.txPackets.size() == 1 && loraMgr.txPackets[0].indexOf("REGISTERED|POLE-0011") >= 0,
             "Authoritative REGISTERED packet dispatched via LoRa");

  // ── TEST 14: Deletion & Revocation Management ──
  std::cout << "\n[TEST 14] Deletion Management & Child Route Impact" << std::endl;
  sess.selectedPole = "POLE-0004";
  String delMsg = menuMgr.buildDeleteManagementMessage(sess);
  assertTest(delMsg.indexOf("DOWNSTREAM CHILD NODE IMPACT") >= 0, "Impact analysis displayed");
  assertTest(delMsg.indexOf("Direct Children:") >= 0, "Direct child count shown");

  // Execute deletion with 2-step verification token
  String delTok = sessionMgr.generateConfirmationToken(sess, "POLE-0004", WIZ_DELETE_CONFIRM, 60000);
  gwTelegram.getBot().injectIncomingCallback("5505296197", "5505296197", "Q_DEL_EXEC", "DEL:EXEC:POLE-0004:" + delTok);
  gwTelegram.handleTelegramUpdates();
  assertTest(revocationMgr.getRevokedCount() == 1, "Pole permanently revoked in NVS");

  // ── TEST 15: Voltage Monitoring ──
  std::cout << "\n[TEST 15] 3-Phase Bus Voltage Monitoring" << std::endl;
  sess.pageIndex = 0;
  String voltMsg = menuMgr.buildVoltageMonitoringMessage(sess);
  assertTest(voltMsg.indexOf("3-PHASE VOLTAGE MONITORING") >= 0, "Voltage screen header rendered");
  assertTest(voltMsg.indexOf("Architecture Note") >= 0, "Common pole sensor architectural notice displayed");
  assertTest(voltMsg.indexOf("VR(V)") >= 0 && voltMsg.indexOf("VY(V)") >= 0 && voltMsg.indexOf("VB(V)") >= 0,
             "All 3 common phases present in table");

  // ── TEST 16: R/Y/B Current Monitoring (900 Channels Capacity) ──
  std::cout << "\n[TEST 16] 18 Current Channels per Pole (900 System Capacity)" << std::endl;
  sess.selectedPole = "POLE-0001";
  String currMsg = menuMgr.buildCurrentMonitoringMessage(sess);
  assertTest(currMsg.indexOf("18 Dedicated Current Channels") >= 0, "18 channels identified per pole");
  assertTest(currMsg.indexOf("F1: R=10.0A Y=9.5A B=10.2A") >= 0, "Feeder 1 3-phase currents rendered");
  assertTest(currMsg.indexOf("F6: R=15.0A Y=14.5A B=15.2A") >= 0, "Feeder 6 3-phase currents rendered");

  // ── TEST 17: Offline Node Display ──
  std::cout << "\n[TEST 17] Online / Offline Categorization" << std::endl;
  meshMgr.nodes[2].onlineState = "OFFLINE";
  String statMsg = menuMgr.buildNodeStatusMessage(sess);
  assertTest(statMsg.indexOf("ONLINE NODES") >= 0, "Online group listed");
  assertTest(statMsg.indexOf("OFFLINE NODES") >= 0 && statMsg.indexOf("POLE-0003") >= 0, "POLE-0003 correctly listed under OFFLINE");

  // ── TEST 18: Network Topology ──
  std::cout << "\n[TEST 18] Network Topology ASCII Tree" << std::endl;
  String topoMsg = menuMgr.buildTopologyMessage(sess);
  assertTest(topoMsg.indexOf("Gateway (Root Rank 0)") >= 0, "Root gateway rendered");
  assertTest(topoMsg.indexOf("├── 📍 POLE-0001") >= 0, "Direct 1-hop branch rendered");
  assertTest(topoMsg.indexOf("├── 📍 POLE-0003") >= 0, "Multi-hop child branch rendered");

  // ── TEST 19: Duplicate Alert Suppression ──
  std::cout << "\n[TEST 19] Duplicate Alert Suppression" << std::endl;
  notifMgr.clearAlert("POLE-0001");
  assertTest(notifMgr.shouldSuppressAlert("POLE-0001", "OVER_VOLT") == false, "First alert not suppressed");
  notifMgr.recordAlertSent("POLE-0001", "OVER_VOLT");
  assertTest(notifMgr.shouldSuppressAlert("POLE-0001", "OVER_VOLT") == true, "Duplicate alert within 120s suppressed");
  notifMgr.clearAlert("POLE-0001");
  assertTest(notifMgr.shouldSuppressAlert("POLE-0001", "OVER_VOLT") == false, "Alert permitted after clear");

  // ── TEST 20: Telegram API Disconnection ──
  std::cout << "\n[TEST 20] Telegram API Disconnection Handling" << std::endl;
  gwTelegram.getBot().apiOnline = false;
  gwTelegram.verifyTelegramApi();
  assertTest(gwTelegram.isTelegramOnline() == false, "API offline reported accurately");
  bool enqOk = notifMgr.enqueueMessage("5505296197", "Test while offline", "Markdown", "", 0, NOTIF_PRIO_NORMAL);
  assertTest(enqOk == true, "Message successfully queued while API offline without crashing");

  // ── TEST 21: Reconnection & Watchdog ──
  std::cout << "\n[TEST 21] Watchdog & Reconnection" << std::endl;
  gwTelegram.getBot().apiOnline = true;
  gwTelegram.checkWiFiAndTelegram();
  assertTest(gwTelegram.isTelegramOnline() == true, "Watchdog restored online status");

  // ── TEST 22: Rate Limit Throttling & Truncation ──
  std::cout << "\n[TEST 22] Rate Limit Throttling & Message Truncation" << std::endl;
  String bigString = "";
  for (int i = 0; i < 500; i++) bigString += "MSCB Smart Pole Data line " + String(i) + "\n";
  String truncated = notifMgr.safeTruncate(bigString, 4000);
  assertTest(truncated.length() <= 4000, "Oversized message truncated safely below Telegram 4096-char limit");
  assertTest(truncated.indexOf("truncated due to Telegram size limit") >= 0, "Truncation disclaimer appended");

  std::cout << "\n========================================================" << std::endl;
  std::cout << "  TOTAL PASS: " << g_passCount << " / " << (g_passCount + g_failCount) << std::endl;
  if (g_failCount == 0) {
    std::cout << "  STATUS    : ALL 22 TEST SUITES PASSED! [100% SUCCESS]" << std::endl;
  } else {
    std::cout << "  STATUS    : SOME TESTS FAILED!" << std::endl;
  }
  std::cout << "========================================================" << std::endl;

  return (g_failCount == 0) ? 0 : 1;
}
