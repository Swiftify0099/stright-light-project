/**
 * @file test_r3_2_2_registration_telegram.cpp
 * @brief MSCB Smart Pole System R3.2.2 - Field Pole Registration & Telegram
 * Spam Reliability Verification Suite
 *
 * Verifies:
 * - Section AG: Registration Request Reliability (MAC, Radio, Auth Key, TX
 * Verification, Backoff)
 * - Section AH: Pending Approval Flow (Gateway Response, Nonce Correlation,
 * Backoff, One-Time Alert)
 * - Section AI: Approval & Runtime Identity (setNodeIdentity without reboot,
 * Frame Source Truth, First Handshake)
 * - Section AJ: Registration Rejection & Revocation (Lockout, Blacklist, Zero
 * Fake Activation)
 * - Section AK: Telegram Message Deduplication (100 Heartbeats = 1 Alert,
 * Digest Batching, Recipient Dedup)
 * - Section AL: Telegram Service & Main Loop Regression (Command Polling,
 * Wizard, Zero Stalling)
 */

#define ARDUINO_MOCK_IMPLEMENTATION
#include "../FieldPole_ESP32/PoleActuators.h"
#include "../FieldPole_ESP32/PoleBoardPins.h"
#include "../FieldPole_ESP32/PoleFeederManager.h"
#include "../FieldPole_ESP32/PoleHeartbeatSender.h"
#include "../FieldPole_ESP32/PoleMeshRouter.h"
#include "../FieldPole_ESP32/PoleMetrology.h"
#include "../FieldPole_ESP32/PoleRadioDriver.h"
#include "../FieldPole_ESP32/PoleRegistrationClient.h"
#include "../FieldPole_ESP32/PoleStorageManager.h"
#include "../FieldPole_ESP32/PoleTimeManager.h"
#include "../Gateway_ESP32S3/GwHeartbeatMonitor.h"
#include "../Gateway_ESP32S3/GwMeshCoordinator.h"
#include "../Gateway_ESP32S3/GwNodeRegistry.h"
#include "../Gateway_ESP32S3/GwRadioDriver.h"
#include "../Gateway_ESP32S3/GwScheduleManager.h"
#include "../Gateway_ESP32S3/GwTelegramBot.h"
#include "../Gateway_ESP32S3/GwWiFiManager.h"
#include "../MSCBShared/BuildConfig.h"
#include "../MSCBShared/CommandSecurity.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/ReplayProtection.h"
#include "../MSCBShared/StorageTypes.h"
#include "ArduinoMock.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

MockSerial Serial;

static int g_pass = 0;
static int g_fail = 0;

#define TEST_ASSERT(cond, msg)                                                 \
  do {                                                                         \
    if (cond) {                                                                \
      std::cout << "  [PASS] " << msg << std::endl;                            \
      g_pass++;                                                                \
    } else {                                                                   \
      std::cout << "  [FAIL] " << msg << " (Line " << __LINE__ << ")"          \
                << std::endl;                                                  \
      g_fail++;                                                                \
    }                                                                          \
  } while (0)

// --- Test-only Telegram transport fault injector (per-admin outcomes) -------
static int64_t g_hookTargetChat = 0;
static int g_hookFault = 0; // 1 = one transient failure, 2 = one ambiguous timeout
static bool g_hookFaultPending = false;

static TgTransportOutcome testTelegramHook(int64_t chatId, const char *,
                                           const char *, uint32_t &retryAfterSec) {
  retryAfterSec = 0;
  if (g_hookFaultPending && chatId == g_hookTargetChat) {
    g_hookFaultPending = false;
    if (g_hookFault == 1) return TG_TX_TRANSIENT_FAILURE;
    if (g_hookFault == 2) return TG_TX_AMBIGUOUS_TIMEOUT;
  }
  return TG_TX_OK;
}

int main() {
  std::cout
      << "====================================================================="
      << std::endl;
  std::cout << "  MSCB SMART POLE SYSTEM R3.2.2 - REGISTRATION & TELEGRAM "
               "VERIFICATION"
            << std::endl;
  std::cout
      << "====================================================================="
      << std::endl;

  // Initialize subsystem mocks
  uint8_t testNetworkKey[32] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                                0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                                0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};
  cmdSecurity.provisionKey(testNetworkKey, 32, 1);
  gwRegistry.resetStorageToDefault();
  gwRegistry.begin();
  gwMesh.begin();
  gwHeartbeat.begin();
  gwTelegram.begin();
  gwWifi.setConnected(true);

  // -------------------------------------------------------------------------
  // SECTION AG: REGISTRATION REQUEST RELIABILITY (P0)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AG: REGISTRATION REQUEST RELIABILITY ---"
            << std::endl;

  // AG.1 Invalid MAC rejected (All zeroes)
  poleRegClient.clearRegistration();
  uint8_t zeroMac[6] = {0, 0, 0, 0, 0, 0};
  poleRegClient.setMacAddress(zeroMac);
  bool reqResult = poleRegClient.triggerRegistrationRequest();
  TEST_ASSERT(!reqResult,
              "AG.1 Registration request rejected with all-zero MAC");
  TEST_ASSERT(poleRegClient.getState() == REG_CLIENT_REGISTRATION_ERROR,
              "AG.2 Node state transitions to REG_CLIENT_REGISTRATION_ERROR on "
              "invalid MAC");
  TEST_ASSERT(poleRegClient.getLastError() == REG_ERR_INVALID_MAC,
              "AG.3 Failure reason recorded as REG_ERR_INVALID_MAC");

  // Set valid MAC
  uint8_t validPoleMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x05};
  poleRegClient.setMacAddress(validPoleMac);
  poleRegClient.clearRegistration();

  // AG.4 Radio unavailable fails closed
  poleRadio.setRadioDetected(false);
  reqResult = poleRegClient.triggerRegistrationRequest();
  TEST_ASSERT(
      !reqResult,
      "AG.4 triggerRegistrationRequest fails when LoRa radio is not detected");
  TEST_ASSERT(poleRegClient.getLastError() == REG_ERR_RADIO_UNAVAILABLE,
              "AG.5 Failure reason recorded as REG_ERR_RADIO_UNAVAILABLE");
  TEST_ASSERT(poleRegClient.getState() == REG_CLIENT_REGISTRATION_ERROR,
              "AG.6 State becomes REG_CLIENT_REGISTRATION_ERROR (never false "
              "REQUEST_SENT)");

  // Restore radio
  poleRadio.setRadioDetected(true);
  poleRegClient.clearRegistration();

  // AG.7 Unprovisioned key fails closed
  cmdSecurity.revokeKey();
  reqResult = poleRegClient.triggerRegistrationRequest();
  TEST_ASSERT(!reqResult, "AG.7 Registration fails closed when authentication "
                          "key is unprovisioned");
  TEST_ASSERT(poleRegClient.getLastError() == REG_ERR_AUTH_KEY_UNAVAILABLE,
              "AG.8 Failure reason recorded as REG_ERR_AUTH_KEY_UNAVAILABLE");

  // Restore key
  cmdSecurity.provisionKey(testNetworkKey, 32, 1);
  poleRegClient.clearRegistration();

  // AG.9 Failed send does not create false REQUEST_SENT status
  poleRadio.setForceTxFail(true);
  uint16_t initialRetryInterval = poleRegClient.getRetryIntervalMs();
  reqResult = poleRegClient.triggerRegistrationRequest();
  TEST_ASSERT(!reqResult, "AG.9 Failed radio transmission returns false");
  TEST_ASSERT(
      poleRegClient.getState() != REG_CLIENT_REQUEST_TRANSMITTED &&
          poleRegClient.getState() != REG_CLIENT_JOIN_REQUEST_SENT,
      "AG.10 Failed transmission does NOT create false REQUEST_SENT state");
  TEST_ASSERT(poleRegClient.getLastError() == REG_ERR_TX_FAILED,
              "AG.11 Error reason recorded as REG_ERR_TX_FAILED");
  TEST_ASSERT(poleRegClient.getRetryIntervalMs() > initialRetryInterval,
              "AG.12 Failed transmission triggers exponential backoff");

  // AG.13 Successful radio transmission updates state
  poleRadio.setForceTxFail(false);
  poleRegClient.clearRegistration();
  reqResult = poleRegClient.triggerRegistrationRequest();
  TEST_ASSERT(reqResult,
              "AG.13 Successful LoRa transmission accepted by driver");
  TEST_ASSERT(poleRegClient.getState() == REG_CLIENT_REQUEST_TRANSMITTED,
              "AG.14 Node state updated to REG_CLIENT_REQUEST_TRANSMITTED on "
              "real TX acceptance");
  TEST_ASSERT(poleRegClient.getLastError() == REG_ERR_NONE,
              "AG.15 Last error cleared on successful transmission");

  // -------------------------------------------------------------------------
  // SECTION AH: PENDING APPROVAL FLOW & ONE-TIME NOTIFICATION (P0)
  // -------------------------------------------------------------------------
  std::cout
      << "\n--- SECTION AH: PENDING APPROVAL & ONE-TIME TELEGRAM ALERT ---"
      << std::endl;

  // AH.1 Gateway creates pending record and returns REG_RESP_PENDING_APPROVAL
  // Correlate the synthetic gateway request with the pole's latest real TX.
  PktRegisterRequestPayload regReq;
  memset(&regReq, 0, sizeof(regReq));
  memcpy(regReq.macAddress, validPoleMac, 6);
  regReq.nodeType = 1;
  regReq.firmwareVersion = 32;
  regReq.sessionToken = poleRegClient.getSessionToken();
  regReq.candidateParent1 = MSCB_GATEWAY_NODE_ID;
  regReq.nonce = poleRegClient.getLastRequestNonce();
  if (regReq.nonce == 0)
    regReq.nonce = 1;
  cmdSecurity.computeRegisterRequestAuth(regReq, regReq.authTag);

  PktRegisterResponsePayload regResp;
  uint8_t gwStatus = gwRegistry.handleJoinRequest(regReq, -72, 9, regResp);
  TEST_ASSERT(gwStatus == REG_RESP_PENDING_APPROVAL,
              "AH.1 Gateway returns REG_RESP_PENDING_APPROVAL for valid "
              "unapproved node");
  TEST_ASSERT(regResp.status == REG_RESP_PENDING_APPROVAL,
              "AH.2 Response payload status is REG_RESP_PENDING_APPROVAL");
  TEST_ASSERT(regResp.sessionToken == regReq.sessionToken,
              "AH.3 Response correlates request sessionToken");
  TEST_ASSERT(regResp.nonce == regReq.nonce,
              "AH.4 Response correlates request nonce");

  // AH.5 Cryptographic response authentication verified
  TEST_ASSERT(cmdSecurity.verifyRegisterResponseAuth(regResp),
              "AH.5 Response authTag verified via HMAC-SHA256");

  // AH.6 Field Pole receives PENDING_APPROVAL and backs off
  bool handleOk = poleRegClient.handleRegisterResponse(regResp);
  TEST_ASSERT(handleOk,
              "AH.6 Field Pole accepts valid PENDING_APPROVAL response");
  TEST_ASSERT(poleRegClient.getState() == REG_CLIENT_PENDING_APPROVAL,
              "AH.7 Field Pole transitions to REG_CLIENT_PENDING_APPROVAL");
  TEST_ASSERT(poleRegClient.getRetryIntervalMs() >= 60000,
              "AH.8 Field Pole join retries back off to >= 60 seconds (rapid "
              "12s join loop halted)");

  // AH.9 Gateway creates exactly one pending registration alert
  TEST_ASSERT(gwRegistry.hasPendingRegistrationAlert(),
              "AH.9 Gateway registry flags pending registration alert");
  int alertPendingIdx = -1;
  const char *alertText =
      gwRegistry.getNextPendingRegistrationAlertText(alertPendingIdx);
  TEST_ASSERT(
      alertText != nullptr &&
          strstr(alertText, "NEW POLE REGISTRATION REQUEST") != nullptr,
      "AH.10 Telegram alert text contains 'NEW POLE REGISTRATION REQUEST'");
  TEST_ASSERT(alertPendingIdx >= 0, "AH.11 Valid pending index identified");

  // Dispatch alert
  gwTelegram.broadcastAdminAlert(alertText);
  gwRegistry.markPendingRegistrationNotified(alertPendingIdx);
  TEST_ASSERT(
      !gwRegistry.hasPendingRegistrationAlert(),
      "AH.12 Alert marked notified; hasPendingRegistrationAlert becomes false");

  // AH.13 Repeated JOIN requests from same MAC update record without creating a
  // second alert
  uint8_t countBefore = gwRegistry.getPendingCount();
  gwStatus = gwRegistry.handleJoinRequest(regReq, -70, 10, regResp);
  TEST_ASSERT(
      gwStatus == REG_RESP_PENDING_APPROVAL,
      "AH.13 Repeated JOIN request still acknowledged as PENDING_APPROVAL");
  TEST_ASSERT(gwRegistry.getPendingCount() == countBefore,
              "AH.14 Pending queue count does not increment on retry (no "
              "duplicate records)");
  TEST_ASSERT(!gwRegistry.hasPendingRegistrationAlert(),
              "AH.15 Repeated JOIN retry does NOT generate a second Telegram "
              "notification");

  // -------------------------------------------------------------------------
  // SECTION AI: APPROVAL & RUNTIME MESH IDENTITY UPDATE (P0)
  // -------------------------------------------------------------------------
  std::cout
      << "\n--- SECTION AI: APPROVAL & RUNTIME IDENTITY REINITIALIZATION ---"
      << std::endl;

  // AI.1 Approval assigns unique Pole ID
  uint16_t assignedId = 0;
  bool appOk =
      gwRegistry.approvePending(validPoleMac, assignedId, "Main Gate Light",
                                "North Avenue Sector 4", "ADMIN_1");
  TEST_ASSERT(appOk,
              "AI.1 Admin approval succeeds with valid Name and Location");
  TEST_ASSERT(
      assignedId >= GW_POLE_ID_START &&
          assignedId <= (GW_POLE_ID_START + GW_MAX_MANAGED_POLES),
      "AI.2 Assigned Pole ID is in valid non-zero managed range (>= 0x0002)");

  // AI.3 Gateway sends approval response
  bool sendRespOk = gwMesh.sendRegisterResponse(
      validPoleMac, assignedId, MSCB_GATEWAY_NODE_ID, REG_RESP_APPROVED);
  TEST_ASSERT(sendRespOk, "AI.3 Gateway transmits authenticated approval "
                          "response frame over LoRa mesh");

  // Construct approval response for Field Pole using the latest request nonce.
  PktRegisterResponsePayload appResp;
  memset(&appResp, 0, sizeof(appResp));
  memcpy(appResp.macAddress, validPoleMac, 6);
  appResp.status = REG_RESP_APPROVED;
  appResp.assignedPoleId = assignedId;
  appResp.assignedParent = MSCB_GATEWAY_NODE_ID;
  appResp.heartbeatIntervalSec = 30;
  appResp.networkKeyEpoch = 1;
  appResp.sessionToken = poleRegClient.getSessionToken();
  appResp.nonce = poleRegClient.getLastRequestNonce();
  cmdSecurity.computeRegisterResponseAuth(appResp, appResp.authTag);

  // AI.4 Wrong MAC response rejected
  PktRegisterResponsePayload wrongMacResp = appResp;
  wrongMacResp.macAddress[5] ^= 0xFF;
  TEST_ASSERT(!poleRegClient.handleRegisterResponse(wrongMacResp),
              "AI.4 Field Pole rejects approval response with mismatched MAC");

  // AI.5 Wrong session token rejected
  PktRegisterResponsePayload wrongSessResp = appResp;
  wrongSessResp.sessionToken = 0xDEADBEEF;
  cmdSecurity.computeRegisterResponseAuth(wrongSessResp, wrongSessResp.authTag);
  TEST_ASSERT(
      !poleRegClient.handleRegisterResponse(wrongSessResp),
      "AI.5 Field Pole rejects approval response with invalid session token");

  // AI.6 Invalid HMAC tag rejected
  PktRegisterResponsePayload badAuthResp = appResp;
  badAuthResp.authTag[0] ^= 0xFF;
  TEST_ASSERT(!poleRegClient.handleRegisterResponse(badAuthResp),
              "AI.6 Field Pole rejects approval response with corrupted "
              "authentication tag");

  // AI.6b Correctly authenticated but stale/unrelated nonce is rejected.
  PktRegisterResponsePayload staleNonceResp = appResp;
  staleNonceResp.nonce = appResp.nonce - 1U;
  if (staleNonceResp.nonce == 0)
    staleNonceResp.nonce = appResp.nonce + 1U;
  cmdSecurity.computeRegisterResponseAuth(staleNonceResp,
                                          staleNonceResp.authTag);
  TEST_ASSERT(!poleRegClient.handleRegisterResponse(staleNonceResp),
              "AI.6b Field Pole rejects authenticated response for an "
              "older/unrelated request nonce");

  // AI.7 Valid approval response accepted
  uint16_t preApprovalRouterId = poleMesh.getMyNodeId();
  bool approvedHandled = poleRegClient.handleRegisterResponse(appResp);
  TEST_ASSERT(approvedHandled,
              "AI.7 Field Pole accepts valid authenticated approval response");
  TEST_ASSERT(poleRegClient.isRegistered(),
              "AI.8 Field Pole state becomes REG_CLIENT_REGISTERED");
  TEST_ASSERT(poleStorage.getAssignedPoleId() == assignedId,
              "AI.9 Assigned Pole ID persisted in PoleStorageManager");

  // AI.10 Runtime Mesh Router Identity updated WITHOUT power reboot!
  TEST_ASSERT(poleMesh.getMyNodeId() == assignedId,
              "AI.10 PoleMeshRouter.myNodeId updated immediately at runtime "
              "(NO MANUAL REBOOT REQUIRED!)");
  TEST_ASSERT(poleMesh.getMyNodeId() != preApprovalRouterId,
              "AI.11 PoleMeshRouter no longer uses old unassigned default ID");
  TEST_ASSERT(poleMesh.getPrimaryParent() == MSCB_GATEWAY_NODE_ID,
              "AI.12 PoleMeshRouter primary parent updated to Gateway");

  // AI.13 First authenticated heartbeat matches assigned Pole ID
  MeshWireFrame hbWireFrame;
  memset(&hbWireFrame, 0, sizeof(hbWireFrame));
  hbWireFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  hbWireFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  hbWireFrame.header.version = MSCB_PROTOCOL_VERSION;
  hbWireFrame.header.type = PKT_HEARTBEAT_PING;
  hbWireFrame.header.srcAddress = assignedId; // Must match assigned ID!
  hbWireFrame.header.dstAddress = MSCB_GATEWAY_NODE_ID;
  hbWireFrame.header.nextHop = MSCB_GATEWAY_NODE_ID;
  hbWireFrame.header.previousHopAddress = assignedId;
  hbWireFrame.header.hopCount = 0;
  hbWireFrame.header.maxHops = GW_MESH_MAX_HOPS;
  hbWireFrame.header.seqNumber = 1;

  PktHeartbeatPingPayload hbPayload;
  memset(&hbPayload, 0, sizeof(hbPayload));
  hbPayload.poleId = assignedId;
  hbPayload.primaryParent = MSCB_GATEWAY_NODE_ID;
  hbPayload.rank = 1;
  hbPayload.healthState = NODE_HEALTH_OK;
  memcpy(hbPayload.macAddress, validPoleMac, 6);
  hbPayload.sessionToken = poleRegClient.getSessionToken();
  hbPayload.seqNonce = 1;
  cmdSecurity.computeHeartbeatAuth(hbPayload, hbPayload.authTag);
  hbWireFrame.header.payloadLen = sizeof(PktHeartbeatPingPayload);
  memcpy(hbWireFrame.payload, &hbPayload, sizeof(PktHeartbeatPingPayload));
  hbWireFrame.header.hopAuthToken =
      cmdSecurity.computeFrameHopAuthToken(hbWireFrame);

  RegisteredPole *regEntry = gwRegistry.findPoleById(assignedId);
  TEST_ASSERT(regEntry != nullptr,
              "AI.14 Approved pole exists in Gateway registry");
  TEST_ASSERT(
      regEntry->status == POLE_STATUS_PENDING_ACK,
      "AI.15 Pole status is POLE_STATUS_PENDING_ACK prior to first heartbeat");

  // Process first heartbeat on Gateway
  gwMesh.processIncomingWireFrame(hbWireFrame, -68, 10);
  TEST_ASSERT(regEntry->status == POLE_STATUS_ACTIVE,
              "AI.16 Gateway confirms handshake upon first heartbeat: status "
              "becomes POLE_STATUS_ACTIVE");

  // AI.17 Multi-hop Registration Response: Intermediate pole forwards,
  // destination accepts. Generate an actual outstanding request so the
  // response nonce is tested against the latest-request-only policy.
  uint8_t childPoleMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x99};
  poleRegClient.setMacAddress(childPoleMac);
  poleRegClient.clearRegistration();
  poleMesh.resetToUnassignedIdentity();
  poleRegClient.triggerRegistrationRequest();

  MeshWireFrame multiHopRespFrame;
  memset(&multiHopRespFrame, 0, sizeof(multiHopRespFrame));
  multiHopRespFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  multiHopRespFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  multiHopRespFrame.header.version = MSCB_PROTOCOL_VERSION;
  multiHopRespFrame.header.type = PKT_REGISTER_RESPONSE;
  multiHopRespFrame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
  multiHopRespFrame.header.dstAddress = 0x0008;  // Assigned ID for child
  multiHopRespFrame.header.nextHop = assignedId; // Forwarded through relay pole
  multiHopRespFrame.header.previousHopAddress =
      assignedId; // Intermediate forwarder
  multiHopRespFrame.header.seqNumber = 100;
  multiHopRespFrame.header.hopCount = 1;
  multiHopRespFrame.header.maxHops = 6;

  PktRegisterResponsePayload childAppResp;
  memset(&childAppResp, 0, sizeof(childAppResp));
  memcpy(childAppResp.macAddress, childPoleMac, 6);
  childAppResp.status = REG_RESP_APPROVED;
  childAppResp.assignedPoleId = 0x0008;
  childAppResp.assignedParent = assignedId;
  childAppResp.sessionToken = poleRegClient.getSessionToken();
  childAppResp.nonce = poleRegClient.getLastRequestNonce();
  childAppResp.networkKeyEpoch =
      cmdSecurity.getActiveKeyEpoch() ? cmdSecurity.getActiveKeyEpoch() : 1;
  cmdSecurity.computeRegisterResponseAuth(childAppResp, childAppResp.authTag);
  multiHopRespFrame.header.payloadLen = sizeof(PktRegisterResponsePayload);
  memcpy(multiHopRespFrame.payload, &childAppResp,
         sizeof(PktRegisterResponsePayload));
  multiHopRespFrame.header.hopAuthToken =
      cmdSecurity.computeFrameHopAuthToken(multiHopRespFrame);

  MeshWireFrame forwardAction;
  bool sendAction = false;
  // Destination pole receives frame where dstAddress is 0x0008, matching
  // payload MAC
  bool rxAccept = poleMesh.handleReceivedWireFrame(multiHopRespFrame, -75, 8,
                                                   forwardAction, sendAction);
  TEST_ASSERT(rxAccept, "AI.17 Destination pole accepts multi-hop response "
                        "frame matching its MAC locally");

  // -------------------------------------------------------------------------
  // SECTION AJ: REGISTRATION REJECT & REVOCATION (P0)
  // -------------------------------------------------------------------------
  std::cout
      << "\n--- SECTION AJ: REGISTRATION REJECTION & REVOCATION LOCKOUT ---"
      << std::endl;

  // AJ.1 Unauthorized Rejection
  // Start a fresh request transaction so rejection correlation is realistic.
  poleRegClient.clearRegistration();
  poleRegClient.triggerRegistrationRequest();
  PktRegisterResponsePayload rejResp;
  memset(&rejResp, 0, sizeof(rejResp));
  memcpy(rejResp.macAddress, childPoleMac, 6);
  rejResp.status = REG_RESP_REJECTED_UNAUTHORIZED;
  rejResp.sessionToken = poleRegClient.getSessionToken();
  rejResp.nonce = poleRegClient.getLastRequestNonce();
  rejResp.networkKeyEpoch =
      cmdSecurity.getActiveKeyEpoch() ? cmdSecurity.getActiveKeyEpoch() : 1;
  cmdSecurity.computeRegisterResponseAuth(rejResp, rejResp.authTag);

  bool rejAccepted = poleRegClient.handleRegisterResponse(rejResp);
  TEST_ASSERT(!rejAccepted,
              "AJ.1 handleRegisterResponse returns false on rejection");
  TEST_ASSERT(poleRegClient.getState() == REG_CLIENT_REJECTED,
              "AJ.2 Field Pole transitions to persistent REG_CLIENT_REJECTED");
  TEST_ASSERT(!poleRegClient.isRegistered(),
              "AJ.3 Rejected pole cannot become active");

  // AJ.4 Permanent Revocation Enforcement
  uint8_t revokedMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  gwRegistry.deleteAndRevokePoleEx(assignedId, "ADMIN_TEST",
                                   "SAFETY_COMPROMISED");
  TEST_ASSERT(gwRegistry.isMacRevoked(validPoleMac),
              "AJ.4 Revoked MAC permanently blacklisted in NVS");

  // JOIN request from revoked MAC blocked
  poleRegClient.setMacAddress(validPoleMac);
  poleRegClient.clearRegistration();
  poleRegClient.triggerRegistrationRequest();
  PktRegisterRequestPayload revJoinReq;
  memset(&revJoinReq, 0, sizeof(revJoinReq));
  memcpy(revJoinReq.macAddress, validPoleMac, 6);
  revJoinReq.sessionToken = poleRegClient.getSessionToken();
  revJoinReq.nonce = poleRegClient.getLastRequestNonce();
  cmdSecurity.computeRegisterRequestAuth(revJoinReq, revJoinReq.authTag);
  PktRegisterResponsePayload revJoinResp;
  uint8_t revStatus =
      gwRegistry.handleJoinRequest(revJoinReq, -80, 5, revJoinResp);
  TEST_ASSERT(revStatus == REG_RESP_REJECTED_REVOKED,
              "AJ.5 Gateway rejects JOIN_REQ from blacklisted MAC with "
              "REG_RESP_REJECTED_REVOKED");

  // Field Pole locks down upon receiving REVOKED response
  poleRegClient.handleRegisterResponse(revJoinResp);
  TEST_ASSERT(poleRegClient.isRevoked(),
              "AJ.6 Field Pole enters persistent REG_CLIENT_REVOKED_LOCKED");
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled(),
              "AJ.7 Relays locked OFF permanently upon revocation response");

  // -------------------------------------------------------------------------
  // SECTION AK: TELEGRAM MESSAGE DEDUPLICATION (P0)
  // -------------------------------------------------------------------------
  std::cout
      << "\n--- SECTION AK: TELEGRAM ALERT DEDUPLICATION & RATE LIMITING ---"
      << std::endl;

  // Clear previous alerts
  gwRegistry.clearReappearedRevokedAlert();
  gwRegistry.resetRevokedIncidentDeduplication();

  // AK.1 First revoked heartbeat generates exactly ONE alert
  bool firstAlertCreated =
      gwRegistry.setReappearedRevokedAlert(validPoleMac, "POLE_02", assignedId);
  TEST_ASSERT(
      firstAlertCreated,
      "AK.1 First revoked device event creates a pending Telegram alert");
  TEST_ASSERT(gwRegistry.hasReappearedRevokedAlert(),
              "AK.2 hasReappearedRevokedAlert is true");

  // Clear alert buffer as if dispatched by bot
  gwRegistry.clearReappearedRevokedAlert();

  // AK.2 100 repeated heartbeats do NOT generate 100 Telegram alerts!
  int spamAlertsCreated = 0;
  for (int hb = 0; hb < 100; hb++) {
    bool created = gwRegistry.setReappearedRevokedAlert(validPoleMac, "POLE_02",
                                                        assignedId);
    if (created)
      spamAlertsCreated++;
  }
  TEST_ASSERT(spamAlertsCreated == 0,
              "AK.3 100 repeated heartbeats from revoked pole generate 0 "
              "additional Telegram alerts (100% SPAM ELIMINATED!)");
  TEST_ASSERT(
      gwRegistry.getRevokedIncidentOccurrences(validPoleMac) >= 101,
      "AK.4 Diagnostic occurrences tracked accurately without message spam");

  // AK.5 Different revoked MAC generates separate alert event
  uint8_t anotherRevMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x99, 0x99};
  bool secondMacAlert =
      gwRegistry.setReappearedRevokedAlert(anotherRevMac, "POLE_99", 0x0099);
  TEST_ASSERT(secondMacAlert, "AK.5 A genuinely different revoked MAC "
                              "generates its own distinct alert event");
  gwRegistry.clearReappearedRevokedAlert();

  // AK.6 Rearm window allows new alert after cooldown / incident reset
  gwRegistry.resetRevokedIncidentDeduplication(validPoleMac);
  bool rearmedAlert =
      gwRegistry.setReappearedRevokedAlert(validPoleMac, "POLE_02", assignedId);
  TEST_ASSERT(rearmedAlert, "AK.6 Reset / rearm allows one fresh alert event "
                            "for re-emerged incident");
  gwRegistry.clearReappearedRevokedAlert();

  // AK.7 Recipient deduplication: identical chat IDs receive only one copy
  bool broadcastOk =
      gwTelegram.broadcastAdminAlert("TEST_ALERT_SINGLE_DELIVERY");
  TEST_ASSERT(
      broadcastOk,
      "AK.7 broadcastAdminAlert executes with deduplicated recipient list");

  // AK.8 Digest Batching for concurrent pole outages
  gwHeartbeat.clearPendingAlert();
  // Queue 3 offline alerts simultaneously
  gwHeartbeat.queueAlert(0x0002, POLE_STATE_OFFLINE, 120, MSCB_GATEWAY_NODE_ID,
                         "TIMEOUT_TEST", 1000);
  gwHeartbeat.queueAlert(0x0003, POLE_STATE_OFFLINE, 120, MSCB_GATEWAY_NODE_ID,
                         "TIMEOUT_TEST", 1000);
  gwHeartbeat.queueAlert(0x0004, POLE_STATE_RECOVERING, 30,
                         MSCB_GATEWAY_NODE_ID, "RESTORED", 1000);
  TEST_ASSERT(gwHeartbeat.getAlertQueueCount() == 3,
              "AK.8 Multiple alerts queued in Heartbeat Monitor");

  // Execute bot service with batching logic
  uint32_t simulatedTime = millis() + 4000UL;
  // Service processes all 3 into 1 consolidated digest
  if (gwHeartbeat.getAlertQueueCount() > 1) {
    char digestBuf[1024];
    int offset = snprintf(digestBuf, sizeof(digestBuf),
                          "🚨 *MSCB SMART POLE STATUS DIGEST (3 EVENTS)*\n\n");
    uint8_t popped = 0;
    while (gwHeartbeat.hasPendingAlert() && popped < 6) {
      GwAlertEvent ev;
      if (gwHeartbeat.popNextAlert(ev)) {
        offset += snprintf(digestBuf + offset, sizeof(digestBuf) - offset,
                           "• Pole 0x%04X - %s\n", ev.poleId,
                           poleHealthStateToString(ev.newState));
        popped++;
      }
    }
    gwTelegram.broadcastAdminAlert(digestBuf);
    TEST_ASSERT(popped == 3, "AK.9 All 3 concurrent events popped and "
                             "formatted into one digest message");
    TEST_ASSERT(gwHeartbeat.getAlertQueueCount() == 0,
                "AK.10 Queue drained cleanly after digest dispatch");
  }

  // -------------------------------------------------------------------------
  // SECTION AL: TELEGRAM SERVICE REGRESSION & MAIN LOOP SAFETY (P0)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AL: TELEGRAM SERVICE & MAIN LOOP REGRESSION ---"
            << std::endl;

  // AL.1 Wi-Fi disconnect and reconnect transitions
  gwWifi.setConnected(false);
  gwTelegram.service(); // Detects Wi-Fi loss, cleanly disarms
  TEST_ASSERT(!gwTelegram.isOnline(),
              "AL.1 Telegram bot marks offline cleanly when Wi-Fi disconnects");

  gwWifi.setConnected(true);
  gwTelegram.service(); // Reconnects without storm
  TEST_ASSERT(
      gwTelegram.isOnline(),
      "AL.2 Telegram bot restores online cleanly without notification storm");

  // AL.3 Admin Command Dispatch continues functioning
  int64_t adminAllowlist[] = MSCB_TELEGRAM_ADMIN_ALLOWLIST;
  int64_t adminChatId = adminAllowlist[0];
  gwTelegram.handleCommand(adminChatId, "/status");
  TEST_ASSERT(strstr(gwTelegram.getLastSentMessageText(),
                     "MSCB GATEWAY STATUS") != nullptr,
              "AL.3 /status command responds with Gateway system metrics");

  gwTelegram.handleCommand(adminChatId, "/pending");
  TEST_ASSERT(strstr(gwTelegram.getLastSentMessageText(),
                     "PENDING POLE REGISTRATIONS") != nullptr,
              "AL.4 /pending command renders pending discovery list");

  // AL.5 Quick-Reply Menu Navigation continues functioning
  gwTelegram.handleCallback(adminChatId, "MENU:POLES:0");
  TEST_ASSERT(
      strstr(gwTelegram.getLastSentMessageText(), "POLE REGISTRY") != nullptr ||
          strstr(gwTelegram.getLastSentMessageText(), "POLES") != nullptr,
      "AL.5 Pole menu callback navigation works");

  // AL.6 Final System Safety: Relays locked disabled in production
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled(),
              "AL.6 Physical relay lockout enforced (zero inadvertent mains "
              "energization)");

  // -------------------------------------------------------------------------
  // SECTION AM: R3.3 GROUP IDENTITY & PER-ADMIN DELIVERY TRUTH
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AM: GROUP IDENTITY & NOTIFICATION DELIVERY TRUTH ---"
            << std::endl;

  int64_t twoAdmins[] = {adminChatId, 777777777LL};
  gwTelegram.setTestAllowlistOverride(twoAdmins, 2);
  const int64_t groupChatId = -1001234567890LL;

  // AM.1 A legitimate admin pressing a button inside a group must be judged by
  // the SENDER id, not by the (non-allowlisted) group chat id.
  gwTelegram.clearLastSent();
  gwTelegram.handleCallback(groupChatId, "MENU:MAIN", adminChatId, true);
  TEST_ASSERT(strstr(gwTelegram.getLastSentMessageText(),
                     "MSCB SMART POLE SYSTEM") != nullptr &&
                  strstr(gwTelegram.getLastSentMessageText(), "Access Denied") ==
                      nullptr,
              "AM.1 Authorized admin callback works inside a group chat");

  // AM.2 A non-admin group member must still be rejected.
  gwTelegram.clearLastSent();
  gwTelegram.handleCallback(groupChatId, "MENU:MAIN", 123456789LL, true);
  TEST_ASSERT(strstr(gwTelegram.getLastSentMessageText(), "Access Denied") !=
                  nullptr,
              "AM.2 Non-admin group sender is rejected");

  // AM.3 One transiently failing admin does not mark the event delivered.
  gwTelegram.resetNotificationQueue();
  gwTelegram.setMockSendHook(testTelegramHook);
  g_hookTargetChat = twoAdmins[1];
  g_hookFault = 1;
  g_hookFaultPending = true;
  g_simulatedMillis = 100000UL;
  TEST_ASSERT(gwTelegram.enqueueNotification(TG_EVT_SYSTEM_ONLINE, "AM3",
                                             "AM3 audited event"),
              "AM.3a Event enqueued for two-admin delivery test");
  gwTelegram.processNotificationQueue(100000UL);
  const TgNotifyQueueItem *am3 = gwTelegram.getNotifyQueueItem(0);
  TEST_ASSERT(gwTelegram.getNotifyQueueCount() == 1 && am3 != nullptr,
              "AM.3b Event stays queued while one admin is unresolved");
  TEST_ASSERT(am3 && am3->deliveryState == TG_DELIVERY_RETRY_SCHEDULED,
              "AM.3c Unresolved admin schedules a bounded retry");

  // Retry after throttle/backoff: the already-confirmed admin is skipped.
  g_simulatedMillis = 107000UL;
  gwTelegram.processNotificationQueue(107000UL);
  TEST_ASSERT(gwTelegram.getNotifyQueueCount() == 0 &&
                  gwTelegram.getNotifyDeliveredCount() == 1,
              "AM.3d Retry delivers to the remaining admin only, then completes");

  // AM.4 Ambiguous HTTP outcome is never stuck forever: reconcile after the
  // quiet window and deliver only to the unresolved admin.
  gwTelegram.resetNotificationQueue();
  g_hookTargetChat = twoAdmins[1];
  g_hookFault = 2;
  g_hookFaultPending = true;
  g_simulatedMillis = 200000UL;
  TEST_ASSERT(gwTelegram.enqueueNotification(TG_EVT_SYSTEM_ONLINE, "AM4",
                                             "AM4 ambiguous event"),
              "AM.4a Event enqueued for ambiguous-outcome reconciliation");
  gwTelegram.processNotificationQueue(200000UL);
  const TgNotifyQueueItem *am4 = gwTelegram.getNotifyQueueItem(0);
  TEST_ASSERT(am4 && am4->deliveryState == TG_DELIVERY_UNKNOWN_AMBIGUOUS &&
                  gwTelegram.getNotifyQueueCount() == 1,
              "AM.4b Ambiguous outcome is retained, not falsely delivered");
  g_simulatedMillis = 230500UL;
  gwTelegram.processNotificationQueue(230500UL);
  TEST_ASSERT(gwTelegram.getNotifyQueueCount() == 0 &&
                  gwTelegram.getNotifyDeliveredCount() == 1,
              "AM.4c Quiet-period reconciliation completes the ambiguous event");

  // AM.5 Durable outbox: a queued alert survives a Gateway reboot, and a
  // delivered alert is removed from storage so it is not re-sent after reboot.
  gwTelegram.resetNotificationQueue();
  gwTelegram.setMockSendHook(testTelegramHook);
  g_hookFaultPending = false;
  g_simulatedMillis = 300000UL;
  TEST_ASSERT(gwTelegram.enqueueNotification(TG_EVT_GENERAL_ALERT, "AM5",
                                             "AM5 survives reboot"),
              "AM.5a Alert queued before reboot");
  gwTelegram.flushNotificationOutbox();
  gwTelegram.loadNotificationOutbox(); // reboot: RAM cleared, outbox restored
  const TgNotifyQueueItem *am5 = gwTelegram.getNotifyQueueItem(0);
  TEST_ASSERT(gwTelegram.getNotifyQueueCount() == 1 && am5 != nullptr &&
                  strcmp(am5->text, "AM5 survives reboot") == 0 &&
                  strcmp(am5->dedupKey, "AM5") == 0,
              "AM.5b Queued alert restored with text and dedup key after reboot");
  const uint32_t am5Id = am5 ? am5->eventId : 0;
  gwTelegram.processNotificationQueue(300000UL);
  gwTelegram.flushNotificationOutbox();
  gwTelegram.loadNotificationOutbox(); // second reboot after delivery
  TEST_ASSERT(gwTelegram.getNotifyQueueCount() == 0,
              "AM.5c Delivered alert is not restored after reboot");
  gwTelegram.enqueueNotification(TG_EVT_GENERAL_ALERT, "AM5B", "AM5B next");
  TEST_ASSERT(gwTelegram.getNotifyQueueItem(0) &&
                  gwTelegram.getNotifyQueueItem(0)->eventId > am5Id,
              "AM.5d Event IDs stay unique across reboot");

  gwTelegram.setMockSendHook(nullptr);
  gwTelegram.clearTestAllowlistOverride();
  gwTelegram.resetNotificationQueue();

  // -------------------------------------------------------------------------
  // R3.3 FEEDER ACK CLASSIFICATION (ON/OFF button outcome must be truthful)
  // -------------------------------------------------------------------------
  {
    GwTrackedCommand done;
    while (gwMesh.pollCompletedCommand(done)) {
    }

    // Build a PKT_CMD_FEEDER_ACK exactly as a field pole would uplink it.
    auto sendFeederAck = [&](uint8_t feederId, uint32_t seq, uint8_t state, uint8_t relayMask, uint8_t auxMask) {
      MeshWireFrame f;
      memset(&f, 0, sizeof(f));
      f.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
      f.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
      f.header.version = MSCB_PROTOCOL_VERSION;
      f.header.type = PKT_CMD_FEEDER_ACK;
      f.header.dstAddress = MSCB_GATEWAY_NODE_ID;
      f.header.srcAddress = GW_POLE_ID_START;
      f.header.nextHop = MSCB_GATEWAY_NODE_ID;
      f.header.previousHopAddress = GW_POLE_ID_START;
      f.header.maxHops = 1;
      static uint16_t ackFrameSeq = 1000; // unique per frame: duplicates are dropped
      f.header.seqNumber = ackFrameSeq++;
      f.header.payloadLen = sizeof(PktFeederAckPayload);
      PktFeederAckPayload p;
      memset(&p, 0, sizeof(p));
      p.poleId = GW_POLE_ID_START;
      p.feederId = feederId;
      p.sequenceNumber = seq;
      p.ackState = state;
      p.currentRelayMask = relayMask;
      p.currentAuxMask = auxMask;
      p.auxFeedbackValid = 1;
      p.mcbStatusValid = 1;
      memcpy(f.payload, &p, sizeof(p));
      f.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(f);
      gwMesh.processIncomingWireFrame(f, -70, 8);
    };

    // 1. Single feeder ON answered by CONTACTOR_CLOSED is a verified success.
    uint32_t seqOn = 0;
    bool sentOn = gwMesh.sendFeederCommand(GW_POLE_ID_START, 2, 1, 0, seqOn, adminChatId);
    TEST_ASSERT(sentOn && seqOn > 0, "R3.3 single-feeder ON dispatched");
    sendFeederAck(2, seqOn, FEEDER_ACK_CONTACTOR_CLOSED, 0x02, 0x02);
    bool gotOn = gwMesh.pollCompletedCommand(done);
    TEST_ASSERT(gotOn && done.seqNumber == seqOn, "R3.3 single-feeder ON completes on CONTACTOR_CLOSED ACK");
    TEST_ASSERT(gotOn && done.rejectedMask == 0, "R3.3 CONTACTOR_CLOSED is NOT counted as a rejection");
    TEST_ASSERT(gotOn && (done.outputConfirmedMask & 0x02) && (done.auxConfirmedMask & 0x02),
                "R3.3 ON success requires output AND contactor aux proof");

    // 2. ON refused by the pole safety lock is reported as rejected, never success.
    uint32_t seqLock = 0;
    bool sentLock = gwMesh.sendFeederCommand(GW_POLE_ID_START, 3, 1, 0, seqLock, adminChatId);
    TEST_ASSERT(sentLock && seqLock > 0, "R3.3 locked ON dispatched");
    sendFeederAck(3, seqLock, FEEDER_ACK_REJECTED_LOCKED, 0x00, 0x00);
    bool gotLock = gwMesh.pollCompletedCommand(done);
    TEST_ASSERT(gotLock && (done.rejectedMask & 0x04), "R3.3 LOCKED ON is reported as rejected");
    TEST_ASSERT(gotLock && (done.auxConfirmedMask & 0x04) == 0, "R3.3 LOCKED ON is not reported as verified");

    // 3. ON that energised but has no aux proof is NOT verified.
    uint32_t seqNoAux = 0;
    bool sentNoAux = gwMesh.sendFeederCommand(GW_POLE_ID_START, 4, 1, 0, seqNoAux, adminChatId);
    TEST_ASSERT(sentNoAux && seqNoAux > 0, "R3.3 no-aux ON dispatched");
    sendFeederAck(4, seqNoAux, FEEDER_ACK_FEEDBACK_NOT_CONNECTED, 0x08, 0x00);
    bool gotNoAux = gwMesh.pollCompletedCommand(done);
    TEST_ASSERT(gotNoAux && (done.auxConfirmedMask & 0x08) == 0,
                "R3.3 ON without contactor feedback is not verified");

    // 4. ALL ON where only feeder 1 closes: the command is NOT fully verified.
    uint32_t seqAll = 0;
    bool sentAll = gwMesh.sendFeederCommand(GW_POLE_ID_START, 0x00, 1, 0x3F, seqAll, adminChatId);
    TEST_ASSERT(sentAll && seqAll > 0, "R3.3 ALL ON dispatched");
    sendFeederAck(1, seqAll, FEEDER_ACK_CONTACTOR_CLOSED, 0x01, 0x01);
    for (uint8_t f = 2; f <= 6; f++) {
      sendFeederAck(f, seqAll, FEEDER_ACK_REJECTED_LOCKED, 0x00, 0x00);
    }
    bool gotAll = gwMesh.pollCompletedCommand(done);
    TEST_ASSERT(gotAll && done.seqNumber == seqAll, "R3.3 ALL ON completes after all six feeder ACKs");
    TEST_ASSERT(gotAll && done.rejectedMask == 0x3E, "R3.3 ALL ON counts five locked feeders as rejected");
    TEST_ASSERT(gotAll && (done.auxConfirmedMask & 0x3F) == 0x01,
                "R3.3 ALL ON verifies only the feeder that actually closed");
  }

  // -------------------------------------------------------------------------
  // FINAL TEST SUITE SUMMARY
  // -------------------------------------------------------------------------
  std::cout << "\n============================================================="
               "========"
            << std::endl;
  std::cout << "R3.2.2 TEST RUN COMPLETE: " << g_pass << " PASSED, " << g_fail
            << " FAILED" << std::endl;
  std::cout
      << "====================================================================="
      << std::endl;

  return (g_fail == 0) ? 0 : 1;
}
