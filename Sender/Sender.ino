/*
 * =====================================================================
 *  SMART STREET LIGHT / FARMER MESH SYSTEM — MASTER GATEWAY FIRMWARE
 *  Version   : GW_V10
 *  Platform  : ESP32 Dev Module + SX1278 RA-02 433 MHz + WiFi + Telegram
 *  Role      : Master Central Gateway & Interactive Telegram Bot
 * =====================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#ifndef TELEGRAM_MAX_MESSAGE_SIZE
#define TELEGRAM_MAX_MESSAGE_SIZE 8192
#endif

#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>
#include <time.h>
#include "config.h"

// ─────────────────────────────────────────────────────────────
//  DATA STRUCTURES: NODE DATABASE
// ─────────────────────────────────────────────────────────────
struct NodeRecord {
  String   hwId;              // Unique Chip HW ID (e.g. "A4CF12E8")
  String   logicalId;         // Assigned Logical ID (e.g. "P1", "P2", "SL-001")
  String   name;              // Friendly Name (e.g. "Main Road Pole 1")
  String   location;          // Installation Location
  String   type;              // "STREET", "FARMER", "CUSTOM"
  bool     registered;        // Is node registered in DB?
  String   onlineState;       // "ONLINE", "DEGRADED", "OFFLINE"
  int      rssi;              // Last received RSSI (dBm)
  int      snr;               // Last received SNR (dB)
  String   lightState;        // "ON", "OFF"
  float    voltage;           // AC Voltage (VAC)
  String   voltageState;      // "VALID", "NO_SIGNAL", "DISCONNECTED", "OUT_OF_RANGE"
  unsigned long voltageUpdatedAt; // Timestamp of last valid reading
  float    current;           // AC Current (A)
  float    power;             // Active Power (W)
  float    totalKwh;          // Accumulated Energy (kWh)
  String   fault;             // "OK", "OVER_VOLT", "UNDER_VOLT", "OVER_CURR", "LAMP_FAIL"
  String   prevFault;         // Fault deduplication
  uint32_t uptimeSec;         // Node reported uptime
  unsigned long firstSeen;    // Boot timestamp
  unsigned long lastSeen;     // Last packet timestamp
  unsigned long lastHeartbeat;// Last heartbeat poll timestamp
  String   primaryNextHop;    // Next hop node ID
  String   backupNextHop;     // Backup next hop
  int      hopCount;          // Hops to reach node
  String   routePath;         // Human readable path
  String   connectionMode;    // "DIRECT", "MESH", "ROUTE_RECOVERY"
  bool     schedEnabled;      // Autonomous schedule enabled
  int      schedOnMin;        // Schedule ON time (minutes from midnight)
  int      schedOffMin;       // Schedule OFF time (minutes from midnight)
  uint16_t cmdGeneration;     // Supersession protection counter
  int      neighborCount;     // Neighbor count reported by node
  String   neighborData;      // Cached neighbor telemetry
};

static NodeRecord nodes[MAX_NODES];
static int nodeCount = 0;

// ─────────────────────────────────────────────────────────────
//  DATA STRUCTURES: MESH ROUTING & TOPOLOGY
// ─────────────────────────────────────────────────────────────
struct GwRouteEntry {
  String   destination;
  String   primaryNextHop;
  String   backupNextHop;
  int      hopCount;
  int      rssi;
  bool     valid;
  unsigned long lastSuccess;
};

static GwRouteEntry gwRoutes[MAX_ROUTES];
static int gwRouteCount = 0;

struct NeighborSummary {
  String reportingNode;
  String neighborId;
  int16_t rssi;
  int8_t  snr;
  bool    responded;
  uint32_t lastSeen;
  bool    valid;
};
static NeighborSummary nbSummaries[MAX_NODES * 3];
static uint8_t nbSummaryCount = 0;

// ─────────────────────────────────────────────────────────────
//  DATA STRUCTURES: REGISTRATION STATE MACHINE
// ─────────────────────────────────────────────────────────────
enum RegState {
  REG_NONE = 0,
  REG_DETECTED,
  REG_TYPE_SELECTED,
  REG_AWAITING_NAME,
  REG_AWAITING_LOCATION,
  REG_CONFIRMATION,
  REG_COMPLETE,
  REG_CANCELLED
};

struct PendingRegistration {
  bool          valid;
  String        hwId;
  String        type;           // "STREET" or "FARMER"
  String        name;
  String        location;
  int           rssi;
  unsigned long detectedAt;
  RegState      state;
};

static PendingRegistration pendingRegs[MAX_PENDING_REG];
static int nextStreetLightId = 1;
static int nextFarmerId = 1;

// ─────────────────────────────────────────────────────────────
//  DATA STRUCTURES: PACKET DEDUPLICATION & SEQUENCE GENERATOR
// ─────────────────────────────────────────────────────────────
struct GwSeqCacheEntry {
  String   src;
  uint16_t seq;
};

static GwSeqCacheEntry seqCache[MAX_SEQ_CACHE];
static int seqHead = 0;
static uint16_t masterSeq = 100;

// ─────────────────────────────────────────────────────────────
//  DATA STRUCTURES: COMMAND QUEUE & PRIORITY SCHEDULER
// ─────────────────────────────────────────────────────────────
struct PendingCommand {
  bool          active;
  uint16_t      seq;
  String        chatId;
  int           messageId;
  String        targetNodeId;
  String        cmdString;
  uint8_t       priority;
  uint16_t      generation;
  unsigned long sentAt;
  uint8_t       retries;
  String        nextHop;
  unsigned long pingSentMs;   // For RTT calculation on PING
};

#define MAX_CMD_QUEUE 16
static PendingCommand cmdQueue[MAX_CMD_QUEUE];

// ─────────────────────────────────────────────────────────────
//  DATA STRUCTURES: TELEGRAM SESSIONS & NAVIGATION (MODULE 1)
// ─────────────────────────────────────────────────────────────
enum MenuScreen {
  MENU_HOME = 0,
  MENU_SETTINGS,
  MENU_STATUS,
  MENU_NEIGHBOURS,
  MENU_POLE_CTRL,
  MENU_POLE_NEIGHBOURS,
  MENU_REGISTRATION
};

#define MAX_SESSIONS 4

struct UserSession {
  String        chatId;
  MenuScreen    currentMenu;
  MenuScreen    previousMenu;
  int           lastMessageId;
  unsigned long lastActive;
  String        selectedPole;
  int           pageIndex;
  int           pendingRegIdx;
};

static UserSession sessions[MAX_SESSIONS];

// ─────────────────────────────────────────────────────────────
//  DATA STRUCTURES: GATEWAY AUDIT LOG
// ─────────────────────────────────────────────────────────────
struct GwLogEntry {
  char     action[32];
  char     target[16];
  uint32_t timestamp;
  uint8_t  result; // 0=OK, 1=FAIL
};

static GwLogEntry gwLog[MAX_CMD_LOG_ENTRIES];
static uint8_t gwLogHead = 0;
static uint8_t gwLogCount = 0;

// ─────────────────────────────────────────────────────────────
//  GLOBAL OBJECTS & STATE
// ─────────────────────────────────────────────────────────────
static WiFiClientSecure client;
static UniversalTelegramBot bot(BOT_TOKEN, client);
static Preferences prefs;

static String wifiSsid = WIFI_DEF_SSID;
static String wifiPass = WIFI_DEF_PASS;
static bool   wifiConnected   = false;
static bool   telegramOnline  = false;
static bool   hasNtpTime      = false;
static bool   bootMenuSent    = false;
static String lastLoRaPacketInfo = "None yet";

// Task Timing Counters (Non-blocking Cooperative Scheduling)
static unsigned long tTelegramPoll  = 0;
static unsigned long tQueueProcess  = 0;
static unsigned long tHeartbeat     = 0;
static unsigned long tNodeTimeout   = 0;
static unsigned long tNvsFlush      = 0;
static unsigned long tNtpResync     = 0;
static unsigned long tTimeSync      = 0;
static unsigned long tWifiWatchdog  = 0;

// ─────────────────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────
void   initHardware();
void   initLoRa();
void   checkWiFiAndTelegram();
bool   verifyTelegramApi();
void   sendStartupMainMenu(const String &chatId);
void   handleTelegramUpdates();
void   handleTelegramCallback(const String &chatId, const String &queryId, const String &data, int messageId);
void   handleTelegramText(const String &chatId, const String &text);
void   renderMenu(const String &chatId, UserSession &sess, int messageId = 0);
String buildHomeKeyboard();
String buildSettingsKeyboard();
String buildStatusKeyboard();
String buildHomeMessage();
String buildSettingsMessage();
String buildStatusMessage();
UserSession& getOrCreateSession(const String &chatId);
bool   isAuthorized(const String &chatId, const String &fromId = "");

void   processIncomingLoRa();
bool   parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out);
void   dispatchLoRaPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);
void   queueCommand(const String &chatId, const String &nodeId, const String &cmd, uint8_t priority = PRIO_MEDIUM);
void   processCommandQueue();

int    findNodeIndex(const String &id);
int    addOrUpdateNode(const String &hwId, const String &logicalId, const String &type, int rssi, int snr);
String findRouteNextHop(const String &dst);
void   updateRouteTable(const String &dst, const String &nextHop, int hops, int rssi);
void   checkNodeTimeouts();
void   processHeartbeatCycle();
void   processTimeSyncPush();
void   checkNtpSync();
void   saveDatabaseToNVS();
void   loadDatabaseFromNVS();

String formatUptime(unsigned long ms);
String getFormattedTime();
uint16_t getNextSeq();
void   addGwLog(const char* action, const char* target, uint8_t res);

// ─────────────────────────────────────────────────────────────
//  TIME & FORMATTING HELPERS
// ─────────────────────────────────────────────────────────────
uint16_t getNextSeq() {
  masterSeq++;
  if (masterSeq == 0 || masterSeq < 100) masterSeq = 100;
  return masterSeq;
}

void addGwLog(const char* action, const char* target, uint8_t res) {
  strncpy(gwLog[gwLogHead].action, action, 31);
  gwLog[gwLogHead].action[31] = '\0';
  strncpy(gwLog[gwLogHead].target, target, 15);
  gwLog[gwLogHead].target[15] = '\0';
  gwLog[gwLogHead].timestamp = (uint32_t)(time(nullptr));
  gwLog[gwLogHead].result = res;
  gwLogHead = (gwLogHead + 1) % MAX_CMD_LOG_ENTRIES;
  if (gwLogCount < MAX_CMD_LOG_ENTRIES) gwLogCount++;
}

String getFormattedTime() {
  time_t raw = time(nullptr);
  struct tm *ti = localtime(&raw);
  if (!ti || raw < 1700000000UL) return "NoSync";
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
  return String(buf);
}

String formatUptime(unsigned long ms) {
  unsigned long sec = ms / 1000;
  int h = sec / 3600;
  int m = (sec % 3600) / 60;
  int s = sec % 60;
  char buf[16];
  snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, s);
  return String(buf);
}

void checkNtpSync() {
  if (WiFi.status() == WL_CONNECTED) {
    configTime(19800, 0, "pool.ntp.org", "time.nist.gov"); // IST +5:30 (19800s)
    time_t raw = time(nullptr);
    if (raw > 1700000000UL) {
      hasNtpTime = true;
      Serial.printf("[GW][NTP] Synced successfully: %s\n", getFormattedTime().c_str());
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  AUTHORIZATION HELPER (Dual-Field Validation)
// ─────────────────────────────────────────────────────────────
bool isAuthorized(const String &chatId, const String &fromId) {
  String configured = String(OWNER_CHAT);
  configured.trim();
  if (configured.length() == 0) return true;

  String cId = chatId; cId.trim();
  String fId = fromId; fId.trim();

  if (cId == configured || fId == configured) {
    return true;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────
//  USER SESSION MANAGEMENT
// ─────────────────────────────────────────────────────────────
UserSession& getOrCreateSession(const String &chatId) {
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].chatId == chatId) {
      sessions[i].lastActive = millis();
      return sessions[i];
    }
  }

  int oldestIdx = 0;
  unsigned long oldestTime = sessions[0].lastActive;
  for (int i = 1; i < MAX_SESSIONS; i++) {
    if (sessions[i].chatId.length() == 0) {
      oldestIdx = i;
      break;
    }
    if (sessions[i].lastActive < oldestTime) {
      oldestTime = sessions[i].lastActive;
      oldestIdx = i;
    }
  }

  sessions[oldestIdx] = {};
  sessions[oldestIdx].chatId = chatId;
  sessions[oldestIdx].currentMenu = MENU_HOME;
  sessions[oldestIdx].previousMenu = MENU_HOME;
  sessions[oldestIdx].lastMessageId = 0;
  sessions[oldestIdx].lastActive = millis();
  return sessions[oldestIdx];
}

// ─────────────────────────────────────────────────────────────
//  DATABASE & ROUTING TABLE
// ─────────────────────────────────────────────────────────────
int findNodeIndex(const String &id) {
  if (id.length() == 0) return -1;
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].logicalId == id || nodes[i].hwId == id) {
      return i;
    }
  }
  return -1;
}

int addOrUpdateNode(const String &hwId, const String &logicalId, const String &type, int rssi, int snr) {
  int idx = findNodeIndex(hwId);
  if (idx >= 0) {
    nodes[idx].rssi = rssi;
    nodes[idx].snr  = snr;
    nodes[idx].lastSeen = millis();
    nodes[idx].onlineState = "ONLINE";
    if (logicalId.length() > 0) nodes[idx].logicalId = logicalId;
    if (type.length() > 0) nodes[idx].type = type;
    return idx;
  }

  if (nodeCount < MAX_NODES) {
    int i = nodeCount++;
    nodes[i] = {};
    nodes[i].hwId = hwId;
    nodes[i].logicalId = (logicalId.length() > 0) ? logicalId : hwId;
    nodes[i].name = "Pole " + nodes[i].logicalId;
    nodes[i].location = "Field Area";
    nodes[i].type = (type.length() > 0) ? type : "STREET";
    nodes[i].registered = false;
    nodes[i].onlineState = "ONLINE";
    nodes[i].rssi = rssi;
    nodes[i].snr  = snr;
    nodes[i].lightState = "OFF";
    nodes[i].voltage = 0.0f;
    nodes[i].voltageState = "";
    nodes[i].voltageUpdatedAt = 0;
    nodes[i].current = 0.0f;
    nodes[i].power = 0.0f;
    nodes[i].totalKwh = 0.0f;
    nodes[i].fault = "OK";
    nodes[i].prevFault = "OK";
    nodes[i].firstSeen = millis();
    nodes[i].lastSeen = millis();
    nodes[i].primaryNextHop = BROADCAST_ID;
    nodes[i].hopCount = 1;
    nodes[i].routePath = "DIRECT";
    nodes[i].connectionMode = "DIRECT";
    nodes[i].cmdGeneration = 0;
    nodes[i].neighborCount = 0;
    nodes[i].neighborData = "";
    Serial.printf("[GW][DB] Node added: %s (HW: %s)\n", nodes[i].logicalId.c_str(), hwId.c_str());
    return i;
  }
  return -1;
}

String findRouteNextHop(const String &dst) {
  if (dst == BROADCAST_ID || dst == "ALL") return BROADCAST_ID;
  for (int i = 0; i < gwRouteCount; i++) {
    if (gwRoutes[i].destination == dst && gwRoutes[i].valid) {
      return gwRoutes[i].primaryNextHop;
    }
  }
  int idx = findNodeIndex(dst);
  if (idx >= 0 && nodes[idx].primaryNextHop.length() > 0) {
    return nodes[idx].primaryNextHop;
  }
  return BROADCAST_ID;
}

void updateRouteTable(const String &dst, const String &nextHop, int hops, int rssi) {
  if (dst.length() == 0 || dst == GATEWAY_ID || dst == BROADCAST_ID) return;

  for (int i = 0; i < gwRouteCount; i++) {
    if (gwRoutes[i].destination == dst) {
      if (hops <= gwRoutes[i].hopCount || !gwRoutes[i].valid) {
        gwRoutes[i].primaryNextHop = nextHop;
        gwRoutes[i].hopCount = hops;
        gwRoutes[i].rssi = rssi;
        gwRoutes[i].valid = true;
        gwRoutes[i].lastSuccess = millis();
      }
      return;
    }
  }

  if (gwRouteCount < MAX_ROUTES) {
    gwRoutes[gwRouteCount].destination = dst;
    gwRoutes[gwRouteCount].primaryNextHop = nextHop;
    gwRoutes[gwRouteCount].backupNextHop = BROADCAST_ID;
    gwRoutes[gwRouteCount].hopCount = hops;
    gwRoutes[gwRouteCount].rssi = rssi;
    gwRoutes[gwRouteCount].valid = true;
    gwRoutes[gwRouteCount].lastSuccess = millis();
    gwRouteCount++;
  }
}

// ─────────────────────────────────────────────────────────────
//  NEIGHBOR & HEALTH REPORT PARSING
// ─────────────────────────────────────────────────────────────
void onNeighborHealthReport(const String &reportingNode, const String &data, int rssi) {
  Serial.printf("[GW][MESH] NB_HEALTH from %s: %s\n", reportingNode.c_str(), data.c_str());

  int nodeIdx = findNodeIndex(reportingNode);
  if (nodeIdx >= 0) {
    nodes[nodeIdx].rssi = rssi;
    nodes[nodeIdx].lastSeen = millis();
    nodes[nodeIdx].onlineState = "ONLINE";
    updateRouteTable(reportingNode, reportingNode, 1, rssi);
  }

  // Format: CHK_ID|uptime|count|nodeId,resp,rssi,snr,age|...
  int pipeCount = 0;
  int start = 0;
  String parts[3];
  
  for (int i = 0; i <= (int)data.length(); i++) {
    if (i == (int)data.length() || data[i] == '|') {
      if (pipeCount < 3) {
        parts[pipeCount] = data.substring(start, i);
      } else {
        String nbData = data.substring(start, i);
        int comma1 = nbData.indexOf(',');
        int comma2 = nbData.indexOf(',', comma1 + 1);
        int comma3 = nbData.indexOf(',', comma2 + 1);
        int comma4 = nbData.indexOf(',', comma3 + 1);
        
        if (comma1 > 0 && comma2 > 0) {
          String nbId = nbData.substring(0, comma1);
          bool resp = (nbData.substring(comma1 + 1, comma2) == "1");
          int nbRssi = (comma3 > 0) ? nbData.substring(comma2 + 1, comma3).toInt() : 0;
          int nbSnr = (comma4 > 0) ? nbData.substring(comma3 + 1, comma4).toInt() : 0;
          
          if (nbSummaryCount < MAX_NODES * 3) {
            nbSummaries[nbSummaryCount].reportingNode = reportingNode;
            nbSummaries[nbSummaryCount].neighborId = nbId;
            nbSummaries[nbSummaryCount].rssi = nbRssi;
            nbSummaries[nbSummaryCount].snr = nbSnr;
            nbSummaries[nbSummaryCount].responded = resp;
            nbSummaries[nbSummaryCount].lastSeen = millis();
            nbSummaries[nbSummaryCount].valid = true;
            nbSummaryCount++;
          } else {
            nbSummaryCount = 0; // rotate
          }
          
          if (resp) {
            updateRouteTable(nbId, reportingNode, 2, nbRssi);
          }
        }
      }
      pipeCount++;
      start = i + 1;
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  PACKET BUILDER & PARSER
// ─────────────────────────────────────────────────────────────
String buildPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String nh = (nextHop.length() > 0) ? nextHop : BROADCAST_ID;
  return type + "|" + String(GATEWAY_ID) + "|" + dst + "|" + nh + "|" + String(ttl) + "|" + String(seq) + "|" + data;
}

bool parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out) {
  out.valid = false;
  out.rssi = rssi;
  out.snr  = snr;

  String fields[7];
  int fieldCount = 0;
  int start = 0;
  for (int i = 0; i <= (int)raw.length(); i++) {
    bool isEnd = (i == (int)raw.length());
    bool isPipe = (!isEnd && raw[i] == '|' && fieldCount < 6);
    if (isEnd || isPipe) {
      fields[fieldCount++] = raw.substring(start, i);
      start = i + 1;
    }
  }

  if (fieldCount < 6) return false;

  out.type    = fields[0];
  out.src     = fields[1];
  out.dst     = fields[2];
  out.nextHop = fields[3];
  out.ttl     = fields[4].toInt();
  out.seq     = (uint16_t)fields[5].toInt();
  out.data    = (fieldCount == 7) ? fields[6] : "";
  out.valid   = true;
  return true;
}

// ─────────────────────────────────────────────────────────────
//  LORA DISPATCH & COMMAND QUEUE
// ─────────────────────────────────────────────────────────────
void dispatchLoRaPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String packet = buildPacket(type, dst, nextHop, ttl, seq, data);
  
  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  LoRa.receive();
  
  Serial.printf("[GW][LORA] TX: %s [SUCCESS]\n", packet.c_str());
  lastLoRaPacketInfo = "TX " + type + " to " + dst + " (Seq " + String(seq) + ")";
}

void queueCommand(const String &chatId, const String &nodeId, const String &cmd, uint8_t priority) {
  uint16_t seq = getNextSeq();
  Serial.printf("[GW] Creating command seq=%u for '%s' -> %s\n", seq, cmd.c_str(), nodeId.c_str());

  for (int i = 0; i < MAX_CMD_QUEUE; i++) {
    if (!cmdQueue[i].active) {
      cmdQueue[i].active = true;
      cmdQueue[i].seq = seq;
      cmdQueue[i].chatId = chatId;
      cmdQueue[i].messageId = 0;
      cmdQueue[i].targetNodeId = nodeId;
      cmdQueue[i].cmdString = cmd;
      cmdQueue[i].priority = priority;
      cmdQueue[i].generation = 0;
      cmdQueue[i].sentAt = 0;
      cmdQueue[i].retries = 0;
      cmdQueue[i].nextHop = findRouteNextHop(nodeId);
      cmdQueue[i].pingSentMs = 0;
      return;
    }
  }
  Serial.println(F("[GW][CMD] ⚠️ Command queue full!"));
  if (chatId.length() > 0 && telegramOnline) {
    bot.sendMessage(chatId, "⚠️ *Command Queue Full!* Please wait a moment.", "Markdown");
  }
}

void processCommandQueue() {
  unsigned long now = millis();
  int bestIdx = -1;
  int highestPrio = -1;

  for (int i = 0; i < MAX_CMD_QUEUE; i++) {
    if (cmdQueue[i].active) {
      if (cmdQueue[i].sentAt == 0) {
        if ((int)cmdQueue[i].priority > highestPrio) {
          highestPrio = (int)cmdQueue[i].priority;
          bestIdx = i;
        }
      }
      else if (now - cmdQueue[i].sentAt >= ACK_TIMEOUT_MS) {
        if (cmdQueue[i].retries < MAX_CMD_RETRIES) {
          if ((int)cmdQueue[i].priority > highestPrio) {
            highestPrio = (int)cmdQueue[i].priority;
            bestIdx = i;
          }
        } else {
          Serial.printf("[GW][CMD] ❌ Command '%s' to %s timed out after %d retries! (Seq: %u)\n",
                        cmdQueue[i].cmdString.c_str(), cmdQueue[i].targetNodeId.c_str(),
                        MAX_CMD_RETRIES, cmdQueue[i].seq);
          
          addGwLog(cmdQueue[i].cmdString.c_str(), cmdQueue[i].targetNodeId.c_str(), 1);
          
          if (cmdQueue[i].chatId.length() > 0 && telegramOnline) {
            String failMsg = "⚠️ *Command Timeout*\n\n"
                             "📍 *Target:* `" + cmdQueue[i].targetNodeId + "`\n"
                             "⚡ *Command:* `" + cmdQueue[i].cmdString + "`\n"
                             "⚠️ *Status:* No pole ACK response received after " + String(MAX_CMD_RETRIES) + " attempts.\n"
                             "💡 *Note:* Pole may be powered off or out of range.";
            bot.sendMessage(cmdQueue[i].chatId, failMsg, "Markdown");
          }

          cmdQueue[i].active = false;
        }
      }
    }
  }

  if (bestIdx >= 0) {
    cmdQueue[bestIdx].sentAt = millis();
    cmdQueue[bestIdx].retries++;

    if (cmdQueue[bestIdx].retries > 1) {
      Serial.printf("[GW][CMD] Retrying (%d/%d) '%s' -> %s (Seq: %u)...\n",
                    cmdQueue[bestIdx].retries, MAX_CMD_RETRIES,
                    cmdQueue[bestIdx].cmdString.c_str(), cmdQueue[bestIdx].targetNodeId.c_str(),
                    cmdQueue[bestIdx].seq);
    } else {
      Serial.printf("[GW] LoRa TX = %s (Seq: %u)\n", cmdQueue[bestIdx].cmdString.c_str(), cmdQueue[bestIdx].seq);
    }

    dispatchLoRaPacket("CMD", cmdQueue[bestIdx].targetNodeId, cmdQueue[bestIdx].nextHop,
                      DEFAULT_TTL, cmdQueue[bestIdx].seq, cmdQueue[bestIdx].cmdString);

    Serial.printf("[GW] Waiting ACK seq=%u\n", cmdQueue[bestIdx].seq);
  }
}

// ─────────────────────────────────────────────────────────────
//  INCOMING LORA PACKET PROCESSOR
// ─────────────────────────────────────────────────────────────
void processIncomingLoRa() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;

  String raw = "";
  while (LoRa.available()) raw += (char)LoRa.read();
  raw.trim();
  if (raw.length() == 0) return;

  int rssi = LoRa.packetRssi();
  int snr  = (int)LoRa.packetSnr();

  ParsedPacket pkt;
  if (!parsePacket(raw, rssi, snr, pkt)) return;

  for (int i = 0; i < MAX_SEQ_CACHE; i++) {
    if (seqCache[i].src == pkt.src && seqCache[i].seq == pkt.seq && pkt.seq != 0) {
      return;
    }
  }
  seqCache[seqHead].src = pkt.src;
  seqCache[seqHead].seq = pkt.seq;
  seqHead = (seqHead + 1) % MAX_SEQ_CACHE;

  Serial.printf("[GW][LORA] RX Type: %s | Src: %s | Dst: %s | RSSI: %d | Data: %s\n",
                pkt.type.c_str(), pkt.src.c_str(), pkt.dst.c_str(), rssi, pkt.data.c_str());

  lastLoRaPacketInfo = "RX " + pkt.type + " from " + pkt.src + " (" + String(rssi) + "dBm)";

  updateRouteTable(pkt.src, pkt.src, 1, rssi);

  int updateIdx = findNodeIndex(pkt.src);
  if (updateIdx >= 0) {
    nodes[updateIdx].lastSeen = millis();
    if (nodes[updateIdx].onlineState == "OFFLINE") {
      if (telegramOnline && nodes[updateIdx].registered) {
        String alert = "🟢 *POLE RECOVERED*\n\n"
                       "📍 *Pole:* *" + nodes[updateIdx].name + "* (`" + nodes[updateIdx].logicalId + "`)\n"
                       "⚠️ *Status:* ONLINE\n"
                       "📶 *RSSI:* `" + String(rssi) + " dBm`\n"
                       "🗺️ *Route:* `Gateway ← " + pkt.src + "`";
        bot.sendMessage(OWNER_CHAT, alert, "Markdown");
      }
    }
    nodes[updateIdx].onlineState = "ONLINE";
  }

  // ── 1. NEW_NODE PACKET ──
  if (pkt.type == "NEW_NODE") {
    String hwId = pkt.src;
    int idxHw = pkt.data.indexOf("HW_ID=");
    if (idxHw >= 0) {
      int endHw = pkt.data.indexOf('|', idxHw);
      hwId = (endHw > 0) ? pkt.data.substring(idxHw + 6, endHw) : pkt.data.substring(idxHw + 6);
    }

    // Check if already pending
    bool isPending = false;
    for (int i = 0; i < MAX_PENDING_REG; i++) {
      if (pendingRegs[i].valid && pendingRegs[i].hwId == hwId) {
        pendingRegs[i].detectedAt = millis();
        isPending = true;
        break;
      }
    }

    int nodeIdx = findNodeIndex(hwId);
    if (nodeIdx >= 0 && nodes[nodeIdx].registered) {
      // Already registered, re-send confirm
      String regConfirm = nodes[nodeIdx].logicalId + "|" + nodes[nodeIdx].name + "|" + nodes[nodeIdx].type;
      dispatchLoRaPacket("CMD", hwId, hwId, DEFAULT_TTL, getNextSeq(), "REGISTERED|" + regConfirm);
      Serial.printf("[GW][REG] Node %s already registered. Re-sent confirm.\n", hwId.c_str());
      
      int uIdx = pkt.data.indexOf("UPTIME=");
      if (uIdx >= 0) {
          nodes[nodeIdx].uptimeSec = pkt.data.substring(uIdx + 7, pkt.data.indexOf('|', uIdx) > 0 ? pkt.data.indexOf('|', uIdx) : pkt.data.length()).toInt();
      }
    } else if (!isPending) {
      // Find empty pending slot
      for (int i = 0; i < MAX_PENDING_REG; i++) {
        if (!pendingRegs[i].valid) {
          pendingRegs[i].valid = true;
          pendingRegs[i].hwId = hwId;
          pendingRegs[i].rssi = rssi;
          pendingRegs[i].detectedAt = millis();
          pendingRegs[i].state = REG_DETECTED;
          
          if (telegramOnline) {
            String msg = "🆕 *NEW POLE DETECTED*\n\n"
                         "⚙️ *HW ID:* `" + hwId + "`\n"
                         "📶 *RSSI:* `" + String(rssi) + " dBm`\n\n"
                         "Do you want to register this pole?";
            
            String keyboard = "{\"inline_keyboard\":[["
                              "{\"text\":\"✅ Register\",\"callback_data\":\"REG:" + hwId + "\"},"
                              "{\"text\":\"❌ Reject\",\"callback_data\":\"RREJ:" + hwId + "\"}"
                              "]]}";
                              
            bot.sendMessageWithInlineKeyboard(OWNER_CHAT, msg, "Markdown", keyboard);
          }
          break;
        }
      }
    }
  }
  // ── 1.5 NBHEALTHREPORT PACKET ──
  else if (pkt.type == "NBHEALTHREPORT") {
    onNeighborHealthReport(pkt.src, pkt.data, rssi);
  }
  // ── 2. ACK PACKET ──
  else if (pkt.type == "ACK") {
    Serial.printf("[GW] ACK received seq=%u from %s\n", pkt.seq, pkt.src.c_str());

    int s1 = pkt.data.indexOf('|');
    int s2 = pkt.data.indexOf('|', s1 + 1);
    String ackCmd = (s1 > 0) ? pkt.data.substring(0, s1) : pkt.data;
    String ackStatus = (s1 > 0 && s2 > 0) ? pkt.data.substring(s1 + 1, s2) : (s1 > 0 ? pkt.data.substring(s1 + 1) : "OK");
    String ackState = (s2 > 0) ? pkt.data.substring(s2 + 1) : "";

    int srcIdx = findNodeIndex(pkt.src);
    if (srcIdx < 0) srcIdx = addOrUpdateNode(pkt.src, pkt.src, "STREET", rssi, snr);
    String srcLogicalId = (srcIdx >= 0) ? nodes[srcIdx].logicalId : pkt.src;
    String srcHwId = (srcIdx >= 0) ? nodes[srcIdx].hwId : pkt.src;

    if (srcIdx >= 0) {
      if (ackState.indexOf("STATE=ON") >= 0 || ackState == "ON") nodes[srcIdx].lightState = "ON";
      if (ackState.indexOf("STATE=OFF") >= 0 || ackState == "OFF") nodes[srcIdx].lightState = "OFF";
      int pV = ackState.indexOf("V=");
      if (pV >= 0) {
        int endV = ackState.indexOf('|', pV);
        nodes[srcIdx].voltage = ackState.substring(pV + 2, endV > 0 ? endV : ackState.length()).toFloat();
        nodes[srcIdx].voltageUpdatedAt = millis();
      }
      int pVs = ackState.indexOf("VS=");
      if (pVs >= 0) {
        int endVs = ackState.indexOf('|', pVs);
        nodes[srcIdx].voltageState = ackState.substring(pVs + 3, endVs > 0 ? endVs : ackState.length());
      }
      int pA = ackState.indexOf("A=");
      if (pA >= 0) {
        int endA = ackState.indexOf('|', pA);
        nodes[srcIdx].current = ackState.substring(pA + 2, endA > 0 ? endA : ackState.length()).toFloat();
      }
    }

    for (int i = 0; i < MAX_CMD_QUEUE; i++) {
      if (cmdQueue[i].active) {
        bool targetMatch = (cmdQueue[i].targetNodeId == pkt.src || 
                            cmdQueue[i].targetNodeId == srcLogicalId || 
                            cmdQueue[i].targetNodeId == srcHwId ||
                            cmdQueue[i].targetNodeId == "ALL");

        if (targetMatch && (cmdQueue[i].seq == pkt.seq || cmdQueue[i].cmdString.startsWith(ackCmd))) {
          cmdQueue[i].active = false;
          addGwLog(cmdQueue[i].cmdString.c_str(), pkt.src.c_str(), 0);

          Serial.printf("[GW] Command result = %s, Light State = %s\n",
                        ackStatus.c_str(), (srcIdx >= 0) ? nodes[srcIdx].lightState.c_str() : "UNKNOWN");

          if (cmdQueue[i].chatId.length() > 0 && telegramOnline) {
            String poleName = (srcIdx >= 0) ? nodes[srcIdx].name : pkt.src;
            String lightSt = (srcIdx >= 0) ? nodes[srcIdx].lightState : "UPDATED";
            
            String confMsg = "━━━━━━━━━━━━━━━━━━━━\n"
                             "✅ *" + String(lightSt == "ON" ? "LIGHT ON" : "LIGHT OFF") + "*\n"
                             "━━━━━━━━━━━━━━━━━━━━\n\n"
                             "Pole:\n*" + poleName + "*\n\n"
                             "Light:\n" + String(lightSt == "ON" ? "🟢 ON" : "🔴 OFF") + "\n\n";

            unsigned long age = (millis() - nodes[srcIdx].voltageUpdatedAt);
            if (nodes[srcIdx].voltageUpdatedAt == 0) {
              confMsg += "Voltage:\n⚠️ No Data\n\n";
              confMsg += "Sensor:\n⚠️ Awaiting Telemetry\n\n";
            } else if (age > 180000UL) {
              confMsg += "Voltage:\n⚡ " + String(nodes[srcIdx].voltage, 1) + " V (STALE)\n\n";
              confMsg += "Sensor:\n⚠️ STALE (" + String(age / 1000) + "s ago)\n\n";
            } else if (nodes[srcIdx].voltageState == "VALID") {
              confMsg += "Voltage:\n⚡ " + String(nodes[srcIdx].voltage, 1) + " V\n\n";
              confMsg += "Sensor:\n✅ VALID\n\n";
            } else if (nodes[srcIdx].voltageState == "NO_SIGNAL") {
              confMsg += "Voltage:\n⚠️ NO SIGNAL (0.0 V)\n\n";
              confMsg += "Sensor:\n❌ ZMPT101B SIGNAL NOT DETECTED\n\n";
            } else if (nodes[srcIdx].voltageState == "DISCONNECTED") {
              confMsg += "Voltage:\n⚠️ NO SENSOR\n\n";
              confMsg += "Sensor:\n❌ DISCONNECTED\n\n";
            } else if (nodes[srcIdx].voltageState == "OUT_OF_RANGE") {
              confMsg += "Voltage:\n⚠️ " + String(nodes[srcIdx].voltage, 1) + " V\n\n";
              confMsg += "Sensor:\n❌ OUT OF RANGE\n\n";
            } else {
              confMsg += "Voltage:\n⚡ " + String(nodes[srcIdx].voltage, 1) + " V\n\n";
              confMsg += "Sensor:\n✅ VALID\n\n";
            }

            if (nodes[srcIdx].current > 0 || lightSt == "ON") {
                confMsg += "Current:\n🔌 " + String(nodes[srcIdx].current, 2) + " A\n\n";
            } else {
                confMsg += "Current:\n🔌 0.00 A\n\n";
            }
            
            confMsg += "RSSI:\n" + String(rssi) + " dBm\n\n"
                       "Route:\nGateway → " + pkt.src + "\n\n"
                       "ACK:\n✅ Confirmed\n"
                       "━━━━━━━━━━━━━━━━━━━━";
            bot.sendMessage(cmdQueue[i].chatId, confMsg, "Markdown");
          }
          break;
        }
      }
    }
  }
  else if (pkt.type == "LINK_ACK") {
    int srcIdx = findNodeIndex(pkt.src);

    for (int i = 0; i < MAX_CMD_QUEUE; i++) {
      if (cmdQueue[i].active && (cmdQueue[i].seq == pkt.seq || cmdQueue[i].cmdString.startsWith("PING"))) {
        unsigned long rtt = (cmdQueue[i].pingSentMs > 0) ? (millis() - cmdQueue[i].pingSentMs) : 0;
        cmdQueue[i].active = false;
        
        if (cmdQueue[i].chatId.length() > 0 && telegramOnline) {
          String linkMsg = "📡 *LoRa Link Test Result*\n\n"
                           "📍 *Node:* `" + pkt.src + "`\n"
                           "⏱ *Round-Trip Time:* `" + String(rtt) + " ms`\n"
                           "📶 *Gateway RX RSSI:* `" + String(rssi) + " dBm`\n"
                           "📶 *Remote Node RSSI:* `" + pkt.data + "`\n"
                           "🟢 *Status:* LINK ACTIVE & HEALTHY";
          bot.sendMessage(cmdQueue[i].chatId, linkMsg, "Markdown");
        }
        break;
      }
    }
  }
  // ── 4. HEARTBEAT PACKET ──
  else if (pkt.type == "HEARTBEAT") {
    int idx = findNodeIndex(pkt.src);
    if (idx < 0) idx = addOrUpdateNode(pkt.src, pkt.src, "STREET", rssi, snr);

    if (idx >= 0) {
      nodes[idx].lastHeartbeat = millis();
      int pV = pkt.data.indexOf("V=");
      if (pV >= 0) {
        int endV = pkt.data.indexOf('|', pV);
        nodes[idx].voltage = pkt.data.substring(pV + 2, endV > 0 ? endV : pkt.data.length()).toFloat();
        nodes[idx].voltageUpdatedAt = millis();
      }
      int pVs = pkt.data.indexOf("VS=");
      if (pVs >= 0) {
        int endVs = pkt.data.indexOf('|', pVs);
        nodes[idx].voltageState = pkt.data.substring(pVs + 3, endVs > 0 ? endVs : pkt.data.length());
      }
      int pA = pkt.data.indexOf("A=");
      if (pA >= 0) {
        int endA = pkt.data.indexOf('|', pA);
        nodes[idx].current = pkt.data.substring(pA + 2, endA > 0 ? endA : pkt.data.length()).toFloat();
      }
      int pU = pkt.data.indexOf("UPTIME=");
      if (pU >= 0) {
        int endU = pkt.data.indexOf('|', pU);
        nodes[idx].uptimeSec = pkt.data.substring(pU + 7, endU > 0 ? endU : pkt.data.length()).toInt();
      }
      int pL = pkt.data.indexOf("LIGHT=");
      if (pL >= 0) {
        int endL = pkt.data.indexOf('|', pL);
        nodes[idx].lightState = pkt.data.substring(pL + 6, endL > 0 ? endL : pkt.data.length());
      }
    }
  }
  // ── 5. STATUS PACKET ──
  else if (pkt.type == "STATUS") {
    int idx = findNodeIndex(pkt.src);
    if (idx < 0) idx = addOrUpdateNode(pkt.src, pkt.src, "STREET", rssi, snr);

    if (idx >= 0) {
      int pV = pkt.data.indexOf("V=");
      if (pV >= 0) {
        int endV = pkt.data.indexOf('|', pV);
        nodes[idx].voltage = pkt.data.substring(pV + 2, endV > 0 ? endV : pkt.data.length()).toFloat();
        nodes[idx].voltageUpdatedAt = millis();
      }
      int pVs = pkt.data.indexOf("VS=");
      if (pVs >= 0) {
        int endVs = pkt.data.indexOf('|', pVs);
        nodes[idx].voltageState = pkt.data.substring(pVs + 3, endVs > 0 ? endVs : pkt.data.length());
      }
      int pA = pkt.data.indexOf("A=");
      if (pA >= 0) {
        int endA = pkt.data.indexOf('|', pA);
        nodes[idx].current = pkt.data.substring(pA + 2, endA > 0 ? endA : pkt.data.length()).toFloat();
      }
      int pP = pkt.data.indexOf("P=");
      if (pP >= 0) nodes[idx].power = pkt.data.substring(pP + 2, pkt.data.indexOf('|', pP)).toFloat();
      int pK = pkt.data.indexOf("KWH=");
      if (pK >= 0) nodes[idx].totalKwh = pkt.data.substring(pK + 4, pkt.data.indexOf('|', pK)).toFloat();
      int pL = pkt.data.indexOf("LGT=");
      if (pL >= 0) nodes[idx].lightState = pkt.data.substring(pL + 4, pkt.data.indexOf('|', pL));
      int pF = pkt.data.indexOf("FLT=");
      if (pF >= 0) nodes[idx].fault = pkt.data.substring(pF + 4, pkt.data.indexOf('|', pF));
    }
  }

  LoRa.receive();
}

// ─────────────────────────────────────────────────────────────
//  TELEGRAM KEYBOARDS & MENU BUILDERS (MODULE 1)
// ─────────────────────────────────────────────────────────────
String buildHomeKeyboard() {
  return "[[{\"text\":\"💡 ALL LIGHTS ON\",\"callback_data\":\"ALL_ON\"}],"
         "[{\"text\":\"🌙 ALL LIGHTS OFF\",\"callback_data\":\"ALL_OFF\"}],"
         "[{\"text\":\"⚙️ SETTINGS\",\"callback_data\":\"SETTINGS\"}]]";
}

String buildSettingsKeyboard() {
  return "[[{\"text\":\"📊 STATUS\",\"callback_data\":\"STATUS\"},"
          "{\"text\":\"🔄 REFRESH\",\"callback_data\":\"REFRESH\"}],"
         "[{\"text\":\"📡 NEIGHBOURS\",\"callback_data\":\"NEIGH\"},"
          "{\"text\":\"📝 REGISTRATION\",\"callback_data\":\"REGLIST\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"BACK\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String buildStatusKeyboard() {
  return "[[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"REFRESH\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"BACK\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String buildHomeMessage() {
  String lightStateSummary = "UNKNOWN";
  if (nodeCount > 0) {
    bool anyOn = false;
    for (int i = 0; i < nodeCount; i++) {
      if (nodes[i].lightState == "ON") { anyOn = true; break; }
    }
    lightStateSummary = anyOn ? "ON 💡" : "OFF 🌙";
  }

  String msg = "💡 *SMART STREET LIGHT*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🟢 *Gateway:* `ONLINE`\n";
  msg += "📡 *LoRa Mesh:* `READY (433MHz SF10)`\n";
  msg += "📶 *WiFi:* `CONNECTED` (`" + wifiSsid + "`)\n";
  msg += "🌐 *IP:* `" + WiFi.localIP().toString() + "`\n";
  msg += "⏱ *Uptime:* `" + formatUptime(millis()) + "`\n";
  msg += "💡 *Street Lights:* *" + lightStateSummary + "*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Select an action below:";
  return msg;
}

String buildSettingsMessage() {
  String msg = "⚙️ *SETTINGS & DIAGNOSTICS*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "Gateway & Network Control\n\n";
  msg += "• 📊 *Status* — View live system telemetry\n";
  msg += "• 🔄 *Refresh* — Update real-time status\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

String buildStatusMessage() {
  String lastPkt = (lastLoRaPacketInfo.length() > 0) ? lastLoRaPacketInfo : "None yet";

  String msg = "📊 *GATEWAY STATUS*\n";
  msg += "━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🟢 *Gateway:* `ONLINE`\n";
  msg += "📡 *LoRa:* `READY (433.0 MHz SF10)`\n";
  msg += "📶 *WiFi:* `CONNECTED` (`" + wifiSsid + "`)\n";
  msg += "🌐 *IP:* `" + WiFi.localIP().toString() + "`\n";
  msg += "📶 *WiFi Signal:* `" + String(WiFi.RSSI()) + " dBm`\n";
  msg += "⏱ *Uptime:* `" + formatUptime(millis()) + "`\n";
  msg += "🕒 *Time:* `" + getFormattedTime() + "`\n";
  msg += "📨 *Telegram:* `ONLINE`\n";
  msg += "📡 *Last LoRa:* `" + lastPkt + "`\n";
  msg += "💾 *Free Heap:* `" + String(ESP.getFreeHeap()) + " bytes`\n";
  msg += "📊 *Nodes in DB:* `" + String(nodeCount) + "`\n";
  msg += "━━━━━━━━━━━━━━━━━━━━";
  return msg;
}

// ── MODULE 2: NEIGHBOURS & POLE CONTROL MENUS ──
String buildNeighboursMessage(UserSession &sess) {
  String msg = "📡 *REGISTERED POLES*\n━━━━━━━━━━━━━━━━━━━━\n";
  if (nodeCount == 0) {
    msg += "No poles registered yet.\n";
  } else {
    int start = sess.pageIndex * POLES_PER_PAGE;
    int end = min(start + POLES_PER_PAGE, nodeCount);
    for (int i = start; i < end; i++) {
      msg += "📍 *" + nodes[i].name + "* (`" + nodes[i].logicalId + "`)\n";
      msg += "   " + (nodes[i].onlineState == "ONLINE" ? String("🟢") : (nodes[i].onlineState == "DEGRADED" ? String("🟡") : String("🔴"))) + " | 💡 " + 
             nodes[i].lightState + " | RSSI: `" + String(nodes[i].rssi) + " dBm`\n";
      if (nodes[i].voltageUpdatedAt == 0) {
        msg += "   ⚡ `No Data`\n";
      } else if (nodes[i].voltageState == "DISCONNECTED") {
        msg += "   ⚡ `❌ NO SENSOR`\n";
      } else if (nodes[i].voltageState == "NO_SIGNAL") {
        msg += "   ⚡ `⚠️ NO SIGNAL (0.0 V)`\n";
      } else if (nodes[i].voltageState == "OUT_OF_RANGE") {
        msg += "   ⚡ `⚠️ " + String(nodes[i].voltage, 1) + " V (OUT OF RANGE)`\n";
      } else {
        msg += "   ⚡ `⚡ " + String(nodes[i].voltage, 1) + " V`\n";
      }
    }
    msg += "\nPage " + String((start / POLES_PER_PAGE) + 1) + " of " + String(((nodeCount - 1) / POLES_PER_PAGE) + 1);
  }
  return msg;
}

String buildNeighboursKeyboard(UserSession &sess) {
  String kb = "[";
  int start = sess.pageIndex * POLES_PER_PAGE;
  int end = min(start + POLES_PER_PAGE, nodeCount);
  for (int i = start; i < end; i++) {
    kb += "[{\"text\":\"📍 " + nodes[i].name + "\",\"callback_data\":\"P:" + nodes[i].logicalId + "\"}],";
  }
  
  String nav = "[";
  if (sess.pageIndex > 0) nav += "{\"text\":\"◀️ Prev\",\"callback_data\":\"PG:P:" + String(sess.pageIndex) + "\"},";
  if (end < nodeCount) nav += "{\"text\":\"Next ▶️\",\"callback_data\":\"PG:N:" + String(sess.pageIndex) + "\"},";
  if (nav.endsWith(",")) nav = nav.substring(0, nav.length() - 1);
  nav += "]";
  
  if (nav != "[]") kb += nav + ",";
  kb += "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"NEIGH\"}],"
        "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"SETTINGS\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

String buildPoleCtrlMessage(UserSession &sess) {
  int idx = findNodeIndex(sess.selectedPole);
  if (idx < 0) return "⚠️ Pole not found.";
  
  String msg = "💡 *POLE CONTROL*\n━━━━━━━━━━━━━━━━━━━━\n";
  msg += "🆔 *ID:* `" + nodes[idx].logicalId + "`\n";
  msg += "📍 *Name:* *" + nodes[idx].name + "*\n";
  msg += "🏢 *Loc:* `" + nodes[idx].location + "`\n";
  String statusIcon = nodes[idx].onlineState == "ONLINE" ? "🟢" : (nodes[idx].onlineState == "DEGRADED" ? "🟡" : "🔴");
  msg += statusIcon + " *Status:* " + nodes[idx].onlineState + "\n";
  msg += "📶 *RSSI:* `" + String(nodes[idx].rssi) + " dBm`\n";
  msg += "🗺️ *Next Hop:* `" + nodes[idx].primaryNextHop + "`\n";
  msg += "💡 *Relay:* *" + nodes[idx].lightState + "*\n";

  unsigned long age = (millis() - nodes[idx].voltageUpdatedAt);
  if (nodes[idx].voltageUpdatedAt == 0) {
    msg += "⚡ *Voltage:* `⚠️ No Data`\n";
    msg += "🔍 *Sensor:* `⚠️ Awaiting Telemetry`\n";
  } else if (age > 180000UL) {
    msg += "⚡ *Voltage:* `" + String(nodes[idx].voltage, 1) + " V (STALE)`\n";
    msg += "🔍 *Sensor:* `⚠️ STALE (Last Update: " + String(age / 1000) + "s ago)`\n";
  } else if (nodes[idx].voltageState == "VALID") {
    msg += "⚡ *Voltage:* `⚡ " + String(nodes[idx].voltage, 1) + " V`\n";
    msg += "🔍 *Sensor:* `✅ VALID`\n";
  } else if (nodes[idx].voltageState == "NO_SIGNAL") {
    msg += "⚡ *Voltage:* `⚠️ NO SIGNAL (0.0 V)`\n";
    msg += "🔍 *Sensor:* `❌ ZMPT101B SIGNAL NOT DETECTED`\n";
  } else if (nodes[idx].voltageState == "DISCONNECTED") {
    msg += "⚡ *Voltage:* `⚠️ NO SENSOR`\n";
    msg += "🔍 *Sensor:* `❌ DISCONNECTED`\n";
  } else if (nodes[idx].voltageState == "OUT_OF_RANGE") {
    msg += "⚡ *Voltage:* `⚠️ " + String(nodes[idx].voltage, 1) + " V`\n";
    msg += "🔍 *Sensor:* `❌ OUT OF RANGE`\n";
  } else {
    msg += "⚡ *Voltage:* `⚡ " + String(nodes[idx].voltage, 1) + " V`\n";
    msg += "🔍 *Sensor:* `✅ VALID`\n";
  }

  msg += "🔌 *Current:* `" + String(nodes[idx].current > 0 ? nodes[idx].current : 0.00, 2) + " A`\n";
  
  return msg;
}

String buildPoleCtrlKeyboard(UserSession &sess) {
  String pid = sess.selectedPole;
  return "[[{\"text\":\"💡 ON\",\"callback_data\":\"PON:" + pid + "\"},"
          "{\"text\":\"🌙 OFF\",\"callback_data\":\"POFF:" + pid + "\"}],"
         "[{\"text\":\"📊 STATUS\",\"callback_data\":\"PSTAT:" + pid + "\"},"
          "{\"text\":\"📡 NEIGHBOURS\",\"callback_data\":\"PNEIGH:" + pid + "\"}],"
         "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"NEIGH\"},"
          "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
}

String buildRegistrationMessage(UserSession &sess) {
  String msg = "📝 *PENDING REGISTRATIONS*\n━━━━━━━━━━━━━━━━━━━━\n";
  bool found = false;
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    if (pendingRegs[i].valid && pendingRegs[i].state == REG_DETECTED) {
      msg += "⚙️ *HW ID:* `" + pendingRegs[i].hwId + "`\n";
      msg += "📶 *RSSI:* `" + String(pendingRegs[i].rssi) + " dBm`\n\n";
      found = true;
    }
  }
  if (!found) msg += "No pending detections.\n";
  return msg;
}

String buildRegistrationKeyboard(UserSession &sess) {
  String kb = "[";
  for (int i = 0; i < MAX_PENDING_REG; i++) {
    if (pendingRegs[i].valid && pendingRegs[i].state == REG_DETECTED) {
      kb += "[{\"text\":\"✅ Register " + pendingRegs[i].hwId + "\",\"callback_data\":\"REG:" + pendingRegs[i].hwId + "\"}],";
    }
  }
  kb += "[{\"text\":\"🔄 REFRESH\",\"callback_data\":\"REGLIST\"}],"
        "[{\"text\":\"⬅️ BACK\",\"callback_data\":\"SETTINGS\"},"
        "{\"text\":\"🏠 HOME\",\"callback_data\":\"HOME\"}]]";
  return kb;
}

void renderMenu(const String &chatId, UserSession &sess, int messageId) {
  String text = "";
  String kb = "";

  switch (sess.currentMenu) {
    case MENU_SETTINGS:
      text = buildSettingsMessage();
      kb   = buildSettingsKeyboard();
      break;

    case MENU_STATUS:
      text = buildStatusMessage();
      kb   = buildStatusKeyboard();
      break;

    case MENU_NEIGHBOURS:
      text = buildNeighboursMessage(sess);
      kb   = buildNeighboursKeyboard(sess);
      break;

    case MENU_POLE_CTRL:
      text = buildPoleCtrlMessage(sess);
      kb   = buildPoleCtrlKeyboard(sess);
      break;
      
    case MENU_REGISTRATION:
      text = buildRegistrationMessage(sess);
      kb   = buildRegistrationKeyboard(sess);
      break;

    case MENU_HOME:
    default:
      sess.currentMenu = MENU_HOME;
      text = buildHomeMessage();
      kb   = buildHomeKeyboard();
      break;
  }

  bool success = false;
  if (messageId > 0) {
    success = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, messageId);
    if (!success) {
      Serial.printf("[GW][TG] editMessageText failed for msg %d, sending new message...\n", messageId);
      success = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, 0);
      if (success) sess.lastMessageId = bot.last_sent_message_id;
    }
  } else {
    success = bot.sendMessageWithInlineKeyboard(chatId, text, "Markdown", kb, 0);
    if (success) sess.lastMessageId = bot.last_sent_message_id;
  }

  if (success) {
    Serial.printf("[GW][TG] Rendered menu %d for chat %s\n", (int)sess.currentMenu, chatId.c_str());
  } else {
    Serial.printf("[GW][TG] ❌ Failed to render menu %d for chat %s\n", (int)sess.currentMenu, chatId.c_str());
  }
}

void sendStartupMainMenu(const String &chatId) {
  UserSession &sess = getOrCreateSession(chatId);
  sess.currentMenu = MENU_HOME;
  sess.previousMenu = MENU_HOME;
  renderMenu(chatId, sess, 0);
  Serial.printf("[GW][TG] Startup Main Menu sent to chat %s\n", chatId.c_str());
}

// ─────────────────────────────────────────────────────────────
//  CENTRALIZED TELEGRAM CALLBACK QUERY ROUTER (MODULE 1)
// ─────────────────────────────────────────────────────────────
void handleTelegramCallback(const String &chatId, const String &queryId, const String &data, int messageId) {
  String action = data;
  action.trim();
  Serial.printf("[GW] Telegram callback = %s (Chat: %s, MsgID: %d)\n", action.c_str(), chatId.c_str(), messageId);

  UserSession &sess = getOrCreateSession(chatId);
  sess.lastMessageId = messageId;

  // 1. Immediate ACK to stop Telegram button loading spinner
  if (queryId.length() > 0) {
    if (action == "REFRESH") {
      bot.answerCallbackQuery(queryId, "🔄 Refreshed!");
    } else if (action == "ALL_ON") {
      bot.answerCallbackQuery(queryId, "⚡ Sending ALL LIGHTS ON...");
    } else if (action == "ALL_OFF") {
      bot.answerCallbackQuery(queryId, "🌙 Sending ALL LIGHTS OFF...");
    } else {
      bot.answerCallbackQuery(queryId, "");
    }
  }

  // 2. Centralized Callback Router
  if (action == "HOME") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_HOME;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "BACK") {
    if (sess.currentMenu == MENU_STATUS) {
      sess.currentMenu = MENU_SETTINGS;
    } else {
      sess.currentMenu = MENU_HOME;
    }
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "SETTINGS") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_SETTINGS;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "STATUS") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_STATUS;
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "REFRESH") {
    renderMenu(chatId, sess, messageId);
  }
  else if (action == "ALL_ON") {
    bot.sendMessage(chatId, "⚡ *Sending ALL LIGHTS ON command over LoRa...*", "Markdown");
    queueCommand(chatId, "ALL", "LIGHT_ON", PRIO_HIGH);
  }
  else if (action == "ALL_OFF") {
    bot.sendMessage(chatId, "🌙 *Sending ALL LIGHTS OFF command over LoRa...*", "Markdown");
    queueCommand(chatId, "ALL", "LIGHT_OFF", PRIO_HIGH);
  }
  // ── MODULE 2 CALLBACKS ──
  else if (action == "NEIGH") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_NEIGHBOURS;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("PG:N:")) {
    sess.pageIndex = action.substring(5).toInt() + 1;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("PG:P:")) {
    sess.pageIndex = action.substring(5).toInt() - 1;
    if (sess.pageIndex < 0) sess.pageIndex = 0;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("P:")) {
    sess.selectedPole = action.substring(2);
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_POLE_CTRL;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("PON:")) {
    String p = action.substring(4);
    bot.sendMessage(chatId, "⚡ Sending ON to " + p + "...", "Markdown");
    queueCommand(chatId, p, "LIGHT_ON", PRIO_HIGH);
  }
  else if (action.startsWith("POFF:")) {
    String p = action.substring(5);
    bot.sendMessage(chatId, "🌙 Sending OFF to " + p + "...", "Markdown");
    queueCommand(chatId, p, "LIGHT_OFF", PRIO_HIGH);
  }
  else if (action.startsWith("PSTAT:")) {
    String p = action.substring(6);
    bot.sendMessage(chatId, "📊 Requesting status from " + p + "...", "Markdown");
    queueCommand(chatId, p, "GET_STATUS", PRIO_MEDIUM);
  }
  else if (action == "REGLIST") {
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_REGISTRATION;
    renderMenu(chatId, sess, messageId);
  }
  else if (action.startsWith("REG:")) {
    String hwId = action.substring(4);
    for (int i = 0; i < MAX_PENDING_REG; i++) {
      if (pendingRegs[i].valid && pendingRegs[i].hwId == hwId) {
        pendingRegs[i].state = REG_TYPE_SELECTED; // Skip type selection for now, default STREET
        pendingRegs[i].type = "STREET";
        pendingRegs[i].state = REG_AWAITING_NAME;
        sess.pendingRegIdx = i;
        bot.sendMessage(chatId, "Please type a name for the new pole (e.g., 'Main Road 1')\n\nOr type /cancel to abort.", "Markdown");
        break;
      }
    }
  }
  else if (action.startsWith("RREJ:")) {
    String hwId = action.substring(5);
    for (int i = 0; i < MAX_PENDING_REG; i++) {
      if (pendingRegs[i].valid && pendingRegs[i].hwId == hwId) {
        pendingRegs[i].valid = false;
        bot.sendMessage(chatId, "❌ Rejected registration for " + hwId, "Markdown");
        renderMenu(chatId, sess, messageId);
        break;
      }
    }
  }
  else {
    Serial.printf("[GW][TG] Unknown callback: '%s'\n", action.c_str());
    bot.sendMessage(chatId, "⚠️ Unknown action: `" + action + "`", "Markdown");
  }
}

// ─────────────────────────────────────────────────────────────
//  TELEGRAM TEXT MESSAGE HANDLER
// ─────────────────────────────────────────────────────────────
void handleTelegramText(const String &chatId, const String &text) {
  String t = text;
  t.trim();

  UserSession &sess = getOrCreateSession(chatId);

  if (t == "/cancel" || t.equalsIgnoreCase("CANCEL")) {
    if (sess.pendingRegIdx >= 0) {
      pendingRegs[sess.pendingRegIdx].valid = false;
      sess.pendingRegIdx = -1;
      bot.sendMessage(chatId, "❌ Registration cancelled.", "Markdown");
      sess.currentMenu = MENU_HOME;
      renderMenu(chatId, sess, 0);
    }
    return;
  }

  // Handle active registration input
  if (sess.pendingRegIdx >= 0) {
    int pi = sess.pendingRegIdx;
    if (pendingRegs[pi].valid && pendingRegs[pi].state == REG_AWAITING_NAME) {
      pendingRegs[pi].name = t;
      pendingRegs[pi].state = REG_AWAITING_LOCATION;
      bot.sendMessage(chatId, "Name set to: *" + t + "*\n\nNow, type the location (or /cancel).", "Markdown");
      return;
    } 
    else if (pendingRegs[pi].valid && pendingRegs[pi].state == REG_AWAITING_LOCATION) {
      pendingRegs[pi].location = t;
      pendingRegs[pi].state = REG_COMPLETE;
      
      // Generate ID
      String newId = "";
      if (pendingRegs[pi].type == "STREET") {
        newId = "P" + String(nextStreetLightId++);
      } else {
        newId = "FR-" + String(nextFarmerId++);
      }
      
      // Add to DB
      int nodeIdx = findNodeIndex(pendingRegs[pi].hwId);
      if (nodeIdx < 0) {
        nodeIdx = addOrUpdateNode(pendingRegs[pi].hwId, newId, pendingRegs[pi].type, pendingRegs[pi].rssi, 0);
      } else {
        nodes[nodeIdx].logicalId = newId;
        nodes[nodeIdx].type = pendingRegs[pi].type;
      }
      nodes[nodeIdx].name = pendingRegs[pi].name;
      nodes[nodeIdx].location = pendingRegs[pi].location;
      nodes[nodeIdx].registered = true;
      
      pendingRegs[pi].valid = false; // done
      sess.pendingRegIdx = -1;
      
      // Confirm to node
      String regConfirm = nodes[nodeIdx].logicalId + "|" + nodes[nodeIdx].name + "|" + nodes[nodeIdx].type;
      dispatchLoRaPacket("CMD", nodes[nodeIdx].hwId, nodes[nodeIdx].hwId, DEFAULT_TTL, getNextSeq(), "REGISTERED|" + regConfirm);
      
      bot.sendMessage(chatId, "✅ *Registration Complete!*\n\nID: `" + newId + "`\nName: *" + nodes[nodeIdx].name + "*", "Markdown");
      
      sess.currentMenu = MENU_HOME;
      renderMenu(chatId, sess, 0);
      return;
    }
  }

  if (t == "/start" || t == "/menu" || t == "/help") {
    sendStartupMainMenu(chatId);
    return;
  }

  if (t.equalsIgnoreCase("ON") || t.equalsIgnoreCase("ALL ON")) {
    bot.sendMessage(chatId, "⚡ *Sending ALL LIGHTS ON command over LoRa...*", "Markdown");
    queueCommand(chatId, "ALL", "LIGHT_ON", PRIO_HIGH);
    return;
  }
  if (t.equalsIgnoreCase("OFF") || t.equalsIgnoreCase("ALL OFF")) {
    bot.sendMessage(chatId, "🌙 *Sending ALL LIGHTS OFF command over LoRa...*", "Markdown");
    queueCommand(chatId, "ALL", "LIGHT_OFF", PRIO_HIGH);
    return;
  }
  if (t.equalsIgnoreCase("STATUS")) {
    UserSession &sess = getOrCreateSession(chatId);
    sess.previousMenu = sess.currentMenu;
    sess.currentMenu = MENU_STATUS;
    renderMenu(chatId, sess, 0);
    return;
  }

  bot.sendMessage(chatId, "💡 Type /start to open the Street Light Control Dashboard.", "Markdown");
}

// ─────────────────────────────────────────────────────────────
//  TELEGRAM UPDATE POLLER
// ─────────────────────────────────────────────────────────────
void handleTelegramUpdates() {
  int numUpdates = bot.getUpdates(bot.last_message_received + 1);
  if (numUpdates <= 0) return;

  Serial.printf("[GW][TG] >>> Received %d update(s)\n", numUpdates);

  for (int i = 0; i < numUpdates; i++) {
    String chatId   = bot.messages[i].chat_id;
    String fromId   = bot.messages[i].from_id;
    String type     = bot.messages[i].type;
    String text     = bot.messages[i].text;
    String queryId  = bot.messages[i].query_id;
    int messageId   = bot.messages[i].message_id;

    if (chatId.length() == 0) chatId = fromId;
    if (chatId.length() == 0) continue;

    if (!isAuthorized(chatId, fromId)) {
      Serial.printf("[GW][TG] ⛔ Unauthorized access from Chat: %s, User: %s (Configured: %s)\n",
                    chatId.c_str(), fromId.c_str(), String(OWNER_CHAT).c_str());
      if (type == "callback_query" && queryId.length() > 0) {
        bot.answerCallbackQuery(queryId, "⛔ Unauthorized Access Denied", true);
      }
      bot.sendMessage(chatId, "🔒 *ACCESS DENIED*\n\nYour Chat ID: `" + chatId + "`\nAuthorized ID: `" + String(OWNER_CHAT) + "`", "Markdown");
      continue;
    }

    if (type == "callback_query") {
      handleTelegramCallback(chatId, queryId, text, messageId);
    } else {
      handleTelegramText(chatId, text);
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  WIFI & TELEGRAM BOT API VERIFICATION
// ─────────────────────────────────────────────────────────────
bool verifyTelegramApi() {
  if (WiFi.status() != WL_CONNECTED) {
    telegramOnline = false;
    return false;
  }

  Serial.println(F("[GW][TG] Verifying Telegram Bot API connectivity (getMe)..."));
  client.setInsecure();
  if (bot.getMe()) {
    telegramOnline = true;
    Serial.printf("[GW][TG] ✅ Telegram Bot ONLINE! Bot Name: %s (@%s)\n",
                  bot.name.c_str(), bot.userName.c_str());
    return true;
  } else {
    telegramOnline = false;
    Serial.println(F("[GW][TG] ⚠️ Telegram API verification failed. Check BOT_TOKEN or Internet."));
    return false;
  }
}

void checkWiFiAndTelegram() {
  unsigned long now = millis();
  if (now - tWifiWatchdog < WIFI_WATCHDOG_INTERVAL_MS) return;
  tWifiWatchdog = now;

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.printf("[GW][WIFI] Connected! IP: %s (RSSI: %d dBm)\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      client.setInsecure();
      checkNtpSync();
      
      if (verifyTelegramApi()) {
        if (!bootMenuSent) {
          sendStartupMainMenu(OWNER_CHAT);
          bootMenuSent = true;
        }
      }
    } else if (!telegramOnline) {
      verifyTelegramApi();
    }
  } else {
    if (wifiConnected) {
      wifiConnected = false;
      telegramOnline = false;
      Serial.println(F("[GW][WIFI] Connection lost. LoRa mesh continues running independently."));
    }
    Serial.println(F("[GW][WIFI] Reconnecting to mobile hotspot..."));
    WiFi.disconnect();
    WiFi.reconnect();
  }
}

// ─────────────────────────────────────────────────────────────
//  NODE TIMEOUT & HEARTBEAT CYCLES
// ─────────────────────────────────────────────────────────────
void checkNodeTimeouts() {
  unsigned long now = millis();
  for (int i = 0; i < nodeCount; i++) {
    if (nodes[i].onlineState != "OFFLINE" && (now - nodes[i].lastSeen >= NODE_OFFLINE_TIMEOUT_MS)) {
      nodes[i].onlineState = "OFFLINE";
      Serial.printf("[GW][DB] ⚠️ Node %s went OFFLINE (last seen %lu ms ago)\n",
                    nodes[i].logicalId.c_str(), now - nodes[i].lastSeen);
      
      if (telegramOnline && nodes[i].registered) {
        String alert = "🔴 *NODE OFFLINE ALERT*\n\n"
                       "📍 *Pole:* *" + nodes[i].name + "* (`" + nodes[i].logicalId + "`)\n"
                       "⚠️ *Status:* No response for 3 minutes.\n"
                       "🕒 *Time:* `" + getFormattedTime() + "`";
        bot.sendMessage(OWNER_CHAT, alert, "Markdown");
      }
    } else if (nodes[i].onlineState == "ONLINE" && (now - nodes[i].lastSeen >= HEARTBEAT_INTERVAL_MS * 2)) {
      nodes[i].onlineState = "DEGRADED";
      Serial.printf("[GW][DB] 🟡 Node %s DEGRADED (missed heartbeats, last seen %lu ms ago)\n",
                    nodes[i].logicalId.c_str(), now - nodes[i].lastSeen);
    }
  }
}

void processHeartbeatCycle() {
  // Deliberately left empty. 
  // Gateway no longer polls using GET_STATUS. 
  // Poles now autonomously push HEARTBEAT packets.
}

void processTimeSyncPush() {
  if (!hasNtpTime) return;
  time_t raw = time(nullptr);
  if (raw > 1700000000UL) {
    dispatchLoRaPacket("CMD", BROADCAST_ID, BROADCAST_ID, 1, getNextSeq(), "SET_TIME|" + String((unsigned long)raw));
  }
}

// ─────────────────────────────────────────────────────────────
//  NVS PERSISTENCE
// ─────────────────────────────────────────────────────────────
void saveDatabaseToNVS() {
  prefs.begin(NVS_NS_MESH, false);
  prefs.putInt("count", nodeCount);
  prefs.putInt("nextSLId", nextStreetLightId);
  prefs.putInt("nextFId", nextFarmerId);
  for (int i = 0; i < nodeCount; i++) {
    char pfx[8];
    snprintf(pfx, sizeof(pfx), "n%d_", i);
    prefs.putString((String(pfx) + "hw").c_str(), nodes[i].hwId);
    prefs.putString((String(pfx) + "id").c_str(), nodes[i].logicalId);
    prefs.putString((String(pfx) + "nm").c_str(), nodes[i].name);
    prefs.putString((String(pfx) + "lc").c_str(), nodes[i].location);
    prefs.putString((String(pfx) + "tp").c_str(), nodes[i].type);
    prefs.putBool((String(pfx) + "rg").c_str(), nodes[i].registered);
    prefs.putBool((String(pfx) + "se").c_str(), nodes[i].schedEnabled);
    prefs.putInt((String(pfx) + "so").c_str(), nodes[i].schedOnMin);
    prefs.putInt((String(pfx) + "sf").c_str(), nodes[i].schedOffMin);
  }
  prefs.end();
  Serial.printf("[GW][NVS] Database saved (%d nodes).\n", nodeCount);
}

void loadDatabaseFromNVS() {
  prefs.begin(NVS_NS_MESH, true);
  int count = prefs.getInt("count", 0);
  nextStreetLightId = prefs.getInt("nextSLId", 1);
  nextFarmerId = prefs.getInt("nextFId", 1);
  if (count > MAX_NODES) count = MAX_NODES;
  nodeCount = count;

  for (int i = 0; i < nodeCount; i++) {
    char pfx[8];
    snprintf(pfx, sizeof(pfx), "n%d_", i);
    nodes[i].hwId       = prefs.getString((String(pfx) + "hw").c_str(), "");
    nodes[i].logicalId  = prefs.getString((String(pfx) + "id").c_str(), "");
    nodes[i].name       = prefs.getString((String(pfx) + "nm").c_str(), "");
    nodes[i].location   = prefs.getString((String(pfx) + "lc").c_str(), "");
    nodes[i].type       = prefs.getString((String(pfx) + "tp").c_str(), "STREET");
    nodes[i].registered = prefs.getBool((String(pfx) + "rg").c_str(), false);
    nodes[i].schedEnabled = prefs.getBool((String(pfx) + "se").c_str(), false);
    nodes[i].schedOnMin = prefs.getInt((String(pfx) + "so").c_str(), -1);
    nodes[i].schedOffMin = prefs.getInt((String(pfx) + "sf").c_str(), -1);
    nodes[i].onlineState = "OFFLINE";
    nodes[i].lightState = "OFF";
    nodes[i].voltage    = 0.0f;
    nodes[i].voltageState = "";
    nodes[i].voltageUpdatedAt = 0;
    nodes[i].current    = 0.0f;
    nodes[i].power      = 0.0f;
    nodes[i].totalKwh   = 0.0f;
    nodes[i].fault      = "OK";
    nodes[i].prevFault  = "OK";
    nodes[i].primaryNextHop = BROADCAST_ID;
    nodes[i].connectionMode = "DIRECT";
    nodes[i].neighborData = "";
  }
  prefs.end();
  Serial.printf("[GW][NVS] Loaded %d nodes from database.\n", nodeCount);
}

// ─────────────────────────────────────────────────────────────
//  HARDWARE & RADIO INITIALIZATION
// ─────────────────────────────────────────────────────────────
void initHardware() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=================================================="));
  Serial.printf("  SMART STREET LIGHT MASTER GATEWAY %s\n", GW_VERSION);
  Serial.println(F("=================================================="));

  loadDatabaseFromNVS();

  prefs.begin(NVS_NS_WIFI, true);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();
  if (savedSsid.length() > 0) {
    wifiSsid = savedSsid;
    wifiPass = savedPass;
  }
}

void initLoRa() {
  Serial.println(F("[LoRa] Initializing..."));
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println(F("[LoRa] FAILED! Check SPI wiring and SX1278 power."));
    while (1) { delay(1000); }
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TXPOW, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  if (LORA_CRC_ENABLED) LoRa.enableCrc();
  LoRa.receive();

  Serial.printf("[LoRa] Frequency = %.1f MHz\n", LORA_FREQ / 1E6);
  Serial.printf("[LoRa] SF = %d\n", LORA_SF);
  Serial.printf("[LoRa] BW = %.1f kHz\n", LORA_BW / 1E3);
  Serial.println(F("[LoRa] CR = 4/5"));
  Serial.printf("[LoRa] TX Power = %d dBm\n", LORA_TXPOW);
  Serial.printf("[LoRa] Sync Word = 0x%02X\n", LORA_SYNC_WORD);
  Serial.println(F("[LoRa] Ready"));
}

// ─────────────────────────────────────────────────────────────
//  SETUP & MAIN LOOP
// ─────────────────────────────────────────────────────────────
void setup() {
  initHardware();
  initLoRa();

  bot.maxMessageLength = 4096;
  bot.waitForResponse  = 2500;

  Serial.printf("[GW][WIFI] Connecting to SSID: '%s'...\n", wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 8000) {
    delay(250);
    Serial.print(F("."));
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("[GW][WIFI] Connected! IP: %s (RSSI: %d dBm)\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
    client.setInsecure();
    checkNtpSync();

    if (verifyTelegramApi()) {
      sendStartupMainMenu(OWNER_CHAT);
      bootMenuSent = true;
    }
  } else {
    Serial.println(F("[GW][WIFI] ⚠️ Hotspot not connected at boot. LoRa active. Background reconnect scheduled."));
  }

  unsigned long now = millis();
  tTelegramPoll  = now;
  tQueueProcess  = now;
  tHeartbeat     = now;
  tNodeTimeout   = now;
  tNvsFlush      = now;
  tNtpResync     = now;
  tTimeSync      = now;
  tWifiWatchdog  = now;
}

void loop() {
  unsigned long now = millis();

  processIncomingLoRa();

  if (now - tQueueProcess >= 50) {
    tQueueProcess = now;
    processCommandQueue();
  }

  if (wifiConnected && telegramOnline) {
    if (now - tTelegramPoll >= TG_POLL_INTERVAL) {
      tTelegramPoll = now;
      handleTelegramUpdates();
    }
  }

  checkWiFiAndTelegram();

  if (now - tNodeTimeout >= 10000) {
    tNodeTimeout = now;
    checkNodeTimeouts();
  }

  if (now - tHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    tHeartbeat = now;
    processHeartbeatCycle();
  }

  if (hasNtpTime && (now - tTimeSync >= TIME_PUSH_INTERVAL_MS)) {
    tTimeSync = now;
    processTimeSyncPush();
  }

  if (now - tNvsFlush >= NVS_SAVE_INTERVAL_MS) {
    tNvsFlush = now;
    saveDatabaseToNVS();
  }

  if (now - tNtpResync >= NTP_SYNC_INTERVAL_MS) {
    tNtpResync = now;
    checkNtpSync();
  }
}
