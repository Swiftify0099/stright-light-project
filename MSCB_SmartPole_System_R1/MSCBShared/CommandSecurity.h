/**
 * @file CommandSecurity.h
 * @brief MSCB Smart Pole System - Cryptographic Command Envelope Authentication
 * @version 1.1.0
 * 
 * STRICT EMBEDDED SECURITY CONSTRAINTS:
 * 1. FAIL-CLOSED KEY PROVISIONING:
 *    - Zero fallback secrets. Removed MSCB_DEFAULT_AUTH_SECRET.
 *    - If no cryptographic key is securely provisioned, all command creation
 *      and verification strictly fails closed (AUTH_ERR_NO_PROVISIONED_KEY).
 * 
 * 2. COMPLETE ENVELOPE AUTHENTICATION (HMAC-SHA256):
 *    - Authenticates network ID, source, destination, feeder ID, command,
 *      command parameter, sequence number, expiry timestamp, and key epoch.
 * 
 * 3. PERSISTENT MONOTONIC SEQUENCE & ANTI-REPLAY:
 *    - Integrated with ReplayProtection (Strategy A dual-commit persistent NVS lease).
 *    - Sliding window verification rejects replayed or out-of-order sequence numbers.
 * 
 * 4. TIME INTEGRITY:
 *    - Uses absolute UTC Unix timestamp (expiryEpochSec).
 *    - Strictly prohibits using millis() across devices as a shared expiry clock.
 * 
 * 5. HMAC INTEGRITY SEPARATION:
 *    - HMAC is strictly message authentication (integrity and authenticity), NOT encryption.
 */

#ifndef MSCB_COMMAND_SECURITY_H
#define MSCB_COMMAND_SECURITY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO) && defined(ESP32)
  #include <Arduino.h>
#else
  #include <cstring>
  #include "../Tests/ArduinoMock.h"
#endif

#include "BuildConfig.h"
#include "NetworkTypes.h"
#include "Protocol.h"
#include "ReplayProtection.h"

#define MSCB_CMD_NETWORK_MAGIC       0x4D53 // 'M', 'S'
#define MSCB_KEY_LEN_BYTES           32
#define MSCB_HMAC_TAG_BYTES          32
#define MSCB_ENVELOPE_HEADER_BYTES   19

#pragma pack(push, 1)
struct AuthenticatedCommandEnvelope {
  uint16_t networkId;       // 0x4D53 ("MS")
  uint16_t sourceAddress;   // Sender Node ID (0x0001 = Gateway)
  uint16_t destAddress;     // Destination Node ID (0x0002 = Pole 2, 0xFFFF = Broadcast)
  uint8_t  feederId;        // Feeder index 1..6 (0 = All Feeders / Global Pole)
  uint8_t  command;         // Feeder command: e.g. PKT_CMD_FEEDER_SWITCH (0x20), PKT_CMD_EMERGENCY_SHUTDOWN (0x22)
  uint8_t  commandParam;    // 0 = OFF, 1 = ON, 2 = TOGGLE
  uint32_t sequenceNumber;  // Monotonic sequence number from ReplayProtection
  uint32_t expiryEpochSec;  // Absolute UTC Unix epoch seconds (NEVER millis()!)
  uint32_t keyEpoch;        // Key epoch
  uint8_t  hmacTag[MSCB_HMAC_TAG_BYTES]; // 256-bit HMAC tag computed over the 19 envelope bytes
};
#pragma pack(pop)

enum CommandAuthResult : uint8_t {
  AUTH_SUCCESS                 = 0,
  AUTH_ERR_NO_PROVISIONED_KEY  = 1, // Fail-Closed: No secure key provisioned
  AUTH_ERR_NETWORK_ID_MISMATCH = 2, // Packet belongs to different network
  AUTH_ERR_DEST_MISMATCH       = 3, // Packet not addressed to this node
  AUTH_ERR_KEY_EPOCH_MISMATCH  = 4, // Key epoch does not match provisioned epoch
  AUTH_ERR_COMMAND_EXPIRED     = 5, // Absolute expiry timestamp exceeded
  AUTH_ERR_REPLAY_DETECTED     = 6, // Sequence number already seen or outside sliding window
  AUTH_ERR_HMAC_INVALID        = 7, // Cryptographic signature mismatch
  AUTH_ERR_OVERFLOW_SATURATION = 8, // Sequence counter reached 32-bit limit
  AUTH_ERR_TIME_UNSYNCED       = 9  // Unsynchronized clock: real epoch unavailable
};

inline const char* commandAuthResultToString(CommandAuthResult res) {
  switch (res) {
    case AUTH_SUCCESS:                 return "AUTH_SUCCESS";
    case AUTH_ERR_NO_PROVISIONED_KEY:  return "NO_PROVISIONED_KEY_FAIL_CLOSED";
    case AUTH_ERR_NETWORK_ID_MISMATCH: return "NETWORK_ID_MISMATCH";
    case AUTH_ERR_DEST_MISMATCH:       return "DEST_ADDRESS_MISMATCH";
    case AUTH_ERR_KEY_EPOCH_MISMATCH:  return "KEY_EPOCH_MISMATCH";
    case AUTH_ERR_COMMAND_EXPIRED:     return "COMMAND_EXPIRED_TIMESTAMP";
    case AUTH_ERR_REPLAY_DETECTED:     return "REPLAY_DETECTED_OR_OUT_OF_WINDOW";
    case AUTH_ERR_HMAC_INVALID:        return "HMAC_INTEGRITY_MISMATCH";
    case AUTH_ERR_OVERFLOW_SATURATION: return "NONCE_COUNTER_SATURATED";
    case AUTH_ERR_TIME_UNSYNCED:       return "TIME_UNSYNCED_FAIL_CLOSED";
    default:                           return "UNKNOWN_AUTH_ERROR";
  }
}

class CommandSecurity {
public:
  CommandSecurity();

  // Secure Key Provisioning (Fails Closed if not provisioned)
  bool provisionKey(const uint8_t *keyBytes, size_t keyLen, uint32_t epoch);
  bool isKeyProvisioned() const { return keyProvisioned; }
  uint32_t getActiveKeyEpoch() const { return activeKeyEpoch; }
  void revokeKey(); // Clears key from memory, locks out all operations

  // Command Envelope Creation (Transmitter / Gateway)
  bool createCommand(
    uint16_t srcAddr,
    uint16_t dstAddr,
    uint8_t  feederId,
    uint8_t  cmd,
    uint8_t  cmdParam,
    uint32_t expiryEpochSec, // Absolute UTC Unix timestamp (NEVER millis()!)
    AuthenticatedCommandEnvelope &outEnvelope
  );

  // Command Envelope Verification (Receiver / Pole)
  CommandAuthResult verifyCommand(
    const AuthenticatedCommandEnvelope &envelope,
    uint16_t localNodeId,
    uint32_t currentEpochSec // Absolute UTC Unix timestamp (NEVER millis()!)
  );

  // Authenticated Time Synchronization
  bool createTimeSyncPacket(uint32_t epochSec, PktTimeSyncPayload &outPkt);
  bool verifyTimeSyncPacket(const PktTimeSyncPayload &pkt);

  // Authenticated Revocation Envelope (Gateway / Pole)
  bool createRevokeCommand(
    uint16_t targetPoleId,
    const uint8_t mac[6],
    const char* txId,
    const char* reason,
    uint32_t currentEpochSec,
    uint32_t expiryEpochSec,
    PktRevokeCmdPayload &outPkt
  );
  CommandAuthResult verifyRevokeCommand(
    const PktRevokeCmdPayload &pkt,
    uint16_t localNodeId,
    const uint8_t localMac[6],
    uint32_t currentEpochSec
  );

  // Non-secret key fingerprint (e.g. CRC16 hex, NEVER prints raw key bytes!)
  void getKeyFingerprint(char* outBuf, size_t maxLen) const;

  // Cryptographic HMAC-SHA256
  static void computeHmacSha256(
    const uint8_t *key, size_t keyLen,
    const uint8_t *data, size_t dataLen,
    uint8_t outHmac[MSCB_HMAC_TAG_BYTES]
  );

  // Constant-time memory comparison to mitigate timing attacks
  static bool constantTimeCompare(const uint8_t *a, const uint8_t *b, size_t len);

private:
  uint8_t  provisionedKey[MSCB_KEY_LEN_BYTES];
  uint32_t activeKeyEpoch;
  bool     keyProvisioned;
};

extern CommandSecurity cmdSecurity;

#endif // MSCB_COMMAND_SECURITY_H
