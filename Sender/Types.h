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
//  LIFECYCLE & STATE MACHINE ENUMS (PROMPT 12)
// ─────────────────────────────────────────────────────────────
enum PoleLifecycleState {
  POLE_STATE_ACTIVE = 0,
  POLE_STATE_DELETE_REQUESTED,
  POLE_STATE_DELETE_CONFIRMATION_REQUIRED,
  POLE_STATE_DELETION_APPROVED,
  POLE_STATE_REVOCATION_PENDING,
  POLE_STATE_CHILD_ROUTE_MIGRATION,
  POLE_STATE_DE_REGISTERED,
  POLE_STATE_ARCHIVED,
  POLE_STATE_DELETE_FAILED
};

inline const char* poleLifecycleStateToString(PoleLifecycleState st) {
  switch (st) {
    case POLE_STATE_ACTIVE:                        return "ACTIVE";
    case POLE_STATE_DELETE_REQUESTED:             return "DELETE_REQUESTED";
    case POLE_STATE_DELETE_CONFIRMATION_REQUIRED: return "DELETE_CONFIRMATION_REQUIRED";
    case POLE_STATE_DELETION_APPROVED:            return "DELETION_APPROVED";
    case POLE_STATE_REVOCATION_PENDING:           return "REVOCATION_PENDING";
    case POLE_STATE_CHILD_ROUTE_MIGRATION:        return "CHILD_ROUTE_MIGRATION";
    case POLE_STATE_DE_REGISTERED:                return "DE-REGISTERED";
    case POLE_STATE_ARCHIVED:                     return "ARCHIVED";
    case POLE_STATE_DELETE_FAILED:                return "DELETE_FAILED";
    default:                                      return "UNKNOWN";
  }
}

enum FeederStatus {
  FEEDER_STATUS_ACTIVE = 0,
  FEEDER_STATUS_DEACTIVATED,
  FEEDER_STATUS_REMOVED
};

inline const char* feederStatusToString(FeederStatus st) {
  switch (st) {
    case FEEDER_STATUS_ACTIVE:      return "ACTIVE";
    case FEEDER_STATUS_DEACTIVATED: return "DEACTIVATED";
    case FEEDER_STATUS_REMOVED:     return "REMOVED";
    default:                        return "UNKNOWN";
  }
}

// ─────────────────────────────────────────────────────────────
//  MEASUREMENT VALIDITY & INTELLIGENT ALERTS (PROMPT 18)
// ─────────────────────────────────────────────────────────────
enum MeasurementValidity : uint8_t {
  MEAS_VALID               = 0, // Normal verified reading
  MEAS_ZERO_CURRENT        = 1, // Below noise floor / load off
  MEAS_NO_SIGNAL           = 2, // Mains dead / no AC waveform
  MEAS_UNDER_VOLTAGE       = 3, // Voltage below allowable boundary
  MEAS_OVER_VOLTAGE        = 4, // Voltage above allowable boundary
  MEAS_OVER_CURRENT        = 5, // Exceeds safe feeder current threshold
  MEAS_CURRENT_IMBALANCE   = 6, // Unbalanced 3-phase load
  MEAS_PHASE_LOSS          = 7, // Missing phase on 3-phase mains
  MEAS_SENSOR_DISCONNECTED = 8, // Open CT / open input / zero variance / sensor fault
  MEAS_SATURATED           = 9, // ADC hitting +/- FS digital rail
  MEAS_ADC_OFFLINE         = 10,// SPI comm failure / missing DRDY / ADC failure
  MEAS_CAL_INVALID         = 11,// Corrupted or uninitialized calibration
  MEAS_UNAVAILABLE         = 12,// Sync or required waveform missing / invalid
  MEAS_STALE               = 13 // Reading expired / stale measurement
};

inline const char* measurementValidityToString(MeasurementValidity v) {
  switch (v) {
    case MEAS_VALID:               return "VALID";
    case MEAS_ZERO_CURRENT:        return "ZERO_CURRENT";
    case MEAS_NO_SIGNAL:           return "NO_SIGNAL";
    case MEAS_UNDER_VOLTAGE:       return "UNDER_VOLT";
    case MEAS_OVER_VOLTAGE:        return "OVER_VOLT";
    case MEAS_OVER_CURRENT:        return "OVER_CURR";
    case MEAS_CURRENT_IMBALANCE:   return "IMBALANCE";
    case MEAS_PHASE_LOSS:          return "PHASE_LOSS";
    case MEAS_SENSOR_DISCONNECTED: return "SENSOR_FAULT";
    case MEAS_SATURATED:           return "SATURATED";
    case MEAS_ADC_OFFLINE:         return "ADC_OFFLINE";
    case MEAS_CAL_INVALID:         return "CAL_INVALID";
    case MEAS_UNAVAILABLE:         return "INVALID";
    case MEAS_STALE:               return "STALE";
    default:                       return "UNKNOWN";
  }
}

enum AlertSeverity : uint8_t {
  ALERT_SEV_P0_CRITICAL = 0,
  ALERT_SEV_P1_HIGH     = 1,
  ALERT_SEV_P2_MEDIUM   = 2,
  ALERT_SEV_P3_INFO     = 3
};

inline const char* alertSeverityToString(AlertSeverity s) {
  switch (s) {
    case ALERT_SEV_P0_CRITICAL: return "CRITICAL (P0)";
    case ALERT_SEV_P1_HIGH:     return "HIGH (P1)";
    case ALERT_SEV_P2_MEDIUM:   return "MEDIUM (P2)";
    case ALERT_SEV_P3_INFO:     return "INFO (P3)";
    default:                    return "UNKNOWN";
  }
}

enum EventType : uint8_t {
  EVT_NONE = 0,
  EVT_POLE_ONLINE,
  EVT_POLE_OFFLINE,
  EVT_POLE_RECOVERED,
  EVT_PARENT_CHANGED,
  EVT_ROUTE_RECOVERED,
  EVT_REG_APPROVED,
  EVT_REG_REJECTED,
  EVT_POLE_DELETED,
  EVT_FEEDER_DEACTIVATED,
  EVT_MCB_TRIP,
  EVT_OVERCURRENT,
  EVT_VOLTAGE_FAULT,
  EVT_SENSOR_FAILURE,
  EVT_COMMAND_FAILURE,
  EVT_WIFI_CHANGE,
  EVT_FIRMWARE_UPDATE,
  EVT_BACKUP_PARENT_ACTIVE,
  EVT_RADIO_FAILURE
};

inline const char* eventTypeToString(EventType t) {
  switch (t) {
    case EVT_POLE_ONLINE:          return "Pole Online";
    case EVT_POLE_OFFLINE:         return "Pole Offline";
    case EVT_POLE_RECOVERED:       return "Pole Recovered";
    case EVT_PARENT_CHANGED:       return "Parent Changed";
    case EVT_ROUTE_RECOVERED:      return "Route Recovered";
    case EVT_REG_APPROVED:         return "Registration Approved";
    case EVT_REG_REJECTED:         return "Registration Rejected";
    case EVT_POLE_DELETED:         return "Pole Deleted";
    case EVT_FEEDER_DEACTIVATED:   return "Feeder Deactivated";
    case EVT_MCB_TRIP:             return "MCB Trip";
    case EVT_OVERCURRENT:          return "Overcurrent";
    case EVT_VOLTAGE_FAULT:        return "Voltage Fault";
    case EVT_SENSOR_FAILURE:       return "Sensor Failure";
    case EVT_COMMAND_FAILURE:      return "Command Failure";
    case EVT_WIFI_CHANGE:          return "Wi-Fi Change";
    case EVT_FIRMWARE_UPDATE:      return "Firmware Update";
    case EVT_BACKUP_PARENT_ACTIVE: return "Backup Parent Active";
    case EVT_RADIO_FAILURE:        return "Radio Failure";
    default:                       return "System Event";
  }
}

enum AckStatus : uint8_t {
  ACK_UNACKNOWLEDGED = 0,
  ACK_ACKNOWLEDGED   = 1
};

enum ResolutionStatus : uint8_t {
  RES_ACTIVE   = 0,
  RES_RESOLVED = 1
};

enum NodeConnectivityState : uint8_t {
  CONN_ONLINE = 0,
  CONN_UNREACHABLE,
  CONN_PRIMARY_PARENT_FAILED,
  CONN_BACKUP_PARENT_ACTIVE,
  CONN_ROUTE_LOST,
  CONN_RADIO_FAILURE,
  CONN_CONFIRMED_OFFLINE
};

inline const char* nodeConnectivityStateToString(NodeConnectivityState st) {
  switch (st) {
    case CONN_ONLINE:                 return "Online";
    case CONN_UNREACHABLE:            return "Unreachable";
    case CONN_PRIMARY_PARENT_FAILED:  return "Primary Parent Failed";
    case CONN_BACKUP_PARENT_ACTIVE:   return "Backup Route Active";
    case CONN_ROUTE_LOST:             return "Route Lost";
    case CONN_RADIO_FAILURE:          return "Radio Failure";
    case CONN_CONFIRMED_OFFLINE:      return "Confirmed Offline";
    default:                          return "Unknown";
  }
}

struct ChannelFaultTracker {
  bool     inFault;
  uint32_t firstTriggerTime;
  bool     incidentGenerated;
  uint32_t lastAlertTime;
  float    peakObserved;
};

struct EventRecord {
  uint32_t         eventId;
  EventType        type;
  AlertSeverity    severity;
  char             nodeLogicalId[16];
  uint8_t          feederId;      // 0 = N/A, 1..6
  char             phase;         // '-', 'R', 'Y', 'B'
  uint32_t         timestamp;     // Unix timestamp or millis/1000
  bool             timestampCertain;
  char             description[64];
  AckStatus        ackStatus;
  char             ackBy[24];
  uint32_t         ackTime;
  ResolutionStatus resolutionStatus;
  uint32_t         resolvedTime;
  bool             valid;
};

struct QueuedAlert {
  uint32_t      alertId;
  uint32_t      eventId;
  AlertSeverity severity;
  char          nodeLogicalId[16];
  char          area[32];
  char          eventTitle[32];
  char          primaryParent[16];
  char          backupParent[16];
  uint32_t      lastSeen;
  bool          timestampCertain;
  char          networkState[32];
  char          details[96];
  uint8_t       retries;
  uint32_t      queuedAt;
  uint32_t      nextRetryAt;
  bool          active;
  bool          isAggregated;
  uint8_t       aggregatedCount;
};

// ─────────────────────────────────────────────────────────────
//  FEEDER DATA MODEL (GATEWAY CACHE)
// ─────────────────────────────────────────────────────────────
struct FeederState {
  uint8_t      feederId;              // 1 to 6
  String       poleId;                // Parent Pole Logical ID (e.g. "SL-0001")
  String       name;                  // "Feeder 1" .. "Feeder 6"
  uint8_t      relayChannel;          // Relay index (0 to 5)
  uint8_t      currentChannelR;       // R Phase Current measurement index (0-17)
  uint8_t      currentChannelY;       // Y Phase Current measurement index (0-17)
  uint8_t      currentChannelB;       // B Phase Current measurement index (0-17)
  FeederStatus status;                // ACTIVE, DEACTIVATED, REMOVED
  String       onlineState;           // "ONLINE", "DEGRADED", "OFFLINE"
  String       relayState;            // "ON", "OFF"
  float        voltage1;              // VAC (Phase R)
  float        voltage2;              // VAC (Phase Y)
  float        voltage3;              // VAC (Phase B)
  float        currentR;              // AC Current Phase R (A)
  float        currentY;              // AC Current Phase Y (A)
  float        currentB;              // AC Current Phase B (A)
  float        totalPowerW;           // Total 3-Phase Real Power (W)
  float        powerFactor;           // Power Factor (-1.0 to 1.0)
  float        energyKwh;             // Accumulated Energy (kWh)
  bool         voltage1Valid;
  bool         voltage2Valid;
  bool         voltage3Valid;
  String       voltage1State;         // "VALID", "NO_SIGNAL", "DISCONNECTED", "OUT_OF_RANGE"
  String       voltage2State;         // "VALID", "NO_SIGNAL", "DISCONNECTED", "OUT_OF_RANGE"
  String       voltage3State;
  unsigned long lastVoltageUpdate;    // Timestamp
  unsigned long lastHeartbeat;        // Timestamp
  String       loraState;             // "ONLINE" (🟢), "DEGRADED" (🟡), "OFFLINE" (🔴)
  String       faultState;            // "OK", "FAULT", "MCB_TRIP", "SENSOR_ERROR", etc.
  bool         mcbR;                  // true = OK, false = Tripped
  bool         mcbY;                  // true = OK, false = Tripped
  bool         mcbB;                  // true = OK, false = Tripped
  bool         contactor;
  bool         protectionEnabled;
  String       lastCommand;
  String       lastCommandResult;
  // Historical retention
  float        historicalKwh;
  uint32_t     tripCount;
  unsigned long deactivatedAt;
  // Prompt 18 Metrology & Fault Tracking
  MeasurementValidity currentValidityR;
  MeasurementValidity currentValidityY;
  MeasurementValidity currentValidityB;
  unsigned long lastMeasurementTime;
  bool         contactorFeedback;     // true = Closed, false = Open
  bool         mcbStatus;             // true = Normal, false = Tripped
  ChannelFaultTracker faultTrackerR;
  ChannelFaultTracker faultTrackerY;
  ChannelFaultTracker faultTrackerB;
};

// ─────────────────────────────────────────────────────────────
//  NODE DATABASE RECORD (COMMON TO STREET LIGHT & FARMER)
// ─────────────────────────────────────────────────────────────
struct NodeRecord {
  String   hwId;                  // Unique Chip HW ID (e.g. "A4CF12E8")
  String   logicalId;             // Assigned Logical ID (e.g. "SL-0001", "FR-0001")
  String   name;                  // Friendly Name (e.g. "Main Road Pole 1", "Main Farm Pump")
  String   location;              // Installation Location
  char     area[32];              // Configured Area (e.g. "Dattawad Sector 1")
  String   type;                  // "STREET", "FARMER", "CUSTOM"
  bool     registered;            // Is node registered in DB?
  PoleLifecycleState lifecycleState; // Pole Delete State Machine State
  String   revocationTxId;        // Revocation transaction ID if revoked
  String   onlineState;           // "ONLINE", "DEGRADED", "OFFLINE"
  NodeConnectivityState connectivityDetail; // Detailed offline classification
  String   primaryParent;         // Primary mesh parent ID (e.g. "GATEWAY", "MSCB-P002")
  String   backupParent;          // Backup mesh parent ID (e.g. "MSCB-P005")
  String   routeHealth;           // "EXCELLENT", "GOOD", "DEGRADED", "WEAK", "BROKEN"
  uint16_t firmwareVersion;       // Integer firmware version
  int      rssi;                  // Last received RSSI (dBm)
  int      snr;                   // Last received SNR (dB)
  String   lightState;            // "ON", "OFF" (maps to pumpState for FARMER)
  float    voltage;               // Legacy alias (Phase R)
  String   voltageState;          // Legacy alias
  unsigned long voltageUpdatedAt; // Timestamp of last valid reading
  // 3 Common R/Y/B Voltage Sensors per Pole
  float    commonVoltageR;
  float    commonVoltageY;
  float    commonVoltageB;
  String   commonVoltageStateR;
  String   commonVoltageStateY;
  String   commonVoltageStateB;
  MeasurementValidity commonVoltageValidityR;
  MeasurementValidity commonVoltageValidityY;
  MeasurementValidity commonVoltageValidityB;
  ChannelFaultTracker voltFaultTrackerR;
  ChannelFaultTracker voltFaultTrackerY;
  ChannelFaultTracker voltFaultTrackerB;
  unsigned long offlineCandidateTime;   // Timestamp when missed heartbeat began
  uint8_t  offlineConfirmationCount;   // Confirmation cycles before permanent offline incident
  float    gridFrequency;         // 3-Phase Bus Frequency (Hz)
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
  int      routeCost;             // Cumulative DAG path cost
  uint8_t  rank;                  // DAG Rank (Gateway = 0, Pole 1 = 1, etc.)
  String   routePath;             // Human readable path
  String   connectionMode;        // "DIRECT", "MESH", "ROUTE_RECOVERY", "ORPHANED"
  bool     orphan;                // Is node currently orphaned?
  uint32_t recoveryEvents;        // Number of route recovery events
  unsigned long lastRouteUpdate;  // Timestamp of last route update
  bool     schedEnabled;          // Autonomous schedule enabled
  int      schedOnMin;            // Schedule ON time (minutes from midnight)
  int      schedOffMin;           // Schedule OFF time (minutes from midnight)
  uint16_t cmdGeneration;         // Supersession protection counter
  int      neighborCount;         // Neighbor count reported by node
  String   neighborData;          // Cached neighbor telemetry
  FeederState feeders[MAX_FEEDERS]; // Multi-feeder cache per node (6 Feeders x 3 Phases = 18 Current Sensors)
};

// ─────────────────────────────────────────────────────────────
//  MESH ROUTING & TOPOLOGY
// ─────────────────────────────────────────────────────────────
struct GwRouteEntry {
  String   destination;
  String   primaryNextHop;
  String   backupNextHop;
  int      hopCount;
  int      routeCost;
  int      rssi;
  int      snr;
  bool     valid;
  unsigned long lastSuccess;
  unsigned long lastUpdate;
  String   routePath;
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
  MENU_DASHBOARD = MENU_HOME,
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
  MENU_REGISTRATION,
  // Prompt 16 Main Dashboard Screens
  MENU_POLE_MGMT,
  MENU_POLE_DETAIL,
  MENU_FEEDER_MGMT,
  MENU_FEEDER_DETAIL,
  MENU_REG_PENDING,
  MENU_DELETE_MGMT,
  MENU_DELETE_CONFIRM,
  MENU_REVOKED_LIST,
  MENU_FEEDER_CTRL,
  MENU_GROUP_CTRL,
  MENU_GROUP_CONFIRM,
  MENU_ALL_POLES_CTRL,
  MENU_ALL_CONFIRM,
  MENU_VOLTAGE_MON,
  MENU_VOLT_DETAIL,
  MENU_CURRENT_MON,
  MENU_CURR_DETAIL,
  MENU_TOPOLOGY,
  MENU_NODE_STATUS,
  MENU_FAULT_ALERTS,
  MENU_TIME_CONFIG,
  MENU_WIFI_SETTINGS,
  MENU_WIFI_CONFIRM,
  MENU_GW_SETTINGS,
  MENU_FW_MGMT,
  MENU_EVENT_HISTORY,
  MENU_HELP_INFO,
  // Prompt 18 Central Monitoring & Intelligent Alerts
  MENU_MAIN_DASHBOARD,
  MENU_GW_STATUS,
  MENU_ALL_POLES,
  MENU_ONLINE_POLES,
  MENU_OFFLINE_POLES,
  MENU_VOLTAGE_TREND,
  MENU_PHASE_COMPARE,
  MENU_INCIDENT_DETAIL
};

enum WizardState {
  WIZ_NONE = 0,
  WIZ_REG_NAME,
  WIZ_REG_AREA,
  WIZ_DELETE_CONFIRM,
  WIZ_GROUP_CONFIRM,
  WIZ_ALL_CONFIRM
};

#define MAX_SESSIONS 4

struct UserSession {
  String        chatId;
  String        userId;
  MenuScreen    currentMenu;
  MenuScreen    previousMenu;
  int           lastMessageId;
  unsigned long lastActive;
  String        selectedPole;
  uint8_t       selectedFeeder;
  String        selectedNeighbour;
  String        selectedGroup;
  int           pageIndex;
  int           pendingRegIdx;
  WizardState   wizardState;
  String        wizardTargetId;
  String        confirmationToken;
  unsigned long tokenExpiryMs;
  String        pendingPoleName;
  String        pendingPoleArea;
  uint32_t      selectedEventId;
  String        poleFilter;       // "ALL", "ONLINE", "OFFLINE"
  String        currentMode;      // "A", "B", "C", "D", "E", "F", "G", "H"
};

// ─────────────────────────────────────────────────────────────
//  REVOCATION & DELETION DATA STRUCTURES (PROMPT 12)
// ─────────────────────────────────────────────────────────────
struct RevocationRecord {
  char     logicalId[16];          // Logical Node ID (e.g. "SL-0002")
  char     hwId[16];               // Unique Chip HW ID (e.g. "A4CF12E8")
  uint32_t revocationTime;         // Epoch timestamp
  char     transactionId[32];      // Revocation Transaction ID
  char     reason[32];             // Reason (e.g. "ADMIN_DECOMMISSION", "FAULT")
  char     previousParent[16];     // Previous Primary Next Hop
  char     previousRegState[16];   // e.g. "REGISTERED", "ACTIVE"
  uint8_t  revocationVersion;      // Schema version (1)
  char     revokedBy[24];          // Administrator Identity (e.g. "ADMIN:5505296197")
  bool     valid;
};

struct ChildMigrationPlan {
  char childId[16];
  char previousParent[16];
  char newPrimaryParent[16];
  char newBackupParent[16];
  int  newHopCount;
  int  linkRssi;
  bool canMigrate;
  bool isOrphaned;
  char reason[32];
};

struct DeletionImpact {
  char poleId[16];
  char poleName[32];
  char area[32];
  char status[16];
  bool isOnline;
  int  directChildCount;
  int  totalDownstreamCount;
  int  affectedRouteCount;
  int  activeFeederCount;
  bool hasOrphansWithoutRoute;
  ChildMigrationPlan children[MAX_NODES];
  int  migrationPlanCount;
};

enum DeletionType {
  DEL_TYPE_POLE = 0,
  DEL_TYPE_FEEDER
};

struct DeletionTransaction {
  bool                 inProgress;
  char                 txId[32];             // "DEL-<timestamp>-<rand>"
  DeletionType         type;                 // Pole or Feeder
  char                 targetId[16];         // Pole ID
  uint8_t              feederId;             // 1-6 for Feeder
  char                 adminId[24];          // Admin Identity
  char                 confirmationToken[16];// Two-step verification token
  PoleLifecycleState   state;                // State Machine State
  uint32_t             startedAt;            // Timestamp
  uint32_t             expiresAt;            // Confirmation expiration timestamp
  bool                 confirmed;            // Explicitly confirmed
  bool                 revocationCommitted;  // NVS committed
  bool                 routesMigrated;       // Children reassigned
  bool                 nodeAcknowledged;     // ACK received
  char                 failureReason[32];
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

// ─────────────────────────────────────────────────────────────
//  TELEGRAM CONTROL STATUS STRUCTURES (PROMPT 15 SECTION 13)
// ─────────────────────────────────────────────────────────────
struct FeederControlStatus {
  uint8_t feederId; // 1..6
  String  name;
  bool    relayOn;
  bool    contactorClosed;
  bool    mcbOk;
  String  faultState;
  float   currentR;
  float   currentY;
  float   currentB;
  float   activePowerW;
  float   powerFactor;
  float   energyKwh;
};

struct PoleControlStatus {
  String  nodeId;
  String  name;
  String  onlineState;
  uint8_t activeRelayMask;
  uint8_t activeContactorMask;
  uint8_t mcbTripMask;
  bool    emergencyStop;
  bool    manualMode;
  float   voltageR;
  float   voltageY;
  float   voltageB;
  float   frequency;
  FeederControlStatus feeders[MAX_FEEDERS];
};

struct CommandExecutionRecord {
  uint8_t  commandId;
  String   nodeId;
  uint8_t  feederId;
  String   requestedAction;
  String   status; // "PENDING", "ACCEPTED", "CONFIRMED", "FAILED", "TIMEOUT"
  uint32_t requestTimeMs;
  uint32_t ackTimeMs;
  String   stage;
  String   details;
};

struct PhaseCurrentStatus {
  uint8_t feederId;
  float   currentR;
  float   currentY;
  float   currentB;
  bool    isImbalanced;
  float   imbalancePercent;
  bool    isOverCurrent;
  String  statusText;
};

struct PhaseVoltageStatus {
  String  nodeId;
  float   voltageR;
  float   voltageY;
  float   voltageB;
  float   frequency;
  bool    phaseLossR;
  bool    phaseLossY;
  bool    phaseLossB;
  bool    isUnderVolt;
  bool    isOverVolt;
  String  statusText;
};

#endif // TYPES_H
