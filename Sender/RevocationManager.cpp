/*
 * =====================================================================
 *  REVOCATIONMANAGER.CPP — POLE / FEEDER DELETE & REVOCATION SYSTEM
 *  MSCB Smart Pole & Master Central Gateway Framework (Prompt 12)
 * =====================================================================
 */

#include "RevocationManager.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "StorageManager.h"
#include "FeederManager.h"
#include <time.h>

RevocationManager revocationMgr;

RevocationManager::RevocationManager() : revocationCount(0) {
  memset(&currentTx, 0, sizeof(currentTx));
  currentTx.inProgress = false;
  currentTx.state = POLE_STATE_ACTIVE;

  for (int i = 0; i < MAX_REVOKED_NODES; i++) {
    revokedNodes[i].valid = false;
    memset(revokedNodes[i].logicalId, 0, sizeof(revokedNodes[i].logicalId));
    memset(revokedNodes[i].hwId, 0, sizeof(revokedNodes[i].hwId));
    memset(revokedNodes[i].transactionId, 0, sizeof(revokedNodes[i].transactionId));
  }
}

void RevocationManager::init() {
  loadRevocationRegistry();
  checkCrashRecovery();
  Serial.printf("[REVOC] Initialized. %d revoked nodes loaded.\n", revocationCount);
}

// ─────────────────────────────────────────────────────────────
//  STATE MACHINE & TRANSACTION QUERY
// ─────────────────────────────────────────────────────────────
PoleLifecycleState RevocationManager::getPoleState(const String &poleId) const {
  if (currentTx.inProgress && currentTx.type == DEL_TYPE_POLE && String(currentTx.targetId) == poleId) {
    return currentTx.state;
  }
  int idx = meshMgr.findNodeIndex(poleId);
  if (idx >= 0) {
    const NodeRecord *n = meshMgr.getNode(idx);
    if (n) {
      if (isRevoked(n->logicalId) || isRevoked(n->hwId)) {
        return POLE_STATE_DE_REGISTERED;
      }
      return n->lifecycleState;
    }
  }
  if (isRevoked(poleId)) return POLE_STATE_DE_REGISTERED;
  return POLE_STATE_ACTIVE;
}

bool RevocationManager::isDeletionInProgress() const {
  return currentTx.inProgress;
}

const DeletionTransaction& RevocationManager::getCurrentTransaction() const {
  return currentTx;
}

// ─────────────────────────────────────────────────────────────
//  NVS PERSISTENCE & CRASH RECOVERY (REQUIREMENT 8)
// ─────────────────────────────────────────────────────────────
void RevocationManager::loadRevocationRegistry() {
  prefsRevoc.begin(NVS_NS_REVOC, true);
  revocationCount = prefsRevoc.getInt("count", 0);
  if (revocationCount > MAX_REVOKED_NODES) revocationCount = MAX_REVOKED_NODES;

  for (int i = 0; i < revocationCount; i++) {
    char pfx[12];
    snprintf(pfx, sizeof(pfx), "r%d_", i);
    String logId = prefsRevoc.getString((String(pfx) + "id").c_str(), "");
    String hwId  = prefsRevoc.getString((String(pfx) + "hw").c_str(), "");
    String txId  = prefsRevoc.getString((String(pfx) + "tx").c_str(), "");
    String rsn   = prefsRevoc.getString((String(pfx) + "rs").c_str(), "REVOKED");
    String prnt  = prefsRevoc.getString((String(pfx) + "pr").c_str(), "");
    String rst   = prefsRevoc.getString((String(pfx) + "st").c_str(), "ACTIVE");
    String by    = prefsRevoc.getString((String(pfx) + "by").c_str(), ADMIN_ROLE_ID);
    uint32_t tm  = prefsRevoc.getUInt((String(pfx) + "tm").c_str(), 0);
    uint8_t  ver = prefsRevoc.getUChar((String(pfx) + "vr").c_str(), 1);

    if (logId.length() > 0 || hwId.length() > 0) {
      strncpy(revokedNodes[i].logicalId, logId.c_str(), sizeof(revokedNodes[i].logicalId) - 1);
      strncpy(revokedNodes[i].hwId, hwId.c_str(), sizeof(revokedNodes[i].hwId) - 1);
      strncpy(revokedNodes[i].transactionId, txId.c_str(), sizeof(revokedNodes[i].transactionId) - 1);
      strncpy(revokedNodes[i].reason, rsn.c_str(), sizeof(revokedNodes[i].reason) - 1);
      strncpy(revokedNodes[i].previousParent, prnt.c_str(), sizeof(revokedNodes[i].previousParent) - 1);
      strncpy(revokedNodes[i].previousRegState, rst.c_str(), sizeof(revokedNodes[i].previousRegState) - 1);
      strncpy(revokedNodes[i].revokedBy, by.c_str(), sizeof(revokedNodes[i].revokedBy) - 1);
      revokedNodes[i].revocationTime = tm;
      revokedNodes[i].revocationVersion = ver;
      revokedNodes[i].valid = true;
    }
  }
  prefsRevoc.end();
}

void RevocationManager::saveRevocationRegistry() {
  prefsRevoc.begin(NVS_NS_REVOC, false);
  prefsRevoc.putInt("count", revocationCount);
  prefsRevoc.putInt("schema_ver", NVS_SCHEMA_VERSION);

  for (int i = 0; i < revocationCount; i++) {
    if (!revokedNodes[i].valid) continue;
    char pfx[12];
    snprintf(pfx, sizeof(pfx), "r%d_", i);
    prefsRevoc.putString((String(pfx) + "id").c_str(), revokedNodes[i].logicalId);
    prefsRevoc.putString((String(pfx) + "hw").c_str(), revokedNodes[i].hwId);
    prefsRevoc.putString((String(pfx) + "tx").c_str(), revokedNodes[i].transactionId);
    prefsRevoc.putString((String(pfx) + "rs").c_str(), revokedNodes[i].reason);
    prefsRevoc.putString((String(pfx) + "pr").c_str(), revokedNodes[i].previousParent);
    prefsRevoc.putString((String(pfx) + "st").c_str(), revokedNodes[i].previousRegState);
    prefsRevoc.putString((String(pfx) + "by").c_str(), revokedNodes[i].revokedBy);
    prefsRevoc.putUInt((String(pfx) + "tm").c_str(), revokedNodes[i].revocationTime);
    prefsRevoc.putUChar((String(pfx) + "vr").c_str(), revokedNodes[i].revocationVersion);
  }
  prefsRevoc.end();
}

void RevocationManager::saveTransactionJournal() {
  prefsTx.begin(NVS_NS_DELTX, false);
  prefsTx.putBool("in_prog", currentTx.inProgress);
  prefsTx.putString("tx_id", currentTx.txId);
  prefsTx.putInt("type", (int)currentTx.type);
  prefsTx.putString("target", currentTx.targetId);
  prefsTx.putUChar("fdr_id", currentTx.feederId);
  prefsTx.putString("admin", currentTx.adminId);
  prefsTx.putString("token", currentTx.confirmationToken);
  prefsTx.putInt("state", (int)currentTx.state);
  prefsTx.putUInt("start", currentTx.startedAt);
  prefsTx.putUInt("exp", currentTx.expiresAt);
  prefsTx.putBool("conf", currentTx.confirmed);
  prefsTx.putBool("rev_com", currentTx.revocationCommitted);
  prefsTx.end();
}

void RevocationManager::clearTransactionJournal() {
  prefsTx.begin(NVS_NS_DELTX, false);
  prefsTx.putBool("in_prog", false);
  prefsTx.putString("tx_id", "");
  prefsTx.end();
}

void RevocationManager::checkCrashRecovery() {
  prefsTx.begin(NVS_NS_DELTX, true);
  bool inProg = prefsTx.getBool("in_prog", false);
  if (inProg) {
    String txId   = prefsTx.getString("tx_id", "");
    int type      = prefsTx.getInt("type", 0);
    String target = prefsTx.getString("target", "");
    String admin  = prefsTx.getString("admin", ADMIN_ROLE_ID);
    int state     = prefsTx.getInt("state", 0);
    prefsTx.end();

    Serial.printf("[REVOC][RECOVERY] ⚠️ Unfinished deletion transaction %s for target %s (state: %d)\n",
                  txId.c_str(), target.c_str(), state);

    if (type == (int)DEL_TYPE_POLE) {
      // REQUIREMENT 8: Revoke authorization first, resume cleanup, avoid restoring as Active.
      NodeRecord *node = meshMgr.findNode(target);
      String hwId = node ? node->hwId : target;
      String prevParent = node ? node->primaryNextHop : "UNKNOWN";

      addRevocation(target, hwId, txId, "CRASH_RECOVERY_DECOMMISSION", prevParent, "DELETION_IN_PROGRESS", admin);
      meshMgr.markNodeDeregistered(target);

      storageMgr.addGwLog("CRASH_RECOVERY", target.c_str(), 0);
      Serial.printf("[REVOC][RECOVERY] ✅ Node %s safely de-registered and committed to persistent revocation.\n", target.c_str());
    }

    clearTransactionJournal();
  } else {
    prefsTx.end();
  }
}

void RevocationManager::checkTimeout() {
  if (currentTx.inProgress && !currentTx.confirmed) {
    if (millis() >= (unsigned long)currentTx.expiresAt) {
      Serial.printf("[REVOC] ⏱️ Deletion transaction %s timed out. Aborting.\n", currentTx.txId);
      int idx = meshMgr.findNodeIndex(currentTx.targetId);
      if (idx >= 0) {
        NodeRecord *n = meshMgr.getNode(idx);
        if (n && n->lifecycleState == POLE_STATE_DELETE_CONFIRMATION_REQUIRED) {
          n->lifecycleState = POLE_STATE_ACTIVE;
        }
      }
      currentTx.inProgress = false;
      currentTx.state = POLE_STATE_ACTIVE;
      clearTransactionJournal();
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  PERSISTENT REVOCATION REGISTRY (REQUIREMENTS 5 & 6)
// ─────────────────────────────────────────────────────────────
bool RevocationManager::isRevoked(const String &nodeOrHwId) const {
  if (nodeOrHwId.length() == 0) return false;
  for (int i = 0; i < revocationCount; i++) {
    if (revokedNodes[i].valid) {
      if (nodeOrHwId.equalsIgnoreCase(revokedNodes[i].logicalId) ||
          nodeOrHwId.equalsIgnoreCase(revokedNodes[i].hwId)) {
        return true;
      }
    }
  }
  return false;
}

const RevocationRecord* RevocationManager::getRevocationRecord(const String &nodeOrHwId) const {
  for (int i = 0; i < revocationCount; i++) {
    if (revokedNodes[i].valid) {
      if (nodeOrHwId.equalsIgnoreCase(revokedNodes[i].logicalId) ||
          nodeOrHwId.equalsIgnoreCase(revokedNodes[i].hwId)) {
        return &revokedNodes[i];
      }
    }
  }
  return nullptr;
}

bool RevocationManager::addRevocation(const String &logicalId, const String &hwId, const String &txId,
                                     const String &reason, const String &prevParent, const String &prevRegState,
                                     const String &adminId) {
  if (isRevoked(logicalId) || isRevoked(hwId)) {
    Serial.printf("[REVOC] Node %s already in revocation registry.\n", logicalId.c_str());
    return true;
  }

  // Bounded storage: check retention policy before compaction
  if (revocationCount >= MAX_REVOKED_NODES) {
    Serial.println(F("[REVOC] ⚠️ Revocation registry full! Inspecting retention policy..."));
    time_t now = time(nullptr);
    int oldestIdx = -1;
    uint32_t oldestTime = 0xFFFFFFFF;

    for (int i = 0; i < revocationCount; i++) {
      if (now > 1700000000UL && revokedNodes[i].revocationTime > 0) {
        uint32_t ageDays = (now - revokedNodes[i].revocationTime) / 86400;
        if (ageDays >= REVOCATION_RETENTION_DAYS && revokedNodes[i].revocationTime < oldestTime) {
          oldestTime = revokedNodes[i].revocationTime;
          oldestIdx = i;
        }
      }
    }

    if (oldestIdx >= 0) {
      Serial.printf("[REVOC] Garbage collecting expired record: %s\n", revokedNodes[oldestIdx].logicalId);
      for (int i = oldestIdx; i < revocationCount - 1; i++) {
        revokedNodes[i] = revokedNodes[i + 1];
      }
      revocationCount--;
    } else {
      Serial.println(F("[REVOC] ❌ Cannot evict records without meeting retention policy!"));
      return false;
    }
  }

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
  if (revokedNodes[idx].revocationTime < 1700000000UL) {
    revokedNodes[idx].revocationTime = (uint32_t)(millis() / 1000);
  }
  revokedNodes[idx].revocationVersion = NVS_SCHEMA_VERSION;
  revokedNodes[idx].valid = true;

  saveRevocationRegistry();
  Serial.printf("[REVOC] 🔒 Node %s (HW:%s) permanently REVOKED. Tx: %s\n",
                logicalId.c_str(), hwId.c_str(), txId.c_str());
  return true;
}

int RevocationManager::getRevokedCount() const {
  return revocationCount;
}

const RevocationRecord* RevocationManager::getRevocationByIndex(int index) const {
  if (index >= 0 && index < revocationCount) {
    return &revokedNodes[index];
  }
  return nullptr;
}

// ─────────────────────────────────────────────────────────────
//  CHILD NODE ROUTE MIGRATION (REQUIREMENT 7)
// ─────────────────────────────────────────────────────────────
bool RevocationManager::isDescendantOf(const String &targetNodeId, const String &candidateParentId) {
  if (targetNodeId == candidateParentId) return true;
  String current = targetNodeId;
  int guard = 0;

  while (current.length() > 0 && current != GATEWAY_ID && current != BROADCAST_ID && guard < 10) {
    guard++;
    NodeRecord *n = meshMgr.findNode(current);
    if (!n) break;
    if (n->primaryNextHop == candidateParentId || n->backupNextHop == candidateParentId) {
      return true;
    }
    current = n->primaryNextHop;
  }
  return false;
}

String RevocationManager::selectAlternativeParent(const String &childId, const String &deletedNodeId,
                                                  int &outHops, int &outRssi, String &outBackup) {
  outHops = 1;
  outRssi = -80;
  outBackup = BROADCAST_ID;

  String bestPrimary = "";
  int bestScore = -9999;
  String bestSec = "";
  int bestSecScore = -9999;

  int nbCount = meshMgr.getNeighborSummaryCount();
  for (int i = 0; i < nbCount; i++) {
    NeighborSummary *nb = meshMgr.getNeighborSummary(i);
    if (!nb || !nb->valid || !nb->responded) continue;

    String candidate = "";
    int candRssi = nb->rssi;

    if (nb->reportingNode == childId && nb->neighborId != deletedNodeId && nb->neighborId != childId) {
      candidate = nb->neighborId;
    } else if (nb->neighborId == childId && nb->reportingNode != deletedNodeId && nb->reportingNode != childId) {
      candidate = nb->reportingNode;
    }

    if (candidate.length() > 0) {
      // Loop prevention: Candidate cannot be a descendant of child or deleted node!
      if (candidate == deletedNodeId || isDescendantOf(candidate, deletedNodeId) || isDescendantOf(candidate, childId)) {
        continue;
      }

      NodeRecord *candNode = meshMgr.findNode(candidate);
      if (!candNode || !candNode->registered || candNode->onlineState == "OFFLINE") continue;
      if (candNode->lifecycleState != POLE_STATE_ACTIVE) continue;

      // Score: better RSSI + fewer hops
      int score = candRssi - (candNode->hopCount * 15);

      if (score > bestScore) {
        bestSec = bestPrimary;
        bestSecScore = bestScore;
        bestPrimary = candidate;
        bestScore = score;
        outHops = candNode->hopCount + 1;
        outRssi = candRssi;
      } else if (score > bestSecScore && candidate != bestPrimary) {
        bestSec = candidate;
        bestSecScore = score;
      }
    }
  }

  // If no summary was found, check if child has direct link or registered neighbor with active route
  if (bestPrimary.length() == 0) {
    int totalNodes = meshMgr.getNodeCount();
    for (int i = 0; i < totalNodes; i++) {
      NodeRecord *candNode = meshMgr.getNode(i);
      if (!candNode || !candNode->registered || candNode->logicalId == childId || candNode->logicalId == deletedNodeId) continue;
      if (candNode->onlineState == "OFFLINE" || candNode->lifecycleState != POLE_STATE_ACTIVE) continue;
      if (isDescendantOf(candNode->logicalId, deletedNodeId) || isDescendantOf(candNode->logicalId, childId)) continue;

      // Candidate has valid next hop to Gateway
      if (candNode->primaryNextHop.length() > 0 && candNode->primaryNextHop != deletedNodeId) {
        bestPrimary = candNode->logicalId;
        outHops = candNode->hopCount + 1;
        outRssi = -85;
        break;
      }
    }
  }

  outBackup = (bestSec.length() > 0) ? bestSec : BROADCAST_ID;
  return bestPrimary;
}

bool RevocationManager::computeDeletionImpact(const String &poleId, DeletionImpact &impact) {
  memset(&impact, 0, sizeof(DeletionImpact));
  strncpy(impact.poleId, poleId.c_str(), sizeof(impact.poleId) - 1);

  NodeRecord *target = meshMgr.findNode(poleId);
  if (!target) {
    strncpy(impact.status, "NOT_FOUND", sizeof(impact.status) - 1);
    return false;
  }

  strncpy(impact.poleName, target->name.c_str(), sizeof(impact.poleName) - 1);
  strncpy(impact.area, target->location.c_str(), sizeof(impact.area) - 1);
  strncpy(impact.status, target->onlineState.c_str(), sizeof(impact.status) - 1);
  impact.isOnline = (target->onlineState == "ONLINE");

  // Count active feeders
  for (int f = 0; f < MAX_FEEDERS; f++) {
    if (target->feeders[f].status == FEEDER_STATUS_ACTIVE) {
      impact.activeFeederCount++;
    }
  }

  // Scan mesh topology for children
  int nodeCount = meshMgr.getNodeCount();
  for (int i = 0; i < nodeCount; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n || !n->registered || n->logicalId == poleId) continue;

    bool isDirect = (n->primaryNextHop == poleId || n->backupNextHop == poleId);
    bool isDownstream = isDescendantOf(n->logicalId, poleId);

    if (isDirect || isDownstream) {
      if (isDirect) impact.directChildCount++;
      impact.totalDownstreamCount++;

      if (impact.migrationPlanCount < MAX_NODES) {
        int idx = impact.migrationPlanCount++;
        strncpy(impact.children[idx].childId, n->logicalId.c_str(), sizeof(impact.children[idx].childId) - 1);
        strncpy(impact.children[idx].previousParent, n->primaryNextHop.c_str(), sizeof(impact.children[idx].previousParent) - 1);

        int newHops = 1;
        int newRssi = -80;
        String backupParent = "";
        String newParent = selectAlternativeParent(n->logicalId, poleId, newHops, newRssi, backupParent);

        if (newParent.length() > 0) {
          strncpy(impact.children[idx].newPrimaryParent, newParent.c_str(), sizeof(impact.children[idx].newPrimaryParent) - 1);
          strncpy(impact.children[idx].newBackupParent, backupParent.c_str(), sizeof(impact.children[idx].newBackupParent) - 1);
          impact.children[idx].newHopCount = newHops;
          impact.children[idx].linkRssi = newRssi;
          impact.children[idx].canMigrate = true;
          impact.children[idx].isOrphaned = false;
          strncpy(impact.children[idx].reason, "ALTERNATIVE_FOUND", sizeof(impact.children[idx].reason) - 1);
        } else {
          impact.children[idx].canMigrate = false;
          impact.children[idx].isOrphaned = true;
          impact.hasOrphansWithoutRoute = true;
          strncpy(impact.children[idx].newPrimaryParent, BROADCAST_ID, sizeof(impact.children[idx].newPrimaryParent) - 1);
          strncpy(impact.children[idx].newBackupParent, BROADCAST_ID, sizeof(impact.children[idx].newBackupParent) - 1);
          strncpy(impact.children[idx].reason, "NO_VALID_PARENT", sizeof(impact.children[idx].reason) - 1);
        }
      }
    }
  }

  impact.affectedRouteCount = impact.totalDownstreamCount;
  cachedImpact = impact;
  return true;
}

bool RevocationManager::executeChildRouteMigration(const String &poleId, DeletionImpact &impact, String &outLog) {
  outLog = "";
  Serial.printf("[REVOC][MIGRATE] Reassigning %d children of deleted pole %s...\n",
                impact.migrationPlanCount, poleId.c_str());

  for (int i = 0; i < impact.migrationPlanCount; i++) {
    ChildMigrationPlan &plan = impact.children[i];

    if (plan.canMigrate) {
      meshMgr.updateNodeParent(plan.childId, plan.newPrimaryParent, plan.newBackupParent, plan.newHopCount);
      outLog += "• `" + String(plan.childId) + "` ➔ New Parent: `" + String(plan.newPrimaryParent) + "` (Hops: " + String(plan.newHopCount) + ")\n";

      // Dispatch authenticated route migration packet
      String cmdPayload = "ROUTE_MIGRATE|" + String(plan.childId) + "|" + String(plan.newPrimaryParent) + "|" + String(plan.newBackupParent);
      String sig = authSec.signPayload(cmdPayload, loraMgr.getNextSeq(), currentTx.txId);
      loraMgr.queueCommand("", plan.childId, cmdPayload + "|" + sig, PRIO_HIGH);
    } else {
      meshMgr.updateNodeParent(plan.childId, BROADCAST_ID, BROADCAST_ID, 255);
      outLog += "• ⚠️ `" + String(plan.childId) + "` ➔ ORPHANED (Controlled route discovery initiated)\n";
    }
  }
  return true;
}

// ─────────────────────────────────────────────────────────────
//  TELEGRAM INTEGRATION PREPARATION APIS (PROMPT 12)
// ─────────────────────────────────────────────────────────────
String RevocationManager::listRegisteredNodes() {
  String out = "📋 *REGISTERED POLE NODES (ACTIVE & DEGRADED)*\n";
  out += "━━━━━━━━━━━━━━━━━━━━\n";
  int count = meshMgr.getNodeCount();
  int regCount = 0;

  for (int i = 0; i < count; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n || !n->registered) continue;
    regCount++;

    String icon = (n->onlineState == "ONLINE") ? "🟢" : (n->onlineState == "DEGRADED" ? "🟡" : "🔴");
    out += icon + " *" + n->name + "* (`" + n->logicalId + "`)\n";
    out += "   ├ HW ID: `" + n->hwId + "` | Loc: " + n->location + "\n";
    out += "   ├ State: " + String(poleLifecycleStateToString(n->lifecycleState)) + " | Hops: " + String(n->hopCount) + "\n";
    out += "   └ Next Hop: `" + n->primaryNextHop + "`\n\n";
  }

  if (regCount == 0) out += "No registered nodes found in registry.\n";
  out += "━━━━━━━━━━━━━━━━━━━━";
  return out;
}

String RevocationManager::getNodeDetails(const String &poleId) {
  NodeRecord *n = meshMgr.findNode(poleId);
  if (!n) return "❌ Node `" + poleId + "` not found in registry.";

  String out = "💡 *POLE NODE FULL DETAILS*\n";
  out += "━━━━━━━━━━━━━━━━━━━━\n";
  out += "📍 *Name:* " + n->name + "\n";
  out += "🆔 *Logical ID:* `" + n->logicalId + "`\n";
  out += "⚙️ *HW Chip ID:* `" + n->hwId + "`\n";
  out += "🗺️ *Area / Location:* " + n->location + "\n";
  out += "📶 *Online State:* `" + n->onlineState + "` (" + String(n->rssi) + " dBm)\n";
  out += "🔄 *Lifecycle State:* `" + String(poleLifecycleStateToString(n->lifecycleState)) + "`\n";
  out += "📡 *Next Hop (Primary):* `" + n->primaryNextHop + "`\n";
  out += "📡 *Next Hop (Backup):* `" + n->backupNextHop + "`\n";
  out += "📶 *Hop Count to Root:* `" + String(n->hopCount) + "`\n";
  out += "⚡ *Common Voltage (R/Y/B):* " + String(n->voltage, 1) + " V / " + String(n->commonVoltageY, 1) + " V / " + String(n->commonVoltageB, 1) + " V\n";
  out += "🔌 *Total Active Power:* " + String(n->power, 1) + " W\n";
  out += "━━━━━━━━━━━━━━━━━━━━\n";
  out += "📊 *Feeders (1–6):*\n";
  for (int f = 0; f < MAX_FEEDERS; f++) {
    out += "  [" + String(f + 1) + "] " + n->feeders[f].name + ": " +
           feederStatusToString(n->feeders[f].status) + " (" + n->feeders[f].relayState + ")\n";
  }
  out += "━━━━━━━━━━━━━━━━━━━━";
  return out;
}

bool RevocationManager::getDeletionImpact(const String &poleId, DeletionImpact &impact) {
  return computeDeletionImpact(poleId, impact);
}

bool RevocationManager::requestNodeDeletion(const String &adminId, const String &poleId,
                                            String &outTxId, String &outConfirmToken, String &outMessage) {
  checkTimeout();

  // 1. Authorization verification
  if (!authSec.verifyAdmin(adminId)) {
    outMessage = "⛔ *UNAUTHORIZED:* Only the authoritative Main Gateway Administrator can request deletion.";
    return false;
  }

  // 2. Duplicate operation prevention
  if (currentTx.inProgress) {
    outMessage = "⚠️ *OPERATION IN PROGRESS:* Transaction `" + String(currentTx.txId) + "` is currently pending confirmation.";
    return false;
  }

  // 3. Node lookup & state validation
  NodeRecord *target = meshMgr.findNode(poleId);
  if (!target || !target->registered) {
    outMessage = "❌ *NOT FOUND:* Node `" + poleId + "` does not exist or is not registered.";
    return false;
  }

  if (isRevoked(poleId) || target->lifecycleState == POLE_STATE_DE_REGISTERED) {
    outMessage = "⚠️ *ALREADY REVOKED:* Node `" + poleId + "` has already been de-registered and revoked.";
    return false;
  }

  // 4. Compute impact analysis
  DeletionImpact impact;
  computeDeletionImpact(poleId, impact);

  // 5. Generate secure Transaction ID & Confirmation Token
  outTxId = authSec.generateTransactionId("DEL", poleId);
  outConfirmToken = authSec.generateConfirmationToken();

  // 6. Transition state machine
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
  currentTx.confirmed = false;
  currentTx.revocationCommitted = false;

  saveTransactionJournal();

  // 7. Format Impact Report
  outMessage = "⚠️ *POLE DELETION & REVOCATION IMPACT REPORT*\n";
  outMessage += "━━━━━━━━━━━━━━━━━━━━\n";
  outMessage += "📍 *Target Pole:* *" + target->name + "* (`" + target->logicalId + "`)\n";
  outMessage += "⚙️ *HW Chip ID:* `" + target->hwId + "`\n";
  outMessage += "🗺️ *Area:* " + target->location + "\n";
  outMessage += "📶 *Current Status:* `" + target->onlineState + "`\n";
  outMessage += "👶 *Direct Children:* `" + String(impact.directChildCount) + "`\n";
  outMessage += "🌐 *Total Downstream Nodes:* `" + String(impact.totalDownstreamCount) + "`\n";
  outMessage += "⚡ *Active Feeders Affected:* `" + String(impact.activeFeederCount) + "`\n\n";

  if (impact.migrationPlanCount > 0) {
    outMessage += "🔄 *Planned Child Route Migrations:*\n";
    for (int i = 0; i < impact.migrationPlanCount; i++) {
      if (impact.children[i].canMigrate) {
        outMessage += "  • `" + String(impact.children[i].childId) + "` ➔ New: `" + String(impact.children[i].newPrimaryParent) + "`\n";
      } else {
        outMessage += "  • ⚠️ `" + String(impact.children[i].childId) + "` ➔ *ORPHAN (No alternative parent)*\n";
      }
    }
    outMessage += "\n";
  }

  outMessage += "🔐 *Transaction ID:* `" + outTxId + "`\n";
  outMessage += "🔑 *Confirmation Token:* `" + outConfirmToken + "`\n";
  outMessage += "⏱️ *Expires In:* 60 seconds\n";
  outMessage += "━━━━━━━━━━━━━━━━━━━━\n";
  outMessage += "Send confirmation with Token to permanently de-register.";
  return true;
}

bool RevocationManager::confirmNodeDeletion(const String &adminId, const String &txId,
                                             const String &confirmToken, String &outResultMsg) {
  checkTimeout();

  // 1. Authorization check
  if (!authSec.verifyAdmin(adminId)) {
    outResultMsg = "⛔ *ACCESS DENIED:* Unauthorized administrator identity.";
    return false;
  }

  // 2. Transaction validation
  if (!currentTx.inProgress || String(currentTx.txId) != txId) {
    outResultMsg = "❌ *INVALID TRANSACTION:* Transaction `" + txId + "` not found or already completed.";
    return false;
  }

  // 3. Confirmation token verification
  if (String(currentTx.confirmationToken) != confirmToken) {
    outResultMsg = "❌ *SECURITY VERIFICATION FAILED:* Incorrect confirmation token.";
    return false;
  }

  String targetId = String(currentTx.targetId);
  NodeRecord *target = meshMgr.findNode(targetId);
  String hwId = target ? target->hwId : targetId;
  String prevParent = target ? target->primaryNextHop : "UNKNOWN";
  bool isOnline = (target && target->onlineState == "ONLINE");

  // State Transition: DELETION_APPROVED -> REVOCATION_PENDING
  currentTx.state = POLE_STATE_DELETION_APPROVED;
  currentTx.confirmed = true;
  if (target) target->lifecycleState = POLE_STATE_REVOCATION_PENDING;

  // 4. Add to persistent Revocation Registry & write NVS immediately (Requirement 5 & 8)
  addRevocation(targetId, hwId, txId, "ADMINISTRATIVE_DECOMMISSION", prevParent, "REGISTERED", adminId);
  currentTx.revocationCommitted = true;
  saveTransactionJournal();

  // 5. LoRa Revocation Distribution
  String revokePayload = "REVOKE|" + txId;
  String sig = authSec.signPayload(revokePayload, loraMgr.getNextSeq(), txId);

  if (isOnline) {
    // If online, dispatch directed REVOKE command
    loraMgr.queueCommand("", targetId, revokePayload + "|" + sig, PRIO_HIGH);
    Serial.printf("[REVOC] Directed REVOKE sent to online node %s\n", targetId.c_str());
  }

  // Broadcast REVOKE_SYNC so intermediate nodes drop it from neighbor & routing tables
  String syncPayload = "REVOKE_SYNC|" + targetId + "|" + txId + "|" + sig;
  loraMgr.dispatchLoRaPacket("CMD", BROADCAST_ID, BROADCAST_ID, DEFAULT_TTL, loraMgr.getNextSeq(), syncPayload);

  // State Transition: CHILD_ROUTE_MIGRATION
  currentTx.state = POLE_STATE_CHILD_ROUTE_MIGRATION;
  if (target) target->lifecycleState = POLE_STATE_CHILD_ROUTE_MIGRATION;

  // 6. Child route migration & loop-free reassignment (Requirement 7)
  DeletionImpact impact;
  computeDeletionImpact(targetId, impact);
  String migrationLog = "";
  executeChildRouteMigration(targetId, impact, migrationLog);
  currentTx.routesMigrated = true;

  // 7. Cleanup routing tables and mark DE-REGISTERED -> ARCHIVED (Requirement 4)
  meshMgr.markNodeDeregistered(targetId);
  storageMgr.saveDatabaseToNVS();
  storageMgr.addGwLog("NODE_DELETED", targetId.c_str(), 0);

  currentTx.state = POLE_STATE_ARCHIVED;
  if (target) target->lifecycleState = POLE_STATE_ARCHIVED;
  currentTx.inProgress = false;
  clearTransactionJournal();

  outResultMsg = "✅ *POLE DELETION & REVOCATION COMPLETED*\n";
  outResultMsg += "━━━━━━━━━━━━━━━━━━━━\n";
  outResultMsg += "📍 *Node:* `" + targetId + "` (HW: `" + hwId + "`)\n";
  outResultMsg += "🔒 *Authorization:* REVOKED & PERSISTED\n";
  outResultMsg += "🔄 *Mesh Routes:* PURGED & CLEANED\n";
  outResultMsg += "📡 *Status at Deletion:* " + String(isOnline ? "ONLINE (REVOKE Dispatched)" : "OFFLINE (Revoked Authoritatively)") + "\n";
  outResultMsg += "👶 *Children Migrated:* " + String(impact.migrationPlanCount) + "\n\n";
  if (migrationLog.length() > 0) {
    outResultMsg += "*Migration Summary:*\n" + migrationLog + "\n";
  }
  outResultMsg += "━━━━━━━━━━━━━━━━━━━━\n";
  outResultMsg += "Device cannot rejoin without authorized commissioning.";
  return true;
}

bool RevocationManager::cancelNodeDeletion(const String &adminId, const String &txId, String &outMessage) {
  if (!authSec.verifyAdmin(adminId)) {
    outMessage = "⛔ Unauthorized.";
    return false;
  }

  if (!currentTx.inProgress || String(currentTx.txId) != txId) {
    outMessage = "❌ Transaction not found or not active.";
    return false;
  }

  int idx = meshMgr.findNodeIndex(currentTx.targetId);
  if (idx >= 0) {
    NodeRecord *n = meshMgr.getNode(idx);
    if (n) n->lifecycleState = POLE_STATE_ACTIVE;
  }

  currentTx.inProgress = false;
  currentTx.state = POLE_STATE_ACTIVE;
  clearTransactionJournal();

  outMessage = "✅ Deletion transaction `" + txId + "` CANCELLED. Node restored to ACTIVE.";
  return true;
}

String RevocationManager::getDeletionStatus(const String &txId) {
  if (currentTx.inProgress && String(currentTx.txId) == txId) {
    String out = "📊 *DELETION TRANSACTION STATUS*\n";
    out += "Tx ID: `" + txId + "`\n";
    out += "Target: `" + String(currentTx.targetId) + "`\n";
    out += "State: " + String(poleLifecycleStateToString(currentTx.state)) + "\n";
    out += "Confirmed: " + String(currentTx.confirmed ? "YES" : "AWAITING_TOKEN") + "\n";
    out += "Revocation Committed: " + String(currentTx.revocationCommitted ? "YES" : "NO") + "\n";
    return out;
  }

  for (int i = 0; i < revocationCount; i++) {
    if (revokedNodes[i].valid && String(revokedNodes[i].transactionId) == txId) {
      String out = "📊 *COMPLETED REVOCATION STATUS*\n";
      out += "Tx ID: `" + txId + "`\n";
      out += "Target: `" + String(revokedNodes[i].logicalId) + "` (HW: `" + String(revokedNodes[i].hwId) + "`)\n";
      out += "State: ARCHIVED / REVOKED\n";
      out += "Reason: " + String(revokedNodes[i].reason) + "\n";
      return out;
    }
  }
  return "❌ Transaction `" + txId + "` not found.";
}

String RevocationManager::listRevokedNodes() {
  String out = "🔒 *PERSISTENT REVOCATION REGISTRY*\n";
  out += "━━━━━━━━━━━━━━━━━━━━\n";

  if (revocationCount == 0) {
    out += "No nodes currently revoked.\n";
  } else {
    for (int i = 0; i < revocationCount; i++) {
      if (!revokedNodes[i].valid) continue;
      out += "• 🚫 `" + String(revokedNodes[i].logicalId) + "` (HW: `" + String(revokedNodes[i].hwId) + "`)\n";
      out += "   ├ Tx: `" + String(revokedNodes[i].transactionId) + "`\n";
      out += "   ├ Reason: " + String(revokedNodes[i].reason) + "\n";
      out += "   └ Revoked By: `" + String(revokedNodes[i].revokedBy) + "`\n\n";
    }
  }
  out += "━━━━━━━━━━━━━━━━━━━━";
  return out;
}

// ─────────────────────────────────────────────────────────────
//  FEEDER DEACTIVATION (REQUIREMENT 2 & 9)
// ─────────────────────────────────────────────────────────────
bool RevocationManager::requestFeederDeactivation(const String &adminId, const String &poleId, uint8_t feederId,
                                                 String &outTxId, String &outConfirmToken, String &outMessage) {
  if (!authSec.verifyAdmin(adminId)) {
    outMessage = "⛔ *UNAUTHORIZED:* Admin permission required.";
    return false;
  }

  if (feederId < 1 || feederId > MAX_FEEDERS) {
    outMessage = "❌ *INVALID FEEDER ID:* Must be between 1 and 6.";
    return false;
  }

  NodeRecord *target = meshMgr.findNode(poleId);
  if (!target || !target->registered) {
    outMessage = "❌ *NODE NOT FOUND:* `" + poleId + "` is not registered.";
    return false;
  }

  FeederState &fdr = target->feeders[feederId - 1];
  if (fdr.status == FEEDER_STATUS_DEACTIVATED) {
    outMessage = "⚠️ *ALREADY DEACTIVATED:* Feeder " + String(feederId) + " on `" + poleId + "` is already deactivated.";
    return false;
  }

  outTxId = authSec.generateTransactionId("FDR-DEL", poleId + "-F" + String(feederId));
  outConfirmToken = authSec.generateConfirmationToken();

  memset(&currentTx, 0, sizeof(currentTx));
  currentTx.inProgress = true;
  currentTx.type = DEL_TYPE_FEEDER;
  strncpy(currentTx.txId, outTxId.c_str(), sizeof(currentTx.txId) - 1);
  strncpy(currentTx.targetId, poleId.c_str(), sizeof(currentTx.targetId) - 1);
  currentTx.feederId = feederId;
  strncpy(currentTx.adminId, adminId.c_str(), sizeof(currentTx.adminId) - 1);
  strncpy(currentTx.confirmationToken, outConfirmToken.c_str(), sizeof(currentTx.confirmationToken) - 1);
  currentTx.startedAt = (uint32_t)(millis() / 1000);
  currentTx.expiresAt = (uint32_t)(millis() + DELETE_CONFIRMATION_TIMEOUT_MS);

  outMessage = "⚠️ *FEEDER DEACTIVATION REQUEST*\n";
  outMessage += "━━━━━━━━━━━━━━━━━━━━\n";
  outMessage += "📍 *Pole:* *" + target->name + "* (`" + target->logicalId + "`)\n";
  outMessage += "⚡ *Feeder Channel:* Feeder " + String(feederId) + " (" + fdr.name + ")\n";
  outMessage += "🔌 *Current State:* Relay " + fdr.relayState + " | MCB " + fdr.faultState + "\n";
  outMessage += "💡 *Safety Notice:* Deactivation removes channel from service without toggling physical relay.\n";
  outMessage += "🔐 *Transaction ID:* `" + outTxId + "`\n";
  outMessage += "🔑 *Confirmation Token:* `" + outConfirmToken + "`\n";
  outMessage += "━━━━━━━━━━━━━━━━━━━━";
  return true;
}

bool RevocationManager::confirmFeederDeactivation(const String &adminId, const String &txId,
                                                  const String &confirmToken, String &outResultMsg) {
  if (!authSec.verifyAdmin(adminId)) {
    outResultMsg = "⛔ *ACCESS DENIED:* Unauthorized administrator.";
    return false;
  }

  if (!currentTx.inProgress || currentTx.type != DEL_TYPE_FEEDER || String(currentTx.txId) != txId) {
    outResultMsg = "❌ *INVALID TRANSACTION:* No matching feeder deactivation transaction.";
    return false;
  }

  if (String(currentTx.confirmationToken) != confirmToken) {
    outResultMsg = "❌ *VERIFICATION FAILED:* Incorrect confirmation token.";
    return false;
  }

  String poleId = String(currentTx.targetId);
  uint8_t fdrId = currentTx.feederId;

  NodeRecord *target = meshMgr.findNode(poleId);
  if (target && fdrId >= 1 && fdrId <= MAX_FEEDERS) {
    FeederState &fdr = target->feeders[fdrId - 1];
    fdr.status = FEEDER_STATUS_DEACTIVATED;
    fdr.deactivatedAt = millis();

    // Dispatch authenticated deactivation command to Field Pole
    // Note: Field Pole will NOT alter physical relay output!
    String cmd = "FDR_DEACTIVATE|" + String(fdrId);
    String sig = authSec.signPayload(cmd, loraMgr.getNextSeq(), txId);
    loraMgr.queueCommand("", poleId, cmd + "|" + sig, PRIO_HIGH);

    storageMgr.addGwLog("FEEDER_DEACTIVATED", (poleId + "-F" + String(fdrId)).c_str(), 0);
  }

  currentTx.inProgress = false;
  clearTransactionJournal();

  outResultMsg = "✅ *FEEDER DEACTIVATION CONFIRMED*\n";
  outResultMsg += "━━━━━━━━━━━━━━━━━━━━\n";
  outResultMsg += "📍 *Pole:* `" + poleId + "` | *Feeder:* " + String(fdrId) + "\n";
  outResultMsg += "🔒 *Status:* DEACTIVATED (Removed from active control)\n";
  outResultMsg += "⚡ *Relay State:* Preserved safely (NO switching triggered)\n";
  outResultMsg += "📊 *Historical Telemetry:* Retained in archive\n";
  outResultMsg += "━━━━━━━━━━━━━━━━━━━━";
  return true;
}

String RevocationManager::getChildRouteMigrationStatus(const String &poleId) {
  DeletionImpact impact;
  computeDeletionImpact(poleId, impact);

  String out = "🗺️ *CHILD ROUTE MIGRATION STATUS FOR " + poleId + "*\n";
  out += "━━━━━━━━━━━━━━━━━━━━\n";
  out += "👶 Total Children: " + String(impact.migrationPlanCount) + "\n";
  out += "⚠️ Orphaned Count: " + String(impact.hasOrphansWithoutRoute ? "YES" : "NONE") + "\n\n";

  for (int i = 0; i < impact.migrationPlanCount; i++) {
    ChildMigrationPlan &c = impact.children[i];
    out += "• `" + String(c.childId) + "`: " + (c.canMigrate ? "✅ Can Migrate to `" + String(c.newPrimaryParent) + "`" : "🔴 ORPHANED") + "\n";
  }
  out += "━━━━━━━━━━━━━━━━━━━━";
  return out;
}
