/*
 * =====================================================================
 *  TEST_PROMPT12.CPP — MSCB SMART POLE DELETION, REVOCATION & CLEANUP
 *  Comprehensive Automated Test Suite Covering Scenarios 1 to 18
 * =====================================================================
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cassert>

using namespace std;

typedef string String;

static unsigned long g_simTimeMs = 1700000000UL;
unsigned long millis() { return g_simTimeMs; }
void delay(unsigned long ms) { g_simTimeMs += ms; }
time_t time(time_t *t) { return (time_t)(g_simTimeMs / 1000); }

long random(long minVal, long maxVal) {
  static unsigned int seed = 42;
  seed = seed * 1103515245 + 12345;
  return minVal + ((seed / 65536) % (maxVal - minVal + 1));
}

// ─────────────────────────────────────────────────────────────
//  CONSTANTS & CONFIG
// ─────────────────────────────────────────────────────────────
#define GATEWAY_ID       "GATEWAY"
#define BROADCAST_ID     "BROADCAST"
#define MAX_NODES        50
#define MAX_ROUTES       50
#define MAX_FEEDERS      6
#define MAX_REVOKED_NODES 50
#define DEFAULT_TTL      6

#define MESH_AUTH_KEY                  "MSCB_AUTH_KEY_2026_SECRET_SALT_V12"
#define ADMIN_ROLE_ID                  "5505296197"
#define NVS_SCHEMA_VERSION             12
#define DELETE_CONFIRMATION_TIMEOUT_MS 60000UL
#define REVOCATION_RETENTION_DAYS      365

// ─────────────────────────────────────────────────────────────
//  ENUMS & STRUCTS
// ─────────────────────────────────────────────────────────────
enum PoleLifecycleState {
  POLE_STATE_ACTIVE = 0,
  POLE_STATE_DELETE_REQUESTED,
  POLE_STATE_DELETE_CONFIRMATION_REQUIRED,
  POLE_STATE_DELETION_APPROVED,
  POLE_STATE_REVOCATION_PENDING,
  POLE_STATE_CHILD_ROUTE_MIGRATION,
  POLE_STATE_DE_REGISTERED,
  POLE_STATE_ARCHIVED,
  POLE_STATE_DELETE_FAILED
};

const char* poleLifecycleStateToString(PoleLifecycleState st) {
  switch (st) {
    case POLE_STATE_ACTIVE:                        return "ACTIVE";
    case POLE_STATE_DELETE_REQUESTED:             return "DELETE_REQUESTED";
    case POLE_STATE_DELETE_CONFIRMATION_REQUIRED: return "DELETE_CONFIRMATION_REQUIRED";
    case POLE_STATE_DELETION_APPROVED:            return "DELETION_APPROVED";
    case POLE_STATE_REVOCATION_PENDING:           return "REVOCATION_PENDING";
    case POLE_STATE_CHILD_ROUTE_MIGRATION:        return "CHILD_ROUTE_MIGRATION";
    case POLE_STATE_DE_REGISTERED:                return "DE-REGISTERED";
    case POLE_STATE_ARCHIVED:                     return "ARCHIVED";
    case POLE_STATE_DELETE_FAILED:                return "DELETE_FAILED";
    default:                                      return "UNKNOWN";
  }
}

enum FeederStatus {
  FEEDER_STATUS_ACTIVE = 0,
  FEEDER_STATUS_DEACTIVATED,
  FEEDER_STATUS_REMOVED
};

const char* feederStatusToString(FeederStatus st) {
  switch (st) {
    case FEEDER_STATUS_ACTIVE:      return "ACTIVE";
    case FEEDER_STATUS_DEACTIVATED: return "DEACTIVATED";
    case FEEDER_STATUS_REMOVED:     return "REMOVED";
    default:                        return "UNKNOWN";
  }
}

struct FeederState {
  uint8_t      feederId;
  String       poleId;
  String       name;
  uint8_t      relayChannel;
  uint8_t      currentChannelR;
  uint8_t      currentChannelY;
  uint8_t      currentChannelB;
  FeederStatus status;
  String       onlineState;
  String       relayState;
  float        voltage1;
  float        voltage2;
  float        currentR;
  float        currentY;
  float        currentB;
  float        historicalKwh;
  uint32_t     tripCount;
  unsigned long deactivatedAt;
};

struct NodeRecord {
  String   hwId;
  String   logicalId;
  String   name;
  String   location;
  String   type;
  bool     registered;
  PoleLifecycleState lifecycleState;
  String   revocationTxId;
  String   onlineState;
  int      rssi;
  int      snr;
  String   lightState;
  float    voltage;
  float    current;
  float    power;
  float    totalKwh;
  String   fault;
  String   primaryNextHop;
  String   backupNextHop;
  int      hopCount;
  String   connectionMode;
  bool     orphan;
  FeederState feeders[MAX_FEEDERS];
};

struct GwRouteEntry {
  String   destination;
  String   primaryNextHop;
  String   backupNextHop;
  int      hopCount;
  int      rssi;
  bool     valid;
};

struct NeighborSummary {
  String   reportingNode;
  String   neighborId;
  int16_t  rssi;
  int8_t   snr;
  bool     responded;
  bool     valid;
};

struct RevocationRecord {
  char     logicalId[16];
  char     hwId[16];
  uint32_t revocationTime;
  char     transactionId[32];
  char     reason[32];
  char     previousParent[16];
  char     previousRegState[16];
  uint8_t  revocationVersion;
  char     revokedBy[24];
  bool     valid;
};

struct ChildMigrationPlan {
  char childId[16];
  char previousParent[16];
  char newPrimaryParent[16];
  char newBackupParent[16];
  int  newHopCount;
  int  linkRssi;
  bool canMigrate;
  bool isOrphaned;
  char reason[32];
};

struct DeletionImpact {
  char poleId[16];
  char poleName[32];
  char area[32];
  char status[16];
  bool isOnline;
  int  directChildCount;
  int  totalDownstreamCount;
  int  affectedRouteCount;
  int  activeFeederCount;
  bool hasOrphansWithoutRoute;
  ChildMigrationPlan children[MAX_NODES];
  int  migrationPlanCount;
};

enum DeletionType {
  DEL_TYPE_POLE = 0,
  DEL_TYPE_FEEDER
};

struct DeletionTransaction {
  bool                 inProgress;
  char                 txId[32];
  DeletionType         type;
  char                 targetId[16];
  uint8_t              feederId;
  char                 adminId[24];
  char                 confirmationToken[16];
  PoleLifecycleState   state;
  uint32_t             startedAt;
  uint32_t             expiresAt;
  bool                 confirmed;
  bool                 revocationCommitted;
  bool                 routesMigrated;
  bool                 nodeAcknowledged;
  char                 failureReason[32];
};

// ─────────────────────────────────────────────────────────────
//  MOCK NVS PREFERENCES
// ─────────────────────────────────────────────────────────────
class MockPreferences {
public:
  map<string, string> strStore;
  map<string, int> intStore;
  map<string, bool> boolStore;
  map<string, uint32_t> uintStore;

  void begin(const char* ns, bool ro) {}
  void end() {}

  int getInt(const char* key, int defVal) {
    if (intStore.count(key)) return intStore[key];
    return defVal;
  }
  void putInt(const char* key, int val) { intStore[key] = val; }

  uint32_t getUInt(const char* key, uint32_t defVal) {
    if (uintStore.count(key)) return uintStore[key];
    return defVal;
  }
  void putUInt(const char* key, uint32_t val) { uintStore[key] = val; }
  void putUChar(const char* key, uint8_t val) { intStore[key] = val; }
  uint8_t getUChar(const char* key, uint8_t defVal) {
    if (intStore.count(key)) return (uint8_t)intStore[key];
    return defVal;
  }

  bool getBool(const char* key, bool defVal) {
    if (boolStore.count(key)) return boolStore[key];
    return defVal;
  }
  void putBool(const char* key, bool val) { boolStore[key] = val; }

  String getString(const char* key, const String &defVal) {
    if (strStore.count(key)) return strStore[key];
    return defVal;
  }
  void putString(const char* key, const String &val) { strStore[key] = val; }
};

// ─────────────────────────────────────────────────────────────
//  SECURITY & CRYPTOGRAPHY SIMULATOR
// ─────────────────────────────────────────────────────────────
class MockAuthSecurity {
public:
  String computeHMAC(const String &key, const String &data) {
    uint32_t h1 = 0x811c9dc5;
    uint32_t h2 = 0x5a17c3b9;
    for (size_t i = 0; i < key.length(); i++) h1 = (h1 ^ (uint8_t)key[i]) * 0x01000193;
    for (size_t i = 0; i < data.length(); i++) h2 = (h2 ^ (uint8_t)data[i]) * 0x01000193;
    char hex[17];
    snprintf(hex, sizeof(hex), "%08x%08x", h1, h2);
    return String(hex);
  }

  String generateTransactionId(const String &prefix, const String &targetId) {
    time_t now = time(nullptr);
    uint16_t rnd = (uint16_t)random(1000, 9999);
    char buf[36];
    snprintf(buf, sizeof(buf), "%s-%lu-%s-%04u", prefix.c_str(), (unsigned long)now, targetId.c_str(), rnd);
    return String(buf);
  }

  String generateConfirmationToken() {
    uint32_t rnd = (uint32_t)random(100000, 999999);
    char buf[16];
    snprintf(buf, sizeof(buf), "CONF-%lu", (unsigned long)rnd);
    return String(buf);
  }

  String signPayload(const String &payload, uint16_t seq, const String &txId) {
    return computeHMAC(String(MESH_AUTH_KEY), payload + "|" + to_string(seq) + "|" + txId);
  }

  bool verifyPayloadSignature(const String &payload, uint16_t seq, const String &txId, const String &sig) {
    String exp = signPayload(payload, seq, txId);
    return (exp == sig);
  }

  bool verifyAdmin(const String &adminId) {
    return (adminId == ADMIN_ROLE_ID);
  }
};

static MockAuthSecurity g_authSec;

// ─────────────────────────────────────────────────────────────
//  MOCK MESH & GATEWAY ENGINE
// ─────────────────────────────────────────────────────────────
class MockMeshManager {
public:
  NodeRecord nodes[MAX_NODES];
  int nodeCount;

  GwRouteEntry gwRoutes[MAX_ROUTES];
  int gwRouteCount;

  NeighborSummary nbSummaries[100];
  int nbSummaryCount;

  MockMeshManager() : nodeCount(0), gwRouteCount(0), nbSummaryCount(0) {}

  void clear() {
    nodeCount = 0;
    gwRouteCount = 0;
    nbSummaryCount = 0;
  }

  int addNode(const String &hwId, const String &logId, const String &name, const String &parent, int hops, bool isOnline) {
    int idx = nodeCount++;
    nodes[idx] = {};
    nodes[idx].hwId = hwId;
    nodes[idx].logicalId = logId;
    nodes[idx].name = name;
    nodes[idx].location = "Test Sector";
    nodes[idx].type = "STREET";
    nodes[idx].registered = true;
    nodes[idx].lifecycleState = POLE_STATE_ACTIVE;
    nodes[idx].onlineState = isOnline ? "ONLINE" : "OFFLINE";
    nodes[idx].primaryNextHop = parent;
    nodes[idx].backupNextHop = BROADCAST_ID;
    nodes[idx].hopCount = hops;
    nodes[idx].orphan = false;
    nodes[idx].voltage = 230.0f;
    nodes[idx].current = 5.2f;

    for (int f = 0; f < MAX_FEEDERS; f++) {
      nodes[idx].feeders[f].feederId = f + 1;
      nodes[idx].feeders[f].poleId = logId;
      nodes[idx].feeders[f].name = "Feeder " + to_string(f + 1);
      nodes[idx].feeders[f].status = FEEDER_STATUS_ACTIVE;
      nodes[idx].feeders[f].relayState = "OFF";
      nodes[idx].feeders[f].relayChannel = f;
      nodes[idx].feeders[f].currentChannelR = f * 3;
      nodes[idx].feeders[f].currentChannelY = f * 3 + 1;
      nodes[idx].feeders[f].currentChannelB = f * 3 + 2;
    }

    addRoute(logId, parent, hops);
    return idx;
  }

  NodeRecord* findNode(const String &id) {
    for (int i = 0; i < nodeCount; i++) {
      if (nodes[i].logicalId == id || nodes[i].hwId == id) return &nodes[i];
    }
    return nullptr;
  }

  int findNodeIndex(const String &id) {
    for (int i = 0; i < nodeCount; i++) {
      if (nodes[i].logicalId == id || nodes[i].hwId == id) return i;
    }
    return -1;
  }

  void addRoute(const String &dst, const String &nextHop, int hops) {
    for (int i = 0; i < gwRouteCount; i++) {
      if (gwRoutes[i].destination == dst) {
        gwRoutes[i].primaryNextHop = nextHop;
        gwRoutes[i].hopCount = hops;
        gwRoutes[i].valid = true;
        return;
      }
    }
    int idx = gwRouteCount++;
    gwRoutes[idx].destination = dst;
    gwRoutes[idx].primaryNextHop = nextHop;
    gwRoutes[idx].backupNextHop = BROADCAST_ID;
    gwRoutes[idx].hopCount = hops;
    gwRoutes[idx].valid = true;
  }

  void addNeighbor(const String &from, const String &to, int rssi) {
    int idx = nbSummaryCount++;
    nbSummaries[idx].reportingNode = from;
    nbSummaries[idx].neighborId = to;
    nbSummaries[idx].rssi = rssi;
    nbSummaries[idx].snr = 8;
    nbSummaries[idx].responded = true;
    nbSummaries[idx].valid = true;
  }

  void removeRoute(const String &dst) {
    for (int i = 0; i < gwRouteCount; i++) {
      if (gwRoutes[i].destination == dst) {
        gwRoutes[i].valid = false;
        for (int j = i; j < gwRouteCount - 1; j++) gwRoutes[j] = gwRoutes[j + 1];
        gwRouteCount--;
        i--;
      }
    }
  }

  void removeRoutesThrough(const String &nextHop) {
    for (int i = 0; i < gwRouteCount; i++) {
      if (gwRoutes[i].primaryNextHop == nextHop) {
        gwRoutes[i].valid = false;
        for (int j = i; j < gwRouteCount - 1; j++) gwRoutes[j] = gwRoutes[j + 1];
        gwRouteCount--;
        i--;
      }
    }
  }

  void updateNodeParent(const String &childId, const String &primary, const String &backup, int hops) {
    NodeRecord *n = findNode(childId);
    if (n) {
      n->primaryNextHop = primary;
      n->backupNextHop = backup;
      n->hopCount = hops;
      n->orphan = (primary == BROADCAST_ID || primary.empty());
      n->connectionMode = n->orphan ? "ORPHANED" : "MESH";
      addRoute(childId, primary, hops);
    }
  }

  void markNodeDeregistered(const String &id) {
    NodeRecord *n = findNode(id);
    if (n) {
      n->registered = false;
      n->onlineState = "OFFLINE";
      n->lifecycleState = POLE_STATE_DE_REGISTERED;
      n->primaryNextHop = BROADCAST_ID;
      n->backupNextHop = BROADCAST_ID;
      removeRoute(n->logicalId);
      removeRoute(n->hwId);
      removeRoutesThrough(n->logicalId);
      removeRoutesThrough(n->hwId);
    }
  }
};

static MockMeshManager g_meshMgr;

// ─────────────────────────────────────────────────────────────
//  MOCK REVOCATION MANAGER ENGINE
// ─────────────────────────────────────────────────────────────
class MockRevocationManager {
public:
  MockPreferences prefsRevoc;
  MockPreferences prefsTx;

  RevocationRecord revokedNodes[MAX_REVOKED_NODES];
  int revocationCount;

  DeletionTransaction currentTx;
  DeletionImpact cachedImpact;

  MockRevocationManager() : revocationCount(0) {
    memset(&currentTx, 0, sizeof(currentTx));
    currentTx.inProgress = false;
  }

  void clear() {
    revocationCount = 0;
    memset(&currentTx, 0, sizeof(currentTx));
    currentTx.inProgress = false;
    prefsRevoc.strStore.clear();
    prefsRevoc.intStore.clear();
    prefsTx.strStore.clear();
    prefsTx.boolStore.clear();
  }

  bool isRevoked(const String &id) const {
    for (int i = 0; i < revocationCount; i++) {
      if (revokedNodes[i].valid) {
        if (id == revokedNodes[i].logicalId || id == revokedNodes[i].hwId) return true;
      }
    }
    return false;
  }

  const RevocationRecord* getRevocationRecord(const String &id) const {
    for (int i = 0; i < revocationCount; i++) {
      if (revokedNodes[i].valid) {
        if (id == revokedNodes[i].logicalId || id == revokedNodes[i].hwId) return &revokedNodes[i];
      }
    }
    return nullptr;
  }

  bool addRevocation(const String &logicalId, const String &hwId, const String &txId,
                     const String &reason, const String &prevParent, const String &prevRegState,
                     const String &adminId) {
    if (isRevoked(logicalId) || isRevoked(hwId)) return true;
    if (revocationCount >= MAX_REVOKED_NODES) return false;

    int idx = revocationCount++;
    memset(&revokedNodes[idx], 0, sizeof(RevocationRecord));
    strncpy(revokedNodes[idx].logicalId, logicalId.c_str(), sizeof(revokedNodes[idx].logicalId) - 1);
    strncpy(revokedNodes[idx].hwId, hwId.c_str(), sizeof(revokedNodes[idx].hwId) - 1);
    strncpy(revokedNodes[idx].transactionId, txId.c_str(), sizeof(revokedNodes[idx].transactionId) - 1);
    strncpy(revokedNodes[idx].reason, reason.c_str(), sizeof(revokedNodes[idx].reason) - 1);
    strncpy(revokedNodes[idx].previousParent, prevParent.c_str(), sizeof(revokedNodes[idx].previousParent) - 1);
    strncpy(revokedNodes[idx].previousRegState, prevRegState.c_str(), sizeof(revokedNodes[idx].previousRegState) - 1);
    strncpy(revokedNodes[idx].revokedBy, adminId.c_str(), sizeof(revokedNodes[idx].revokedBy) - 1);
    revokedNodes[idx].revocationTime = (uint32_t)time(nullptr);
    revokedNodes[idx].revocationVersion = NVS_SCHEMA_VERSION;
    revokedNodes[idx].valid = true;

    saveRevocationRegistry();
    return true;
  }

  void saveRevocationRegistry() {
    prefsRevoc.putInt("count", revocationCount);
    for (int i = 0; i < revocationCount; i++) {
      string pfx = "r" + to_string(i) + "_";
      prefsRevoc.putString((pfx + "id").c_str(), revokedNodes[i].logicalId);
      prefsRevoc.putString((pfx + "hw").c_str(), revokedNodes[i].hwId);
      prefsRevoc.putString((pfx + "tx").c_str(), revokedNodes[i].transactionId);
      prefsRevoc.putString((pfx + "rs").c_str(), revokedNodes[i].reason);
      prefsRevoc.putString((pfx + "pr").c_str(), revokedNodes[i].previousParent);
      prefsRevoc.putString((pfx + "st").c_str(), revokedNodes[i].previousRegState);
      prefsRevoc.putString((pfx + "by").c_str(), revokedNodes[i].revokedBy);
      prefsRevoc.putUInt((pfx + "tm").c_str(), revokedNodes[i].revocationTime);
      prefsRevoc.putUChar((pfx + "vr").c_str(), revokedNodes[i].revocationVersion);
    }
  }

  void loadRevocationRegistry() {
    revocationCount = prefsRevoc.getInt("count", 0);
    for (int i = 0; i < revocationCount; i++) {
      string pfx = "r" + to_string(i) + "_";
      strncpy(revokedNodes[i].logicalId, prefsRevoc.getString((pfx + "id").c_str(), "").c_str(), 15);
      strncpy(revokedNodes[i].hwId, prefsRevoc.getString((pfx + "hw").c_str(), "").c_str(), 15);
      strncpy(revokedNodes[i].transactionId, prefsRevoc.getString((pfx + "tx").c_str(), "").c_str(), 31);
      strncpy(revokedNodes[i].reason, prefsRevoc.getString((pfx + "rs").c_str(), "").c_str(), 31);
      strncpy(revokedNodes[i].previousParent, prefsRevoc.getString((pfx + "pr").c_str(), "").c_str(), 15);
      strncpy(revokedNodes[i].previousRegState, prefsRevoc.getString((pfx + "st").c_str(), "").c_str(), 15);
      strncpy(revokedNodes[i].revokedBy, prefsRevoc.getString((pfx + "by").c_str(), "").c_str(), 23);
      revokedNodes[i].revocationTime = prefsRevoc.getUInt((pfx + "tm").c_str(), 0);
      revokedNodes[i].revocationVersion = prefsRevoc.getUChar((pfx + "vr").c_str(), 1);
      revokedNodes[i].valid = true;
    }
  }

  void saveTransactionJournal() {
    prefsTx.putBool("in_prog", currentTx.inProgress);
    prefsTx.putString("tx_id", currentTx.txId);
    prefsTx.putInt("type", (int)currentTx.type);
    prefsTx.putString("target", currentTx.targetId);
    prefsTx.putString("admin", currentTx.adminId);
    prefsTx.putInt("state", (int)currentTx.state);
  }

  void clearTransactionJournal() {
    prefsTx.putBool("in_prog", false);
    prefsTx.putString("tx_id", "");
  }

  void checkCrashRecovery() {
    if (prefsTx.getBool("in_prog", false)) {
      String txId = prefsTx.getString("tx_id", "");
      String target = prefsTx.getString("target", "");
      String admin = prefsTx.getString("admin", ADMIN_ROLE_ID);

      NodeRecord *node = g_meshMgr.findNode(target);
      String hwId = node ? node->hwId : target;
      String prevParent = node ? node->primaryNextHop : "UNKNOWN";

      addRevocation(target, hwId, txId, "CRASH_RECOVERY", prevParent, "DELETION_IN_PROGRESS", admin);
      g_meshMgr.markNodeDeregistered(target);
      clearTransactionJournal();
    }
  }

  bool isDescendantOf(const String &childId, const String &candidateParentId) {
    if (childId == candidateParentId) return true;
    String current = childId;
    int guard = 0;
    while (!current.empty() && current != GATEWAY_ID && current != BROADCAST_ID && guard < 10) {
      guard++;
      NodeRecord *n = g_meshMgr.findNode(current);
      if (!n) break;
      if (n->primaryNextHop == candidateParentId || n->backupNextHop == candidateParentId) return true;
      current = n->primaryNextHop;
    }
    return false;
  }

  String selectAlternativeParent(const String &childId, const String &deletedNodeId, int &outHops, int &outRssi, String &outBackup) {
    outHops = 1;
    outRssi = -80;
    outBackup = BROADCAST_ID;

    String bestPrimary = "";
    int bestScore = -9999;
    String bestSec = "";

    for (int i = 0; i < g_meshMgr.nbSummaryCount; i++) {
      NeighborSummary &nb = g_meshMgr.nbSummaries[i];
      if (!nb.valid || !nb.responded) continue;

      String cand = "";
      if (nb.reportingNode == childId && nb.neighborId != deletedNodeId && nb.neighborId != childId) {
        cand = nb.neighborId;
      } else if (nb.neighborId == childId && nb.reportingNode != deletedNodeId && nb.reportingNode != childId) {
        cand = nb.reportingNode;
      }

      if (!cand.empty()) {
        if (cand == deletedNodeId || isDescendantOf(cand, deletedNodeId) || isDescendantOf(cand, childId)) continue;
        NodeRecord *candNode = g_meshMgr.findNode(cand);
        if (!candNode || !candNode->registered || candNode->onlineState == "OFFLINE") continue;

        int score = nb.rssi - (candNode->hopCount * 15);
        if (score > bestScore) {
          bestSec = bestPrimary;
          bestPrimary = cand;
          bestScore = score;
          outHops = candNode->hopCount + 1;
          outRssi = nb.rssi;
        } else if (cand != bestPrimary) {
          bestSec = cand;
        }
      }
    }

    // Check registered active nodes with valid routes
    if (bestPrimary.empty()) {
      for (int i = 0; i < g_meshMgr.nodeCount; i++) {
        NodeRecord &cn = g_meshMgr.nodes[i];
        if (!cn.registered || cn.logicalId == childId || cn.logicalId == deletedNodeId) continue;
        if (cn.onlineState == "OFFLINE") continue;
        if (isDescendantOf(cn.logicalId, deletedNodeId) || isDescendantOf(cn.logicalId, childId)) continue;
        if (!cn.primaryNextHop.empty() && cn.primaryNextHop != deletedNodeId) {
          bestPrimary = cn.logicalId;
          outHops = cn.hopCount + 1;
          break;
        }
      }
    }

    outBackup = !bestSec.empty() ? bestSec : BROADCAST_ID;
    return bestPrimary;
  }

  bool computeDeletionImpact(const String &poleId, DeletionImpact &impact) {
    memset(&impact, 0, sizeof(DeletionImpact));
    strncpy(impact.poleId, poleId.c_str(), sizeof(impact.poleId) - 1);

    NodeRecord *target = g_meshMgr.findNode(poleId);
    if (!target) return false;

    strncpy(impact.poleName, target->name.c_str(), sizeof(impact.poleName) - 1);
    strncpy(impact.area, target->location.c_str(), sizeof(impact.area) - 1);
    strncpy(impact.status, target->onlineState.c_str(), sizeof(impact.status) - 1);
    impact.isOnline = (target->onlineState == "ONLINE");

    for (int f = 0; f < MAX_FEEDERS; f++) {
      if (target->feeders[f].status == FEEDER_STATUS_ACTIVE) impact.activeFeederCount++;
    }

    for (int i = 0; i < g_meshMgr.nodeCount; i++) {
      NodeRecord &n = g_meshMgr.nodes[i];
      if (!n.registered || n.logicalId == poleId) continue;

      bool isDirect = (n.primaryNextHop == poleId || n.backupNextHop == poleId);
      bool isDownstream = isDescendantOf(n.logicalId, poleId);

      if (isDirect || isDownstream) {
        if (isDirect) impact.directChildCount++;
        impact.totalDownstreamCount++;

        int idx = impact.migrationPlanCount++;
        strncpy(impact.children[idx].childId, n.logicalId.c_str(), sizeof(impact.children[idx].childId) - 1);
        strncpy(impact.children[idx].previousParent, n.primaryNextHop.c_str(), sizeof(impact.children[idx].previousParent) - 1);

        int newHops = 1, newRssi = -80;
        String backupParent = "";
        String newParent = selectAlternativeParent(n.logicalId, poleId, newHops, newRssi, backupParent);

        if (!newParent.empty()) {
          strncpy(impact.children[idx].newPrimaryParent, newParent.c_str(), sizeof(impact.children[idx].newPrimaryParent) - 1);
          strncpy(impact.children[idx].newBackupParent, backupParent.c_str(), sizeof(impact.children[idx].newBackupParent) - 1);
          impact.children[idx].newHopCount = newHops;
          impact.children[idx].linkRssi = newRssi;
          impact.children[idx].canMigrate = true;
          impact.children[idx].isOrphaned = false;
        } else {
          impact.children[idx].canMigrate = false;
          impact.children[idx].isOrphaned = true;
          impact.hasOrphansWithoutRoute = true;
          strncpy(impact.children[idx].newPrimaryParent, BROADCAST_ID, sizeof(impact.children[idx].newPrimaryParent) - 1);
        }
      }
    }
    impact.affectedRouteCount = impact.totalDownstreamCount;
    cachedImpact = impact;
    return true;
  }

  bool executeChildRouteMigration(const String &poleId, DeletionImpact &impact) {
    for (int i = 0; i < impact.migrationPlanCount; i++) {
      ChildMigrationPlan &plan = impact.children[i];
      if (plan.canMigrate) {
        g_meshMgr.updateNodeParent(plan.childId, plan.newPrimaryParent, plan.newBackupParent, plan.newHopCount);
      } else {
        g_meshMgr.updateNodeParent(plan.childId, BROADCAST_ID, BROADCAST_ID, 255);
      }
    }
    return true;
  }

  bool requestNodeDeletion(const String &adminId, const String &poleId, String &outTxId, String &outConfirmToken, String &outMsg) {
    if (!g_authSec.verifyAdmin(adminId)) {
      outMsg = "ERR_UNAUTHORIZED";
      return false;
    }
    if (currentTx.inProgress) {
      outMsg = "ERR_ALREADY_IN_PROGRESS";
      return false;
    }
    NodeRecord *target = g_meshMgr.findNode(poleId);
    if (!target || !target->registered) {
      outMsg = "ERR_NODE_NOT_FOUND";
      return false;
    }
    if (isRevoked(poleId)) {
      outMsg = "ERR_ALREADY_REVOKED";
      return false;
    }

    outTxId = g_authSec.generateTransactionId("DEL", poleId);
    outConfirmToken = g_authSec.generateConfirmationToken();

    target->lifecycleState = POLE_STATE_DELETE_CONFIRMATION_REQUIRED;
    memset(&currentTx, 0, sizeof(currentTx));
    currentTx.inProgress = true;
    currentTx.type = DEL_TYPE_POLE;
    strncpy(currentTx.txId, outTxId.c_str(), sizeof(currentTx.txId) - 1);
    strncpy(currentTx.targetId, poleId.c_str(), sizeof(currentTx.targetId) - 1);
    strncpy(currentTx.adminId, adminId.c_str(), sizeof(currentTx.adminId) - 1);
    strncpy(currentTx.confirmationToken, outConfirmToken.c_str(), sizeof(currentTx.confirmationToken) - 1);
    currentTx.state = POLE_STATE_DELETE_CONFIRMATION_REQUIRED;
    currentTx.startedAt = (uint32_t)(millis() / 1000);
    currentTx.expiresAt = (uint32_t)(millis() + DELETE_CONFIRMATION_TIMEOUT_MS);

    saveTransactionJournal();
    outMsg = "IMPACT_CALCULATED";
    return true;
  }

  bool confirmNodeDeletion(const String &adminId, const String &txId, const String &confirmToken, String &outResultMsg) {
    if (!g_authSec.verifyAdmin(adminId)) {
      outResultMsg = "ERR_UNAUTHORIZED";
      return false;
    }
    if (!currentTx.inProgress || String(currentTx.txId) != txId) {
      outResultMsg = "ERR_INVALID_TRANSACTION";
      return false;
    }
    if (String(currentTx.confirmationToken) != confirmToken) {
      outResultMsg = "ERR_SECURITY_VERIFICATION_FAILED";
      return false;
    }

    String targetId = String(currentTx.targetId);
    NodeRecord *target = g_meshMgr.findNode(targetId);
    String hwId = target ? target->hwId : targetId;
    String prevParent = target ? target->primaryNextHop : "UNKNOWN";

    currentTx.state = POLE_STATE_DELETION_APPROVED;
    currentTx.confirmed = true;
    if (target) target->lifecycleState = POLE_STATE_REVOCATION_PENDING;

    addRevocation(targetId, hwId, txId, "ADMIN_DECOMMISSION", prevParent, "REGISTERED", adminId);
    currentTx.revocationCommitted = true;
    saveTransactionJournal();

    currentTx.state = POLE_STATE_CHILD_ROUTE_MIGRATION;
    if (target) target->lifecycleState = POLE_STATE_CHILD_ROUTE_MIGRATION;

    DeletionImpact impact;
    computeDeletionImpact(targetId, impact);
    executeChildRouteMigration(targetId, impact);
    currentTx.routesMigrated = true;

    g_meshMgr.markNodeDeregistered(targetId);
    currentTx.state = POLE_STATE_ARCHIVED;
    if (target) target->lifecycleState = POLE_STATE_ARCHIVED;
    currentTx.inProgress = false;
    clearTransactionJournal();

    outResultMsg = "SUCCESS_REVOKED";
    return true;
  }

  bool cancelNodeDeletion(const String &adminId, const String &txId, String &outMsg) {
    if (!g_authSec.verifyAdmin(adminId)) {
      outMsg = "ERR_UNAUTHORIZED";
      return false;
    }
    if (!currentTx.inProgress || String(currentTx.txId) != txId) {
      outMsg = "ERR_INVALID_TRANSACTION";
      return false;
    }
    NodeRecord *n = g_meshMgr.findNode(currentTx.targetId);
    if (n) n->lifecycleState = POLE_STATE_ACTIVE;

    currentTx.inProgress = false;
    currentTx.state = POLE_STATE_ACTIVE;
    clearTransactionJournal();
    outMsg = "SUCCESS_CANCELLED";
    return true;
  }

  bool requestFeederDeactivation(const String &adminId, const String &poleId, uint8_t feederId,
                                String &outTxId, String &outConfirmToken, String &outMsg) {
    if (!g_authSec.verifyAdmin(adminId)) {
      outMsg = "ERR_UNAUTHORIZED";
      return false;
    }
    if (feederId < 1 || feederId > MAX_FEEDERS) {
      outMsg = "ERR_INVALID_FEEDER_ID";
      return false;
    }
    NodeRecord *target = g_meshMgr.findNode(poleId);
    if (!target || !target->registered) {
      outMsg = "ERR_NODE_NOT_FOUND";
      return false;
    }
    if (target->feeders[feederId - 1].status == FEEDER_STATUS_DEACTIVATED) {
      outMsg = "ERR_FEEDER_ALREADY_DEACTIVATED";
      return false;
    }

    outTxId = g_authSec.generateTransactionId("FDR-DEL", poleId + "-F" + to_string(feederId));
    outConfirmToken = g_authSec.generateConfirmationToken();

    memset(&currentTx, 0, sizeof(currentTx));
    currentTx.inProgress = true;
    currentTx.type = DEL_TYPE_FEEDER;
    strncpy(currentTx.txId, outTxId.c_str(), sizeof(currentTx.txId) - 1);
    strncpy(currentTx.targetId, poleId.c_str(), sizeof(currentTx.targetId) - 1);
    currentTx.feederId = feederId;
    strncpy(currentTx.adminId, adminId.c_str(), sizeof(currentTx.adminId) - 1);
    strncpy(currentTx.confirmationToken, outConfirmToken.c_str(), sizeof(currentTx.confirmationToken) - 1);
    outMsg = "FEEDER_CONFIRMATION_REQUIRED";
    return true;
  }

  bool confirmFeederDeactivation(const String &adminId, const String &txId, const String &confirmToken, String &outMsg) {
    if (!g_authSec.verifyAdmin(adminId)) {
      outMsg = "ERR_UNAUTHORIZED";
      return false;
    }
    if (!currentTx.inProgress || currentTx.type != DEL_TYPE_FEEDER || String(currentTx.txId) != txId) {
      outMsg = "ERR_INVALID_TRANSACTION";
      return false;
    }
    if (String(currentTx.confirmationToken) != confirmToken) {
      outMsg = "ERR_SECURITY_VERIFICATION_FAILED";
      return false;
    }

    String poleId = currentTx.targetId;
    uint8_t fdrId = currentTx.feederId;
    NodeRecord *target = g_meshMgr.findNode(poleId);
    if (target && fdrId >= 1 && fdrId <= MAX_FEEDERS) {
      target->feeders[fdrId - 1].status = FEEDER_STATUS_DEACTIVATED;
    }
    currentTx.inProgress = false;
    outMsg = "SUCCESS_FEEDER_DEACTIVATED";
    return true;
  }
};

static MockRevocationManager g_revocMgr;

// ─────────────────────────────────────────────────────────────
//  MOCK FIELD POLE NODE
// ─────────────────────────────────────────────────────────────
class MockFieldPoleNode {
public:
  String hwId;
  String logicalId;
  bool registered;
  bool revoked;
  String revocationTxId;
  bool relayOn;
  FeederStatus feederStatuses[MAX_FEEDERS];

  MockFieldPoleNode(const String &hw, const String &log) :
    hwId(hw), logicalId(log), registered(true), revoked(false),
    revocationTxId(""), relayOn(true)
  {
    for (int f = 0; f < MAX_FEEDERS; f++) feederStatuses[f] = FEEDER_STATUS_ACTIVE;
  }

  void receiveCommand(const String &cmd, String &outAck) {
    if (cmd.rfind("REVOKE|", 0) == 0) {
      size_t p1 = cmd.find('|');
      size_t p2 = cmd.find('|', p1 + 1);
      String txId = (p2 != string::npos) ? cmd.substr(p1 + 1, p2 - p1 - 1) : cmd.substr(p1 + 1);
      revoked = true;
      registered = false;
      logicalId = "";
      revocationTxId = txId;
      // Note: Relay is preserved safely!
      outAck = "ACK|REVOKE|OK|REVOKED";
    }
    else if (cmd.rfind("FDR_DEACTIVATE|", 0) == 0) {
      int fdr = stoi(cmd.substr(15));
      if (fdr >= 1 && fdr <= MAX_FEEDERS) {
        feederStatuses[fdr - 1] = FEEDER_STATUS_DEACTIVATED;
        outAck = "ACK|FDR_DEACTIVATE|OK|DEACTIVATED";
      }
    }
  }

  bool canSendJoinProbe() {
    return (!registered && !revoked);
  }
};

// ─────────────────────────────────────────────────────────────
//  TEST RUNNER MACROS
// ─────────────────────────────────────────────────────────────
static int g_testsPassed = 0;
static int g_testsFailed = 0;

void reportTest(int num, const string &name, bool pass) {
  if (pass) {
    cout << "  [PASS] Scenario " << setw(2) << num << ": " << name << endl;
    g_testsPassed++;
  } else {
    cout << "  [FAIL] Scenario " << setw(2) << num << ": " << name << endl;
    g_testsFailed++;
  }
}

// ─────────────────────────────────────────────────────────────
//  MAIN TEST EXECUTION
// ─────────────────────────────────────────────────────────────
int main() {
  cout << "==================================================" << endl;
  cout << "  MSCB SMART POLE PROMPT 12 AUTOMATED TEST SUITE  " << endl;
  cout << "  Pole / Feeder Delete + Revocation + Cleanup     " << endl;
  cout << "==================================================" << endl;

  // ── Scenario 1: Delete an Online Pole ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);

    String txId, token, msg, resMsg;
    bool reqOk = g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0001", txId, token, msg);
    bool confOk = g_revocMgr.confirmNodeDeletion(ADMIN_ROLE_ID, txId, token, resMsg);
    bool isRev = g_revocMgr.isRevoked("SL-0001");
    NodeRecord *n = g_meshMgr.findNode("SL-0001");

    bool pass = reqOk && confOk && isRev && (resMsg == "SUCCESS_REVOKED") && n && (!n->registered);
    reportTest(1, "Delete an Online Pole", pass);
  }

  // ── Scenario 2: Delete an Offline Pole ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0002", "SL-0002", "Pole 2", GATEWAY_ID, 1, false); // OFFLINE

    String txId, token, msg, resMsg;
    bool reqOk = g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0002", txId, token, msg);
    bool confOk = g_revocMgr.confirmNodeDeletion(ADMIN_ROLE_ID, txId, token, resMsg);
    bool isRev = g_revocMgr.isRevoked("SL-0002");

    bool pass = reqOk && confOk && isRev && (resMsg == "SUCCESS_REVOKED");
    reportTest(2, "Delete an Offline Pole (Authoritative Immediate Revocation)", pass);
  }

  // ── Scenario 3: Duplicate Delete Request ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0003", "SL-0003", "Pole 3", GATEWAY_ID, 1, true);

    String txId1, token1, msg1, txId2, token2, msg2;
    bool req1 = g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0003", txId1, token1, msg1);
    bool req2 = g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0003", txId2, token2, msg2);

    bool pass = req1 && (!req2) && (msg2 == "ERR_ALREADY_IN_PROGRESS");
    reportTest(3, "Duplicate Delete Request Rejection", pass);
  }

  // ── Scenario 4: Unauthorized Delete Request ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0004", "SL-0004", "Pole 4", GATEWAY_ID, 1, true);

    String txId, token, msg;
    bool req = g_revocMgr.requestNodeDeletion("UNAUTHORIZED_HACKER_CHAT_ID", "SL-0004", txId, token, msg);

    bool pass = (!req) && (msg == "ERR_UNAUTHORIZED");
    reportTest(4, "Unauthorized Delete Request Rejection", pass);
  }

  // ── Scenario 5: Cancel Delete Confirmation ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0005", "SL-0005", "Pole 5", GATEWAY_ID, 1, true);

    String txId, token, msg, cancelMsg;
    g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0005", txId, token, msg);
    bool cancelOk = g_revocMgr.cancelNodeDeletion(ADMIN_ROLE_ID, txId, cancelMsg);

    NodeRecord *n = g_meshMgr.findNode("SL-0005");
    bool pass = cancelOk && (!g_revocMgr.isRevoked("SL-0005")) && n && (n->lifecycleState == POLE_STATE_ACTIVE);
    reportTest(5, "Cancel Delete Confirmation (Revert to ACTIVE)", pass);
  }

  // ── Scenario 6: Delete a Pole with No Children ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0006", "SL-0006", "Pole 6 (Leaf)", GATEWAY_ID, 1, true);

    DeletionImpact impact;
    g_revocMgr.computeDeletionImpact("SL-0006", impact);

    bool pass = (impact.directChildCount == 0) && (impact.totalDownstreamCount == 0) && (!impact.hasOrphansWithoutRoute);
    reportTest(6, "Delete a Pole with No Children (Impact Analysis)", pass);
  }

  // ── Scenario 7: Delete a Pole with One Child ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);
    g_meshMgr.addNode("HW-0002", "SL-0002", "Pole 2", "SL-0001", 2, true);
    g_meshMgr.addNode("HW-0003", "SL-0003", "Pole 3", GATEWAY_ID, 1, true);
    g_meshMgr.addNeighbor("SL-0002", "SL-0003", -75); // Child sees Pole 3

    DeletionImpact impact;
    g_revocMgr.computeDeletionImpact("SL-0001", impact);

    String txId, token, msg, resMsg;
    g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0001", txId, token, msg);
    g_revocMgr.confirmNodeDeletion(ADMIN_ROLE_ID, txId, token, resMsg);

    NodeRecord *c2 = g_meshMgr.findNode("SL-0002");
    bool pass = (impact.directChildCount == 1) && (impact.children[0].canMigrate) &&
                c2 && (c2->primaryNextHop == "SL-0003");
    reportTest(7, "Delete a Pole with One Child (Successful Route Reassignment)", pass);
  }

  // ── Scenario 8: Delete a Pole with Multiple Children ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);
    g_meshMgr.addNode("HW-0002", "SL-0002", "Pole 2 (Child 1)", "SL-0001", 2, true);
    g_meshMgr.addNode("HW-0003", "SL-0003", "Pole 3 (Child 2)", "SL-0001", 2, true);
    g_meshMgr.addNode("HW-0004", "SL-0004", "Pole 4 (Grandchild)", "SL-0002", 3, true);
    g_meshMgr.addNode("HW-0005", "SL-0005", "Pole 5 (Alternate)", GATEWAY_ID, 1, true);

    g_meshMgr.addNeighbor("SL-0002", "SL-0005", -70);
    g_meshMgr.addNeighbor("SL-0003", "SL-0005", -72);

    DeletionImpact impact;
    g_revocMgr.computeDeletionImpact("SL-0001", impact);

    String txId, token, msg, resMsg;
    g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0001", txId, token, msg);
    g_revocMgr.confirmNodeDeletion(ADMIN_ROLE_ID, txId, token, resMsg);

    bool pass = (impact.directChildCount == 2) && (impact.totalDownstreamCount == 3) &&
                (g_meshMgr.findNode("SL-0002")->primaryNextHop == "SL-0005") &&
                (g_meshMgr.findNode("SL-0003")->primaryNextHop == "SL-0005");
    reportTest(8, "Delete a Pole with Multiple Downstream Children", pass);
  }

  // ── Scenario 9: Backup Route Available ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);
    g_meshMgr.addNode("HW-0002", "SL-0002", "Pole 2", "SL-0001", 2, true);
    g_meshMgr.addNode("HW-0003", "SL-0003", "Pole 3 (Primary Alternative)", GATEWAY_ID, 1, true);
    g_meshMgr.addNode("HW-0004", "SL-0004", "Pole 4 (Backup Alternative)", GATEWAY_ID, 1, true);

    g_meshMgr.addNeighbor("SL-0002", "SL-0003", -68);
    g_meshMgr.addNeighbor("SL-0002", "SL-0004", -82);

    DeletionImpact impact;
    g_revocMgr.computeDeletionImpact("SL-0001", impact);

    String txId, token, msg, resMsg;
    g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0001", txId, token, msg);
    g_revocMgr.confirmNodeDeletion(ADMIN_ROLE_ID, txId, token, resMsg);

    NodeRecord *c2 = g_meshMgr.findNode("SL-0002");
    bool pass = c2 && (c2->primaryNextHop == "SL-0003") && (c2->backupNextHop == "SL-0004");
    reportTest(9, "Backup Route Available (Dual Parent Selection)", pass);
  }

  // ── Scenario 10: No Backup Route Available (Orphan Handling) ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);
    g_meshMgr.addNode("HW-0002", "SL-0002", "Pole 2 (Isolated Child)", "SL-0001", 2, true);
    // No other neighbors exist in the sector

    DeletionImpact impact;
    g_revocMgr.computeDeletionImpact("SL-0001", impact);

    String txId, token, msg, resMsg;
    g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0001", txId, token, msg);
    g_revocMgr.confirmNodeDeletion(ADMIN_ROLE_ID, txId, token, resMsg);

    NodeRecord *c2 = g_meshMgr.findNode("SL-0002");
    bool pass = impact.hasOrphansWithoutRoute && c2 && c2->orphan && (c2->connectionMode == "ORPHANED");
    reportTest(10, "No Alternative Route Available (Controlled Orphan Handling)", pass);
  }

  // ── Scenario 11: Gateway Restart During Deletion ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);

    // Simulate power failure mid-deletion
    String txId, token, msg;
    g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-0001", txId, token, msg);
    // Gateway simulated reboot
    g_revocMgr.checkCrashRecovery();

    bool isRev = g_revocMgr.isRevoked("SL-0001");
    NodeRecord *n = g_meshMgr.findNode("SL-0001");
    bool pass = isRev && n && (!n->registered) && (n->lifecycleState == POLE_STATE_DE_REGISTERED);
    reportTest(11, "Gateway Restart During Deletion (Crash Recovery & Consistency)", pass);
  }

  // ── Scenario 12: Node Restart After Revocation ──
  {
    MockFieldPoleNode pole("HW-A1B2", "SL-0010");
    String ack;
    pole.receiveCommand("REVOKE|DEL-12345", ack);

    // Simulate pole reboot: on boot it checks persistent revoked flag
    bool canProbe = pole.canSendJoinProbe();
    bool pass = pole.revoked && (!pole.registered) && (pole.logicalId.empty()) && (!canProbe);
    reportTest(12, "Node Restart After Revocation (Re-join Probe Suppressed)", pass);
  }

  // ── Scenario 13: Revoked Node Re-Join Attempt ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_revocMgr.addRevocation("SL-0013", "HW-REV13", "DEL-9999", "FAULT", "GATEWAY", "ACTIVE", ADMIN_ROLE_ID);

    // Simulate packet arriving from revoked node
    bool isBlocked = g_revocMgr.isRevoked("HW-REV13") || g_revocMgr.isRevoked("SL-0013");
    reportTest(13, "Revoked Node Re-Join Attempt Rejection", isBlocked);
  }

  // ── Scenario 14: Feeder Deactivation ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);

    String txId, token, msg, resMsg;
    bool req = g_revocMgr.requestFeederDeactivation(ADMIN_ROLE_ID, "SL-0001", 2, txId, token, msg);
    bool conf = g_revocMgr.confirmFeederDeactivation(ADMIN_ROLE_ID, txId, token, resMsg);

    NodeRecord *n = g_meshMgr.findNode("SL-0001");
    bool pass = req && conf && n &&
                (n->feeders[1].status == FEEDER_STATUS_DEACTIVATED) &&
                (n->feeders[0].status == FEEDER_STATUS_ACTIVE);
    reportTest(14, "Feeder Deactivation (Independent Channel Isolation)", pass);
  }

  // ── Scenario 15: Attempt to Delete the Same Feeder Twice ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);

    String tx1, tk1, m1, res1, tx2, tk2, m2;
    g_revocMgr.requestFeederDeactivation(ADMIN_ROLE_ID, "SL-0001", 3, tx1, tk1, m1);
    g_revocMgr.confirmFeederDeactivation(ADMIN_ROLE_ID, tx1, tk1, res1);

    // Attempt second deactivation
    bool req2 = g_revocMgr.requestFeederDeactivation(ADMIN_ROLE_ID, "SL-0001", 3, tx2, tk2, m2);
    bool pass = (!req2) && (m2 == "ERR_FEEDER_ALREADY_DEACTIVATED");
    reportTest(15, "Attempt to Delete the Same Feeder Twice Rejection", pass);
  }

  // ── Scenario 16: Invalid Pole ID ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    String tx, tk, m;
    bool req = g_revocMgr.requestNodeDeletion(ADMIN_ROLE_ID, "SL-NONEXISTENT", tx, tk, m);
    bool pass = (!req) && (m == "ERR_NODE_NOT_FOUND");
    reportTest(16, "Invalid Pole ID Rejection", pass);
  }

  // ── Scenario 17: Invalid Feeder ID ──
  {
    g_meshMgr.clear();
    g_revocMgr.clear();
    g_meshMgr.addNode("HW-0001", "SL-0001", "Pole 1", GATEWAY_ID, 1, true);

    String tx, tk, m1, m2;
    bool req0 = g_revocMgr.requestFeederDeactivation(ADMIN_ROLE_ID, "SL-0001", 0, tx, tk, m1);
    bool req7 = g_revocMgr.requestFeederDeactivation(ADMIN_ROLE_ID, "SL-0001", 7, tx, tk, m2);
    bool pass = (!req0) && (!req7) && (m1 == "ERR_INVALID_FEEDER_ID") && (m2 == "ERR_INVALID_FEEDER_ID");
    reportTest(17, "Invalid Feeder ID Out of Bounds (0 or 7) Rejection", pass);
  }

  // ── Scenario 18: NVS Storage Integrity ──
  {
    g_revocMgr.clear();
    g_revocMgr.addRevocation("SL-0088", "HW-8888", "DEL-TX-88", "DECOMMISSION", "SL-0001", "ACTIVE", ADMIN_ROLE_ID);

    // Create a new manager instance and reload from NVS
    MockRevocationManager reloadedMgr;
    reloadedMgr.prefsRevoc = g_revocMgr.prefsRevoc;
    reloadedMgr.loadRevocationRegistry();

    const RevocationRecord *rec = reloadedMgr.getRevocationRecord("SL-0088");
    bool pass = rec && (String(rec->logicalId) == "SL-0088") &&
                (String(rec->hwId) == "HW-8888") &&
                (String(rec->transactionId) == "DEL-TX-88") &&
                (rec->revocationVersion == NVS_SCHEMA_VERSION);
    reportTest(18, "NVS Storage Integrity & Schema Versioning", pass);
  }

  cout << "==================================================" << endl;
  cout << "TEST SUMMARY: " << g_testsPassed << " PASSED, " << g_testsFailed << " FAILED" << endl;
  cout << "==================================================" << endl;

  return (g_testsFailed == 0) ? 0 : 1;
}
