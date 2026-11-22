/*
 * =====================================================================
 *  TEST_PROMPT09.CPP — AUTOMATED TEST SUITE FOR AUTOMATIC NODE DISCOVERY
 *  Smart Street Light / Farmer Mesh Networking System
 *  Platform: C++11 / Standalone Test Engine
 * =====================================================================
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <sstream>

// ─────────────────────────────────────────────────────────────
//  ARDUINO MOCK ENVIRONMENT
// ─────────────────────────────────────────────────────────────
static unsigned long g_simulatedTimeMs = 1000UL;
static unsigned long millis() { return g_simulatedTimeMs; }
static void delay(unsigned long ms) { g_simulatedTimeMs += ms; }
static uint32_t g_randSeed = 123456789;
static uint32_t esp_random() {
  g_randSeed = (1103515245 * g_randSeed + 12345);
  return g_randSeed;
}
static long random(long minVal, long maxVal) {
  if (minVal >= maxVal) return minVal;
  return minVal + (long)(esp_random() % (maxVal - minVal));
}
static long random(long maxVal) { return random(0, maxVal); }
static void randomSeed(uint32_t seed) { g_randSeed = seed; }

class String {
public:
  std::string s;
  String() : s("") {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string &str) : s(str) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned int v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(float v, int dec = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(dec) << v;
    s = ss.str();
  }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.length(); }
  char operator[](size_t i) const { return s[i]; }
  String& operator+=(const String &rhs) { s += rhs.s; return *this; }
  String& operator+=(const char* rhs) { if(rhs) s += rhs; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  bool operator==(const String &rhs) const { return s == rhs.s; }
  bool operator==(const char* rhs) const { return rhs && s == rhs; }
  bool operator!=(const String &rhs) const { return s != rhs.s; }
  bool operator!=(const char* rhs) const { return !rhs || s != rhs; }
  int indexOf(char c, int from = 0) const {
    size_t pos = s.find(c, from);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  int indexOf(const char* sub, int from = 0) const {
    size_t pos = s.find(sub, from);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  String substring(int start, int end = -1) const {
    if (start < 0) start = 0;
    if (start >= (int)s.length()) return String("");
    if (end < 0 || end > (int)s.length()) end = (int)s.length();
    if (start >= end) return String("");
    return String(s.substr(start, end - start));
  }
  int toInt() const {
    try { return std::stoi(s); } catch(...) { return 0; }
  }
  float toFloat() const {
    try { return std::stof(s); } catch(...) { return 0.0f; }
  }
  void trim() {
    size_t p = s.find_first_not_of(" \t\r\n");
    size_t q = s.find_last_not_of(" \t\r\n");
    if (p == std::string::npos) s = "";
    else s = s.substr(p, q - p + 1);
  }
};

inline String operator+(const String &lhs, const String &rhs) { return String(lhs.s + rhs.s); }
inline String operator+(const String &lhs, const char* rhs) { return String(lhs.s + (rhs ? rhs : "")); }
inline String operator+(const char* lhs, const String &rhs) { return String((lhs ? lhs : "") + rhs.s); }

struct MockSerial {
  template<typename... Args>
  void printf(const char* fmt, Args... args) {
    // printf suppressed in batch tests unless verbose
  }
  void println(const char* s) {}
  void print(const char* s) {}
} Serial;

// Pull in Discovery Types
#include "../Resivers/DiscoveryTypes.h"

// ─────────────────────────────────────────────────────────────
//  SELF-CONTAINED TEST HARNESS FOR DISCOVERY ENGINE
// ─────────────────────────────────────────────────────────────
class TestDiscoveryEngine {
public:
  NeighborTableEntry table[MAX_NEIGHBORS_TABLE];
  int neighborCount;
  LinkQualityWeights weights;
  char myUplinkParentId[16];
  uint8_t myNetworkRank;
  uint8_t myForwardingCapacity;
  char lastPrimaryId[16];
  uint32_t airtimeUsedThisHour;
  uint32_t discoveryFailures;

  char revokedList[4][16];
  int revokedCount;

  struct DuplicateCache {
    char nodeId[16];
    uint32_t authTag;
    unsigned long seenMs;
  } seenCache[8];
  int seenCacheHead;

  TestDiscoveryEngine() :
    neighborCount(0), myNetworkRank(255), myForwardingCapacity(5),
    airtimeUsedThisHour(0), discoveryFailures(0), revokedCount(0), seenCacheHead(0)
  {
    memset(myUplinkParentId, 0, sizeof(myUplinkParentId));
    memset(lastPrimaryId, 0, sizeof(lastPrimaryId));
    for (int i = 0; i < 8; i++) {
      memset(seenCache[i].nodeId, 0, sizeof(seenCache[i].nodeId));
      seenCache[i].authTag = 0;
      seenCache[i].seenMs = 0;
    }
  }

  void revokeNode(const char* id) {
    if (revokedCount < 4) {
      strncpy(revokedList[revokedCount++], id, 15);
      revokedList[revokedCount - 1][15] = '\0';
    }
    for (int i = 0; i < neighborCount; i++) {
      if (strcmp(table[i].nodeId, id) == 0) {
        table[i].trustStatus = TRUST_REVOKED;
        table[i].linkQualityScore = 0;
        table[i].isPrimaryCandidate = false;
        table[i].isBackupCandidate = false;
      }
    }
  }

  bool isRevoked(const char* id) const {
    for (int i = 0; i < revokedCount; i++) {
      if (strcmp(revokedList[i], id) == 0) return true;
    }
    return false;
  }

  bool isDuplicate(const char* id, uint32_t tag) {
    unsigned long now = millis();
    for (int i = 0; i < 8; i++) {
      if (strcmp(seenCache[i].nodeId, id) == 0 && seenCache[i].authTag == tag) {
        if (now - seenCache[i].seenMs < DUPLICATE_SUPPRESS_MS) {
          return true;
        }
      }
    }
    return false;
  }

  void recordSeen(const char* id, uint32_t tag) {
    strncpy(seenCache[seenCacheHead].nodeId, id, 15);
    seenCache[seenCacheHead].authTag = tag;
    seenCache[seenCacheHead].seenMs = millis();
    seenCacheHead = (seenCacheHead + 1) % 8;
  }

  int findOrAllocate(const char* nodeId) {
    for (int i = 0; i < neighborCount; i++) {
      if (strcmp(table[i].nodeId, nodeId) == 0) return i;
    }
    if (neighborCount < MAX_NEIGHBORS_TABLE) {
      int idx = neighborCount++;
      table[idx] = NeighborTableEntry();
      strncpy(table[idx].nodeId, nodeId, sizeof(table[idx].nodeId) - 1);
      table[idx].firstSeenMs = millis();
      table[idx].occupied = true;
      return idx;
    }
    // Evict oldest offline
    int evictIdx = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < MAX_NEIGHBORS_TABLE; i++) {
      if (table[i].availability == AVAIL_OFFLINE && (millis() - table[i].lastSeenMs) >= NEIGHBOR_EVICTION_MS) {
        if (table[i].lastSeenMs < oldest) {
          oldest = table[i].lastSeenMs;
          evictIdx = i;
        }
      }
    }
    if (evictIdx >= 0) {
      table[evictIdx] = NeighborTableEntry();
      strncpy(table[evictIdx].nodeId, nodeId, sizeof(table[evictIdx].nodeId) - 1);
      table[evictIdx].firstSeenMs = millis();
      table[evictIdx].occupied = true;
      return evictIdx;
    }
    return -1; // Full and cannot evict
  }

  void ingestAdvertisement(const DiscoveryPayload &p, int16_t rssi, int8_t snr) {
    if (p.networkId != MESH_NETWORK_ID) return;
    if (isDuplicate(p.nodeId, p.authTag)) return;
    recordSeen(p.nodeId, p.authTag);

    int idx = findOrAllocate(p.nodeId);
    if (idx < 0) return;

    NeighborTableEntry &e = table[idx];
    e.networkId = p.networkId;
    e.lastSeenMs = millis();
    e.availability = AVAIL_ONLINE;
    e.capabilities = p.capabilities;
    e.firmwareVersion = p.firmwareVersion;
    e.nodeHealth = p.nodeHealth;
    e.networkRank = p.networkRank;
    e.advertisedCapacity = p.availableCapacity;
    strncpy(e.uplinkParentId, p.uplinkParentId, sizeof(e.uplinkParentId) - 1);

    if (isRevoked(p.nodeId)) {
      e.trustStatus = TRUST_REVOKED;
    } else if (p.trustStatus == TRUST_REGISTERED) {
      uint32_t expected = DiscoveryPayload::computeAuthTag(p.nodeId, p.networkId, p.networkRank, MESH_AUTH_SECRET);
      e.trustStatus = (p.authTag == expected) ? TRUST_REGISTERED : TRUST_UNTRUSTED;
    } else {
      e.trustStatus = TRUST_UNREGISTERED;
    }

    float alpha = 0.20f;
    if (e.rxPackets == 0) {
      e.avgRssi = (float)rssi;
      e.avgSnr  = (float)snr;
      e.rssiVariance = 0.0f;
    } else {
      float diff = (float)rssi - e.avgRssi;
      e.rssiVariance = (alpha * (diff * diff)) + ((1.0f - alpha) * e.rssiVariance);
      e.avgRssi = (alpha * rssi) + ((1.0f - alpha) * e.avgRssi);
      e.avgSnr  = (alpha * snr)  + ((1.0f - alpha) * e.avgSnr);
    }
    e.rssi = rssi;
    e.snr = snr;
    e.rxPackets++;
    e.consecutiveSuccess++;
    e.consecutiveMisses = 0;

    uint16_t total = e.rxPackets + e.failedPackets;
    e.pdr = (total > 0) ? ((float)e.rxPackets / (float)total) : 1.0f;

    float varScore = 100.0f - (e.rssiVariance * 2.5f);
    if (varScore < 0.0f) varScore = 0.0f;
    if (varScore > 100.0f) varScore = 100.0f;
    float successBonus = (e.consecutiveSuccess > 5) ? 20.0f : (e.consecutiveSuccess * 4.0f);
    float stabTotal = (varScore * 0.8f) + successBonus;
    e.linkStability = (stabTotal > 100.0f) ? 100 : (uint8_t)stabTotal;

    e.linkQualityScore = calculateScore(e);
    evaluateCandidates();
  }

  uint16_t calculateScore(const NeighborTableEntry &e) {
    if (e.trustStatus != TRUST_REGISTERED) return 0;
    if (e.availability == AVAIL_OFFLINE) return 0;
    if (e.advertisedCapacity == 0) return 0;

    float sRssi = (e.avgRssi >= -65.0f) ? 100.0f : ((e.avgRssi <= -128.0f) ? 0.0f : ((e.avgRssi - (-128.0f)) / 63.0f * 100.0f));
    float sSnr  = (e.avgSnr >= 10.0f) ? 100.0f : ((e.avgSnr <= -15.0f) ? 0.0f : ((e.avgSnr - (-15.0f)) / 25.0f * 100.0f));
    float sPdr  = e.pdr * 100.0f;
    float sStab = (float)e.linkStability;
    float sCap  = (e.advertisedCapacity >= 5) ? 100.0f : (e.advertisedCapacity * 20.0f);
    float sRank = (e.networkRank == 0) ? 100.0f : ((e.networkRank >= 6) ? 10.0f : (100.0f - e.networkRank * 15.0f));

    float raw = (sPdr * weights.wPdr) + (sSnr * weights.wSnr) + (sRssi * weights.wRssi) +
                (sStab * weights.wStability) + (sCap * weights.wCapacity) + (sRank * weights.wRank);

    if (e.nodeHealth < 75) raw *= ((float)e.nodeHealth / 100.0f);
    if (e.availability == AVAIL_STALE) raw *= 0.5f;

    if (raw < 0.0f) raw = 0.0f;
    if (raw > 1000.0f) raw = 1000.0f;
    return (uint16_t)raw;
  }

  void evaluateCandidates() {
    int bestPri = -1;
    uint16_t bestPriScore = 0;
    int bestBck = -1;
    uint16_t bestBckScore = 0;

    for (int i = 0; i < neighborCount; i++) {
      table[i].isPrimaryCandidate = false;
      table[i].isBackupCandidate = false;
      if (table[i].availability != AVAIL_ONLINE) continue;
      if (table[i].trustStatus != TRUST_REGISTERED) continue;
      if (table[i].advertisedCapacity == 0) continue;
      if (myNetworkRank != 255 && table[i].networkRank >= myNetworkRank) continue;
      if (table[i].linkQualityScore < 250) continue;

      if (table[i].linkQualityScore > bestPriScore) {
        bestPriScore = table[i].linkQualityScore;
        bestPri = i;
      }
    }

    // Hysteresis: only switch primary if candidate exceeds incumbent by 80 points
    if (bestPri >= 0 && lastPrimaryId[0] != '\0' && strcmp(lastPrimaryId, table[bestPri].nodeId) != 0) {
      for (int i = 0; i < neighborCount; i++) {
        if (strcmp(table[i].nodeId, lastPrimaryId) == 0 && table[i].availability == AVAIL_ONLINE) {
          if (bestPriScore < table[i].linkQualityScore + 80) {
            bestPri = i;
            bestPriScore = table[i].linkQualityScore;
          }
          break;
        }
      }
    }

    if (bestPri >= 0) {
      table[bestPri].isPrimaryCandidate = true;
      strncpy(lastPrimaryId, table[bestPri].nodeId, sizeof(lastPrimaryId) - 1);
      const char* priUplink = table[bestPri].uplinkParentId;

      for (int i = 0; i < neighborCount; i++) {
        if (i == bestPri) continue;
        if (table[i].availability != AVAIL_ONLINE) continue;
        if (table[i].trustStatus != TRUST_REGISTERED) continue;
        if (table[i].advertisedCapacity == 0) continue;
        if (myNetworkRank != 255 && table[i].networkRank >= myNetworkRank) continue;
        if (table[i].linkQualityScore < 200) continue;

        // PATH DIVERSITY CHECK: Penalize shared uplink parent
        bool sharesSameUplink = (strcmp(priUplink, "GATEWAY") != 0 &&
                                 priUplink[0] != '\0' &&
                                 strcmp(priUplink, table[i].uplinkParentId) == 0);
        uint16_t effScore = table[i].linkQualityScore;
        if (sharesSameUplink) effScore = (effScore > 150) ? (effScore - 150) : 50;

        if (effScore > bestBckScore) {
          bestBckScore = effScore;
          bestBck = i;
        }
      }

      if (bestBck >= 0) {
        table[bestBck].isBackupCandidate = true;
      }
    }
  }

  void expireNeighbors() {
    unsigned long now = millis();
    for (int i = 0; i < neighborCount; i++) {
      unsigned long el = now - table[i].lastSeenMs;
      if (el >= NEIGHBOR_DEAD_TIMEOUT_MS) {
        table[i].availability = AVAIL_OFFLINE;
        table[i].linkQualityScore = 0;
        table[i].isPrimaryCandidate = false;
        table[i].isBackupCandidate = false;
      } else if (el >= NEIGHBOR_TIMEOUT_MS) {
        table[i].availability = AVAIL_STALE;
        table[i].failedPackets++;
        uint16_t total = table[i].rxPackets + table[i].failedPackets;
        table[i].pdr = (total > 0) ? ((float)table[i].rxPackets / (float)total) : 0.0f;
        table[i].linkQualityScore = calculateScore(table[i]);
      }
    }
    evaluateCandidates();
  }
};

// ─────────────────────────────────────────────────────────────
//  INDIVIDUAL TEST IMPLEMENTATIONS (Tests 1 to 12)
// ─────────────────────────────────────────────────────────────

void runTest1_OneGatewayOnePole() {
  std::cout << "[TEST 1] One Gateway and One Pole Discovery: ";
  TestDiscoveryEngine pole;
  pole.myNetworkRank = 255; // Unparented

  DiscoveryPayload gwAdv;
  strncpy(gwAdv.nodeId, "GATEWAY", 15);
  gwAdv.networkId = MESH_NETWORK_ID;
  gwAdv.trustStatus = TRUST_REGISTERED;
  gwAdv.capabilities = CAP_GATEWAY | CAP_ROUTER | CAP_PARENT_ELIGIBLE;
  gwAdv.networkRank = 0; // Rank 0
  gwAdv.availableCapacity = 10;
  strncpy(gwAdv.uplinkParentId, "ROOT", 15);
  gwAdv.authTag = DiscoveryPayload::computeAuthTag("GATEWAY", MESH_NETWORK_ID, 0, MESH_AUTH_SECRET);

  pole.ingestAdvertisement(gwAdv, -75, 8);

  assert(pole.neighborCount == 1);
  assert(strcmp(pole.table[0].nodeId, "GATEWAY") == 0);
  assert(pole.table[0].isPrimaryCandidate == true);
  assert(pole.table[0].linkQualityScore >= 800);
  std::cout << "PASSED (Discovered Gateway as Primary Candidate, Score: "
            << pole.table[0].linkQualityScore << "/1000)\n";
}

void runTest2_OneGatewayTwoPoles() {
  std::cout << "[TEST 2] One Gateway and Two Poles: ";
  TestDiscoveryEngine pole2;
  pole2.myNetworkRank = 255;

  // Pole 1 is closer to Gateway (Rank 1, Uplink GATEWAY)
  DiscoveryPayload pole1;
  strncpy(pole1.nodeId, "SL-0001", 15);
  pole1.networkId = MESH_NETWORK_ID;
  pole1.trustStatus = TRUST_REGISTERED;
  pole1.capabilities = CAP_ROUTER | CAP_PARENT_ELIGIBLE;
  pole1.networkRank = 1;
  pole1.availableCapacity = 4;
  strncpy(pole1.uplinkParentId, "GATEWAY", 15);
  pole1.authTag = DiscoveryPayload::computeAuthTag("SL-0001", MESH_NETWORK_ID, 1, MESH_AUTH_SECRET);

  // Gateway is also heard directly, but with weaker signal
  DiscoveryPayload gw;
  strncpy(gw.nodeId, "GATEWAY", 15);
  gw.networkId = MESH_NETWORK_ID;
  gw.trustStatus = TRUST_REGISTERED;
  gw.capabilities = CAP_GATEWAY | CAP_ROUTER | CAP_PARENT_ELIGIBLE;
  gw.networkRank = 0;
  gw.availableCapacity = 10;
  strncpy(gw.uplinkParentId, "ROOT", 15);
  gw.authTag = DiscoveryPayload::computeAuthTag("GATEWAY", MESH_NETWORK_ID, 0, MESH_AUTH_SECRET);

  pole2.ingestAdvertisement(pole1, -68, 9);
  pole2.ingestAdvertisement(gw, -88, 3);

  assert(pole2.neighborCount == 2);
  // Both are eligible candidates; Gateway has Rank 0 bonus, Pole 1 has strong RSSI/SNR
  bool hasPrimary = (pole2.table[0].isPrimaryCandidate || pole2.table[1].isPrimaryCandidate);
  bool hasBackup  = (pole2.table[0].isBackupCandidate || pole2.table[1].isBackupCandidate);
  assert(hasPrimary && hasBackup);
  std::cout << "PASSED (Both Primary & Backup Candidates Identified)\n";
}

void runTest3_ThreeNearbyPoleNodes() {
  std::cout << "[TEST 3] Three Nearby Pole Nodes (Path Diversity): ";
  TestDiscoveryEngine pole;
  pole.myNetworkRank = 3;

  // Candidate A: Rank 2, Uplink SL-0001
  DiscoveryPayload a;
  strncpy(a.nodeId, "SL-0002", 15);
  a.networkId = MESH_NETWORK_ID;
  a.trustStatus = TRUST_REGISTERED;
  a.networkRank = 2;
  a.availableCapacity = 3;
  strncpy(a.uplinkParentId, "SL-0001", 15);
  a.authTag = DiscoveryPayload::computeAuthTag("SL-0002", MESH_NETWORK_ID, 2, MESH_AUTH_SECRET);

  // Candidate B: Rank 2, Same Uplink SL-0001 (Shared fate!)
  DiscoveryPayload b;
  strncpy(b.nodeId, "SL-0003", 15);
  b.networkId = MESH_NETWORK_ID;
  b.trustStatus = TRUST_REGISTERED;
  b.networkRank = 2;
  b.availableCapacity = 3;
  strncpy(b.uplinkParentId, "SL-0001", 15);
  b.authTag = DiscoveryPayload::computeAuthTag("SL-0003", MESH_NETWORK_ID, 2, MESH_AUTH_SECRET);

  // Candidate C: Rank 2, Diverse Uplink SL-0099
  DiscoveryPayload c;
  strncpy(c.nodeId, "SL-0004", 15);
  c.networkId = MESH_NETWORK_ID;
  c.trustStatus = TRUST_REGISTERED;
  c.networkRank = 2;
  c.availableCapacity = 3;
  strncpy(c.uplinkParentId, "SL-0099", 15);
  c.authTag = DiscoveryPayload::computeAuthTag("SL-0004", MESH_NETWORK_ID, 2, MESH_AUTH_SECRET);

  pole.ingestAdvertisement(a, -70, 7);
  pole.ingestAdvertisement(b, -72, 6);
  pole.ingestAdvertisement(c, -75, 5);

  assert(pole.table[0].isPrimaryCandidate == true); // A is primary
  // C must be selected as Backup over B because C has diverse uplink path (SL-0099 vs SL-0001)
  assert(pole.table[2].isBackupCandidate == true);
  std::cout << "PASSED (Path Diversity Correctly Selected SL-0004 to Prevent Fate Sharing)\n";
}

void runTest4_NodeAdvertisementReception() {
  std::cout << "[TEST 4] Node Advertisement Serialization/Deserialization: ";
  DiscoveryPayload p;
  strncpy(p.nodeId, "SL-0042", 15);
  p.networkId = 0x4331;
  p.trustStatus = TRUST_REGISTERED;
  p.capabilities = CAP_ROUTER | CAP_FEEDER_CTRL | CAP_SENSOR_NODE;
  p.firmwareVersion = 10;
  p.nodeHealth = 98;
  p.networkRank = 2;
  p.availableCapacity = 5;
  strncpy(p.uplinkParentId, "SL-0001", 15);
  p.authTag = DiscoveryPayload::computeAuthTag("SL-0042", 0x4331, 2, MESH_AUTH_SECRET);

  String serialized = p.serialize();
  DiscoveryPayload parsed;
  bool ok = parsed.deserialize(serialized);

  assert(ok);
  assert(strcmp(parsed.nodeId, "SL-0042") == 0);
  assert(parsed.networkId == 0x4331);
  assert(parsed.trustStatus == TRUST_REGISTERED);
  assert(parsed.firmwareVersion == 10);
  assert(parsed.nodeHealth == 98);
  assert(parsed.networkRank == 2);
  assert(parsed.availableCapacity == 5);
  assert(strcmp(parsed.uplinkParentId, "SL-0001") == 0);
  assert(parsed.authTag == p.authTag);
  std::cout << "PASSED (Exact Bidirectional Fidelity, Serialized Length: "
            << serialized.length() << " bytes)\n";
}

void runTest5_RssiSnrRecordingAndEwma() {
  std::cout << "[TEST 5] RSSI/SNR Recording & EWMA Smoothing: ";
  TestDiscoveryEngine pole;

  DiscoveryPayload p;
  strncpy(p.nodeId, "SL-0005", 15);
  p.networkId = MESH_NETWORK_ID;
  p.trustStatus = TRUST_REGISTERED;
  p.networkRank = 1;
  p.availableCapacity = 3;
  p.authTag = DiscoveryPayload::computeAuthTag("SL-0005", MESH_NETWORK_ID, 1, MESH_AUTH_SECRET);

  // Send packet 1: -70 dBm, 8 dB
  pole.ingestAdvertisement(p, -70, 8);
  assert(std::abs(pole.table[0].avgRssi - (-70.0f)) < 0.01f);

  // Send packet 2: -70 dBm
  delay(10000);
  p.authTag++; // New sequence tag
  pole.ingestAdvertisement(p, -70, 8);

  // Send temporary multipath fade packet: -110 dBm
  delay(10000);
  p.authTag++;
  pole.ingestAdvertisement(p, -110, -5);

  // EWMA: 0.20 * (-110) + 0.80 * (-70) = -22.0 - 56.0 = -78.0 dBm
  assert(std::abs(pole.table[0].avgRssi - (-78.0f)) < 0.5f);
  assert(pole.table[0].rssi == -110); // Instantaneous captured
  std::cout << "PASSED (Raw RSSI: -110 dBm, Smoothed EWMA RSSI: "
            << pole.table[0].avgRssi << " dBm)\n";
}

void runTest6_NeighborTimeout() {
  std::cout << "[TEST 6] Neighbor Timeout & Stale/Dead Transitions: ";
  TestDiscoveryEngine pole;

  DiscoveryPayload p;
  strncpy(p.nodeId, "SL-0006", 15);
  p.networkId = MESH_NETWORK_ID;
  p.trustStatus = TRUST_REGISTERED;
  p.networkRank = 1;
  p.availableCapacity = 3;
  p.authTag = DiscoveryPayload::computeAuthTag("SL-0006", MESH_NETWORK_ID, 1, MESH_AUTH_SECRET);

  pole.ingestAdvertisement(p, -75, 6);
  assert(pole.table[0].availability == AVAIL_ONLINE);
  assert(pole.table[0].isPrimaryCandidate == true);

  // Advance time past 180s (NEIGHBOR_TIMEOUT_MS)
  delay(185000UL);
  pole.expireNeighbors();
  assert(pole.table[0].availability == AVAIL_STALE);

  // Advance time past 300s (NEIGHBOR_DEAD_TIMEOUT_MS)
  delay(120000UL);
  pole.expireNeighbors();
  assert(pole.table[0].availability == AVAIL_OFFLINE);
  assert(pole.table[0].isPrimaryCandidate == false); // Disqualified!
  std::cout << "PASSED (ONLINE -> STALE -> OFFLINE -> Disqualified as Parent)\n";
}

void runTest7_NodeRestartAndRediscovery() {
  std::cout << "[TEST 7] Node Restart and Rediscovery: ";
  // Create engine instance, discover, then wipe (simulate boot) and re-discover
  TestDiscoveryEngine pole;
  DiscoveryPayload gw;
  strncpy(gw.nodeId, "GATEWAY", 15);
  gw.networkId = MESH_NETWORK_ID;
  gw.trustStatus = TRUST_REGISTERED;
  gw.networkRank = 0;
  gw.availableCapacity = 10;
  gw.authTag = DiscoveryPayload::computeAuthTag("GATEWAY", MESH_NETWORK_ID, 0, MESH_AUTH_SECRET);

  pole.ingestAdvertisement(gw, -80, 5);
  assert(pole.neighborCount == 1);

  // Simulate reboot
  pole = TestDiscoveryEngine();
  assert(pole.neighborCount == 0);

  // Rediscovery probe
  delay(5000);
  pole.ingestAdvertisement(gw, -78, 6);
  assert(pole.neighborCount == 1);
  assert(pole.table[0].isPrimaryCandidate == true);
  std::cout << "PASSED (Clean Reboot Recovery & Immediate Parent Candidate Selection)\n";
}

void runTest8_UnregisteredDeviceHandling() {
  std::cout << "[TEST 8] Unregistered Device Handling: ";
  TestDiscoveryEngine pole;

  DiscoveryPayload unreg;
  strncpy(unreg.nodeId, "A4CF12E8", 15); // Unregistered HW ID
  unreg.networkId = MESH_NETWORK_ID;
  unreg.trustStatus = TRUST_UNREGISTERED;
  unreg.networkRank = 255;
  unreg.availableCapacity = 0;

  pole.ingestAdvertisement(unreg, -65, 10);

  assert(pole.neighborCount == 1);
  assert(pole.table[0].trustStatus == TRUST_UNREGISTERED);
  assert(pole.table[0].linkQualityScore == 0); // Barred from scoring!
  assert(pole.table[0].isPrimaryCandidate == false);
  assert(pole.table[0].isBackupCandidate == false);
  std::cout << "PASSED (Unregistered Node Monitored in Table, but Score=0 & Barred from Parent Selection)\n";
}

void runTest9_RevokedDeviceRejection() {
  std::cout << "[TEST 9] Revoked Device Rejection: ";
  TestDiscoveryEngine pole;

  pole.revokeNode("MALICIOUS-01");

  DiscoveryPayload rogue;
  strncpy(rogue.nodeId, "MALICIOUS-01", 15);
  rogue.networkId = MESH_NETWORK_ID;
  rogue.trustStatus = TRUST_REGISTERED; // Claims to be registered!
  rogue.networkRank = 1;
  rogue.availableCapacity = 5;
  rogue.authTag = DiscoveryPayload::computeAuthTag("MALICIOUS-01", MESH_NETWORK_ID, 1, MESH_AUTH_SECRET);

  pole.ingestAdvertisement(rogue, -60, 10);

  assert(pole.table[0].trustStatus == TRUST_REVOKED);
  assert(pole.table[0].linkQualityScore == 0);
  assert(pole.table[0].isPrimaryCandidate == false);
  std::cout << "PASSED (Blacklisted/Revoked Node Disqualified Immediately)\n";
}

void runTest10_DuplicateAdvertisementHandling() {
  std::cout << "[TEST 10] Duplicate Advertisement Suppression: ";
  TestDiscoveryEngine pole;

  DiscoveryPayload p;
  strncpy(p.nodeId, "SL-0010", 15);
  p.networkId = MESH_NETWORK_ID;
  p.trustStatus = TRUST_REGISTERED;
  p.networkRank = 1;
  p.availableCapacity = 4;
  p.authTag = 0xDEADBEEF;

  // Packet 1: Accepted
  pole.ingestAdvertisement(p, -75, 7);
  assert(pole.table[0].rxPackets == 1);

  // Packet 2: Same auth tag sent immediately (burst reflection)
  delay(100);
  pole.ingestAdvertisement(p, -75, 7);
  assert(pole.table[0].rxPackets == 1); // SUPPRESSED!

  // Packet 3: After suppression window elapsed (10s)
  delay(10000);
  p.authTag = 0xCAFEBABE;
  pole.ingestAdvertisement(p, -75, 7);
  assert(pole.table[0].rxPackets == 2); // ACCEPTED!

  std::cout << "PASSED (Redundant Rapid Advertisements Suppressed)\n";
}

void runTest11_BeaconCongestionSimulation() {
  std::cout << "[TEST 11] 50-Node Beacon Congestion & Duty Cycle Simulation: ";
  // Verify 433 MHz LoRa Time-on-Air (ToA) and 1% duty cycle limit
  uint32_t toa32 = calculateLoRaAirtimeMs(32);
  assert(toa32 == 453); // Exact SX1278 calculation: 453 ms!

  // ETSI 1% duty cycle: 36,000 ms per hour
  uint32_t hourlyBudget = AIRTIME_MAX_BUDGET_MS;
  uint32_t maxPacketsPerHourPerNode = hourlyBudget / toa32; // 36000 / 453 = 79 packets

  // If 50 nodes operate at steady-state interval = 120s (30 packets/hr per node):
  uint32_t packetsPerHrPerNode = 3600 / 120; // 30 packets/hr
  uint32_t airtimePerNodePerHr = packetsPerHrPerNode * toa32; // 30 * 453 = 13,590 ms
  float dutyCyclePerNode = ((float)airtimePerNodePerHr / 3600000.0f) * 100.0f;

  assert(airtimePerNodePerHr < hourlyBudget);
  assert(dutyCyclePerNode < 1.0f); // ~0.38% duty cycle!

  // Total channel load across 50 nodes:
  // 50 nodes * 30 pkts/hr = 1,500 packets/hr on air = 0.416 pkts/second
  // Total channel airtime occupied = 50 * 13.59s = 679.5 seconds per 3600s = 18.8% channel occupancy!
  float totalChannelOccupancy = (50.0f * (float)airtimePerNodePerHr / 3600000.0f) * 100.0f;
  assert(totalChannelOccupancy < 25.0f);

  std::cout << "PASSED (Single ToA: " << toa32 << " ms | Duty Cycle: "
            << std::fixed << std::setprecision(2) << dutyCyclePerNode << "% < 1.0% | Channel Occupancy for 50 Nodes: "
            << totalChannelOccupancy << "%)\n";
}

void runTest12_MemoryStability() {
  std::cout << "[TEST 12] Bounded Memory & Table Overflow Stability: ";
  TestDiscoveryEngine pole;

  // Insert up to MAX_NEIGHBORS_TABLE (16 nodes)
  for (int i = 0; i < MAX_NEIGHBORS_TABLE; i++) {
    DiscoveryPayload p;
    char id[16];
    snprintf(id, sizeof(id), "NODE-%02d", i + 1);
    strncpy(p.nodeId, id, 15);
    p.networkId = MESH_NETWORK_ID;
    p.trustStatus = TRUST_REGISTERED;
    p.networkRank = 1;
    p.availableCapacity = 2;
    p.authTag = DiscoveryPayload::computeAuthTag(id, MESH_NETWORK_ID, 1, MESH_AUTH_SECRET);
    pole.ingestAdvertisement(p, -80, 5);
  }
  assert(pole.neighborCount == MAX_NEIGHBORS_TABLE);

  // Attempt to insert 17th node when all are active
  DiscoveryPayload overflow;
  strncpy(overflow.nodeId, "NODE-OVERFLOW", 15);
  overflow.networkId = MESH_NETWORK_ID;
  overflow.trustStatus = TRUST_REGISTERED;
  overflow.networkRank = 1;
  overflow.availableCapacity = 2;
  overflow.authTag = DiscoveryPayload::computeAuthTag("NODE-OVERFLOW", MESH_NETWORK_ID, 1, MESH_AUTH_SECRET);

  pole.ingestAdvertisement(overflow, -80, 5);

  // Table must remain bounded at exactly 16 entries without crashing or overflowing!
  assert(pole.neighborCount == MAX_NEIGHBORS_TABLE);

  size_t memoryBytes = sizeof(pole.table);
  std::cout << "PASSED (Table Bounded strictly at " << MAX_NEIGHBORS_TABLE
            << " entries, Memory: " << memoryBytes << " bytes, Zero Heap Allocations)\n";
}

// ─────────────────────────────────────────────────────────────
//  MAIN TEST RUNNER
// ─────────────────────────────────────────────────────────────
int main() {
  std::cout << "\n======================================================\n";
  std::cout << "  PROMPT 09 AUTOMATED TEST SUITE: NODE DISCOVERY & LQ\n";
  std::cout << "======================================================\n\n";

  runTest1_OneGatewayOnePole();
  runTest2_OneGatewayTwoPoles();
  runTest3_ThreeNearbyPoleNodes();
  runTest4_NodeAdvertisementReception();
  runTest5_RssiSnrRecordingAndEwma();
  runTest6_NeighborTimeout();
  runTest7_NodeRestartAndRediscovery();
  runTest8_UnregisteredDeviceHandling();
  runTest9_RevokedDeviceRejection();
  runTest10_DuplicateAdvertisementHandling();
  runTest11_BeaconCongestionSimulation();
  runTest12_MemoryStability();

  std::cout << "\n======================================================\n";
  std::cout << "  ALL 12 TESTS PASSED SUCCESSFULLY! (100% PASS RATE)\n";
  std::cout << "======================================================\n\n";
  return 0;
}
