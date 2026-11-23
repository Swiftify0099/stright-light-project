/**
 * @file test_r1_registration_and_revocation.cpp
 * @brief MSCB Smart Pole System R1 - Node Discovery, Telegram Registration & Revocation Test Suite
 */

#include "ArduinoMock.h"
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../Gateway_ESP32S3/GwNodeRegistry.h"
#include "../FieldPole_ESP32/PoleRegistrationClient.h"
#include "../FieldPole_ESP32/PoleStorageManager.h"

MockSerial Serial;

static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_REPORT(cond, tag, msg) \
  do { \
    if (cond) { \
      std::cout << "  [" tag "] " << msg << std::endl; \
      g_passCount++; \
    } else { \
      std::cout << "  [FAIL] " << msg << " (Line " << __LINE__ << ")" << std::endl; \
      g_failCount++; \
    } \
  } while (0)

int main() {
  std::cout << "\n========================================================" << std::endl;
  std::cout << "  TEST SUITE 2: NODE DISCOVERY, REGISTRATION & REVOCATION" << std::endl;
  std::cout << "========================================================" << std::endl;

  GwNodeRegistry reg;
  reg.begin();
  poleStorage.begin();

  // -----------------------------------------------------------------
  // 1. Initial Broadcast Discovery & Pending Queue Insertion
  // -----------------------------------------------------------------
  std::cout << "\n[1. BROADCAST DISCOVERY & PENDING REGISTRATION]" << std::endl;
  uint8_t mac1[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x01};
  {
    PktRegisterRequestPayload req;
    memset(&req, 0, sizeof(req));
    memcpy(req.macAddress, mac1, 6);
    req.firmwareVersion = 0x0100;
    req.candidateParent1 = MSCB_GATEWAY_NODE_ID;
    req.sessionToken = 0x12345678;

    PktRegisterResponsePayload resp;
    uint8_t status = reg.handleJoinRequest(req, -68, 8, resp);

    TEST_REPORT(status == REG_RESP_PENDING_APPROVAL, "INTEGRATION_PASS",
                "New unprovisioned node placed into PENDING_APPROVAL queue (not auto-authorized)");
    TEST_REPORT(reg.getPendingCount() == 1, "UNIT_PASS",
                "Pending registration queue count incremented to 1");

    // Duplicate join request while pending should not duplicate queue entries
    status = reg.handleJoinRequest(req, -67, 9, resp);
    TEST_REPORT(status == REG_RESP_PENDING_APPROVAL && reg.getPendingCount() == 1, "UNIT_PASS",
                "Duplicate join request from same MAC suppressed; pending count unchanged");
  }

  // -----------------------------------------------------------------
  // 2. Telegram Admin Approval Flow & Logical ID Assignment
  // -----------------------------------------------------------------
  std::cout << "\n[2. TELEGRAM ADMIN APPROVAL & LOGICAL ID ALLOCATION]" << std::endl;
  uint16_t assignedId1 = 0;
  {
    bool approved = reg.approvePending(mac1, assignedId1);
    TEST_REPORT(approved, "INTEGRATION_PASS", "Admin approval transaction succeeded");
    TEST_REPORT(assignedId1 == GW_POLE_ID_START, "UNIT_PASS",
                "First approved Pole allocated starting logical ID (0x0002)");
    TEST_REPORT(reg.getActiveCount() == 1, "UNIT_PASS", "Active node count updated to 1");
    TEST_REPORT(reg.getPendingCount() == 0, "UNIT_PASS", "Pending queue decremented upon approval");

    RegisteredPole* p = reg.findPoleById(assignedId1);
    TEST_REPORT(p != NULL && p->status == POLE_STATUS_ACTIVE, "INTEGRATION_PASS",
                "Pole record verified ACTIVE in Gateway registry");
  }

  // -----------------------------------------------------------------
  // 3. Field Pole Client Handles Approval & Persists Logical ID
  // -----------------------------------------------------------------
  std::cout << "\n[3. FIELD POLE CLIENT PERSISTENCE]" << std::endl;
  {
    PoleRegistrationClient client;
    client.begin();
    client.setMacAddress(mac1);

    PktRegisterResponsePayload resp;
    memset(&resp, 0, sizeof(resp));
    memcpy(resp.macAddress, mac1, 6);
    resp.status = REG_RESP_APPROVED;
    resp.assignedPoleId = assignedId1;
    resp.assignedParent = MSCB_GATEWAY_NODE_ID;
    resp.heartbeatIntervalSec = 30;

    bool processed = client.handleRegisterResponse(resp);
    TEST_REPORT(processed, "INTEGRATION_PASS", "Field Pole successfully processed approved registration");
    TEST_REPORT(client.isRegistered() && client.getAssignedId() == assignedId1, "UNIT_PASS",
                "Field Pole state transitions to REGISTERED with assigned ID (0x0002)");
    TEST_REPORT(poleStorage.getAssignedPoleId() == assignedId1, "INTEGRATION_PASS",
                "Field Pole NVS storage confirmed persisting assigned ID");
  }

  // -----------------------------------------------------------------
  // 4. Telegram Admin Rejection Flow
  // -----------------------------------------------------------------
  std::cout << "\n[4. TELEGRAM ADMIN REJECTION FLOW]" << std::endl;
  uint8_t mac2[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x02};
  {
    PktRegisterRequestPayload req;
    memset(&req, 0, sizeof(req));
    memcpy(req.macAddress, mac2, 6);
    req.firmwareVersion = 0x0100;
    req.candidateParent1 = MSCB_GATEWAY_NODE_ID;

    PktRegisterResponsePayload resp;
    reg.handleJoinRequest(req, -80, 5, resp);
    TEST_REPORT(reg.getPendingCount() == 1, "UNIT_PASS", "Second node placed in pending queue");

    bool rejected = reg.rejectPending(mac2);
    TEST_REPORT(rejected, "INTEGRATION_PASS", "Admin successfully rejected pending node");
    TEST_REPORT(reg.getPendingCount() == 0, "UNIT_PASS", "Rejected node evicted from pending queue");
    TEST_REPORT(reg.findPoleByMac(mac2) == NULL, "UNIT_PASS",
                "Rejected node does not exist in active registry");
  }

  // -----------------------------------------------------------------
  // 5. Enforce 50-Node Active Pole Capacity Limit
  // -----------------------------------------------------------------
  std::cout << "\n[5. 50-NODE CAPACITY LIMIT ENFORCEMENT]" << std::endl;
  {
    // Register nodes up to capacity (already have 1 active pole, add 49 more)
    for (uint8_t i = 2; i <= GW_MAX_MANAGED_POLES; i++) {
      uint8_t m[6] = {0x00, 0x50, 0x00, 0x00, 0x00, i};
      PktRegisterRequestPayload req;
      memcpy(req.macAddress, m, 6);
      req.firmwareVersion = 0x0100;
      PktRegisterResponsePayload resp;
      reg.handleJoinRequest(req, -70, 8, resp);
      uint16_t id = 0;
      reg.approvePending(m, id);
    }

    TEST_REPORT(reg.getActiveCount() == GW_MAX_MANAGED_POLES, "INTEGRATION_PASS",
                "Registry filled to exact maximum capacity (50 active nodes)");

    // Attempt to register the 51st node
    uint8_t mac51[6] = {0xFF, 0xFF, 0x51, 0x51, 0x51, 0x51};
    PktRegisterRequestPayload req51;
    memcpy(req51.macAddress, mac51, 6);
    req51.firmwareVersion = 0x0100;
    PktRegisterResponsePayload resp51;
    uint8_t status51 = reg.handleJoinRequest(req51, -70, 8, resp51);

    TEST_REPORT(status51 == REG_RESP_REJECTED_CAPACITY, "INTEGRATION_PASS",
                "51st Node strictly rejected: REG_RESP_REJECTED_CAPACITY (Max 50 Nodes enforced)");
    TEST_REPORT(reg.getActiveCount() == GW_MAX_MANAGED_POLES, "UNIT_PASS",
                "Active node count remains strictly capped at 50");
  }

  // -----------------------------------------------------------------
  // 6. Two-Step Node Deletion & Permanent Revocation Blacklist
  // -----------------------------------------------------------------
  std::cout << "\n[6. PERMANENT REVOCATION BLACKLIST]" << std::endl;
  {
    // Delete Pole 1 (assignedId1)
    bool deleted = reg.deleteAndRevokePole(assignedId1, "TG_ADMIN_99", "HARDWARE_COMPROMISED");
    TEST_REPORT(deleted, "INTEGRATION_PASS", "Node deletion and revocation executed with audit metadata");
    TEST_REPORT(reg.getActiveCount() == 49, "UNIT_PASS", "Active node count decremented to 49");
    TEST_REPORT(reg.isMacRevoked(mac1), "INTEGRATION_PASS",
                "Hardware MAC permanently recorded in Revocation Blacklist");

    // Now, attempt to re-register the revoked hardware MAC
    PktRegisterRequestPayload rejoinReq;
    memcpy(rejoinReq.macAddress, mac1, 6);
    rejoinReq.firmwareVersion = 0x0100;
    PktRegisterResponsePayload rejoinResp;

    uint8_t rejoinStatus = reg.handleJoinRequest(rejoinReq, -65, 10, rejoinResp);
    TEST_REPORT(rejoinStatus == REG_RESP_REJECTED_REVOKED, "INTEGRATION_PASS",
                "Re-join request from revoked hardware strictly rejected (REG_RESP_REJECTED_REVOKED)");
    TEST_REPORT(reg.getPendingCount() == 0, "UNIT_PASS",
                "Revoked node cannot even enter pending queue");
  }

  std::cout << "\n========================================================" << std::endl;
  std::cout << "TEST SUITE 2 SUMMARY: " << g_passCount << " PASSED, " << g_failCount << " FAILED" << std::endl;
  std::cout << "========================================================\n" << std::endl;

  return (g_failCount == 0) ? 0 : 1;
}
