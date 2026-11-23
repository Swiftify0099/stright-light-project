/**
 * @file PoleTimeManager.cpp
 * @brief MSCB Smart Pole Node - Authenticated Mesh Time Synchronization Implementation
 * @version 1.0.0
 */

#include "PoleTimeManager.h"
#include <stdio.h>

PoleTimeManager poleTimeMgr;

PoleTimeManager::PoleTimeManager() :
  epochAnchorSec(0),
  localAnchorMs(0),
  lastSequenceNumber(0),
  synced(false)
{
}

void PoleTimeManager::begin() {
  // Safe initial state: Clock is unsynchronized until authenticated frame arrives
  epochAnchorSec = 0;
  localAnchorMs = millis();
  lastSequenceNumber = 0;
  synced = false;
}

bool PoleTimeManager::processTimeSyncPayload(const PktTimeSyncPayload &pkt) {
  // 1. Cryptographic validation: Verify HMAC, network ID, gateway ID, and epoch threshold
  if (!cmdSecurity.verifyTimeSyncPacket(pkt)) {
    #if defined(ARDUINO)
    Serial.println(F("[TIME_SYNC] ❌ Time sync rejected: Invalid HMAC or unauthenticated packet."));
    #endif
    return false;
  }

  // 2. Anti-replay and negative time jump guard
  if (synced && pkt.sequenceNumber <= lastSequenceNumber) {
    #if defined(ARDUINO)
    Serial.printf("[TIME_SYNC] ⚠️ Time sync ignored: Replay or stale sequence %lu <= %lu\n",
                  (unsigned long)pkt.sequenceNumber, (unsigned long)lastSequenceNumber);
    #endif
    return false;
  }

  // 3. Anchor real UTC epoch with local monotonic time
  epochAnchorSec = pkt.epochSec;
  localAnchorMs = millis();
  lastSequenceNumber = pkt.sequenceNumber;
  synced = true;

  #if defined(ARDUINO)
  Serial.printf("[TIME_SYNC] ✅ Authenticated mesh time synced! UTC Epoch: %lu | Seq: %lu\n",
                (unsigned long)epochAnchorSec, (unsigned long)lastSequenceNumber);
  #endif
  return true;
}

bool PoleTimeManager::isTimeSynced() const {
  if (!synced) return false;
  if (epochAnchorSec < 1700000000UL) return false;
  // Check anchor freshness
  if (millis() - localAnchorMs > POLE_MAX_TIME_SYNC_AGE_MS) {
    return false;
  }
  return true;
}

uint32_t PoleTimeManager::getCurrentEpoch() const {
  if (!isTimeSynced()) {
    return 0;
  }
  uint32_t elapsedSec = (millis() - localAnchorMs) / 1000UL;
  return epochAnchorSec + elapsedSec;
}

void PoleTimeManager::invalidateSync() {
  synced = false;
  epochAnchorSec = 0;
}
