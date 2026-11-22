/*
 * =====================================================================
 *  DISCOVERYMANAGER.H — GATEWAY AUTOMATIC NODE DISCOVERY & LINK QUALITY
 *  Smart Street Light / Farmer Power Distribution Mesh System
 *  Platform : ESP32 + SX1278 RA-02 433 MHz + WiFi + Telegram
 *  Role     : Root Rank 0 Coordinator, Topology Aggregator, Airtime Tracker
 * =====================================================================
 */

#ifndef DISCOVERY_MANAGER_H
#define DISCOVERY_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"
#include "DiscoveryTypes.h"

// Aggregated Pole Link Quality record on Gateway
struct NodeLqRecord {
  char     nodeId[16];
  char     primaryParent[16];
  uint16_t primaryScore;
  char     backupParent[16];
  uint16_t backupScore;
  uint8_t  neighborCount;
  uint32_t airtimeUsedMs;
  float    dutyCyclePercent;
  unsigned long lastReportMs;
  bool     valid;
};

class DiscoveryManager {
public:
  DiscoveryManager();
  void init();

  // Cooperative scheduler update
  void update();

  // LoRa packet processing
  void processDiscoveryPacket(const String &type, const String &src, const String &dst,
                              const String &data, int16_t rssi, int8_t snr);
  void onPacketReceived(const String &src, int16_t rssi, int8_t snr);

  // Gateway root advertisement & responses
  void sendRootAdvertisement();
  void sendNeighborResponse(const char* targetNode, int16_t rxRssi, int8_t rxSnr);

  // Direct radio neighbor queries
  int  getDirectNeighborCount() const;
  NeighborTableEntry* getDirectNeighbor(int index);
  NeighborTableEntry* findDirectNeighbor(const char* nodeId);

  // Network topology aggregations from LQ_REPORT
  void updateNodeLqRecord(const char* nodeId, const char* primary, uint16_t priScore,
                          const char* backup, uint16_t bckScore, uint8_t nbrCount,
                          uint32_t airtimeMs, float dutyCycle);
  const NodeLqRecord* getNodeLqRecord(const char* nodeId) const;

  // Revocation management
  void revokeNode(const char* nodeId);
  void unrevokeNode(const char* nodeId);
  bool isNodeRevoked(const char* nodeId) const;

  // Airtime budget & duty cycle (1%)
  bool canTransmit(uint32_t airtimeMs) const;
  void recordAirtimeUsage(uint32_t airtimeMs);
  uint32_t getAirtimeUsedThisHour() const;
  float getDutyCyclePercent() const;

  // Diagnostics & Telegram dashboard summaries
  void getDiagnostics(DiscoveryDiagnostics &diag) const;
  String buildDiagnosticSummary() const;
  String buildTelegramTopologySummary() const;

private:
  int  findOrAllocateDirectNeighbor(const char* nodeId);
  void updateDirectNeighborRecord(int index, const DiscoveryPayload &payload, int16_t rssi, int8_t snr);
  void expireDirectNeighbors();

  // Duplicate suppression
  bool isDuplicateAdvertisement(const char* nodeId, uint32_t authTag);
  void recordAdvertisementSeen(const char* nodeId, uint32_t authTag);

  // Gateway direct neighbor table
  NeighborTableEntry directNeighbors[MAX_GW_NEIGHBORS_TABLE];
  int directNeighborCount;

  // Aggregated network node LQ records
  NodeLqRecord nodeLqRecords[MAX_NODES];

  // Revocation list
  char revokedNodes[8][16];
  uint8_t revokedCount;

  // Scheduler & Airtime state
  unsigned long tNextRootAdv;
  unsigned long tLastAdvSent;
  unsigned long tAirtimeWindowStart;
  uint32_t      airtimeUsedInWindow;
  uint32_t      discoveryFailures;

  // Duplicate cache
  struct SeenCacheEntry {
    char     nodeId[16];
    uint32_t authTag;
    unsigned long seenMs;
  } seenCache[16];
  uint8_t seenCacheHead;
};

extern DiscoveryManager discoveryMgr;

#endif // DISCOVERY_MANAGER_H
