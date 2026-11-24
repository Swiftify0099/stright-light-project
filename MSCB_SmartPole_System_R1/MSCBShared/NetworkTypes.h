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
  MCB_STATUS_HEALTHY = 0, // Mains switch auxiliary contact closed (normal)
  MCB_STATUS_TRIPPED = 1, // Auxiliary contact open (overload / short circuit)
  MCB_STATUS_UNKNOWN = 2  // Input line disconnected / unmonitored
};

// Section 7 Mandatory Command State Progression:
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
  FEEDER_ACK_REJECTED_TIME_UNSYNCED      = 11
};

inline const char* feederAckStateToString(FeederCommandAckState st) {
  switch (st) {
    case FEEDER_ACK_SENT:                        return "SENT";
    case FEEDER_ACK_RECEIVED:                    return "RECEIVED";
    case FEEDER_ACK_ACCEPTED:                    return "ACCEPTED";
    case FEEDER_ACK_OUTPUT_CHANGED:              return "OUTPUT_CHANGED";
    case FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED: return "AUXILIARY_FEEDBACK_VERIFIED";
    case FEEDER_ACK_COMPLETED:                   return "COMPLETED";
    case FEEDER_ACK_REJECTED_STALE:              return "REJECTED_STALE";
    case FEEDER_ACK_REJECTED_LOCKED:             return "REJECTED_LOCKED";
    case FEEDER_ACK_REJECTED_FAULT:              return "REJECTED_FAULT";
    case FEEDER_ACK_FEEDBACK_NOT_CONNECTED:      return "FEEDBACK_NOT_CONNECTED";
    case FEEDER_ACK_FEEDBACK_UNAVAILABLE:        return "FEEDBACK_UNAVAILABLE";
    case FEEDER_ACK_REJECTED_TIME_UNSYNCED:      return "REJECTED_TIME_UNSYNCED";
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
  MCB_NOT_CONNECTED = 0,
  MCB_UNKNOWN       = 1,
  MCB_VALID         = 2
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
    case MCB_VALID:         return "VALID";
    case MCB_UNKNOWN:       return "UNKNOWN";
    case MCB_NOT_CONNECTED:
    default:                return "NOT_CONNECTED";
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
