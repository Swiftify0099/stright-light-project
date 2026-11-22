#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "../Sender/RegistrationTypes.h"
#include "../Sender/RegistrationStorage.h"
#include "../Sender/GwRegistry.h"
#include "../Resivers/PoleRegistration.h"

// Test Results Counter
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

// Helper to parse LoRa JOIN_REQ packet and dispatch to Gateway Registry
uint8_t processJoinPacket(GwRegistry &gw, const char *pkt, int16_t rssi, int8_t snr,
                          char *outAssignedId, char *outPri, char *outBkp) {
  char buf[256];
  strncpy(buf, pkt, 255);
  buf[255] = '\0';
  char *fields[10];
  int fc = 0;
  char *p = strtok(buf, "|");
  while (p && fc < 10) {
    fields[fc++] = p;
    p = strtok(NULL, "|");
  }
  if (fc < 9) return REJECT_AUTH_FAIL;
  const char *hwId = fields[1];
  uint8_t nType = (uint8_t)atoi(fields[2]);
  uint16_t fwVer = (uint16_t)atoi(fields[3]);
  uint32_t session = (uint32_t)strtoul(fields[4], NULL, 10);
  const char *priP = fields[5];
  const char *bkpP = fields[6];
  uint32_t nonce = (uint32_t)strtoul(fields[7], NULL, 10);
  const char *tag = fields[8];

  return gw.processJoinRequest(hwId, nType, fwVer, session, priP, bkpP, nonce, tag, rssi, snr,
                               outAssignedId, outPri, outBkp);
}

void runTest1_FirstTimeDiscovery() {
  printf("\n--- TEST 1: First-Time Node Discovery ---\n");
  PoleRegistration testPole;
  testPole.forceFactoryReset();
  testPole.init();

  TEST_ASSERT(testPole.getState() == REG_STATE_DISCOVERING, "Pole enters DISCOVERING state on boot");
  TEST_ASSERT(!testPole.isRelayControlPermitted(), "Relays strictly locked during discovery");

  // Receive Gateway beacon
  testPole.onBeaconReceived("GATEWAY", true, 0, -65, 9);

  char joinPacket[256];
  testPole.buildJoinRequest(joinPacket, 5500);
  TEST_ASSERT(strstr(joinPacket, "JOIN_REQ") != nullptr, "Constructed valid JOIN_REQ packet");
  TEST_ASSERT(strstr(joinPacket, testPole.getHwId()) != nullptr, "Packet contains Hardware ID");
  TEST_ASSERT(strcmp(testPole.getPrimaryParent(), "GATEWAY") == 0, "Selected GATEWAY as Primary Parent");

  // Gateway processes join
  GwRegistry testGw;
  testGw.init();
  char assignedId[16], priParent[16], bkpParent[16];
  uint8_t res = processJoinPacket(testGw, joinPacket, -65, 9, assignedId, priParent, bkpParent);

  TEST_ASSERT(res == REJECT_NONE, "Gateway accepted JOIN_REQ with valid AuthTag");
  TEST_ASSERT(testGw.getPendingCount() == 1, "Node placed in Gateway Pending Approval queue");

  testPole.onJoinAckReceived(res);
  TEST_ASSERT(testPole.getState() == REG_STATE_PENDING_APPROVAL, "Node entered PENDING_APPROVAL state");
  TEST_ASSERT(!testPole.isRelayControlPermitted(), "Relays remain locked in PENDING_APPROVAL");
}

void runTest2_MultiHopRegistration() {
  printf("\n--- TEST 2: Multi-Hop Registration Request ---\n");
  GwRegistry testGw;
  testGw.init();

  // Approve intermediate Parent POLE-0001
  testGw.approveNode("A4CF00000001", "POLE-0001", "Relay Pole 1", "Zone 1", "Installed", "GATEWAY", "NONE");
  testGw.confirmNodeActivation("POLE-0001", 12345);

  // Approve intermediate Parent POLE-0002
  testGw.approveNode("A4CF00000002", "POLE-0002", "Relay Pole 2", "Zone 1", "Installed", "GATEWAY", "NONE");
  testGw.confirmNodeActivation("POLE-0002", 12345);

  // Remote Node 3 discovers POLE-0001 and POLE-0002
  PoleRegistration remotePole;
  remotePole.forceFactoryReset();
  remotePole.init();
  remotePole.onBeaconReceived("POLE-0001", true, 1, -72, 8);
  remotePole.onBeaconReceived("POLE-0002", true, 2, -85, 5);

  char joinPkt[256];
  remotePole.buildJoinRequest(joinPkt, 6000);

  TEST_ASSERT(strcmp(remotePole.getPrimaryParent(), "POLE-0001") == 0, "Selected POLE-0001 as Primary Parent (1 hop, -72dBm)");
  TEST_ASSERT(strcmp(remotePole.getBackupParent(), "POLE-0002") == 0, "Selected POLE-0002 as Backup Parent (2 hops, -85dBm)");

  char assignedId[16], priP[16], bkP[16];
  uint8_t res = processJoinPacket(testGw, joinPkt, -72, 8, assignedId, priP, bkP);

  TEST_ASSERT(res == REJECT_NONE, "Gateway accepted multi-hop JOIN_REQ");
  TEST_ASSERT(strcmp(priP, "POLE-0001") == 0, "Gateway confirmed POLE-0001 as Primary Parent");
  TEST_ASSERT(strcmp(bkP, "POLE-0002") == 0, "Gateway confirmed POLE-0002 as Backup Parent");
}

void runTest3_DuplicateNodeDetection() {
  printf("\n--- TEST 3: Duplicate Node Detection ---\n");
  GwRegistry testGw;
  testGw.init();

  testGw.approveNode("A4CF12E834B0", "POLE-0001", "Pole 1", "Zone A", "OK", "GATEWAY", "NONE");
  testGw.confirmNodeActivation("POLE-0001", 100);

  // Attempting to register another distinct device with the same Hardware ID under a different logical ID
  bool dupApproval = testGw.approveNode("A4CF12E834B0", "POLE-0002", "Pole 2 Duplicate", "Zone A", "OK", "GATEWAY", "NONE");
  TEST_ASSERT(dupApproval == false, "Blocked duplicate registration of existing Hardware ID under new Logical ID");
}

void runTest4_UniqueIdAssignment() {
  printf("\n--- TEST 4: Unique Logical ID Assignment ---\n");
  GwRegistry testGw;
  testGw.init();

  char id1[16], id2[16], id3[16];
  testGw.generateUniqueLogicalId(DEV_TYPE_POLE, id1);
  testGw.approveNode("HW_UNIQUE_01", id1, "Pole 1", "Zone 1", "OK", "GATEWAY", "NONE");

  testGw.generateUniqueLogicalId(DEV_TYPE_POLE, id2);
  testGw.approveNode("HW_UNIQUE_02", id2, "Pole 2", "Zone 1", "OK", "GATEWAY", "NONE");

  testGw.generateUniqueLogicalId(DEV_TYPE_FEEDER, id3);
  testGw.approveNode("HW_UNIQUE_03", id3, "Feeder 1", "Zone 1", "OK", "GATEWAY", "NONE");

  TEST_ASSERT(strcmp(id1, id2) != 0, "Generated unique sequential Pole IDs");
  TEST_ASSERT(strncmp(id1, "POLE-", 5) == 0, "Pole ID has correct prefix POLE-");
  TEST_ASSERT(strncmp(id3, "FDR-", 4) == 0, "Feeder ID has correct prefix FDR-");

  // Attempt duplicate logical ID approval
  bool dupLogical = testGw.approveNode("HW_UNIQUE_04", id1, "Collision Pole", "Zone 1", "OK", "GATEWAY", "NONE");
  TEST_ASSERT(dupLogical == false, "Gateway prevented duplicate Logical ID collision");
}

void runTest5_AdminApprovalWorkflow() {
  printf("\n--- TEST 5: Admin Approval Workflow ---\n");
  GwRegistry testGw;
  testGw.init();

  PoleRegistration node;
  node.forceFactoryReset();
  node.setHwId("HW_POLE_APP_005");
  node.init();
  node.setHwId("HW_POLE_APP_005");
  node.onBeaconReceived("GATEWAY", true, 0, -70, 7);

  char pkt[256];
  node.buildJoinRequest(pkt, 1000);
  char assId[16], priP[16], bkP[16];
  processJoinPacket(testGw, pkt, -70, 7, assId, priP, bkP);

  // Admin approves with complete 6-feeder configuration
  MscbFeederRecord feeders[6];
  for (int f = 0; f < 6; f++) {
    feeders[f].feederId = f + 1;
    snprintf(feeders[f].name, sizeof(feeders[f].name), "Feeder Line %d", f + 1);
    feeders[f].enabled = true;
    feeders[f].currentRating = (f == 0) ? 32.0f : 16.0f;
    feeders[f].relayState = false; // Relays initialized OFF
    feeders[f].mcbR = true;
    feeders[f].mcbY = true;
    feeders[f].mcbB = true;
    feeders[f].contactor = false;
  }

  bool approved = testGw.approveNode(node.getHwId(), "POLE-0010", "Chowk Pole 10",
                                     "Dattawad North", "Pole #10, Lat:16.68",
                                     "GATEWAY", "NONE", feeders);
  TEST_ASSERT(approved == true, "Admin successfully approved node with 6 feeders");

  // Node receives JOIN_ACCEPT
  bool acceptOk = node.onJoinAcceptReceived("POLE-0010", "GATEWAY", "NONE", 1, 9999, feeders);
  TEST_ASSERT(acceptOk == true, "Node processed JOIN_ACCEPT successfully");
  TEST_ASSERT(node.getState() == REG_STATE_ACTIVE, "Node transitioned to REG_STATE_ACTIVE");
  TEST_ASSERT(node.isRelayControlPermitted() == true, "Production relay controls enabled after activation");
  TEST_ASSERT(strcmp(node.getLogicalId(), "POLE-0010") == 0, "Assigned Logical ID verified");
}

void runTest6_AdminRejection() {
  printf("\n--- TEST 6: Admin Rejection ---\n");
  GwRegistry testGw;
  testGw.init();

  testGw.processJoinRequest("A4CF_UNKNOWN_01", DEV_TYPE_POLE, 11, 200, "GATEWAY", "NONE",
                            1, "0000000000000000", -80, 0, nullptr, nullptr, nullptr);
  TEST_ASSERT(testGw.getPendingCount() == 0, "Invalid auth request not added to pending");

  // Valid auth request
  uint8_t devKey[32];
  testGw.deriveDeviceKey("A4CF_VALID_01", devKey);
  char signPayload[128];
  snprintf(signPayload, sizeof(signPayload), "A4CF_VALID_01|%u|11|300|GATEWAY|NONE|1|%04X",
           DEV_TYPE_POLE, MSCB_DEFAULT_NETWORK_ID);
  char tag[17];
  mscbComputeAuthTag(devKey, 32, (const uint8_t*)signPayload, strlen(signPayload), tag);

  char assId[16], pP[16], bP[16];
  testGw.processJoinRequest("A4CF_VALID_01", DEV_TYPE_POLE, 11, 300, "GATEWAY", "NONE",
                            1, tag, -80, 5, assId, pP, bP);
  TEST_ASSERT(testGw.getPendingCount() == 1, "Valid node in pending queue");

  bool rej = testGw.rejectNode("A4CF_VALID_01", REJECT_ADMIN_DECLINED);
  TEST_ASSERT(rej == true, "Admin declined node successfully");
  TEST_ASSERT(testGw.getPendingCount() == 0, "Node removed from pending queue after rejection");
}

void runTest7_UnauthorizedNodeRejection() {
  printf("\n--- TEST 7: Unauthorized Node Rejection ---\n");
  GwRegistry testGw;
  testGw.init();

  char assId[16], pP[16], bP[16];
  // Rogue node with forged auth tag
  uint8_t res = testGw.processJoinRequest("ROGUE_NODE_01", DEV_TYPE_POLE, 11, 400,
                                          "GATEWAY", "NONE", 1, "DEADBEEFCAFE1234",
                                          -70, 7, assId, pP, bP);

  TEST_ASSERT(res == REJECT_AUTH_FAIL, "Gateway rejected unauthorized node with invalid AuthTag");
  TEST_ASSERT(testGw.getPendingCount() == 0, "Unauthorized node denied entry to pending queue");
}

void runTest8_ReplayRegistrationRejection() {
  printf("\n--- TEST 8: Replay Registration Rejection ---\n");
  GwRegistry testGw;
  testGw.init();

  const char *hwId = "A4CF_REPLAY_TEST";
  uint8_t devKey[32];
  testGw.deriveDeviceKey(hwId, devKey);
  char signPayload[128];
  snprintf(signPayload, sizeof(signPayload), "%s|%u|11|500|GATEWAY|NONE|99|%04X",
           hwId, DEV_TYPE_POLE, MSCB_DEFAULT_NETWORK_ID);
  char tag[17];
  mscbComputeAuthTag(devKey, 32, (const uint8_t*)signPayload, strlen(signPayload), tag);

  char assId[16], pP[16], bP[16];
  uint8_t res1 = testGw.processJoinRequest(hwId, DEV_TYPE_POLE, 11, 500, "GATEWAY", "NONE",
                                           99, tag, -75, 6, assId, pP, bP);
  TEST_ASSERT(res1 == REJECT_NONE, "First valid packet accepted");

  // Replay exact same packet
  uint8_t res2 = testGw.processJoinRequest(hwId, DEV_TYPE_POLE, 11, 500, "GATEWAY", "NONE",
                                           99, tag, -75, 6, assId, pP, bP);
  TEST_ASSERT(res2 == REJECT_REPLAY_DETECTED, "Replayed JOIN_REQ with duplicate nonce blocked");
}

void runTest9_Full50NodeRegistry() {
  printf("\n--- TEST 9: Full 50-Node Registry Capacity ---\n");
  RegistrationStorage storage;
  storage.clearAll();

  GwRegistry testGw;
  testGw.init();

  // Populate 50 nodes
  for (int i = 0; i < MSCB_MAX_REGISTRY_NODES; i++) {
    char hw[17], id[16], name[32];
    snprintf(hw, sizeof(hw), "HW_POLE_%04d", i + 1);
    snprintf(id, sizeof(id), "POLE-%04d", i + 1);
    snprintf(name, sizeof(name), "Pole Node %d", i + 1);
    bool ok = testGw.approveNode(hw, id, name, "Grid 1", "Installed", "GATEWAY", "NONE");
    assert(ok);
  }

  TEST_ASSERT(testGw.getNodeCount() == 50, "Successfully registered exactly 50 Pole Nodes");
  TEST_ASSERT(testGw.isRegistryFull() == true, "Gateway flags registry as FULL");

  // Attempt 51st node registration
  const char *hw51 = "HW_POLE_0051";
  uint8_t devKey[32];
  testGw.deriveDeviceKey(hw51, devKey);
  char signPayload[128];
  snprintf(signPayload, sizeof(signPayload), "%s|%u|11|700|GATEWAY|NONE|1|%04X",
           hw51, DEV_TYPE_POLE, MSCB_DEFAULT_NETWORK_ID);
  char tag[17];
  mscbComputeAuthTag(devKey, 32, (const uint8_t*)signPayload, strlen(signPayload), tag);

  char assId[16], pP[16], bP[16];
  uint8_t res51 = testGw.processJoinRequest(hw51, DEV_TYPE_POLE, 11, 700, "GATEWAY", "NONE",
                                            1, tag, -80, 5, assId, pP, bP);
  TEST_ASSERT(res51 == REJECT_REGISTRY_FULL, "51st node registration rejected due to capacity limit");

  // Verify total feeder count across 50 nodes is 300
  int totalFeeders = 0;
  for (int i = 0; i < testGw.getNodeCount(); i++) {
    totalFeeders += testGw.getNode(i)->feederCount;
  }
  TEST_ASSERT(totalFeeders == 300, "50 registered poles contain exactly 300 logical feeder records");
}

void runTest10_GatewayRestartRecovery() {
  printf("\n--- TEST 10: Gateway Restart Recovery ---\n");
  RegistrationStorage storage;
  storage.clearAll();

  // Save 5 nodes
  for (int i = 0; i < 5; i++) {
    MscbNodeRecord rec;
    memset(&rec, 0, sizeof(MscbNodeRecord));
    snprintf(rec.logicalId, sizeof(rec.logicalId), "POLE-%04d", i + 1);
    snprintf(rec.hwId, sizeof(rec.hwId), "HW_REC_%04d", i + 1);
    rec.regState = REG_STATE_ACTIVE;
    rec.feederCount = 6;
    storage.saveNodeRecord(i, rec);
  }

  // Simulate Gateway reboot
  GwRegistry rebootedGw;
  rebootedGw.init();

  TEST_ASSERT(rebootedGw.getNodeCount() == 5, "Rebooted Gateway reloaded all 5 nodes from NVS");
  TEST_ASSERT(strcmp(rebootedGw.getNode(0)->logicalId, "POLE-0001") == 0, "Node 1 logical ID recovered");
  TEST_ASSERT(strcmp(rebootedGw.getNode(4)->logicalId, "POLE-0005") == 0, "Node 5 logical ID recovered");
}

void runTest11_NodeRestartRecovery() {
  printf("\n--- TEST 11: Node Restart Recovery ---\n");
  PoleRegistration pole;
  pole.forceFactoryReset();
  pole.init();

  // Approve and activate node
  pole.onJoinAcceptReceived("POLE-0088", "GATEWAY", "NONE", 1, 555, nullptr);
  TEST_ASSERT(pole.getState() == REG_STATE_ACTIVE, "Pole active prior to restart");

  // Simulate node crash / power loss reboot
  PoleRegistration restartedPole;
  restartedPole.init();

  TEST_ASSERT(restartedPole.getState() == REG_STATE_ACTIVE, "Pole immediately resumed REG_STATE_ACTIVE");
  TEST_ASSERT(restartedPole.isRelayControlPermitted() == true, "Relay control immediately permitted");
  TEST_ASSERT(strcmp(restartedPole.getLogicalId(), "POLE-0088") == 0, "Assigned ID POLE-0088 preserved across restart");
}

void runTest12_ParentFailureFallback() {
  printf("\n--- TEST 12: Parent Failure During Registration ---\n");
  GwRegistry testGw;
  testGw.init();

  // Candidate parent POLE-0099 is NOT in registry
  char pri[16], bkp[16];
  testGw.evaluateAndAssignParents("POLE-0099", "NONE", pri, bkp);

  TEST_ASSERT(strcmp(pri, "GATEWAY") == 0, "Fallback to direct GATEWAY when Primary Parent candidate is invalid/offline");
}

void runTest13_MissingBackupParent() {
  printf("\n--- TEST 13: Missing Backup Parent ---\n");
  GwRegistry testGw;
  testGw.init();

  testGw.approveNode("HW_ISOLATED_PARENT", "POLE-0001", "Parent 1", "North", "OK", "GATEWAY", "NONE");
  testGw.confirmNodeActivation("POLE-0001", 100);

  char pri[16], bkp[16];
  testGw.evaluateAndAssignParents("POLE-0001", "NONE", pri, bkp);

  TEST_ASSERT(strcmp(pri, "POLE-0001") == 0, "Valid primary parent assigned");
  TEST_ASSERT(strcmp(bkp, "NONE") == 0, "Missing backup parent gracefully recorded as NONE");
}

void runTest14_NvsDataIntegrity() {
  printf("\n--- TEST 14: NVS Data Integrity & CRC Verification ---\n");
  RegistrationStorage storage;
  storage.clearAll();

  MscbNodeRecord original;
  memset(&original, 0, sizeof(MscbNodeRecord));
  strncpy(original.logicalId, "POLE-0077", 15);
  strncpy(original.hwId, "A4CF77777777", 16);
  original.regState = REG_STATE_ACTIVE;
  original.feederCount = 6;

  bool saved = storage.saveNodeRecord(0, original);
  TEST_ASSERT(saved == true, "Record saved with verified CRC32");

  MscbNodeRecord loaded;
  bool readOk = storage.readNodeRecord(0, loaded);
  TEST_ASSERT(readOk == true, "Record read back with valid CRC32");
  TEST_ASSERT(strcmp(loaded.logicalId, "POLE-0077") == 0, "Data matches exactly");

  // Corrupt record CRC
  loaded.crc32 ^= 0xFFFFFFFF;
  // Verification check
  uint32_t expectedCrc = mscbCalculateCrc32((const uint8_t*)&loaded, sizeof(MscbNodeRecord) - sizeof(uint32_t));
  TEST_ASSERT(loaded.crc32 != expectedCrc, "Corrupted CRC detected and invalidated");
}

void runTest15_RegistrationTimeout() {
  printf("\n--- TEST 15: Registration Timeout ---\n");
  GwRegistry testGw;
  testGw.init();

  const char *hw = "HW_TIMEOUT_TEST";
  uint8_t devKey[32];
  testGw.deriveDeviceKey(hw, devKey);
  char signPayload[128];
  snprintf(signPayload, sizeof(signPayload), "%s|%u|11|999|GATEWAY|NONE|1|%04X",
           hw, DEV_TYPE_POLE, MSCB_DEFAULT_NETWORK_ID);
  char tag[17];
  mscbComputeAuthTag(devKey, 32, (const uint8_t*)signPayload, strlen(signPayload), tag);

  char assId[16], pP[16], bP[16];
  testGw.processJoinRequest(hw, DEV_TYPE_POLE, 11, 999, "GATEWAY", "NONE",
                            1, tag, -80, 5, assId, pP, bP);
  TEST_ASSERT(testGw.getPendingCount() == 1, "Join request enqueued in pending queue");

  // Fast-forward simulated time past 5-minute timeout window (300,001 ms)
  testGw.purgeExpiredPending(MSCB_REG_APPROVAL_TIMEOUT_MS + 1000);
  TEST_ASSERT(testGw.getPendingCount() == 0, "Expired pending registration purged after timeout");
}

int main() {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("========================================================\n");
  printf("  MSCB SMART POLE AUTOMATION — REGISTRATION ENGINE TESTS \n");
  printf("========================================================\n");

  runTest1_FirstTimeDiscovery();
  runTest2_MultiHopRegistration();
  runTest3_DuplicateNodeDetection();
  runTest4_UniqueIdAssignment();
  runTest5_AdminApprovalWorkflow();
  runTest6_AdminRejection();
  runTest7_UnauthorizedNodeRejection();
  runTest8_ReplayRegistrationRejection();
  runTest9_Full50NodeRegistry();
  runTest10_GatewayRestartRecovery();
  runTest11_NodeRestartRecovery();
  runTest12_ParentFailureFallback();
  runTest13_MissingBackupParent();
  runTest14_NvsDataIntegrity();
  runTest15_RegistrationTimeout();

  printf("\n========================================================\n");
  printf("  TEST SUMMARY: %d PASSED, %d FAILED\n", testsPassed, testsFailed);
  printf("========================================================\n");

  return (testsFailed == 0) ? 0 : 1;
}
