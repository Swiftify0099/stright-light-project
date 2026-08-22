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
//  FEEDER DATA MODEL (POLE HARDWARE)
// ─────────────────────────────────────────────────────────────
struct FeederHardwareState {
  bool     relayOn;
  bool     mcbR;
  bool     mcbY;
  bool     mcbB;
  bool     contactor;
  uint8_t  mcbRDebounceCount;
  uint8_t  mcbYDebounceCount;
  uint8_t  mcbBDebounceCount;
  String   faultState;
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
};

// ─────────────────────────────────────────────────────────────
//  POLE ROUTING TABLE ENTRY
// ─────────────────────────────────────────────────────────────
struct PoleRouteEntry {
  String   destination;
  String   nextHop;
  int      hopCount;
  int      rssi;
  unsigned long lastSeen;
  bool     valid;
};

// ─────────────────────────────────────────────────────────────
//  PACKET DEDUPLICATION CACHE ENTRY
// ─────────────────────────────────────────────────────────────
struct PoleSeqCacheEntry {
  String   src;
  uint16_t seq;
};

#endif // TYPES_H
