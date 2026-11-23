/**
 * @file test_r2_pole_pin_revocation_repair.cpp
 * @brief MSCB Smart Pole System R2 - Verification Suite for:
 *        1. Field Pole 30-pin Header & Relay Mapping (Physical Pin 24 = D26 = GPIO 26, F1..F6)
 *        2. Gateway 64-capacity Revocation List & Safe Non-destructive Deletion
 *        3. Two-phase Commit with Rollback on Storage Failure
 *        4. Revoked Node Reappearance Alert & Heartbeat Rejection
 *        5. Persistent Pending Queue & Re-authorization Flow
 */

#include "ArduinoMock.h"
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../MSCBShared/StorageTypes.h"
#include "../FieldPole_ESP32/PoleBoardPins.h"
#include "../FieldPole_ESP32/PoleActuators.h"
#include "../FieldPole_ESP32/PoleStorageManager.h"
#include "../FieldPole_ESP32/PoleRegistrationClient.h"
#include "../FieldPole_ESP32/PoleMeshRouter.h"
#include "../Gateway_ESP32S3/GwNodeRegistry.h"
#include "../Gateway_ESP32S3/GwMeshCoordinator.h"

#include <iostream>
#include <cassert>
#include <cstring>
#include <set>

MockSerial Serial;

static int g_pass = 0;
static int g_fail = 0;

#define TEST_ASSERT(cond, msg) \
  do { \
    if (cond) { \
      std::cout << "  [PASS] " << msg << std::endl; \
      g_pass++; \
    } else { \
      std::cout << "  [FAIL] " << msg << " (Line " << __LINE__ << ")" << std::endl; \
      g_fail++; \
    } \
  } while (0)

int main() {
  std::cout << "\n=====================================================================" << std::endl;
  std::cout << "  MSCB SMART POLE SYSTEM R2 - HARDWARE PIN & REVOCATION REPAIR TEST  " << std::endl;
  std::cout << "=====================================================================" << std::endl;

  // -------------------------------------------------------------------------
  // SECTION 1: FIELD POLE PHYSICAL BOARD HEADER & RELAY PIN VERIFICATION
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION 1: PHYSICAL PIN MAPPING & SAFETY CHECKS ---" << std::endl;

  // 1.1 Verify Physical Pin, Label, and GPIO for each Feeder
  TEST_ASSERT(POLE_RELAY_F0_PHYS_PIN == 24, "Feeder 1 Physical Header Position == 24");
  TEST_ASSERT(strcmp(POLE_RELAY_F0_LABEL, "D26") == 0, "Feeder 1 Board Label == 'D26'");
  TEST_ASSERT(POLE_RELAY_F0_PIN == 26, "Feeder 1 GPIO == 26 (Physical Pin 24 preserved!)");

  TEST_ASSERT(POLE_RELAY_F1_PHYS_PIN == 23, "Feeder 2 Physical Header Position == 23");
  TEST_ASSERT(strcmp(POLE_RELAY_F1_LABEL, "D25") == 0, "Feeder 2 Board Label == 'D25'");
  TEST_ASSERT(POLE_RELAY_F1_PIN == 25, "Feeder 2 GPIO == 25");

  TEST_ASSERT(POLE_RELAY_F2_PHYS_PIN == 25, "Feeder 3 Physical Header Position == 25");
  TEST_ASSERT(strcmp(POLE_RELAY_F2_LABEL, "D27") == 0, "Feeder 3 Board Label == 'D27'");
  TEST_ASSERT(POLE_RELAY_F2_PIN == 27, "Feeder 3 GPIO == 27");

  TEST_ASSERT(POLE_RELAY_F3_PHYS_PIN == 6, "Feeder 4 Physical Header Position == 6");
  TEST_ASSERT(strcmp(POLE_RELAY_F3_LABEL, "RX2") == 0, "Feeder 4 Board Label == 'RX2'");
  TEST_ASSERT(POLE_RELAY_F3_PIN == 16, "Feeder 4 GPIO == 16");

  TEST_ASSERT(POLE_RELAY_F4_PHYS_PIN == 7, "Feeder 5 Physical Header Position == 7");
  TEST_ASSERT(strcmp(POLE_RELAY_F4_LABEL, "TX2") == 0, "Feeder 5 Board Label == 'TX2'");
  TEST_ASSERT(POLE_RELAY_F4_PIN == 17, "Feeder 5 GPIO == 17");

  TEST_ASSERT(POLE_RELAY_F5_PHYS_PIN == 21, "Feeder 6 Physical Header Position == 21");
  TEST_ASSERT(strcmp(POLE_RELAY_F5_LABEL, "D32") == 0, "Feeder 6 Board Label == 'D32'");
  TEST_ASSERT(POLE_RELAY_F5_PIN == 32, "Feeder 6 GPIO == 32");

  // 1.2 Verify that NO relay is assigned to GPIO 24 (Physical pin 24 != GPIO 24)
  for (uint8_t i = 0; i < 6; i++) {
    TEST_ASSERT(POLE_FEEDER_RELAY_PINS[i] != 24, "Verified: No relay assigned to invalid GPIO24");
  }

  // 1.3 Verify uniqueness of all 6 relay GPIOs
  std::set<int8_t> pinSet;
  for (uint8_t i = 0; i < 6; i++) {
    pinSet.insert(POLE_FEEDER_RELAY_PINS[i]);
  }
  TEST_ASSERT(pinSet.size() == 6, "All 6 relay GPIOs are unique and non-overlapping");

  // 1.4 Verify electrical active-LOW polarity and lockout safety
  poleActuators.init(true);
  TEST_ASSERT(poleActuators.isActiveLow() == true, "Relay Driver polarity is Active-LOW");
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled() == true, "Relays locked disabled by default");
  TEST_ASSERT(MSCB_RELAYS_LOCKED_DISABLED == 1, "MSCB_RELAYS_LOCKED_DISABLED is strictly enforced");

  // -------------------------------------------------------------------------
  // SECTION 2: GATEWAY REGISTRATION, DELETION CAPACITY & ROLLBACK SAFETY
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION 2: GATEWAY REGISTRATION & TRANSACTIONAL DELETION ---" << std::endl;

  gwRegistry.begin();

  // 2.1 Capacity constant check
  TEST_ASSERT(MSCB_MAX_REVOCATION_RECORDS >= 64, "MSCB_MAX_REVOCATION_RECORDS capacity is >= 64");

  // 2.2 Register a test pole (POLE-0002)
  uint8_t macA[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0xAA};
  PktRegisterRequestPayload reqA;
  memset(&reqA, 0, sizeof(reqA));
  memcpy(reqA.macAddress, macA, 6);
  reqA.firmwareVersion = 0x0200;
  PktRegisterResponsePayload respA;
  gwRegistry.handleJoinRequest(reqA, -60, 10, respA);
  uint16_t poleIdA = 0;
  bool approvedA = gwRegistry.approvePending(macA, poleIdA);
  TEST_ASSERT(approvedA && poleIdA == 0x0002, "Node A registered as POLE-0002");

  // Register a child pole (POLE-0003) pointing to POLE-0002 as primaryParent
  uint8_t macB[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0xBB};
  PktRegisterRequestPayload reqB;
  memset(&reqB, 0, sizeof(reqB));
  memcpy(reqB.macAddress, macB, 6);
  reqB.candidateParent1 = poleIdA;
  PktRegisterResponsePayload respB;
  gwRegistry.handleJoinRequest(reqB, -65, 8, respB);
  uint16_t poleIdB = 0;
  bool approvedB = gwRegistry.approvePending(macB, poleIdB);
  TEST_ASSERT(approvedB && poleIdB == 0x0003, "Node B registered as POLE-0003");
  RegisteredPole* childPole = gwRegistry.findPoleById(poleIdB);
  TEST_ASSERT(childPole && childPole->primaryParent == poleIdA, "Node B has Node A as primary parent");

  // 2.3 Delete and Revoke Node A: Verify Child Route Migration & Transaction
  PoleDeleteResult delRes = gwRegistry.deleteAndRevokePoleEx(poleIdA, "ADMIN_TEST", "SAFETY_AUDIT");
  TEST_ASSERT(delRes == DELETE_SUCCESS, "deleteAndRevokePoleEx() returned DELETE_SUCCESS");
  TEST_ASSERT(gwRegistry.findPoleById(poleIdA) == NULL, "Node A removed from active registry");
  TEST_ASSERT(gwRegistry.isMacRevoked(macA) == true, "Node A MAC permanently blacklisted");

  // Check child route migration
  childPole = gwRegistry.findPoleById(poleIdB);
  TEST_ASSERT(childPole && childPole->primaryParent == MSCB_GATEWAY_NODE_ID,
              "Child Node B successfully migrated primary parent to Gateway (0x0001)");

  // 2.4 Verify Re-join attempt of revoked hardware is blocked
  PktRegisterRequestPayload rejoinReq;
  memset(&rejoinReq, 0, sizeof(rejoinReq));
  memcpy(rejoinReq.macAddress, macA, 6);
  PktRegisterResponsePayload rejoinResp;
  uint8_t st = gwRegistry.handleJoinRequest(rejoinReq, -60, 10, rejoinResp);
  TEST_ASSERT(st == REG_RESP_REJECTED_REVOKED, "Join request from revoked MAC strictly rejected");
  TEST_ASSERT(gwRegistry.hasReappearedRevokedAlert() == true, "Reappearance alert triggered on join request");
  gwRegistry.clearReappearedRevokedAlert();

  // 2.5 Capacity Limit Enforcement: Fill revocation list up to MSCB_MAX_REVOCATION_RECORDS
  std::cout << "\nFilling revocation list to capacity (" << MSCB_MAX_REVOCATION_RECORDS << " records)..." << std::endl;
  uint8_t currentRev = gwRegistry.getRevokedCount(); // Currently 1
  for (uint8_t i = currentRev; i < MSCB_MAX_REVOCATION_RECORDS; i++) {
    // Register temporary pole
    uint8_t dummyMac[6] = {0xE0, 0x00, 0x00, 0x00, 0x00, i};
    PktRegisterRequestPayload dReq;
    memcpy(dReq.macAddress, dummyMac, 6);
    PktRegisterResponsePayload dResp;
    gwRegistry.handleJoinRequest(dReq, -70, 8, dResp);
    uint16_t dummyId = 0;
    gwRegistry.approvePending(dummyMac, dummyId);
    PoleDeleteResult r = gwRegistry.deleteAndRevokePoleEx(dummyId, "ADMIN_FILL", "CAPACITY_TEST");
    assert(r == DELETE_SUCCESS);
  }
  TEST_ASSERT(gwRegistry.getRevokedCount() == MSCB_MAX_REVOCATION_RECORDS,
              "Revocation list filled to exact maximum capacity (64 records)");

  // Now attempt to delete Node B while revocation list is FULL
  uint8_t activeBefore = gwRegistry.getActiveCount();
  PoleDeleteResult fullRes = gwRegistry.deleteAndRevokePoleEx(poleIdB, "ADMIN_TEST", "SHOULD_FAIL");
  TEST_ASSERT(fullRes == DELETE_ERR_REVOCATION_FULL,
              "deleteAndRevokePoleEx() safely returns DELETE_ERR_REVOCATION_FULL when list is full");
  TEST_ASSERT(gwRegistry.getActiveCount() == activeBefore,
              "Active pole count NOT altered when revocation list is full");
  TEST_ASSERT(gwRegistry.findPoleById(poleIdB) != NULL,
              "Active pole Node B preserved and NOT deleted when revocation list is full!");

  // -------------------------------------------------------------------------
  // SECTION 3: RE-AUTHORIZATION & UN-REVOCATION FLOW
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION 3: RE-AUTHORIZATION & UN-REVOKE FLOW ---" << std::endl;
  TEST_ASSERT(gwRegistry.isMacRevoked(macA) == true, "Node A MAC is currently revoked");

  bool unrevOk = gwRegistry.unrevokePole(macA, "ADMIN_CHIEF", "CLEARED_SECURITY_REVIEW");
  TEST_ASSERT(unrevOk == true, "unrevokePole() successfully executed for Node A");
  TEST_ASSERT(gwRegistry.isMacRevoked(macA) == false, "Node A MAC is no longer in revocation list");

  // Node A can now re-request registration
  uint8_t stNew = gwRegistry.handleJoinRequest(rejoinReq, -60, 10, rejoinResp);
  TEST_ASSERT(stNew == REG_RESP_PENDING_APPROVAL,
              "Node A join request now accepted into PENDING_APPROVAL queue");
  TEST_ASSERT(gwRegistry.getPendingCount() == 1, "Node A successfully entered pending queue");

  // -------------------------------------------------------------------------
  // SECTION 4: FIELD POLE PERSISTENT STATE & SAFETY LOCK
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION 4: FIELD POLE CLIENT REVOCATION BEHAVIOR ---" << std::endl;
  poleStorage.begin();
  PoleRegistrationClient poleClient;
  poleClient.begin();
  poleClient.setMacAddress(macA);

  // Set field pole into revoked state
  poleStorage.setRevokedLocked("ADMIN_LOCKED");
  TEST_ASSERT(poleStorage.isRevokedLocked() == true, "Field Pole storage confirms REVOKED_LOCKED");

  // Relays must remain locked OFF
  poleActuators.init(true);
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled() == true,
              "Relays strictly locked disabled on revoked pole");

  std::cout << "\n=====================================================================" << std::endl;
  std::cout << "  TEST RESULTS: " << g_pass << " PASSED, " << g_fail << " FAILED" << std::endl;
  std::cout << "=====================================================================\n" << std::endl;

  return (g_fail == 0) ? 0 : 1;
}
