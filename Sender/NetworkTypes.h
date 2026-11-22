/*
 * =====================================================================
 *  NETWORKTYPES.H — MSCB SECURE PROTOCOL DATA TYPES & DEFINITIONS
 *  Municipal Smart Control Box (MSCB) Smart Pole System
 *  ESP32-S3 Gateway & ESP32 Secondary Nodes (SX1278 LoRa)
 * =====================================================================
 */

#ifndef MSCB_NETWORK_TYPES_H
#define MSCB_NETWORK_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─────────────────────────────────────────────────────────────
//  NETWORK PROTOCOL CONSTANTS
// ─────────────────────────────────────────────────────────────
#define MSCB_PROTOCOL_VERSION        0x01
#define MSCB_NETWORK_ID_DEFAULT      0x4D53  // "MS" (MSCB Network)

// Standard Node Identifiers (16-bit)
#define MSCB_NODE_ID_UNASSIGNED      0x0000
#define MSCB_NODE_ID_GATEWAY         0x0001
#define MSCB_NODE_ID_BROADCAST       0xFFFF
#define MSCB_NODE_ID_POLE_MIN        0x0002
#define MSCB_NODE_ID_POLE_MAX        0x0033  // Up to 50 secondary poles

// Network Architecture Limits
#define MSCB_MAX_REGISTERED_POLES    50
#define MSCB_FEEDERS_PER_POLE        6
#define MSCB_CURRENTS_PER_FEEDER     3       // R, Y, B phases
#define MSCB_VOLTAGES_PER_POLE       3       // Common R, Y, B phases
#define MSCB_TOTAL_CURRENT_CHANNELS  18      // 6 feeders * 3 phases
#define MSCB_DEFAULT_TTL             6       // Standard mesh hop limit

// Packet Buffer Boundaries (SX1278 LoRa Constraints)
#define MSCB_RADIO_FIFO_MAX_LEN      255     // SX1278 maximum payload register
#define MSCB_HEADER_SIZE             22      // Binary header length
#define MSCB_AUTH_TAG_SIZE           16      // AES-CCM 128-bit MAC tag
#define MSCB_RADIO_CRC_SIZE          2       // CRC16-CCITT error check
#define MSCB_CRYPTO_NONCE_SIZE       13      // RFC 3610 104-bit nonce
#define MSCB_AES_KEY_SIZE            16      // 128-bit cryptographic key

#define MSCB_MIN_PACKET_LEN          (MSCB_HEADER_SIZE + MSCB_AUTH_TAG_SIZE + MSCB_RADIO_CRC_SIZE) // 40 bytes
#define MSCB_MAX_PAYLOAD_LEN         (MSCB_RADIO_FIFO_MAX_LEN - MSCB_MIN_PACKET_LEN)               // 215 bytes

// ─────────────────────────────────────────────────────────────
//  MESSAGE TYPES (16 DEFINED TYPES)
// ─────────────────────────────────────────────────────────────
typedef enum {
    MSCB_MSG_DISCOVERY_BEACON     = 0x01,  // Unencrypted/commissioning network discovery
    MSCB_MSG_JOIN_REQUEST         = 0x02,  // Node registration request with hardware UUID
    MSCB_MSG_JOIN_RESPONSE        = 0x03,  // Gateway registration acceptance & credentials
    MSCB_MSG_REG_CONFIRM          = 0x04,  // Node confirmation of assigned identity
    MSCB_MSG_HEARTBEAT            = 0x05,  // Periodic health & link quality check
    MSCB_MSG_TELEMETRY            = 0x06,  // Periodic electrical measurement payload
    MSCB_MSG_ROUTE_REQUEST        = 0x07,  // Mesh route discovery broadcast (RREQ)
    MSCB_MSG_ROUTE_RESPONSE       = 0x08,  // Unicast route response (RREP)
    MSCB_MSG_ROUTE_UPDATE         = 0x09,  // Route metric / topology announcement
    MSCB_MSG_COMMAND              = 0x0A,  // Authenticated relay control / configuration
    MSCB_MSG_COMMAND_ACK          = 0x0B,  // Command execution receipt and state
    MSCB_MSG_STATUS_RESPONSE      = 0x0C,  // Immediate pole & feeder status report
    MSCB_MSG_NODE_REVOKE          = 0x0D,  // Gateway revocation of compromised node
    MSCB_MSG_CONFIG_UPDATE        = 0x0E,  // Timing, threshold, or parameter update
    MSCB_MSG_FAULT_ALERT          = 0x0F,  // High-priority unsolicited fault notification
    MSCB_MSG_ERROR_RESPONSE       = 0x10   // Malformed command or protocol violation error
} MscbMessageType;

// ─────────────────────────────────────────────────────────────
//  PACKET FLAGS (BITFIELD)
// ─────────────────────────────────────────────────────────────
typedef enum {
    MSCB_FLAG_NONE                = 0x00,
    MSCB_FLAG_ENCRYPTED           = 0x01,  // Payload encrypted with AES-128-CCM
    MSCB_FLAG_ACK_REQ             = 0x02,  // Destination must respond with ACK
    MSCB_FLAG_PRIORITY_URGENT     = 0x04,  // Emergency / safety trip message
    MSCB_FLAG_FORWARDED           = 0x08,  // Packet has been relayed across mesh
    MSCB_FLAG_RESERVED_10         = 0x10,
    MSCB_FLAG_RESERVED_20         = 0x20,
    MSCB_FLAG_RESERVED_40         = 0x40,
    MSCB_FLAG_RESERVED_80         = 0x80
} MscbPacketFlags;

// ─────────────────────────────────────────────────────────────
//  ELECTRICAL MEASUREMENT VALIDITY BITS
// ─────────────────────────────────────────────────────────────
#define MSCB_PHASE_R_VALID           (1 << 0)
#define MSCB_PHASE_Y_VALID           (1 << 1)
#define MSCB_PHASE_B_VALID           (1 << 2)

// ─────────────────────────────────────────────────────────────
//  FEEDER STATUS & MCB BITS
// ─────────────────────────────────────────────────────────────
#define MSCB_FEEDER_STATUS_RELAY_ON  (1 << 0)
#define MSCB_FEEDER_STATUS_CONTACTOR (1 << 1)
#define MSCB_FEEDER_STATUS_PROTECT   (1 << 2)
#define MSCB_FEEDER_STATUS_MANUAL    (1 << 3)

#define MSCB_MCB_STATUS_R_OK         (1 << 0)  // 1 = Normal, 0 = Tripped
#define MSCB_MCB_STATUS_Y_OK         (1 << 1)
#define MSCB_MCB_STATUS_B_OK         (1 << 2)

// ─────────────────────────────────────────────────────────────
//  POLE & FEEDER FAULT CODES
// ─────────────────────────────────────────────────────────────
#define MSCB_POLE_FAULT_NONE         0x0000
#define MSCB_POLE_FAULT_OVER_VOLT    (1 << 0)
#define MSCB_POLE_FAULT_UNDER_VOLT   (1 << 1)
#define MSCB_POLE_FAULT_PHASE_LOSS   (1 << 2)
#define MSCB_POLE_FAULT_FREQ_DEV     (1 << 3)
#define MSCB_POLE_FAULT_TEMP_ALARM   (1 << 4)
#define MSCB_POLE_FAULT_TAMPER_OPEN  (1 << 5)
#define MSCB_POLE_FAULT_ADC_FAIL     (1 << 6)
#define MSCB_POLE_FAULT_NVS_CORRUPT  (1 << 7)

#define MSCB_FEEDER_FAULT_NONE       0x00
#define MSCB_FEEDER_FAULT_OVER_CURR  (1 << 0)
#define MSCB_FEEDER_FAULT_EARTH_LEAK (1 << 1)
#define MSCB_FEEDER_FAULT_MCB_TRIP   (1 << 2)
#define MSCB_FEEDER_FAULT_COIL_FAIL  (1 << 3)
#define MSCB_FEEDER_FAULT_WELDED     (1 << 4)

// ─────────────────────────────────────────────────────────────
//  FEEDER MEASUREMENT COMPACT STRUCTURE (11 BYTES)
// ─────────────────────────────────────────────────────────────
#pragma pack(push, 1)
typedef struct {
    uint8_t  feeder_id;           // 1 to 6
    uint16_t current_r_cA;        // Phase R current in centiamperes (0.01 A, e.g. 1520 = 15.20 A)
    uint16_t current_y_cA;        // Phase Y current in centiamperes
    uint16_t current_b_cA;        // Phase B current in centiamperes
    uint8_t  current_validity;    // Bits: 0=R, 1=Y, 2=B valid
    uint8_t  feeder_status;       // Bit 0: Relay ON, Bit 1: Contactor closed, Bit 2: Protected
    uint8_t  mcb_status;          // Bits: 0=R OK, 1=Y OK, 2=B OK
    uint8_t  feeder_fault;        // Feeder fault bitmask
} MscbFeederTelemetry;
#pragma pack(pop)

// ─────────────────────────────────────────────────────────────
//  POLE SENSOR TELEMETRY PAYLOAD (83 BYTES TOTAL)
// ─────────────────────────────────────────────────────────────
#pragma pack(push, 1)
typedef struct {
    uint16_t pole_id;             // Logical Pole Node ID (0x0002 .. 0x0033)
    uint32_t timestamp;           // Unix epoch seconds or node uptime seconds
    uint16_t measurement_seq;     // Monotonic measurement reading counter
    uint16_t voltage_r_dV;        // Phase R AC RMS voltage in deci-volts (0.1 V, e.g. 2305 = 230.5 V)
    uint16_t voltage_y_dV;        // Phase Y AC RMS voltage in deci-volts
    uint16_t voltage_b_dV;        // Phase B AC RMS voltage in deci-volts
    uint8_t  voltage_validity;    // Bits: 0=R, 1=Y, 2=B valid
    uint16_t pole_fault_flags;    // Pole-level fault bitmask
    MscbFeederTelemetry feeders[MSCB_FEEDERS_PER_POLE]; // 6 * 11 = 66 bytes
} MscbPoleTelemetryPayload;
#pragma pack(pop)

// Compile-time verification of telemetry payload size
typedef char mscb_telemetry_size_check[(sizeof(MscbPoleTelemetryPayload) == 83) ? 1 : -1];

// ─────────────────────────────────────────────────────────────
//  COMMAND DEFINITIONS
// ─────────────────────────────────────────────────────────────
typedef enum {
    MSCB_CMD_RELAY_OFF           = 0x01,  // Turn selected feeder relay OFF
    MSCB_CMD_RELAY_ON            = 0x02,  // Turn selected feeder relay ON
    MSCB_CMD_RELAY_TOGGLE        = 0x03,  // Toggle selected feeder relay
    MSCB_CMD_TIMED_ON            = 0x04,  // Turn ON for specified duration (seconds)
    MSCB_CMD_EMERGENCY_SHUTDOWN  = 0x05,  // Immediate safety trip of all feeders
    MSCB_CMD_REQUEST_STATUS      = 0x06,  // Query immediate status response
    MSCB_CMD_TIME_SYNC           = 0x07,  // Broadcast Gateway epoch timestamp
    MSCB_CMD_REBOOT              = 0x08,  // Controlled node reboot
    MSCB_CMD_KEY_ROTATION        = 0x09   // Secure session key rotation
} MscbCommandCode;

#pragma pack(push, 1)
typedef struct {
    uint8_t  command_id;          // Transaction correlation ID
    uint8_t  command_code;        // MscbCommandCode
    uint8_t  feeder_mask;         // Bitmask of target feeders (bit 0 = Feeder 1 ... bit 5 = Feeder 6)
    uint32_t duration_sec;        // Timed operation duration in seconds (0 = indefinite)
    uint16_t param;               // Optional command parameter
} MscbCommandPayload;
#pragma pack(pop)

typedef enum {
    MSCB_CMD_STATUS_SUCCESS      = 0x00,
    MSCB_CMD_STATUS_REJECTED     = 0x01,
    MSCB_CMD_STATUS_INVALID_CMD  = 0x02,
    MSCB_CMD_STATUS_HW_FAULT     = 0x03,
    MSCB_CMD_STATUS_UNAUTHORIZED = 0x04
} MscbCommandStatusCode;

#pragma pack(push, 1)
typedef struct {
    uint8_t  command_id;          // Matches incoming command_id
    uint8_t  status_code;         // MscbCommandStatusCode
    uint32_t execution_timestamp; // Node timestamp at execution
    uint8_t  active_relay_mask;   // Actual relay output bitmask after execution
    uint8_t  mcb_trip_mask;       // Any MCB trip state observed during execution
} MscbCommandAckPayload;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif // MSCB_NETWORK_TYPES_H
