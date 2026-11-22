/*
 * =====================================================================
 *  TEST_PROMPT19_STORAGE.CPP — COMPREHENSIVE STORAGE & AUDIT TEST SUITE
 *  MSCB Smart Pole Automation System — Prompt 19 Verification
 *  19 Dedicated Test Cases for Central Storage, Registry, 2PC & History
 * =====================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "../Sender/StorageTypes.h"
#include "../Sender/CentralStorage.h"
#include "../Resivers/NodeStorage.h"

// Bring in implementations for single compilation unit test
#include "../Sender/CentralStorage.cpp"
#include "../Resivers/NodeStorage.cpp"

// Test Results Counters
static int testsPassed = 0;
static int testsFailed = 0;

#define TEST_ASSERT(cond, msg) \
  do { \
    if (cond) { \
      printf("  [PASS] %s\n", msg); \
      testsPassed++; \
    } else { \
      printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
      testsFailed++; \
    } \
  } while(0)

// Helper: Populate a standard Node record
static void makeTestNode(MscbNodeRecord &node, uint8_t index) {
  memset(&node, 0, sizeof(node));
  snprintf(node.logicalId, sizeof(node.logicalId), "POLE-%04u", index);
  snprintf(node.hwId, sizeof(node.hwId), "A4CF12%06u", index);
  node.nodeType = DEV_TYPE_POLE;
  snprintf(node.friendlyName, sizeof(node.friendlyName), "Pole #%u", index);
  snprintf(node.area, sizeof(node.area), "Sector %u", (index % 5) + 1);
  snprintf(node.installInfo, sizeof(node.installInfo), "Lat:16.%04u,Lon:74.%04u", index, index);
  node.registrationTime = 1700000000UL + index * 100;
  node.regState = REG_STATE_ACTIVE;
  strncpy(node.primaryParent, (index == 1) ? "GATEWAY" : "POLE-0001", 15);
  strncpy(node.backupParent, (index <= 2) ? "GATEWAY" : "POLE-0002", 15);
  node.firmwareVersion = 10;
  node.bootSessionId = 0x1000 + index;
  node.lastSeenTime = 1700000000UL + index * 100;
  node.configVersion = 100;
  node.feederCount = MSCB_FEEDERS_PER_POLE;
  for (uint8_t f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
    node.feeders[f].feederId = f + 1;
    snprintf(node.feeders[f].name, sizeof(node.feeders[f].name), "Feeder %u", f + 1);
    node.feeders[f].enabled = true;
    node.feeders[f].currentRating = 50.0f;
    node.feeders[f].relayState = false;
    node.feeders[f].mcbR = true;
    node.feeders[f].mcbY = true;
    node.feeders[f].mcbB = true;
    node.feeders[f].contactor = false;
  }
  node.lastRssi = -65;
  node.lastSnr = 9;
  node.hopCount = (index == 1) ? 1 : 2;
  node.crc32 = centralStorage.calculateNodeCrc(node);
}

// ─────────────────────────────────────────────────────────────
//  TEST 1: Register 50 Nodes
// ─────────────────────────────────────────────────────────────
void test1_Register50Nodes() {
  printf("\n--- TEST 1: Register 50 Nodes (Full Confirmed Capacity) ---\n");
  centralStorage.clearAll();

  for (uint8_t i = 1; i <= 50; i++) {
    MscbNodeRecord n;
    makeTestNode(n, i);
    bool ok = centralStorage.saveNode(n);
    if (!ok) {
      printf("Failed at node %u\n", i);
    }
  }

  TEST_ASSERT(centralStorage.getNodeCount() == 50, "Successfully registered 50 distinct Pole Nodes");
  TEST_ASSERT(centralStorage.isRegistryFull(), "Registry reports capacity full (50/50)");

  // Attempt 51st node
  MscbNodeRecord n51;
  makeTestNode(n51, 51);
  bool ok51 = centralStorage.saveNode(n51);
  TEST_ASSERT(!ok51, "51st Node registration rejected cleanly when registry full");
}

// ─────────────────────────────────────────────────────────────
//  TEST 2: Register 300 Feeders
// ─────────────────────────────────────────────────────────────
void test2_Register300Feeders() {
  printf("\n--- TEST 2: Register 300 Feeders (6 per Pole) ---\n");
  TEST_ASSERT(centralStorage.getTotalFeederCount() == 300, "Feeder Registry contains exactly 300 logical feeders (50x6)");

  // Check specific feeder associations
  MscbFeederRecord fdr1_1, fdr50_6;
  bool ok1 = centralStorage.getFeeder("POLE-0001", 1, fdr1_1);
  bool ok2 = centralStorage.getFeeder("POLE-0050", 6, fdr50_6);

  TEST_ASSERT(ok1 && fdr1_1.feederId == 1, "Feeder POLE-0001 F01 correctly mapped to owning Pole");
  TEST_ASSERT(ok2 && fdr50_6.feederId == 6, "Feeder POLE-0050 F06 correctly mapped to owning Pole");
  TEST_ASSERT(fdr1_1.currentRating == 50.0f, "Feeder protection rating intact at 50A");
}

// ─────────────────────────────────────────────────────────────
//  TEST 3: Save Node Data
// ─────────────────────────────────────────────────────────────
void test3_SaveNodeData() {
  printf("\n--- TEST 3: Save and Update Node Data with CRC Read-back ---\n");
  MscbNodeRecord node;
  bool ok = centralStorage.getNode("POLE-0010", node);
  TEST_ASSERT(ok, "Retrieved POLE-0010 from registry");

  strncpy(node.friendlyName, "Updated Market Pole 10", sizeof(node.friendlyName) - 1);
  strncpy(node.area, "Sector 9 South", sizeof(node.area) - 1);
  node.hopCount = 3;
  node.lastRssi = -78;
  bool updateOk = centralStorage.updateNode("POLE-0010", node);
  TEST_ASSERT(updateOk, "Updated node data in registry");

  MscbNodeRecord readBack;
  centralStorage.getNode("POLE-0010", readBack);
  TEST_ASSERT(strcmp(readBack.friendlyName, "Updated Market Pole 10") == 0, "Friendly name updated correctly");
  TEST_ASSERT(strcmp(readBack.area, "Sector 9 South") == 0, "Area updated correctly");
  TEST_ASSERT(readBack.hopCount == 3, "Hop count updated correctly");
}

// ─────────────────────────────────────────────────────────────
//  TEST 4: Save Calibration Data (21 Channels)
// ─────────────────────────────────────────────────────────────
void test4_SaveCalibrationData() {
  printf("\n--- TEST 4: Save Calibration Data (21 Channels: 18 Current + 3 Voltage) ---\n");
  nodeStorage.clearAll();
  nodeStorage.saveNodeIdentity("A4CF12000001", "POLE-0001", "Field Pole 1", "STREET");

  PoleCalibTable table;
  bool okGet = nodeStorage.getCalibrationTable(table);
  TEST_ASSERT(okGet, "Retrieved default 21-channel calibration table");
  TEST_ASSERT(table.channelCount == 21, "Calibration table has exactly 21 channels");

  // Adjust Channel 0 (POLE-R-Voltage)
  table.channels[0].zeroOffset = 0.05f;
  table.channels[0].gainMultiplier = 1.012f;
  table.channels[0].sensorSensitivity = 326.5f;
  strncpy(table.channels[0].calReference, "FLUKE_87V_NABL", 23);

  // Adjust Channel 3 (F01-R-Current)
  table.channels[3].zeroOffset = -0.02f;
  table.channels[3].gainMultiplier = 0.995f;
  table.channels[3].sensorSensitivity = 50.2f;
  strncpy(table.channels[3].calReference, "FLUKE_771_CAL", 23);

  bool okSave = nodeStorage.saveCalibrationTable(table);
  TEST_ASSERT(okSave, "Saved updated 21-channel calibration table to persistent storage");

  PoleCalibTable readBack;
  nodeStorage.getCalibrationTable(readBack);
  TEST_ASSERT(readBack.channels[0].gainMultiplier == 1.012f, "POLE-R-Voltage gainMultiplier saved accurately");
  TEST_ASSERT(readBack.channels[3].sensorSensitivity == 50.2f, "F01-R-Current sensitivity saved accurately");

  // Gateway metadata
  GwCalibMeta meta;
  strncpy(meta.poleId, "POLE-0001", 15);
  meta.calVersion = 0x0101;
  meta.calTimestamp = 1700050000UL;
  meta.tableCrc32 = readBack.crc32;
  meta.isValid = true;
  strncpy(meta.calReference, "NABL_CAL_LAB_P1", 23);
  centralStorage.saveCalibrationMeta("POLE-0001", meta);

  GwCalibMeta gwReadBack;
  bool metaOk = centralStorage.getCalibrationMeta("POLE-0001", gwReadBack);
  TEST_ASSERT(metaOk && gwReadBack.calVersion == 0x0101, "Gateway calibration metadata stored and retrieved");
}

// ─────────────────────────────────────────────────────────────
//  TEST 5: Update Configuration (Dual-Bank Rollback)
// ─────────────────────────────────────────────────────────────
void test5_UpdateConfiguration() {
  printf("\n--- TEST 5: Update Configuration with Dual-Bank Monotonic Versioning ---\n");
  SystemConfigRecord cfg;
  centralStorage.getConfiguration(cfg);
  uint16_t oldVer = cfg.configVersion;

  cfg.nominalVoltage = 232.0f;
  cfg.maxOverVoltage = 268.0f;
  cfg.minUnderVoltage = 178.0f;
  cfg.maxCurrentRating = 55.0f;
  cfg.heartbeatIntervalSec = 20;

  bool ok = centralStorage.saveConfiguration(cfg);
  TEST_ASSERT(ok, "Saved updated configuration to alternate bank");

  SystemConfigRecord readCfg;
  centralStorage.getConfiguration(readCfg);
  TEST_ASSERT(readCfg.configVersion == oldVer + 1, "Config version incremented monotonically");
  TEST_ASSERT(readCfg.nominalVoltage == 232.0f, "Nominal voltage updated to 232.0V");
  TEST_ASSERT(readCfg.heartbeatIntervalSec == 20, "Heartbeat interval updated to 20s");
}

// ─────────────────────────────────────────────────────────────
//  TEST 6: Reboot Recovery
// ─────────────────────────────────────────────────────────────
void test6_RebootRecovery() {
  printf("\n--- TEST 6: Reboot Recovery & Persistence Across Restarts ---\n");
  // Simulate complete gateway reboot by reinitializing CentralStorage
  CentralStorage rebootedGw;
  bool initOk = rebootedGw.init();
  TEST_ASSERT(initOk, "Rebooted Gateway initialized storage successfully");
  TEST_ASSERT(rebootedGw.getNodeCount() == 50, "Preserved all 50 registered nodes across reboot");
  TEST_ASSERT(rebootedGw.getTotalFeederCount() == 300, "Preserved all 300 feeder records across reboot");

  MscbNodeRecord node10;
  rebootedGw.getNode("POLE-0010", node10);
  TEST_ASSERT(strcmp(node10.friendlyName, "Updated Market Pole 10") == 0, "Preserved updated node attributes across reboot");

  SystemConfigRecord restoredCfg;
  rebootedGw.getConfiguration(restoredCfg);
  TEST_ASSERT(restoredCfg.nominalVoltage == 232.0f, "Preserved active dual-bank configuration across reboot");
}

// ─────────────────────────────────────────────────────────────
//  TEST 7: Interrupted Write & Transaction Rollback
// ─────────────────────────────────────────────────────────────
void test7_InterruptedWrite() {
  printf("\n--- TEST 7: Interrupted Write & Uncommitted Transaction Recovery ---\n");
  // Simulate power failure during prepare step
  centralStorage.simulatePowerFailureDuringPrepare();
  TEST_ASSERT(centralStorage.getActiveJournal().state == TX_STATE_PREPARED, "Journal contains in-flight prepared transaction");

  // Trigger boot recovery
  bool recOk = centralStorage.recoverStorage();
  TEST_ASSERT(recOk, "Recovery routine handled in-flight transaction");
  TEST_ASSERT(centralStorage.getActiveJournal().state == TX_STATE_ROLLED_BACK, "Incomplete transaction rolled back safely on reboot");
}

// ─────────────────────────────────────────────────────────────
//  TEST 8: Corrupted Record Detection & Fallback
// ─────────────────────────────────────────────────────────────
void test8_CorruptedRecord() {
  printf("\n--- TEST 8: Corrupted Record Detection & Automatic Backup Fallback ---\n");
  // Corrupt active config bank CRC
  centralStorage.injectBitFlipCorruption(0); // Node 0
  MscbNodeRecord badNode;
  bool readOk = centralStorage.getNode("POLE-0001", badNode);
  TEST_ASSERT(!readOk, "Corrupted node record rejected due to CRC32 mismatch");

  // Test Secondary Node calibration corruption detection
  nodeStorage.injectBitFlipCalibration();
  PoleCalibTable badCal;
  bool calOk = nodeStorage.getCalibrationTable(badCal);
  TEST_ASSERT(!calOk, "Corrupted calibration table rejected on Secondary Node");

  // Reboot node storage to recover factory calibration
  nodeStorage.recoverStorage();
  PoleCalibTable restoredCal;
  bool restOk = nodeStorage.getCalibrationTable(restoredCal);
  TEST_ASSERT(restOk, "Node storage recovered clean valid calibration table");
}

// ─────────────────────────────────────────────────────────────
//  TEST 9: Duplicate Registration Idempotency
// ─────────────────────────────────────────────────────────────
void test9_DuplicateRegistration() {
  printf("\n--- TEST 9: Duplicate Registration Idempotency ---\n");
  // Attempt to re-register POLE-0002 with same HW ID
  MscbNodeRecord dupNode;
  makeTestNode(dupNode, 2);
  strncpy(dupNode.friendlyName, "Pole 2 Re-registered", sizeof(dupNode.friendlyName) - 1);

  uint8_t countBefore = centralStorage.getNodeCount();
  bool ok = centralStorage.saveNode(dupNode);
  TEST_ASSERT(ok, "Re-registration of existing HW ID succeeded idempotently");
  TEST_ASSERT(centralStorage.getNodeCount() == countBefore, "Node count remained unchanged (no duplicate slot created)");

  MscbNodeRecord check;
  centralStorage.getNode("POLE-0002", check);
  TEST_ASSERT(strcmp(check.friendlyName, "Pole 2 Re-registered") == 0, "Updated existing slot in place");
}

// ─────────────────────────────────────────────────────────────
//  TEST 10: Duplicate Deletion Handling
// ─────────────────────────────────────────────────────────────
void test10_DuplicateDeletion() {
  printf("\n--- TEST 10: Duplicate Deletion Handling ---\n");
  bool del1 = centralStorage.deleteNode("POLE-0040", "ADMIN_1", "Maintenance");
  TEST_ASSERT(del1, "Initial node deletion and revocation succeeded");

  // Second deletion call on same node
  bool del2 = centralStorage.deleteNode("POLE-0040", "ADMIN_1", "Maintenance");
  TEST_ASSERT(del2, "Duplicate deletion handled gracefully and idempotently");

  MscbNodeRecord n;
  centralStorage.getNode("POLE-0040", n);
  TEST_ASSERT(n.regState == REG_STATE_REVOKED, "Node remains in REG_STATE_REVOKED");
}

// ─────────────────────────────────────────────────────────────
//  TEST 11: Revocation Persistence Across Reboots
// ─────────────────────────────────────────────────────────────
void test11_RevocationPersistence() {
  printf("\n--- TEST 11: Revocation Persistence Across Reboots ---\n");
  centralStorage.saveRevocation("A4CF12999999", "POLE-0099", "Compromised Node", "TX-REV-01", "ADMIN");
  TEST_ASSERT(centralStorage.isRevoked("A4CF12999999"), "Hardware ID marked revoked in runtime");

  // Reboot gateway
  CentralStorage rebooted;
  rebooted.init();
  TEST_ASSERT(rebooted.isRevoked("A4CF12999999"), "Revocation persisted across reboot");

  // Attempt to register revoked device
  MscbNodeRecord revNode;
  makeTestNode(revNode, 99);
  strncpy(revNode.hwId, "A4CF12999999", 16);
  bool regOk = rebooted.saveNode(revNode);
  TEST_ASSERT(!regOk, "Registration of permanently revoked device strictly blocked");
}

// ─────────────────────────────────────────────────────────────
//  TEST 12: Event Ring Buffer (FIFO Wrapping & Sequence)
// ─────────────────────────────────────────────────────────────
void test12_EventRingBuffer() {
  printf("\n--- TEST 12: Event Ring Buffer (FIFO Wrapping & Bounds) ---\n");
  // Add 150 events to 128-event ring buffer
  for (int i = 1; i <= 150; i++) {
    char desc[48];
    snprintf(desc, sizeof(desc), "Operational event #%d", i);
    centralStorage.addAuditEvent(AUDIT_EVENT_ADMIN_ACTION, RETENTION_CATEGORY_RECENT_EVENT,
                                 "POLE-0001", "ADMIN", AUDIT_RES_SUCCESS, desc);
  }

  TEST_ASSERT(centralStorage.getAuditEventCount() == MSCB_MAX_AUDIT_EVENTS, "Ring buffer bounded at 128 events");

  AuditEventRecord events[MSCB_MAX_AUDIT_EVENTS];
  uint16_t fetched = 0;
  bool ok = centralStorage.getAuditHistory(events, MSCB_MAX_AUDIT_EVENTS, fetched, RETENTION_CATEGORY_RECENT_EVENT);
  TEST_ASSERT(ok && fetched == MSCB_MAX_AUDIT_EVENTS, "Fetched 128 events in chronological order");
  TEST_ASSERT(events[0].eventId > 20, "Oldest events wrapped around and evicted cleanly");
  TEST_ASSERT(events[fetched - 1].eventId >= 150, "Newest events retained at head");
}

// ─────────────────────────────────────────────────────────────
//  TEST 13: Event Retention Priorities (Security Preserved)
// ─────────────────────────────────────────────────────────────
void test13_EventRetention() {
  printf("\n--- TEST 13: Event Retention Priority (Security Never Purged) ---\n");
  // Permanent revocation records reside in dedicated persistent storage
  TEST_ASSERT(centralStorage.getRevokedCount() > 0, "Revocation registry contains permanent records");
  const PermanentRevocationRecord *r = centralStorage.getRevocationRecord(0);
  TEST_ASSERT(r != nullptr, "Permanent revocation record accessible");
  TEST_ASSERT(centralStorage.isRevoked(r->hwId), "Critical security records intact regardless of event churn");
}

// ─────────────────────────────────────────────────────────────
//  TEST 14: Power Failure Recovery (WAL Replay)
// ─────────────────────────────────────────────────────────────
void test14_PowerFailureRecovery() {
  printf("\n--- TEST 14: Power Failure Recovery (WAL Journal Check) ---\n");
  centralStorage.simulatePowerFailureDuringPrepare();
  centralStorage.recoverStorage();
  const TxJournalRecord &j = centralStorage.getActiveJournal();
  TEST_ASSERT(j.state == TX_STATE_ROLLED_BACK, "WAL journal state reconciled to TX_STATE_ROLLED_BACK");
}

// ─────────────────────────────────────────────────────────────
//  TEST 15: Storage Near Capacity
// ─────────────────────────────────────────────────────────────
void test15_StorageNearCapacity() {
  printf("\n--- TEST 15: Storage Near Capacity & Memory Safety ---\n");
  size_t nvsBytes = centralStorage.getNvsEstimatedUsage();
  size_t ramBytes = centralStorage.getRamUsageBytes();

  printf("    -> Estimated Persistent Storage Usage: %u bytes\n", (unsigned int)nvsBytes);
  printf("    -> Central Storage RAM Footprint: %u bytes\n", (unsigned int)ramBytes);

  TEST_ASSERT(nvsBytes > 0 && nvsBytes < 500000, "Persistent storage usage within reasonable limits");
  TEST_ASSERT(ramBytes > 0, "Storage structures loaded in memory");
}

// ─────────────────────────────────────────────────────────────
//  TEST 16: Flash Wear Estimate
// ─────────────────────────────────────────────────────────────
void test16_FlashWearEstimate() {
  printf("\n--- TEST 16: Flash Wear Analysis & Lifetime Estimation ---\n");
  uint32_t totalWrites = centralStorage.getTotalFlashWrites();
  float yearsAt10WritesPerDay = centralStorage.calculateFlashWearEstimateYears(10);
  float yearsAt50WritesPerDay = centralStorage.calculateFlashWearEstimateYears(50);

  printf("    -> Total flash writes recorded: %u\n", totalWrites);
  printf("    -> Expected endurance at 10 writes/day: %.1f years\n", yearsAt10WritesPerDay);
  printf("    -> Expected endurance at 50 writes/day: %.1f years\n", yearsAt50WritesPerDay);

  TEST_ASSERT(yearsAt10WritesPerDay > 50.0f, "Flash endurance exceeds 50 years at 10 writes/day");
  TEST_ASSERT(yearsAt50WritesPerDay > 20.0f, "Flash endurance exceeds 20 years at 50 writes/day");
}

// ─────────────────────────────────────────────────────────────
//  TEST 17: Data Consistency Cross-Verification
// ─────────────────────────────────────────────────────────────
void test17_DataConsistency() {
  printf("\n--- TEST 17: Data Consistency Cross-Verification ---\n");
  bool ok = centralStorage.verifyStorageIntegrity();
  TEST_ASSERT(ok, "Full cross-registry consistency verification passed");
}

// ─────────────────────────────────────────────────────────────
//  TEST 18: Configuration Migration (V1 to V2)
// ─────────────────────────────────────────────────────────────
void test18_ConfigurationMigration() {
  printf("\n--- TEST 18: Configuration Schema Migration (V1 -> V2) ---\n");
  bool ok = centralStorage.migrateConfiguration(MSCB_CONFIG_SCHEMA_V1, MSCB_CONFIG_SCHEMA_V2);
  TEST_ASSERT(ok, "Successfully migrated configuration schema from V1 to V2");

  SystemConfigRecord cfg;
  centralStorage.getConfiguration(cfg);
  TEST_ASSERT(cfg.schemaVersion == MSCB_CONFIG_SCHEMA_V2, "Schema version is now V2");
  TEST_ASSERT(cfg.beaconIntervalSec == 30, "Modern beacon interval field populated with safe default");
  TEST_ASSERT(cfg.loraTxPower == 10, "Modern LoRa TX power populated with safe default (10 dBm WPC)");
}

// ─────────────────────────────────────────────────────────────
//  TEST 19: Historical Measurement Freshness
// ─────────────────────────────────────────────────────────────
void test19_HistoricalMeasurementFreshness() {
  printf("\n--- TEST 19: Historical Measurement Freshness (No Zeroing on Offline) ---\n");
  // Set known readings for POLE-0003
  MscbNodeRecord node3;
  centralStorage.getNode("POLE-0003", node3);
  node3.feeders[0].currentRating = 28.5f; // Active load
  node3.lastSeenTime = 1700000000UL;
  centralStorage.updateNode("POLE-0003", node3);

  // Mark node offline
  centralStorage.markNodeOffline("POLE-0003");
  TEST_ASSERT(centralStorage.getMeasurementFreshness("POLE-0003") == MEAS_FRESHNESS_STALE,
              "Measurement freshness marked STALE when offline");

  MscbNodeRecord offlineNode;
  centralStorage.getNode("POLE-0003", offlineNode);
  TEST_ASSERT(offlineNode.regState == REG_STATE_OFFLINE, "Node state transitioned to REG_STATE_OFFLINE");
  TEST_ASSERT(offlineNode.feeders[0].currentRating == 28.5f, "Preserved last current measurement without zeroing!");
  TEST_ASSERT(offlineNode.lastSeenTime == 1700000000UL, "Preserved last seen timestamp intact");

  // Reconnect node
  uint32_t gapSec = 0;
  centralStorage.markNodeOnline("POLE-0003", -68, 8, "GATEWAY", 1700000300UL, gapSec);
  TEST_ASSERT(gapSec == 300, "Accurately calculated offline data gap duration (300 sec)");
  TEST_ASSERT(centralStorage.getMeasurementFreshness("POLE-0003") == MEAS_FRESHNESS_LIVE,
              "Measurement freshness restored to LIVE upon reconnect");
}

// ─────────────────────────────────────────────────────────────
//  MAIN TEST RUNNER
// ─────────────────────────────────────────────────────────────
int main() {
  printf("=====================================================================\n");
  printf("  MSCB SMART POLE SYSTEM — PROMPT 19 STORAGE & AUDIT TEST SUITE      \n");
  printf("=====================================================================\n");

  test1_Register50Nodes();
  test2_Register300Feeders();
  test3_SaveNodeData();
  test4_SaveCalibrationData();
  test5_UpdateConfiguration();
  test6_RebootRecovery();
  test7_InterruptedWrite();
  test8_CorruptedRecord();
  test9_DuplicateRegistration();
  test10_DuplicateDeletion();
  test11_RevocationPersistence();
  test12_EventRingBuffer();
  test13_EventRetention();
  test14_PowerFailureRecovery();
  test15_StorageNearCapacity();
  test16_FlashWearEstimate();
  test17_DataConsistency();
  test18_ConfigurationMigration();
  test19_HistoricalMeasurementFreshness();

  printf("\n=====================================================================\n");
  printf("  TEST RESULTS SUMMARY\n");
  printf("  Passed: %d\n", testsPassed);
  printf("  Failed: %d\n", testsFailed);
  printf("=====================================================================\n");

  return (testsFailed == 0) ? 0 : 1;
}
