/*
 * =====================================================================
 *  TYPES.H — FIELD POLE & FEEDER NODE DATA TYPES & ENUMERATIONS
 *  Smart Street Light / Farmer Power Distribution Mesh System
 * =====================================================================
 */

#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"

// ─────────────────────────────────────────────────────────────
//  FEEDER STATUS & HARDWARE STATE (PROMPT 12)
// ─────────────────────────────────────────────────────────────
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
//  COMMAND EXECUTION STATE MACHINE & FEEDBACK (PROMPT 15)
// ─────────────────────────────────────────────────────────────
enum CommandExecutionState {
  CMD_STATE_IDLE = 0,
  CMD_STATE_COMMAND_RECEIVED,
  CMD_STATE_AUTHENTICATION_VERIFIED,
  CMD_STATE_TARGET_VALIDATED,
  CMD_STATE_SAFETY_CHECK,
  CMD_STATE_COMMAND_ACCEPTED,
  CMD_STATE_SWITCHING_REQUESTED,
  CMD_STATE_RELAY_OUTPUT_CHANGED,
  CMD_STATE_CONTACTOR_FEEDBACK_PENDING,
  CMD_STATE_EXECUTION_VERIFIED,
  CMD_STATE_COMMAND_FAILED,
  CMD_STATE_TIMEOUT
};

inline const char* commandExecutionStateToString(CommandExecutionState st) {
  switch (st) {
    case CMD_STATE_IDLE:                         return "IDLE";
    case CMD_STATE_COMMAND_RECEIVED:             return "COMMAND_RECEIVED";
    case CMD_STATE_AUTHENTICATION_VERIFIED:      return "AUTHENTICATION_VERIFIED";
    case CMD_STATE_TARGET_VALIDATED:             return "TARGET_VALIDATED";
    case CMD_STATE_SAFETY_CHECK:                 return "SAFETY_CHECK";
    case CMD_STATE_COMMAND_ACCEPTED:             return "COMMAND_ACCEPTED";
    case CMD_STATE_SWITCHING_REQUESTED:          return "SWITCHING_REQUESTED";
    case CMD_STATE_RELAY_OUTPUT_CHANGED:         return "RELAY_OUTPUT_CHANGED";
    case CMD_STATE_CONTACTOR_FEEDBACK_PENDING:   return "CONTACTOR_FEEDBACK_PENDING";
    case CMD_STATE_EXECUTION_VERIFIED:           return "EXECUTION_VERIFIED";
    case CMD_STATE_COMMAND_FAILED:               return "COMMAND_FAILED";
    case CMD_STATE_TIMEOUT:                      return "TIMEOUT";
    default:                                     return "UNKNOWN";
  }
}

enum ContactorAuxState {
  CONTACTOR_AUX_UNKNOWN = 0,
  CONTACTOR_AUX_OPEN,
  CONTACTOR_AUX_CLOSED
};

enum ContactorFeedbackQuality {
  FEEDBACK_QUALITY_NORMAL = 0,
  FEEDBACK_QUALITY_PENDING,
  FEEDBACK_QUALITY_WELDED,
  FEEDBACK_QUALITY_COIL_FAIL,
  FEEDBACK_QUALITY_UNEXPECTED_OPEN,
  FEEDBACK_QUALITY_UNEXPECTED_CLOSE,
  FEEDBACK_QUALITY_DISCONNECTED
};

inline const char* feedbackQualityToString(ContactorFeedbackQuality q) {
  switch (q) {
    case FEEDBACK_QUALITY_NORMAL:           return "NORMAL";
    case FEEDBACK_QUALITY_PENDING:          return "PENDING";
    case FEEDBACK_QUALITY_WELDED:           return "WELDED";
    case FEEDBACK_QUALITY_COIL_FAIL:        return "COIL_FAIL";
    case FEEDBACK_QUALITY_UNEXPECTED_OPEN:  return "UNEXPECTED_OPEN";
    case FEEDBACK_QUALITY_UNEXPECTED_CLOSE: return "UNEXPECTED_CLOSE";
    case FEEDBACK_QUALITY_DISCONNECTED:     return "DISCONNECTED";
    default:                                return "UNKNOWN";
  }
}

struct CommandTransaction {
  uint8_t               commandId;
  uint8_t               feederIndex;            // 0..5 (Feeder 1..6)
  bool                  requestedState;         // true = ON, false = OFF
  CommandExecutionState state;
  uint32_t              receivedTimeMs;
  uint32_t              stateChangedMs;
  uint32_t              expirationTimeMs;
  uint16_t              seqNum;
  String                callerId;
  String                failureReason;
  bool                  active;
  // 5-Stage Verification Flags
  bool                  stageA_Received;
  bool                  stageB_Accepted;
  bool                  stageC_RelayChanged;
  bool                  stageD_FeedbackConfirmed;
  bool                  stageE_MeasurementVerified;
};

struct StaggeredGroupControl {
  bool                  active;
  uint8_t               targetMask;             // Bit 0 = Feeder 1 ... Bit 5 = Feeder 6
  bool                  requestedState;         // true = ON, false = OFF
  uint32_t              interFeederDelayMs;     // e.g. 500ms
  uint32_t              nextStepMs;
  uint8_t               currentStep;            // 0..5
  uint8_t               successMask;
  uint8_t               failureMask;
  String                callerId;
};

struct FeederHardwareState {
  bool                     relayOn;
  bool                     mcbR;
  bool                     mcbY;
  bool                     mcbB;
  bool                     mcbTrippedLatched;
  bool                     contactor;
  ContactorAuxState        contactorState;
  ContactorFeedbackQuality feedbackQuality;
  uint8_t                  mcbRDebounceCount;
  uint8_t                  mcbYDebounceCount;
  uint8_t                  mcbBDebounceCount;
  uint8_t                  contDebounceCount;
  String                   faultState;
  FeederStatus             status;           // ACTIVE, DEACTIVATED, REMOVED
  float                    currentR;         // ACS712 / ADS131M08 R-Phase current (A)
  float                    currentY;         // Y-Phase current (A)
  float                    currentB;         // B-Phase current (A)
  float                    powerActive;      // Real power (W)
  float                    powerFactor;      // Power Factor
  float                    energyWh;         // Accumulated energy (Wh)
  float                    historicalKwh;    // Preserved historical energy
  uint32_t                 lastActuationMs;  // Millis timestamp when relay switched
  bool                     feedbackPending;  // Awaiting contactor confirmation
};

// ─────────────────────────────────────────────────────────────
//  MESH NODE STATE & PARENT DEFINITIONS (PROMPT 10)
// ─────────────────────────────────────────────────────────────
enum MeshNodeState {
  MESH_STATE_INIT = 0,
  MESH_STATE_NORMAL,
  MESH_STATE_DEGRADED,
  MESH_STATE_ORPHAN
};

struct ParentInfo {
  String   nodeId;
  uint8_t  rank;
  uint16_t cost;
  String   upstreamParent;  // Parent's primary parent (for loop & path diversity validation)
  int      rssi;
  int      snr;
  uint8_t  consecutiveFailures;
  unsigned long lastSeen;
  bool     valid;
};

// ─────────────────────────────────────────────────────────────
//  NEIGHBOR TABLE ENTRY
// ─────────────────────────────────────────────────────────────
struct NeighborEntry {
  String   nodeId;
  int      rssi;
  int      snr;
  unsigned long lastSeen;
  bool     active;
  bool     healthReplied;
  uint8_t  rank;
  String   primaryParent;   // Candidate's primary parent (for loop & path diversity)
  uint16_t routeCost;       // Candidate's advertised cost to root
  uint8_t  linkQuality;     // 0-100 composite link quality
  uint8_t  pdr;             // 0-100 packet delivery ratio
  uint16_t txSuccessCount;
  uint16_t txTotalCount;
  uint8_t  consecutiveFailures;
};

// ─────────────────────────────────────────────────────────────
//  POLE ROUTING TABLE ENTRY
// ─────────────────────────────────────────────────────────────
struct PoleRouteEntry {
  String   destination;
  String   nextHop;
  int      hopCount;
  uint16_t routeCost;
  int      rssi;
  int      snr;
  unsigned long lastSeen;
  bool     valid;
  uint16_t routeVersion;
};

// ─────────────────────────────────────────────────────────────
//  PACKET DEDUPLICATION CACHE ENTRY
// ─────────────────────────────────────────────────────────────
struct PoleSeqCacheEntry {
  String   src;
  uint16_t seq;
};

#endif // TYPES_H
