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
//  FEEDER RELAY & MCB STATUS
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
  CHANNEL_ADC_NOT_CONNECTED     = 1, // Underlying ADC chip missing / not detected on SPI
  CHANNEL_SENSOR_FAULT          = 2, // Sensor open, short, or invalid impedance
  CHANNEL_DATA_UNAVAILABLE      = 3, // Measurement unavailable (hardware offline)
  CHANNEL_DATA_STALE            = 4, // Sampling stopped / timed out; values preserved
  CHANNEL_ZERO_FRAME_REJECTED   = 5, // All-zero or all-one dead bus frame rejected
  CHANNEL_OVER_RANGE_CLIPPING   = 6  // Waveform clipped or saturated
};

inline const char* channelDataStateToString(ChannelDataState st) {
  switch (st) {
    case CHANNEL_STATE_OK:            return "OK";
    case CHANNEL_ADC_NOT_CONNECTED:   return "ADC_NOT_CONNECTED";
    case CHANNEL_SENSOR_FAULT:        return "SENSOR_FAULT";
    case CHANNEL_DATA_UNAVAILABLE:    return "DATA_UNAVAILABLE";
    case CHANNEL_DATA_STALE:          return "DATA_STALE";
    case CHANNEL_ZERO_FRAME_REJECTED: return "ZERO_FRAME_REJECTED";
    case CHANNEL_OVER_RANGE_CLIPPING: return "OVER_RANGE_CLIPPING";
    default:                          return "UNKNOWN";
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
