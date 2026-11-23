/**
 * @file PoleTimeManager.h
 * @brief MSCB Smart Pole Node - Authenticated Mesh Time Synchronization & Epoch Derivation
 * @version 1.0.0
 * 
 * Strict Embedded Architecture:
 * 1. Zero unauthenticated clock changes: Validates HMAC-SHA256 and key epoch via CommandSecurity.
 * 2. Monotonic local derivation: Uses monotonic millis() elapsed from trusted UTC epoch anchor.
 * 3. Fail-Closed on sync loss: On reboot or stale anchor, isTimeSynced() returns false.
 */

#ifndef POLE_TIME_MANAGER_H
#define POLE_TIME_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#if __has_include("../MSCBShared/Protocol.h")
  #include "../MSCBShared/Protocol.h"
  #include "../MSCBShared/CommandSecurity.h"
#elif __has_include(<Protocol.h>)
  #include <Protocol.h>
  #include <CommandSecurity.h>
#else
  #include "Protocol.h"
  #include "CommandSecurity.h"
#endif

#define POLE_MAX_TIME_SYNC_AGE_MS 86400000UL // 24 hours max before re-sync required

class PoleTimeManager {
public:
  PoleTimeManager();

  void begin();

  // Process authenticated time sync packet received from Gateway
  bool processTimeSyncPayload(const PktTimeSyncPayload &pkt);

  // Status and epoch access
  bool isTimeSynced() const;
  uint32_t getCurrentEpoch() const;
  uint32_t getLastSyncLocalMs() const { return localAnchorMs; }
  void invalidateSync();

private:
  uint32_t epochAnchorSec;
  uint32_t localAnchorMs;
  uint32_t lastSequenceNumber;
  bool     synced;
};

extern PoleTimeManager poleTimeMgr;

#endif // POLE_TIME_MANAGER_H
