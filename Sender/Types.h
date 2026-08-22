/*
 * =====================================================================
 *  TYPES.H — MASTER GATEWAY DATA TYPES & ENUMERATIONS
 *  Smart Street Light / Farmer Power Distribution Mesh System
 * =====================================================================
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"

// ─────────────────────────────────────────────────────────────
//  FEEDER DATA MODEL (GATEWAY CACHE)
// ─────────────────────────────────────────────────────────────
struct FeederState {
  uint8_t  feederId;              // 1 to 6
  String   name;                  // "Feeder 1" .. "Feeder 6"
  String   onlineState;           // "ONLINE", "DEGRADED", "OFFLINE"
  String   relayState;            // "ON", "OFF"
  float    voltage1;              // VAC (Phase R)
  float    voltage2;              // VAC (Phase Y)
  bool     voltage1Valid;
  bool     voltage2Valid;
  String   voltage1State;         // "VALID", "NO_SIGNAL", "DISCONNECTED", "OUT_OF_RANGE"
  String   voltage2State;         // "VALID", "NO_SIGNAL", "DISCONNECTED", "OUT_OF_RANGE"
  unsigned long lastVoltageUpdate;// Timestamp
  unsigned long lastHeartbeat;    // Timestamp
  String   loraState;             // "ONLINE" (🟢), "DEGRADED" (🟡), "OFFLINE" (🔴)
  String   faultState;            // "OK", "FAULT", "MCB_TRIP", "SENSOR_ERROR", etc.
  bool     mcbR;                  // true = OK, false = Tripped
  bool     mcbY;                  // true = OK, false = Tripped
  bool     mcbB;                  // true = OK, false = Tripped
  bool     contactor;
  bool     protectionEnabled;
  String   lastCommand;
  String   lastCommandResult;
};

// ─────────────────────────────────────────────────────────────
//  NODE DATABASE RECORD (COMMON TO STREET LIGHT & FARMER)
// ─────────────────────────────────────────────────────────────
struct NodeRecord {
  String   hwId;                  // Unique Chip HW ID (e.g. "A4CF12E8")
  String   logicalId;             // Assigned Logical ID (e.g. "SL-0001", "FR-0001")
  String   name;                  // Friendly Name (e.g. "Main Road Pole 1", "Main Farm Pump")
  String   location;              // Installation Location
  String   type;                  // "STREET", "FARMER", "CUSTOM"
  bool     registered;            // Is node registered in DB?
  String   onlineState;           // "ONLINE", "DEGRADED", "OFFLINE"
  int      rssi;                  // Last received RSSI (dBm)
  int      snr;                   // Last received SNR (dB)
  String   lightState;            // "ON", "OFF" (maps to pumpState for FARMER)
  float    voltage;               // AC Voltage (VAC)
  String   voltageState;          // "VALID", "NO_SIGNAL", "DISCONNECTED", "OUT_OF_RANGE"
  unsigned long voltageUpdatedAt; // Timestamp of last valid reading
  float    current;               // AC Current (A)
  float    power;                 // Active Power (W)
  float    totalKwh;              // Accumulated Energy (kWh)
  String   fault;                 // "OK", "OVER_VOLT", "UNDER_VOLT", "OVER_CURR", "LAMP_FAIL", "PUMP_FAIL"
  String   prevFault;             // Fault deduplication
  uint32_t uptimeSec;             // Node reported uptime
  unsigned long firstSeen;        // Boot timestamp
  unsigned long lastSeen;         // Last packet timestamp
  unsigned long lastHeartbeat;    // Last heartbeat poll timestamp
  String   primaryNextHop;        // Next hop node ID
  String   backupNextHop;         // Backup next hop
  int      hopCount;              // Hops to reach node
  String   routePath;             // Human readable path
  String   connectionMode;        // "DIRECT", "MESH", "ROUTE_RECOVERY"
  bool     schedEnabled;          // Autonomous schedule enabled
  int      schedOnMin;            // Schedule ON time (minutes from midnight)
  int      schedOffMin;           // Schedule OFF time (minutes from midnight)
  uint16_t cmdGeneration;         // Supersession protection counter
  int      neighborCount;         // Neighbor count reported by node
  String   neighborData;          // Cached neighbor telemetry
  FeederState feeders[MAX_FEEDERS]; // Multi-feeder cache per node
};

// ─────────────────────────────────────────────────────────────
//  MESH ROUTING & TOPOLOGY
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

struct NeighborSummary {
  String   reportingNode;
  String   neighborId;
  int16_t  rssi;
  int8_t   snr;
  bool     responded;
  uint32_t lastSeen;
  bool     valid;
};

// ─────────────────────────────────────────────────────────────
//  REGISTRATION STATE MACHINE
// ─────────────────────────────────────────────────────────────
enum RegState {
  REG_NONE = 0,
  REG_DETECTED,
  REG_TYPE_SELECT,
  REG_AWAITING_NAME,
  REG_AWAITING_LOCATION,
  REG_CONFIRMATION,
  REG_COMPLETE,
  REG_CANCELLED
};

struct PendingRegistration {
  bool          valid;
  String        hwId;
  String        type;             // "STREET" or "FARMER"
  String        name;
  String        location;
  int           rssi;
  unsigned long detectedAt;
  RegState      state;
};

// ─────────────────────────────────────────────────────────────
//  PACKET DEDUPLICATION CACHE ENTRY
// ─────────────────────────────────────────────────────────────
struct GwSeqCacheEntry {
  String   src;
  uint16_t seq;
};

// ─────────────────────────────────────────────────────────────
//  COMMAND QUEUE & PRIORITY SCHEDULER
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
  unsigned long pingSentMs;       // For RTT calculation on PING
};

#define MAX_CMD_QUEUE 16

// ─────────────────────────────────────────────────────────────
//  TELEGRAM NAVIGATION & SESSIONS
// ─────────────────────────────────────────────────────────────
enum MenuScreen {
  MENU_HOME = 0,
  MENU_SETTINGS,
  MENU_STATUS,
  MENU_FEEDER_STATUS,
  MENU_NEIGHBOURS,
  MENU_POLE_CTRL,
  MENU_POLE_NEIGHBOURS,
  MENU_FARMER_LIST,
  MENU_FARMER_CTRL,
  MENU_TIMER_SELECT,
  MENU_RESET_SELECT,
  MENU_RESET_CONFIRM_POLE,
  MENU_RESET_CONFIRM_NEIGH,
  MENU_RESET_CONFIRM_ALL,
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
  String        selectedNeighbour;
  int           pageIndex;
  int           pendingRegIdx;
};

// ─────────────────────────────────────────────────────────────
//  GATEWAY AUDIT LOG
// ─────────────────────────────────────────────────────────────
struct GwLogEntry {
  char     action[32];
  char     target[16];
  uint32_t timestamp;
  uint8_t  result; // 0=OK, 1=FAIL
};

// ─────────────────────────────────────────────────────────────
//  BATCH COMMAND TRACKER (ALL ON / ALL OFF / RESET ALL)
// ─────────────────────────────────────────────────────────────
struct BatchCmdTracker {
  bool          active;
  String        cmdType;          // "ALL_ON", "ALL_OFF", "RESET_ALL"
  String        chatId;
  int           statusMsgId;
  unsigned long startTime;
  unsigned long timeoutMs;
  int           totalExpected;
  int           confirmedCount;
  int           failedCount;
  int           unreachableCount;
  bool          nodeAcked[MAX_NODES];
  bool          finalReportSent;
};

#endif // TYPES_H
