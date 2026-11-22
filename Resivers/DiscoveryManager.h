/*
 * =====================================================================
 *  DISCOVERYMANAGER.H — FIELD POLE AUTOMATIC NODE DISCOVERY & LINK QUALITY
 *  Smart Street Light / Farmer Power Distribution Mesh System
 *  Platform : ESP32 + SX1278 RA-02 433 MHz
 *  Standards: Bounded Table, EWMA, Airtime Budgeting, Path Diversity
 * =====================================================================
 */

#ifndef DISCOVERY_MANAGER_H
#define DISCOVERY_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"
#include "DiscoveryTypes.h"

class DiscoveryManager {
public:
  DiscoveryManager();
  void init(uint8_t initialRank = 255);

  // Cooperative loop task
  void update();

  // Packet ingestion & dispatch
  void processDiscoveryPacket(const String &type, const String &src, const String &dst,
                              const String &data, int16_t rssi, int8_t snr);
  void onPacketReceived(const String &src, int16_t rssi, int8_t snr);

  // Neighbor table queries
  int  getNeighborCount() const;
  int  getValidNeighborCount() const;
  NeighborTableEntry* getNeighbor(int index);
  NeighborTableEntry* findNeighbor(const char* nodeId);

  // Candidate inspection
  const NeighborTableEntry* getPrimaryParentCandidate() const;
  const NeighborTableEntry* getBackupParentCandidate() const;
  bool  hasValidPrimaryCandidate() const;

  // Beacon transmission triggers
  void sendDiscoveryBeacon();
  void sendNeighborAdvertisement(const char* targetDst = BROADCAST_ID);
  void sendNeighborResponse(const char* targetNode, int16_t rxRssi, int8_t rxSnr);
  void sendLinkQualityReport();

  // Airtime budget & duty cycle
  bool canTransmit(uint32_t airtimeMs) const;
  void recordAirtimeUsage(uint32_t airtimeMs);
  uint32_t getAirtimeUsedThisHour() const;
  float getDutyCyclePercent() const;

  // Trust & Revocation management
  void revokeNode(const char* nodeId);
  void unrevokeNode(const char* nodeId);
  bool isNodeRevoked(const char* nodeId) const;

  // Diagnostics & Reporting
  void getDiagnostics(DiscoveryDiagnostics &diag) const;
  String buildDiagnosticSummary() const;
  String buildNeighborTableDump() const;

  // Link scoring configuration
  void setWeights(const LinkQualityWeights &w);
  const LinkQualityWeights& getWeights() const;

  // Rank & state updates
  void setNetworkRank(uint8_t rank);
  uint8_t getNetworkRank() const;
  void setForwardingCapacity(uint8_t capacity);
  uint8_t getForwardingCapacity() const;
  void setUplinkParent(const char* parentId);
  const char* getUplinkParent() const;

private:
  // Internal neighbor management
  int  findOrAllocateNeighbor(const char* nodeId);
  void updateNeighborRecord(int index, const DiscoveryPayload &payload, int16_t rssi, int8_t snr);
  void expireNeighborEntries();
  void evaluateParentCandidates();
  uint16_t calculateLinkQualityScore(NeighborTableEntry &e);

  // Duplicate suppression
  bool isDuplicateAdvertisement(const char* nodeId, uint32_t authTag);
  void recordAdvertisementSeen(const char* nodeId, uint32_t authTag);

  // Beacon scheduler
  void scheduleNextBeacon();
  unsigned long calculateJitteredInterval(unsigned long baseInterval);

  // Member storage
  NeighborTableEntry neighborTable[MAX_NEIGHBORS_TABLE];
  int  neighborCount;

  // Revocation list (bounded)
  char revokedNodes[4][16];
  uint8_t revokedCount;

  // Link quality weights
  LinkQualityWeights weights;

  // Node runtime state
  uint8_t  myNetworkRank;
  uint8_t  myForwardingCapacity;
  char     myUplinkParentId[16];

  // Scheduler state
  unsigned long tNextBeacon;
  unsigned long tLastBeaconSent;
  unsigned long tLastLqReportSent;
  unsigned long tAirtimeWindowStart;
  uint32_t      airtimeUsedInWindow;
  uint32_t      discoveryFailures;

  // Duplicate suppression cache (ring buffer)
  struct SeenCacheEntry {
    char     nodeId[16];
    uint32_t authTag;
    unsigned long seenMs;
  } seenCache[8];
  uint8_t seenCacheHead;

  // Hysteresis tracking
  char lastPrimaryId[16];
  uint8_t hysteresisHoldCount;
};

extern DiscoveryManager discoveryMgr;

#endif // DISCOVERY_MANAGER_H
