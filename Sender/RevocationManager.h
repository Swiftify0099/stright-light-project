/*
 * =====================================================================
 *  REVOCATIONMANAGER.H — POLE / FEEDER DELETE & REVOCATION SYSTEM
 *  MSCB Smart Pole & Master Central Gateway Framework (Prompt 12)
 * =====================================================================
 */

#ifndef REVOCATION_MANAGER_H
#define REVOCATION_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "Types.h"
#include "AuthSecurity.h"

class RevocationManager {
public:
  RevocationManager();
  void init();

  // ─────────────────────────────────────────────────────────────
  //  STATE MACHINE & TRANSACTION MANAGEMENT
  // ─────────────────────────────────────────────────────────────
  PoleLifecycleState getPoleState(const String &poleId) const;
  bool isDeletionInProgress() const;
  const DeletionTransaction& getCurrentTransaction() const;

  // Crash recovery & consistency on gateway boot
  void checkCrashRecovery();
  void checkTimeout();

  // ─────────────────────────────────────────────────────────────
  //  PERSISTENT REVOCATION REGISTRY
  // ─────────────────────────────────────────────────────────────
  bool isRevoked(const String &nodeOrHwId) const;
  const RevocationRecord* getRevocationRecord(const String &nodeOrHwId) const;
  bool addRevocation(const String &logicalId, const String &hwId, const String &txId,
                     const String &reason, const String &prevParent, const String &prevRegState,
                     const String &adminId);
  int  getRevokedCount() const;
  const RevocationRecord* getRevocationByIndex(int index) const;

  void loadRevocationRegistry();
  void saveRevocationRegistry();
  void saveTransactionJournal();
  void clearTransactionJournal();

  // ─────────────────────────────────────────────────────────────
  //  CHILD NODE ROUTE MIGRATION ENGINE
  // ─────────────────────────────────────────────────────────────
  bool computeDeletionImpact(const String &poleId, DeletionImpact &impact);
  bool executeChildRouteMigration(const String &poleId, DeletionImpact &impact, String &outLog);

  // ─────────────────────────────────────────────────────────────
  //  TELEGRAM INTEGRATION PREPARATION APIS (PROMPT 12)
  // ─────────────────────────────────────────────────────────────
  String listRegisteredNodes();
  String getNodeDetails(const String &poleId);
  bool   getDeletionImpact(const String &poleId, DeletionImpact &impact);
  bool   requestNodeDeletion(const String &adminId, const String &poleId,
                             String &outTxId, String &outConfirmToken, String &outMessage);
  bool   confirmNodeDeletion(const String &adminId, const String &txId,
                             const String &confirmToken, String &outResultMsg);
  bool   cancelNodeDeletion(const String &adminId, const String &txId, String &outMessage);
  String getDeletionStatus(const String &txId);
  String listRevokedNodes();
  bool   requestFeederDeactivation(const String &adminId, const String &poleId, uint8_t feederId,
                                   String &outTxId, String &outConfirmToken, String &outMessage);
  bool   confirmFeederDeactivation(const String &adminId, const String &txId,
                                   const String &confirmToken, String &outResultMsg);
  String getChildRouteMigrationStatus(const String &poleId);

private:
  Preferences prefsRevoc;
  Preferences prefsTx;

  RevocationRecord revokedNodes[MAX_REVOKED_NODES];
  int revocationCount;

  DeletionTransaction currentTx;
  DeletionImpact cachedImpact;

  // Internal helpers
  bool isDescendantOf(const String &targetNodeId, const String &candidateParentId);
  String selectAlternativeParent(const String &childId, const String &deletedNodeId,
                                 int &outHops, int &outRssi, String &outBackup);
};

extern RevocationManager revocationMgr;

#endif // REVOCATION_MANAGER_H
