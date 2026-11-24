/**
 * @file NetworkTypes.h
 * @brief MSCB Smart Pole & Gateway System - Common Network & Telemetry Data Types
 * @version 1.0.0
 * 
 * Defines standard enumerations, status flags, and structured payloads for
 * three-phase voltage and multi-feeder current monitoring across 50 nodes.
 */

#ifndef MSCB_NETWORK_TYPES_H
#define MSCB_NETWORK_TYPES_H

#include <Arduino.h>
#include "BuildConfig.h"

// =====================================================================
//  PACKET PRIORITY LEVELS
// =====================================================================
enum PacketPriority : uint8_t {
  PRIO_ROUTINE    = 0, // Background telemetry, periodic beacons
  PRIO_NORMAL     = 1, // Status inquiries, routing announcements
  PRIO_COMMAND    = 2, // User remote-control switching, config sync
  PRIO_CRITICAL   = 3  // Hardware fault, over-current trip, emergency stop
};

// =====================================================================
//  FEEDER RELAY & MCB STATUS & COMMAND STATE PROGRESSION
// =====================================================================
enum FeederRelayState : uint8_t {
  FEEDER_STATE_OFF         = 0, // Coil de-energized, contacts open
  FEEDER_STATE_ON          = 1, // Coil energized, contacts closed
  FEEDER_STATE_TRIPPED     = 2, // MCB tripped / hardware fault
  FEEDER_STATE_LOCKOUT     = 3, // Safety interlock active
  FEEDER_STATE_UNAVAILABLE = 4  // Channel unpopulated or disabled
};

enum McbTripStatus : uint8_t {
  MCB_STATUS_HEALTHY     = 0, // Mains switch auxiliary contact closed (normal)
  MCB_STATUS_TRIPPED     = 1, // Auxiliary contact open (overload / short circuit)
  MCB_STATUS_UNKNOWN     = 2, // Input line disconnected / unmonitored
  MCB_STATUS_UNAVAILABLE = 3  // Hardware unpopulated
};

// =====================================================================
//  EXPLICIT TRUTHFUL HARDWARE FEEDBACK STATES (R3.2.1 P0)
// =====================================================================
enum ContactorState : uint8_t {
  CONTACTOR_CLOSED      = 0,
  CONTACTOR_OPEN        = 1,
  CONTACTOR_UNKNOWN     = 2,
  CONTACTOR_UNAVAILABLE = 3
};

inline const char* contactorStateToString(ContactorState st) {
  switch (st) {
    case CONTACTOR_CLOSED:      return "CLOSED";
    case CONTACTOR_OPEN:        return "OPEN";
    case CONTACTOR_UNKNOWN:     return "UNKNOWN";
    case CONTACTOR_UNAVAILABLE: return "UNAVAILABLE";
    default:                    return "UNKNOWN";
  }
}

enum McbState : uint8_t {
  MCB_HEALTHY     = 0,
  MCB_TRIPPED     = 1,
  MCB_UNKNOWN     = 2,
  MCB_UNAVAILABLE = 3
};

inline const char* mcbStateToString(McbState st) {
  switch (st) {
    case MCB_HEALTHY:     return "HEALTHY";
    case MCB_TRIPPED:     return "TRIPPED";
    case MCB_UNKNOWN:     return "UNKNOWN";
    case MCB_UNAVAILABLE: return "UNAVAILABLE";
    default:              return "UNKNOWN";
  }
}

enum MeasurementValidity : uint8_t {
  MEAS_VALID              = 0,
  MEAS_INVALID            = 1,
  MEAS_SENSOR_UNAVAILABLE = 2,
  MEAS_OUT_OF_RANGE       = 3,
  MEAS_UNKNOWN            = 4
};

inline const char* measurementValidityToString(MeasurementValidity st) {
  switch (st) {
    case MEAS_VALID:              return "VALID";
    case MEAS_INVALID:            return "INVALID";
    case MEAS_SENSOR_UNAVAILABLE: return "SENSOR_UNAVAILABLE";
    case MEAS_OUT_OF_RANGE:       return "OUT_OF_RANGE";
    case MEAS_UNKNOWN:
    default:                      return "UNKNOWN";
  }
}

// Section 7 Mandatory Command State Progression & Explicit Truth States:
// SENT -> RECEIVED -> ACCEPTED -> OUTPUT_CHANGED -> AUXILIARY_FEEDBACK_VERIFIED -> COMPLETED
enum FeederCommandAckState : uint8_t {
  FEEDER_ACK_SENT                        = 0,
  FEEDER_ACK_RECEIVED                    = 1,
  FEEDER_ACK_ACCEPTED                    = 2,
  FEEDER_ACK_OUTPUT_CHANGED              = 3,
  FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED = 4,
  FEEDER_ACK_COMPLETED                   = 5,
  FEEDER_ACK_REJECTED_STALE              = 6,
  FEEDER_ACK_REJECTED_LOCKED             = 7,
  FEEDER_ACK_REJECTED_FAULT              = 8,
  FEEDER_ACK_FEEDBACK_NOT_CONNECTED      = 9,
  FEEDER_ACK_FEEDBACK_UNAVAILABLE        = 10,
  FEEDER_ACK_REJECTED_TIME_UNSYNCED      = 11,
  FEEDER_ACK_RELAY_OUTPUT_ENERGIZED      = 12,
  FEEDER_ACK_CONTACTOR_CLOSED            = 13,
  FEEDER_ACK_MCB_TRIPPED                 = 14,
  FEEDER_ACK_COMMAND_TIMEOUT             = 15,
  FEEDER_ACK_HARDWARE_FAULT              = 16
};

// Required Architecture Truth State Aliases (Phase 2 & 9)
#define TRUTH_STATE_COMMAND_RECEIVED          FEEDER_ACK_RECEIVED
#define TRUTH_STATE_COMMAND_ACCEPTED          FEEDER_ACK_ACCEPTED
#define TRUTH_STATE_RELAY_OUTPUT_ENERGIZED    FEEDER_ACK_RELAY_OUTPUT_ENERGIZED
#define TRUTH_STATE_CONTACTOR_CLOSED          FEEDER_ACK_CONTACTOR_CLOSED
#define TRUTH_STATE_MCB_TRIPPED               FEEDER_ACK_MCB_TRIPPED
#define TRUTH_STATE_COMMAND_REJECTED          FEEDER_ACK_REJECTED_LOCKED
#define TRUTH_STATE_FEEDBACK_UNAVAILABLE      FEEDER_ACK_FEEDBACK_UNAVAILABLE
#define TRUTH_STATE_COMMAND_TIMEOUT           FEEDER_ACK_COMMAND_TIMEOUT
#define TRUTH_STATE_HARDWARE_FAULT            FEEDER_ACK_HARDWARE_FAULT

inline const char* feederAckStateToString(FeederCommandAckState st) {
  switch (st) {
    case FEEDER_ACK_SENT:                        return "SENT";
    case FEEDER_ACK_RECEIVED:                    return "COMMAND_RECEIVED";
    case FEEDER_ACK_ACCEPTED:                    return "COMMAND_ACCEPTED";
    case FEEDER_ACK_OUTPUT_CHANGED:              return "OUTPUT_CHANGED";
    case FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED: return "AUXILIARY_FEEDBACK_VERIFIED";
    case FEEDER_ACK_COMPLETED:                   return "COMPLETED";
    case FEEDER_ACK_REJECTED_STALE:              return "REJECTED_STALE";
    case FEEDER_ACK_REJECTED_LOCKED:             return "COMMAND_REJECTED";
    case FEEDER_ACK_REJECTED_FAULT:              return "COMMAND_REJECTED_FAULT";
    case FEEDER_ACK_FEEDBACK_NOT_CONNECTED:      return "FEEDBACK_NOT_CONNECTED";
    case FEEDER_ACK_FEEDBACK_UNAVAILABLE:        return "FEEDBACK_UNAVAILABLE";
    case FEEDER_ACK_REJECTED_TIME_UNSYNCED:      return "REJECTED_TIME_UNSYNCED";
    case FEEDER_ACK_RELAY_OUTPUT_ENERGIZED:      return "RELAY_OUTPUT_ENERGIZED";
    case FEEDER_ACK_CONTACTOR_CLOSED:            return "CONTACTOR_CLOSED";
    case FEEDER_ACK_MCB_TRIPPED:                 return "MCB_TRIPPED";
    case FEEDER_ACK_COMMAND_TIMEOUT:             return "COMMAND_TIMEOUT";
    case FEEDER_ACK_HARDWARE_FAULT:              return "HARDWARE_FAULT";
    default:                                     return "UNKNOWN";
  }
}

// =====================================================================
//  NODE OPERATIONAL HEALTH & CHANNEL AVAILABILITY
// =====================================================================
enum NodeHealthState : uint8_t {
  NODE_HEALTH_BOOTING     = 0,
  NODE_HEALTH_OK          = 1,
  NODE_HEALTH_WARNING     = 2,
  NODE_HEALTH_FAULT       = 3,
  NODE_HEALTH_OFFLINE     = 4
};

enum ChannelDataState : uint8_t {
  CHANNEL_STATE_OK              = 0, // Sampling active, within valid range
  CHANNEL_VALID                 = 0, // Alias for CHANNEL_STATE_OK
  CHANNEL_ADC_NOT_CONNECTED     = 1, // Underlying ADC chip missing / not detected on SPI
  CHANNEL_SENSOR_NOT_CONNECTED  = 2, // Sensor open / disconnected (when reliably detectable)
  CHANNEL_SENSOR_STATE_UNKNOWN  = 3, // Sensor state cannot be confirmed
  CHANNEL_DATA_UNAVAILABLE      = 4, // Measurement unavailable (hardware offline / uninitialized)
  CHANNEL_UNCALIBRATED          = 5, // Channel lacks valid calibration
  CHANNEL_DATA_STALE            = 6, // Sampling stopped / timed out; values preserved
  CHANNEL_ZERO_FRAME_REJECTED   = 7, // All-zero or all-one dead bus frame rejected
  CHANNEL_OUT_OF_RANGE          = 8, // Signal outside permissible dynamic range
  CHANNEL_OVER_RANGE_CLIPPING   = 9, // Waveform clipped or saturated (+/- 1.2V FSR)
  CHANNEL_CLIPPED               = 9, // Alias for CHANNEL_OVER_RANGE_CLIPPING
  CHANNEL_SENSOR_FAULT          = 10 // Hardware transducer fault
};

inline const char* channelDataStateToString(ChannelDataState st) {
  switch (st) {
    case CHANNEL_STATE_OK:             return "VALID";
    case CHANNEL_ADC_NOT_CONNECTED:    return "ADC_NOT_CONNECTED";
    case CHANNEL_SENSOR_NOT_CONNECTED: return "SENSOR_NOT_CONNECTED";
    case CHANNEL_SENSOR_STATE_UNKNOWN: return "SENSOR_STATE_UNKNOWN";
    case CHANNEL_DATA_UNAVAILABLE:     return "DATA_UNAVAILABLE";
    case CHANNEL_UNCALIBRATED:         return "UNCALIBRATED";
    case CHANNEL_DATA_STALE:           return "DATA_STALE";
    case CHANNEL_ZERO_FRAME_REJECTED:  return "ZERO_FRAME_REJECTED";
    case CHANNEL_OUT_OF_RANGE:         return "OUT_OF_RANGE";
    case CHANNEL_OVER_RANGE_CLIPPING:  return "CLIPPED";
    case CHANNEL_SENSOR_FAULT:         return "SENSOR_FAULT";
    default:                           return "UNKNOWN";
  }
}

// =====================================================================
//  HARDWARE FEEDBACK VALIDITY ENUMS (MCP23S17 CONTACT SUPERVISION)
// =====================================================================
enum AuxFeedbackState : uint8_t {
  AUX_NOT_CONNECTED = 0,
  AUX_UNKNOWN       = 1,
  AUX_VALID         = 2
};

enum McbFeedbackState : uint8_t {
  MCB_FEEDBACK_NOT_CONNECTED = 0,
  MCB_FEEDBACK_UNKNOWN       = 1,
  MCB_FEEDBACK_VALID         = 2
};

inline const char* auxFeedbackStateToString(AuxFeedbackState st) {
  switch (st) {
    case AUX_VALID:         return "VALID";
    case AUX_UNKNOWN:       return "UNKNOWN";
    case AUX_NOT_CONNECTED:
    default:                return "NOT_CONNECTED";
  }
}

inline const char* mcbFeedbackStateToString(McbFeedbackState st) {
  switch (st) {
    case MCB_FEEDBACK_VALID:         return "VALID";
    case MCB_FEEDBACK_UNKNOWN:       return "UNKNOWN";
    case MCB_FEEDBACK_NOT_CONNECTED:
    default:                         return "NOT_CONNECTED";
  }
}

// =====================================================================
//  ELECTRICAL MEASUREMENT DATA STRUCTURES (21 CHANNELS PER POLE)
// =====================================================================

/**
 * @brief Common 3-Phase Mains Voltage Data (3 Channels: R, Y, B)
 */
struct ThreePhaseVoltage {
  float v_r_rms;      // Red Phase RMS Voltage (VAC)
  float v_y_rms;      // Yellow Phase RMS Voltage (VAC)
  float v_b_rms;      // Blue Phase RMS Voltage (VAC)
  float frequency_hz; // System Frequency (Hz, nominal 50.0)
  uint8_t flags;      // Bit 0: R Under/Over, Bit 1: Y Under/Over, Bit 2: B Under/Over
};

/**
 * @brief Independent 3-Phase Current Data per Feeder (3 Channels: R, Y, B)
 */
struct ThreePhaseCurrent {
  float i_r_rms;      // Red Phase RMS Current (Amperes)
  float i_y_rms;      // Yellow Phase RMS Current (Amperes)
  float i_b_rms;      // Blue Phase RMS Current (Amperes)
  float power_factor; // Calculated aggregate power factor (0.00 to 1.00)
};

/**
 * @brief Full Telemetry Record for a Single Feeder (1 of 6 per pole)
 */
struct FeederTelemetry {
  uint8_t           feederIndex; // 0 to 5
  FeederRelayState  relayState;  // Current commanded state
  McbTripStatus     mcbStatus;   // Auxiliary contact feedback
  ThreePhaseCurrent current;     // 3-Phase current readings
  float             activePower_kW; // Estimated active power
};

/**
 * @brief Comprehensive Pole Telemetry Frame (50 Nodes max in Gateway DB)
 */
struct PoleTelemetryFrame {
  uint16_t           poleId;        // Pole address (e.g., 0x0002 for Pole 2)
  uint32_t           timestamp;     // Local epoch or uptime (seconds)
  NodeHealthState    healthState;   // Node operational condition
  int16_t            rssi_dBm;      // RF RSSI received at gateway/hop
  int8_t             snr_dB;        // RF SNR received
  uint8_t            hopCount;      // Number of hops packet traversed
  ThreePhaseVoltage  busVoltage;    // Common R/Y/B phase voltage (3 channels)
  FeederTelemetry    feeders[MSCB_FEEDERS_PER_POLE]; // 6 Feeders * 3 currents = 18 channels
};

#endif // MSCB_NETWORK_TYPES_H
