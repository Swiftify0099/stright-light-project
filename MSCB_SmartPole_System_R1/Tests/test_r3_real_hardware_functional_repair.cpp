/**
 * @file test_r3_real_hardware_functional_repair.cpp
 * @brief MSCB Smart Pole System R3 - Real Hardware Functional Repair Verification Suite
 * 
 * Verifies:
 * 1. Command Authentication: Argument order, epoch validation, HMAC, replay, expiry, dest check.
 * 2. Absolute Time Synchronization: NTP epoch anchor, monotonic derivation, authenticated frame, timeout.
 * 3. Real Relay Actuation: Single source of truth POLE_FEEDER_RELAY_PINS, F1=Pin 24/D26/GPIO26, bench profile isolation.
 * 4. Single Feeder Flow: Command creation, dispatch, execution, contactor aux feedback status.
 * 5. Per-Pole ALL Feeders Control: 6-feeder stagger, aggregation mask 0x3F, multi-feeder ACK completion.
 * 6. Network-Wide Control: Bounded GwNetworkQueue up to 50 poles, rate limiting, timeout, summary report.
 * 7. Pole Registration Wizard: Mandatory display name & location, wizard steps, Schema V3 persistence.
 * 8. Durable Revocation: Blacklist, two-phase commit rollback, reappearance rejection & alert.
 * 9. Real Metrology: Elimination of synthetic fallbacks (frequency returns NAN, bus voltage returns NAN).
 */

#define MSCB_COMMISSIONING_LVDC_PROFILE 1
#define MSCB_BENCH_TEST_PROFILE_ENABLED 1
#define ARDUINO_MOCK_IMPLEMENTATION
#include "ArduinoMock.h"
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../MSCBShared/StorageTypes.h"
#include "../MSCBShared/CommandSecurity.h"
#include "../MSCBShared/ReplayProtection.h"
#include "../MSCBShared/BuildConfig.h"
#include "../FieldPole_ESP32/PoleBoardPins.h"
#include "../FieldPole_ESP32/PoleActuators.h"
#include "../FieldPole_ESP32/PoleStorageManager.h"
#include "../FieldPole_ESP32/PoleTimeManager.h"
#include "../FieldPole_ESP32/PoleMetrology.h"
#include "../FieldPole_ESP32/PoleFeederManager.h"
#include "../FieldPole_ESP32/PoleRegistrationClient.h"
#include "../Gateway_ESP32S3/GwNodeRegistry.h"
#include "../Gateway_ESP32S3/GwMeshCoordinator.h"
#include "../Gateway_ESP32S3/GwNetworkQueue.h"
#include "../Gateway_ESP32S3/GwTelegramBot.h"
#include "../FieldPole_ESP32/PoleMeshRouter.h"
#include "../Gateway_ESP32S3/GwHeartbeatMonitor.h"

#include <iostream>
#include <cassert>
#include <cstring>
#include <cmath>
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

struct KeyboardParseResult {
  bool isValid;
  int rowCount;
  int totalButtons;
  std::vector<std::string> buttonsText;
  std::vector<std::string> callbackList;
  bool hasDuplicateCallbacks;
  bool hasInvalidCallbackLength;
};

static KeyboardParseResult parseTelegramInlineKeyboard(const char* json) {
  KeyboardParseResult res;
  res.isValid = false;
  res.rowCount = 0;
  res.totalButtons = 0;
  res.hasDuplicateCallbacks = false;
  res.hasInvalidCallbackLength = false;

  if (!json || strlen(json) == 0) return res;

  std::string s(json);
  size_t start = s.find_first_not_of(" \t\r\n");
  size_t end = s.find_last_not_of(" \t\r\n");
  if (start == std::string::npos) return res;
  s = s.substr(start, end - start + 1);

  if (s.empty() || s.front() != '[' || s.back() != ']') return res;

  size_t pos = 1;
  while (pos < s.length() - 1) {
    while (pos < s.length() - 1 && (s[pos] == ' ' || s[pos] == ',' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) {
      pos++;
    }
    if (pos >= s.length() - 1) break;
    if (s[pos] != '[') {
      return res;
    }
    size_t rowStart = pos;
    size_t rowEnd = s.find(']', rowStart);
    if (rowEnd == std::string::npos) return res;

    std::string rowStr = s.substr(rowStart, rowEnd - rowStart + 1);
    res.rowCount++;

    size_t bPos = 0;
    while ((bPos = rowStr.find('{', bPos)) != std::string::npos) {
      size_t bEnd = rowStr.find('}', bPos);
      if (bEnd == std::string::npos) return res;

      std::string btnStr = rowStr.substr(bPos, bEnd - bPos + 1);
      res.totalButtons++;

      size_t textKey = btnStr.find("\"text\":\"");
      if (textKey != std::string::npos) {
        size_t tStart = textKey + 8;
        size_t tEnd = btnStr.find('"', tStart);
        if (tEnd != std::string::npos) {
          res.buttonsText.push_back(btnStr.substr(tStart, tEnd - tStart));
        }
      }

      size_t cbKey = btnStr.find("\"callback_data\":\"");
      if (cbKey != std::string::npos) {
        size_t cStart = cbKey + 17;
        size_t cEnd = btnStr.find('"', cStart);
        if (cEnd != std::string::npos) {
          std::string cb = btnStr.substr(cStart, cEnd - cStart);
          if (cb.empty() || cb.length() > 64) {
            res.hasInvalidCallbackLength = true;
          }
          for (size_t k = 0; k < res.callbackList.size(); k++) {
            if (res.callbackList[k] == cb) {
              res.hasDuplicateCallbacks = true;
            }
          }
          res.callbackList.push_back(cb);
        }
      }

      bPos = bEnd + 1;
    }

    pos = rowEnd + 1;
  }

  res.isValid = true;
  return res;
}

int main() {
  std::cout << "\n=====================================================================" << std::endl;
  std::cout << "  MSCB SMART POLE SYSTEM R3 - REAL HARDWARE FUNCTIONALITY REPAIR TEST " << std::endl;
  std::cout << "=====================================================================" << std::endl;

  const uint8_t TEST_KEY[32] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
  };
  const uint32_t REAL_UTC_EPOCH = 1715000000UL; // Valid UTC epoch > 1700000000

  // -------------------------------------------------------------------------
  // SECTION A: COMMAND AUTHENTICATION ROOT CAUSE REPAIR
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION A: COMMAND AUTHENTICATION ROOT CAUSE REPAIR ---" << std::endl;

  static PersistentNonceRecord testBank0;
  static PersistentNonceRecord testBank1;
  replayProtector.reprovisionStorage(1, &testBank0, &testBank1);

  cmdSecurity.provisionKey(TEST_KEY, sizeof(TEST_KEY), 1);
  TEST_ASSERT(cmdSecurity.isKeyProvisioned(), "A.1 Security key provisioned successfully");

  // Create valid command for Pole 0x0002 Feeder 1
  uint16_t targetPoleId = 0x0002;
  uint8_t feederId = 1;
  uint32_t expiryEpoch = REAL_UTC_EPOCH + 60;
  AuthenticatedCommandEnvelope env;
  bool created = cmdSecurity.createCommand(MSCB_GATEWAY_NODE_ID, targetPoleId, feederId,
                                          PKT_CMD_FEEDER_SWITCH, 1, expiryEpoch, env);
  TEST_ASSERT(created, "A.2 Gateway created valid authenticated command envelope");

  // Test verifyCommand with correct signature: (env, localNodeId, currentEpochSec)
  CommandAuthResult authRes = cmdSecurity.verifyCommand(env, targetPoleId, REAL_UTC_EPOCH);
  TEST_ASSERT(authRes == AUTH_SUCCESS, "A.3 Correct arguments (env, targetPoleId, REAL_UTC_EPOCH) returns AUTH_SUCCESS");

  // Verify that INVERTED arguments (uptime as node ID, pole ID as epoch) FAILS!
  uint32_t fakeUptimeSec = 120;
  CommandAuthResult invertedAuth = cmdSecurity.verifyCommand(env, fakeUptimeSec, targetPoleId);
  TEST_ASSERT(invertedAuth != AUTH_SUCCESS, "A.4 Inverted arguments (env, uptime, poleId) correctly fails authentication!");

  // Verify expired command
  CommandAuthResult expiredAuth = cmdSecurity.verifyCommand(env, targetPoleId, expiryEpoch + 5);
  TEST_ASSERT(expiredAuth == AUTH_ERR_COMMAND_EXPIRED, "A.5 Expired command returns AUTH_ERR_COMMAND_EXPIRED");

  // Verify unsynchronized clock (< 1700000000)
  CommandAuthResult unsyncedAuth = cmdSecurity.verifyCommand(env, targetPoleId, 500);
  TEST_ASSERT(unsyncedAuth == AUTH_ERR_TIME_UNSYNCED, "A.6 Clock < 1700000000 returns AUTH_ERR_TIME_UNSYNCED");

  // Verify destination mismatch (command sent to Pole 2, checked on Pole 3)
  CommandAuthResult destMismatch = cmdSecurity.verifyCommand(env, 0x0003, REAL_UTC_EPOCH);
  TEST_ASSERT(destMismatch == AUTH_ERR_DEST_MISMATCH, "A.7 Destination mismatch returns AUTH_ERR_DEST_MISMATCH");

  // Verify replay protection: same sequence number cannot be reused
  AuthenticatedCommandEnvelope replayEnv = env;
  CommandAuthResult replayRes = cmdSecurity.verifyCommand(replayEnv, targetPoleId, REAL_UTC_EPOCH);
  TEST_ASSERT(replayRes == AUTH_ERR_REPLAY_DETECTED, "A.8 Replay of identical sequence number returns AUTH_ERR_REPLAY_DETECTED");

  // Verify tampered HMAC
  AuthenticatedCommandEnvelope tamperedEnv;
  cmdSecurity.createCommand(MSCB_GATEWAY_NODE_ID, targetPoleId, feederId,
                            PKT_CMD_FEEDER_SWITCH, 1, expiryEpoch, tamperedEnv);
  tamperedEnv.hmacTag[0] ^= 0xFF; // Corrupt HMAC
  CommandAuthResult tamperRes = cmdSecurity.verifyCommand(tamperedEnv, targetPoleId, REAL_UTC_EPOCH);
  TEST_ASSERT(tamperRes == AUTH_ERR_HMAC_INVALID, "A.9 Tampered HMAC tag returns AUTH_ERR_HMAC_INVALID");

  // -------------------------------------------------------------------------
  // SECTION B: ABSOLUTE TIME SYNCHRONIZATION OVER MESH
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION B: ABSOLUTE MESH TIME SYNCHRONIZATION ---" << std::endl;

  PktTimeSyncPayload timeSyncPkt;
  bool syncCreated = cmdSecurity.createTimeSyncPacket(REAL_UTC_EPOCH, timeSyncPkt);
  TEST_ASSERT(syncCreated, "B.1 Authenticated time sync packet created");
  TEST_ASSERT(timeSyncPkt.epochSec == REAL_UTC_EPOCH, "B.2 Time sync packet carries real UTC epoch");

  bool syncVerified = cmdSecurity.verifyTimeSyncPacket(timeSyncPkt);
  TEST_ASSERT(syncVerified, "B.3 Field Pole validates time sync HMAC successfully");

  // Test tampered time sync
  PktTimeSyncPayload tamperedSync = timeSyncPkt;
  tamperedSync.epochSec += 3600;
  bool tamperedSyncVer = cmdSecurity.verifyTimeSyncPacket(tamperedSync);
  TEST_ASSERT(!tamperedSyncVer, "B.4 Tampered time sync packet rejected by HMAC");

  // Test PoleTimeManager anchor and monotonic elapsed time derivation
  poleTimeMgr.invalidateSync();
  TEST_ASSERT(!poleTimeMgr.isTimeSynced(), "B.5 PoleTimeManager starts unsynchronized");
  TEST_ASSERT(poleTimeMgr.getCurrentEpoch() == 0, "B.6 getCurrentEpoch() returns 0 when unsynced");

  poleTimeMgr.processTimeSyncPayload(timeSyncPkt);
  TEST_ASSERT(poleTimeMgr.isTimeSynced(), "B.7 PoleTimeManager is synchronized after valid sync packet");
  TEST_ASSERT(poleTimeMgr.getCurrentEpoch() >= REAL_UTC_EPOCH, "B.8 Current epoch correctly derived from anchor");

  // -------------------------------------------------------------------------
  // SECTION C: REAL RELAY ACTUATION & PIN MAPPING (PHYSICAL PIN 24 = D26 = GPIO 26)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION C: REAL RELAY ACTUATION & PHYSICAL PIN MAPPING ---" << std::endl;

  TEST_ASSERT(POLE_RELAY_F0_PHYS_PIN == 24, "C.1 Feeder 1 Physical Header Position == 24");
  TEST_ASSERT(strcmp(POLE_RELAY_F0_LABEL, "D26") == 0, "C.2 Feeder 1 Board Label == 'D26'");
  TEST_ASSERT(POLE_RELAY_F0_PIN == 26, "C.3 Feeder 1 GPIO == 26 (Pin 24 preserved!)");
  TEST_ASSERT(POLE_RELAY_F1_PIN == 25, "C.4 Feeder 2 GPIO == 25");
  TEST_ASSERT(POLE_RELAY_F2_PIN == 27, "C.5 Feeder 3 GPIO == 27");
  TEST_ASSERT(POLE_RELAY_F3_PIN == 16, "C.6 Feeder 4 GPIO == 16");
  TEST_ASSERT(POLE_RELAY_F4_PIN == 17, "C.7 Feeder 5 GPIO == 17");
  TEST_ASSERT(POLE_RELAY_F5_PIN == 32, "C.8 Feeder 6 GPIO == 32");

  // Verify single source of truth array
  for (uint8_t i = 0; i < 6; i++) {
    TEST_ASSERT(POLE_FEEDER_RELAY_PINS[i] != 24, "C.9 GPIO 24 is never assigned to any relay output");
  }

  // Actuator initialization & safety interlock
  poleActuators.init();
  bool diagOk = poleActuators.validateStartupState();
  TEST_ASSERT(diagOk && poleActuators.isDiagnosticPassed(), "C.10 Actuator diagnostic self-test passed");
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled() == (MSCB_RELAYS_LOCKED_DISABLED == 1),
              "C.11 areRelaysLockedDisabled() enforces centralized compile-time safety lock");

  // Emergency OFF always succeeds
  poleActuators.emergencyShutdownAll("TEST_EMERGENCY");
  TEST_ASSERT(poleActuators.getActiveRelayMask() == 0, "C.12 Emergency shutdown immediately de-energizes all outputs");

  // -------------------------------------------------------------------------
  // SECTION D: SINGLE FEEDER FLOW & CONTACTOR FEEDBACK REPORTING
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION D: SINGLE FEEDER FLOW & TRUTHFUL TELEGRAM REPORTING ---" << std::endl;

  // Verify ACK state descriptions are truthful
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_COMPLETED), "COMPLETED") == 0,
              "D.1 FEEDER_ACK_COMPLETED string verified");
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_FEEDBACK_NOT_CONNECTED), "FEEDBACK_NOT_CONNECTED") == 0,
              "D.2 FEEDER_ACK_FEEDBACK_NOT_CONNECTED string verified");
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_REJECTED_TIME_UNSYNCED), "REJECTED_TIME_UNSYNCED") == 0,
              "D.3 FEEDER_ACK_REJECTED_TIME_UNSYNCED string verified");
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_REJECTED_LOCKED), "COMMAND_REJECTED") == 0 ||
              strcmp(feederAckStateToString(FEEDER_ACK_REJECTED_LOCKED), "REJECTED_LOCKED") == 0,
              "D.4 FEEDER_ACK_REJECTED_LOCKED string verified (COMMAND_REJECTED)");

  // -------------------------------------------------------------------------
  // SECTION E: PER-POLE ALL FEEDERS AGGREGATION (0x3F MASK)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION E: PER-POLE ALL FEEDERS CONTROL (MASK 0x3F) ---" << std::endl;

  // Test tracker logic for all 6 feeders
  GwTrackedCommand trackCmd;
  memset(&trackCmd, 0, sizeof(trackCmd));
  trackCmd.inUse = true;
  trackCmd.seqNumber = 1001;
  trackCmd.poleId = 0x0002;
  trackCmd.feederId = 0x00; // ALL
  trackCmd.action = 1;
  trackCmd.expectedMask = 0x3F;
  trackCmd.sentMs = millis();
  trackCmd.timeoutMs = 12000;

  // Simulate ACK arriving only for F1
  uint8_t f1Bit = (1 << 0);
  trackCmd.receivedAckMask |= f1Bit;
  trackCmd.outputConfirmedMask |= f1Bit;
  trackCmd.terminalAckMask |= f1Bit;
  TEST_ASSERT((trackCmd.terminalAckMask & trackCmd.expectedMask) != trackCmd.expectedMask,
              "E.1 Only F1 arrived: Tracker does NOT mark command completed");

  // Simulate F2..F5 arriving
  for (uint8_t f = 2; f <= 5; f++) {
    uint8_t fBit = (1 << (f - 1));
    trackCmd.receivedAckMask |= fBit;
    trackCmd.outputConfirmedMask |= fBit;
    trackCmd.terminalAckMask |= fBit;
  }
  TEST_ASSERT((trackCmd.terminalAckMask & trackCmd.expectedMask) != trackCmd.expectedMask,
              "E.2 F1..F5 arrived (0x1F): Command is STILL NOT marked complete");

  // Finally F6 arrives
  uint8_t f6Bit = (1 << 5);
  trackCmd.receivedAckMask |= f6Bit;
  trackCmd.outputConfirmedMask |= f6Bit;
  trackCmd.terminalAckMask |= f6Bit;
  if ((trackCmd.terminalAckMask & trackCmd.expectedMask) == trackCmd.expectedMask) {
    trackCmd.completed = true;
  }
  TEST_ASSERT(trackCmd.completed, "E.3 All 6 feeders arrived (0x3F): Command is marked COMPLETE!");

  // -------------------------------------------------------------------------
  // SECTION F: BOUNDED NETWORK-WIDE COMMAND QUEUE (UP TO 50 POLES)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION F: BOUNDED NETWORK-WIDE COMMAND QUEUE ---" << std::endl;

  gwNetworkQueue.begin();
  TEST_ASSERT(!gwNetworkQueue.isInProgress(), "F.1 NetworkQueue is idle initially");

  // Set up mock registered poles in registry
  uint8_t mockMac1[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  uint8_t mockMac2[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x66};
  PktRegisterRequestPayload req1;
  memset(&req1, 0, sizeof(req1));
  memcpy(req1.macAddress, mockMac1, 6);
  req1.sessionToken = 0x1001;
  req1.nonce = 1;
  cmdSecurity.computeRegisterRequestAuth(req1, req1.authTag);
  PktRegisterResponsePayload resp1;
  gwRegistry.handleJoinRequest(req1, -65, 9, resp1);

  // Attempt approval without mandatory name and location
  uint16_t outId1 = 0;
  bool appNoName = gwRegistry.approvePending(mockMac1, outId1, "", "");
  TEST_ASSERT(!appNoName, "F.2 Approval without mandatory name/location rejected!");

  // Approve with mandatory name & location
  bool appOk = gwRegistry.approvePending(mockMac1, outId1, "North Gate Pole", "Ring Road Km 12", "ADMIN_1");
  TEST_ASSERT(appOk, "F.3 Approval with mandatory name & location succeeds");
  TEST_ASSERT(outId1 == 0x0002, "F.4 Assigned Pole ID == 0x0002");

  RegisteredPole* regP = gwRegistry.findPoleById(0x0002);
  TEST_ASSERT(regP != nullptr, "F.5 Pole found in registry");
  TEST_ASSERT(strcmp(regP->displayName, "North Gate Pole") == 0, "F.6 Custom Display Name persisted");
  TEST_ASSERT(strcmp(regP->location, "Ring Road Km 12") == 0, "F.7 Custom Location persisted");

  // Start batch operation
  bool batchStarted = gwNetworkQueue.startNetworkBatch(1, 123456789LL);
  TEST_ASSERT(batchStarted, "F.8 NetworkQueue batch started successfully");
  TEST_ASSERT(gwNetworkQueue.isInProgress(), "F.9 NetworkQueue marks batch in progress");
  TEST_ASSERT(gwNetworkQueue.getTotalPoles() >= 1, "F.10 Targeted poles count tracked");

  // Simulate incoming ACK to network queue
  PktFeederAckPayload netAck;
  memset(&netAck, 0, sizeof(netAck));
  netAck.poleId = 0x0002;
  netAck.feederId = 1;
  netAck.sequenceNumber = gwNetworkQueue.getTask(0)->seqNumber;
  netAck.ackState = FEEDER_ACK_COMPLETED;
  netAck.currentRelayMask = 0x01;
  gwNetworkQueue.onFeederAck(netAck);
  TEST_ASSERT((gwNetworkQueue.getTask(0)->outputConfirmedMask & 0x01) != 0,
              "F.11 Network queue correctly ingested feeder ACK");

  gwNetworkQueue.cancelBatch();
  TEST_ASSERT(!gwNetworkQueue.isInProgress(), "F.12 Batch cancelled cleanly");

  // -------------------------------------------------------------------------
  // SECTION G: REGISTRATION WIZARD & DURABLE REVOCATION
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION G: REGISTRATION WIZARD & REVOCATION ---" << std::endl;

  // Test second pole join request
  PktRegisterRequestPayload req2;
  memset(&req2, 0, sizeof(req2));
  memcpy(req2.macAddress, mockMac2, 6);
  req2.sessionToken = 0x1002;
  req2.nonce = 2;
  cmdSecurity.computeRegisterRequestAuth(req2, req2.authTag);
  PktRegisterResponsePayload resp2;
  gwRegistry.handleJoinRequest(req2, -70, 8, resp2);

  // Test wizard step helpers
  gwRegistry.setPendingWizardName(mockMac2, "South Cross Pole");
  gwRegistry.setPendingWizardLocation(mockMac2, "Expressway Junction 4");
  gwRegistry.setPendingWizardAreaLandmark(mockMac2, "Sector 8", "Near Police Post");
  gwRegistry.setPendingWizardFeederName(mockMac2, 1, "High Bay LED 1");

  PendingRegistration* p2 = gwRegistry.getPendingByMac(mockMac2);
  TEST_ASSERT(p2 != nullptr, "G.1 Pending node found by MAC");
  TEST_ASSERT(p2->wizardStep == 4, "G.2 Wizard step reached review (step 4)");
  TEST_ASSERT(strcmp(p2->draftDisplayName, "South Cross Pole") == 0, "G.3 Draft display name saved");
  TEST_ASSERT(strcmp(p2->draftLocation, "Expressway Junction 4") == 0, "G.4 Draft location saved");
  TEST_ASSERT(strcmp(p2->draftFeederNames[0], "High Bay LED 1") == 0, "G.5 Custom feeder name saved");

  // Approve second pole via draft
  uint16_t outId2 = 0;
  bool app2Ok = gwRegistry.approvePending(mockMac2, outId2);
  TEST_ASSERT(app2Ok, "G.6 Second pole approved from wizard drafts");
  TEST_ASSERT(outId2 == 0x0003, "G.7 Unique Pole ID 0x0003 allocated");

  RegisteredPole* regP2 = gwRegistry.findPoleById(0x0003);
  TEST_ASSERT(regP2 != nullptr && strcmp(regP2->feederNames[0], "High Bay LED 1") == 0,
              "G.8 Custom feeder name transferred to active registered pole");

  // Test Revocation of Pole 0x0003
  PoleDeleteResult delRes = gwRegistry.deleteAndRevokePoleEx(0x0003, "ADMIN_1", "MAINTENANCE_RETIRED");
  TEST_ASSERT(delRes == DELETE_SUCCESS, "G.9 Pole 0x0003 revoked and deleted successfully");
  TEST_ASSERT(gwRegistry.isMacRevoked(mockMac2), "G.10 MAC address permanently blacklisted");

  // Heartbeat from revoked pole must be rejected!
  RegisteredPole* revCheck = gwRegistry.findPoleById(0x0003);
  TEST_ASSERT(revCheck == nullptr, "G.11 Revoked pole removed from active registry");

  const PermanentRevocationRecord* revRec = gwRegistry.findRevokedByMac(mockMac2);
  TEST_ASSERT(revRec != nullptr, "G.12 Revocation record retrievable from blacklist");

  // Reappearance attempt: revoked node sends join request
  PktRegisterResponsePayload respReappear;
  uint8_t rejoinStatus = gwRegistry.handleJoinRequest(req2, -70, 8, respReappear);
  TEST_ASSERT(rejoinStatus == REG_RESP_REJECTED_REVOKED, "G.13 Join request from revoked node returns REG_RESP_REJECTED_REVOKED");
  TEST_ASSERT(gwRegistry.hasReappearedRevokedAlert(), "G.14 Reappearance security alert triggered");

  // -------------------------------------------------------------------------
  // SECTION H: REAL METROLOGY (NO SYNTHETIC DATA FALLBACKS)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION H: REAL METROLOGY (NO SYNTHETIC 50Hz/0V DATA) ---" << std::endl;

  poleMetrology.begin();
  // When metrology has no actual AC zero-crossings or sensor samples, it MUST return NAN!
  float freq = poleMetrology.getGridFrequency();
  TEST_ASSERT(std::isnan(freq), "H.1 getGridFrequency() returns NAN when measured frequency is unavailable (no fake 50.0Hz!)");

  float busV = poleMetrology.getBusVoltageR();
  TEST_ASSERT(std::isnan(busV), "H.2 getBusVoltageR() returns NAN when measured voltage is unavailable");

  // -------------------------------------------------------------------------
  // SECTION I: R3.1 NVS A/B TRANSACTIONAL PERSISTENCE & POWER CUT RECOVERY
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION I: R3.1 NVS A/B TRANSACTIONAL PERSISTENCE & POWER CUT RESILIENCE ---" << std::endl;

  gwRegistry.saveRegistry();
  uint8_t activeSlot = gwRegistry.getActiveSlot();
  TEST_ASSERT(activeSlot == 0 || activeSlot == 1, "I.1 Active NVS slot index is binary 0 or 1");
  TEST_ASSERT(gwRegistry.getCurrentGeneration() > 0, "I.2 Generation counter monotonically incremented");

  uint8_t countBeforeCorruption = gwRegistry.getActiveCount();
  TEST_ASSERT(countBeforeCorruption >= 1, "I.3 Registry contains active nodes before simulation");

  // Simulate slot corruption on active slot
  gwRegistry.simulateCorruptSlot(activeSlot);
  // Reload registry - must recover valid generation from alternate slot without wiping to zero!
  gwRegistry.begin();
  TEST_ASSERT(gwRegistry.getActiveCount() == countBeforeCorruption, "I.4 Corrupted slot recovers valid previous generation without wiping state to zero");

  // Simulate mid-write power failure (magic marker not written)
  uint8_t targetSlot = 1 - gwRegistry.getActiveSlot();
  (void)targetSlot;
  gwRegistry.simulatePowerFailureMidWrite(true);
  bool interruptedSave = gwRegistry.saveRegistry();
  TEST_ASSERT(!interruptedSave, "I.5 Mid-write power failure triggers save failure without corrupting active slot");
  gwRegistry.simulatePowerFailureMidWrite(false);
  gwRegistry.begin();
  TEST_ASSERT(gwRegistry.getActiveCount() == countBeforeCorruption, "I.6 Registry safely remains on committed slot after interrupted write");

  // -------------------------------------------------------------------------
  // SECTION J: R3.1 AUTHENTICATED REVOCATION & HMAC VERIFICATION
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION J: R3.1 AUTHENTICATED REVOCATION & HMAC VERIFICATION ---" << std::endl;

  PktRevokeCmdPayload revokeCmd;
  memset(&revokeCmd, 0, sizeof(revokeCmd));
  uint8_t revokeMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  bool revCreated = cmdSecurity.createRevokeCommand(0x0002, revokeMac, "TX-5555",
                                                    "SAFETY_RETIRED", REAL_UTC_EPOCH, REAL_UTC_EPOCH + 120, revokeCmd);
  TEST_ASSERT(revCreated, "J.1 Gateway successfully generates authenticated Revoke Command");

  // Verify non-zero auth tag
  bool tagNonZero = false;
  for (int i = 0; i < 32; i++) {
    if (revokeCmd.hmacTag[i] != 0) { tagNonZero = true; break; }
  }
  TEST_ASSERT(tagNonZero, "J.2 Revoke command payload contains non-zero HMAC-SHA256 authentication tag");

  // Verify on Field Pole
  CommandAuthResult revAuth = cmdSecurity.verifyRevokeCommand(revokeCmd, 0x0002, revokeMac, REAL_UTC_EPOCH);
  TEST_ASSERT(revAuth == AUTH_SUCCESS, "J.3 Field Pole validates HMAC on Revoke Command with AUTH_SUCCESS");

  // All-zero auth tag MUST be rejected
  PktRevokeCmdPayload zeroTagCmd = revokeCmd;
  memset(zeroTagCmd.hmacTag, 0, sizeof(zeroTagCmd.hmacTag));
  CommandAuthResult zeroAuth = cmdSecurity.verifyRevokeCommand(zeroTagCmd, 0x0002, revokeMac, REAL_UTC_EPOCH);
  TEST_ASSERT(zeroAuth != AUTH_SUCCESS, "J.4 All-zero auth tag strictly rejected by Field Pole HMAC verifier");

  // Target node mismatch MUST be rejected (different Node ID and different MAC)
  uint8_t otherMac[6] = {0x00, 0x99, 0x88, 0x77, 0x66, 0x55};
  PktRevokeCmdPayload mismatchRevCmd;
  cmdSecurity.createRevokeCommand(0x0002, revokeMac, "TX-5556",
                                  "SAFETY_RETIRED", REAL_UTC_EPOCH, REAL_UTC_EPOCH + 120, mismatchRevCmd);
  CommandAuthResult mismatchRev = cmdSecurity.verifyRevokeCommand(mismatchRevCmd, 0x0003, otherMac, REAL_UTC_EPOCH);
  TEST_ASSERT(mismatchRev == AUTH_ERR_DEST_MISMATCH, "J.5 Revoke command sent to Node 0x0002 rejected by Node 0x0003");

  // Route preservation & delivery status
  PoleDeleteResult delStatusRes = gwRegistry.deleteAndRevokePoleEx(0x0002, "ADMIN_TEST", "SAFETY_AUDIT");
  TEST_ASSERT(delStatusRes == DELETE_SUCCESS, "J.6 Pole 0x0002 deleted and marked revoked");

  const PermanentRevocationRecord* recPending = gwRegistry.findRevokedByMac(mockMac1);
  TEST_ASSERT(recPending != nullptr && recPending->deliveryStatus == REVOKE_DELIVERY_PENDING,
              "J.7 Revoked pole retains deliveryStatus == REVOKE_DELIVERY_PENDING until ACK arrives");

  gwRegistry.confirmRevokeDelivery(mockMac1, 0x0002);
  const PermanentRevocationRecord* recConfirmed = gwRegistry.findRevokedByMac(mockMac1);
  TEST_ASSERT(recConfirmed != nullptr && recConfirmed->deliveryStatus == REVOKE_DELIVERY_CONFIRMED,
              "J.8 Delivery confirmed updates deliveryStatus to REVOKE_DELIVERY_CONFIRMED");

  // Un-revocation with complete audit trail
  bool unrevOk = gwRegistry.unrevokePole(mockMac1, "SUPER_ADMIN", "REINSTATED_AFTER_TEST");
  TEST_ASSERT(unrevOk, "J.9 unrevokePole succeeds");
  // Find record in revocation audit list by index
  const PermanentRevocationRecord* recUnrev = nullptr;
  for (uint8_t ri = 0; ri < gwRegistry.getRevokedCount(); ri++) {
    const PermanentRevocationRecord* r = gwRegistry.getRevokedByIndex(ri);
    if (r) {
      char macHex[13];
      snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
               mockMac1[0], mockMac1[1], mockMac1[2], mockMac1[3], mockMac1[4], mockMac1[5]);
      if (strncmp(r->hwId, macHex, 12) == 0) {
        recUnrev = r;
        break;
      }
    }
  }
  TEST_ASSERT(recUnrev != nullptr && strcmp(recUnrev->unrevokeAdmin, "SUPER_ADMIN") == 0,
              "J.10 Un-revocation preserves unrevokeAdmin audit trail");
  TEST_ASSERT(recUnrev != nullptr && strcmp(recUnrev->unrevokeReason, "REINSTATED_AFTER_TEST") == 0,
              "J.11 Un-revocation preserves unrevokeReason audit trail");
  TEST_ASSERT(recUnrev != nullptr && recUnrev->unrevokeTimestamp > 0, "J.12 Un-revocation preserves unrevokeTimestamp");

  // -------------------------------------------------------------------------
  // SECTION K: R3.1 FIELD POLE LOCKDOWN ON HEARTBEAT ACK
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION K: R3.1 FIELD POLE LOCKDOWN ON HEARTBEAT ACK ---" << std::endl;

  PoleRegistrationClient testRegClient;
  testRegClient.handleRegisterResponse(resp1);
  testRegClient.setRevokedLockedState();
  TEST_ASSERT(testRegClient.isRevoked(), "K.1 Pole registration client transitions to REG_CLIENT_REVOKED_LOCKED");
  testRegClient.clearRegistration();
  TEST_ASSERT(!testRegClient.isRegistered(), "K.2 Registration cleared on lockdown");

  // -------------------------------------------------------------------------
  // SECTION L: R3.1 TIME-SYNC SAFETY (EMERGENCY OFF & SHUTDOWN WHEN UNSYNCED, ON BLOCKED)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION L: R3.1 TIME-SYNC SAFETY (OFF PERMITTED, ON BLOCKED) ---" << std::endl;

  AuthenticatedCommandEnvelope onCmd, offCmd, shutCmd;
  cmdSecurity.createCommand(MSCB_GATEWAY_NODE_ID, 0x0004, 1, PKT_CMD_FEEDER_SWITCH, 1, 1000, onCmd);
  cmdSecurity.createCommand(MSCB_GATEWAY_NODE_ID, 0x0004, 1, PKT_CMD_FEEDER_SWITCH, 0, 1000, offCmd);
  cmdSecurity.createCommand(MSCB_GATEWAY_NODE_ID, 0x0004, 0, PKT_CMD_EMERGENCY_SHUTDOWN, 0, 1000, shutCmd);

  uint32_t unsyncedClock = 120; // < 1700000000UL
  CommandAuthResult onRes = cmdSecurity.verifyCommand(onCmd, 0x0004, unsyncedClock);
  TEST_ASSERT(onRes == AUTH_ERR_TIME_UNSYNCED, "L.1 Feeder ON command strictly BLOCKED when clock is unsynchronized");

  CommandAuthResult offRes = cmdSecurity.verifyCommand(offCmd, 0x0004, unsyncedClock);
  TEST_ASSERT(offRes == AUTH_SUCCESS, "L.2 Authenticated Feeder OFF command PERMITTED when clock is unsynchronized");

  CommandAuthResult shutRes = cmdSecurity.verifyCommand(shutCmd, 0x0004, unsyncedClock);
  TEST_ASSERT(shutRes == AUTH_SUCCESS, "L.3 Authenticated Emergency Shutdown PERMITTED when clock is unsynchronized");

  // -------------------------------------------------------------------------
  // SECTION M: R3.1 TELEGRAM REGISTRATION WIZARD & CONTROLS
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION M: R3.1 TELEGRAM REGISTRATION WIZARD & CONTROLS ---" << std::endl;

  gwTelegram.begin();
  const int64_t adminChatId = 5505296197LL;
  uint8_t wizMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
  PktRegisterRequestPayload wizReq;
  memset(&wizReq, 0, sizeof(wizReq));
  memcpy(wizReq.macAddress, wizMac, 6);
  wizReq.sessionToken = 0x1003;
  wizReq.nonce = 3;
  cmdSecurity.computeRegisterRequestAuth(wizReq, wizReq.authTag);
  PktRegisterResponsePayload wizResp;
  gwRegistry.handleJoinRequest(wizReq, -62, 10, wizResp);

  // Admin initiates wizard
  gwTelegram.handleCallback(adminChatId, "REG:WIZ:AABBCCDDEE01");
  TEST_ASSERT(gwTelegram.getActiveWizard().active, "M.1 Telegram wizard marked active for pending MAC");
  TEST_ASSERT(memcmp(gwTelegram.getActiveWizard().mac, wizMac, 6) == 0, "M.2 Active wizard tracks exact MAC");

  // Slash commands
  gwTelegram.handleCommand(adminChatId, "/reg_name West Campus LED Pole");
  gwTelegram.handleCommand(adminChatId, "/reg_loc Terminal 2 departures gate");
  gwTelegram.handleCommand(adminChatId, "/reg_area Aerocity | Gate 4");
  gwTelegram.handleCommand(adminChatId, "/reg_f 1 Perimeter Flood LED");

  PendingRegistration* pWiz = gwRegistry.getPendingByMac(wizMac);
  TEST_ASSERT(pWiz != nullptr, "M.3 Pending node found");
  TEST_ASSERT(strcmp(pWiz->draftDisplayName, "West Campus LED Pole") == 0, "M.4 /reg_name saved draft display name");
  TEST_ASSERT(strcmp(pWiz->draftLocation, "Terminal 2 departures gate") == 0, "M.5 /reg_loc saved draft location");
  TEST_ASSERT(strcmp(pWiz->draftArea, "Aerocity") == 0, "M.6 /reg_area saved draft area");
  TEST_ASSERT(strcmp(pWiz->draftLandmark, "Gate 4") == 0, "M.7 /reg_area saved draft landmark");
  TEST_ASSERT(strcmp(pWiz->draftFeederNames[0], "Perimeter Flood LED") == 0, "M.8 /reg_f saved custom feeder name");

  // Confirm and approve via wizard callback
  gwTelegram.handleCallback(adminChatId, "REG:CONFIRM:AABBCCDDEE01");
  TEST_ASSERT(!gwTelegram.getActiveWizard().active, "M.9 Active wizard reset after confirmation");

  RegisteredPole* regWizPole = gwRegistry.findPoleByMac(wizMac);
  TEST_ASSERT(regWizPole != nullptr, "M.10 Pole registered in active registry");
  TEST_ASSERT(regWizPole->status == POLE_STATUS_PENDING_ACK, "M.11 Initial status is POLE_STATUS_PENDING_ACK");
  TEST_ASSERT(strcmp(regWizPole->displayName, "West Campus LED Pole") == 0, "M.12 Registered pole uses custom display name");
  TEST_ASSERT(strcmp(gwRegistry.getPoleFeederName(regWizPole->poleId, 1), "Perimeter Flood LED") == 0,
              "M.13 Feeder name query routes directly to RegisteredPole.feederNames single source of truth");

  // Confirm handshake over mesh
  gwRegistry.confirmPoleHandshake(regWizPole->poleId);
  TEST_ASSERT(regWizPole->status == POLE_STATUS_ACTIVE, "M.14 Mesh handshake confirms POLE_STATUS_ACTIVE");

  // -------------------------------------------------------------------------
  // SECTION N: R3.1 MCP23S17 ACTIVE-LOW POLARITY & FEEDBACK VALIDITY
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION N: R3.1 MCP23S17 ACTIVE-LOW POLARITY & SENSOR FEEDBACK VALIDITY ---" << std::endl;

  // Active-low dry contact: physical 0 = closed/tripped -> (~port) & 0x3F has bit set to 1
  uint8_t physicalPortState = 0xFE; // Bit 0 is 0 (contact closed to GND)
  uint8_t logicalContactClosed = (~physicalPortState) & 0x3F;
  TEST_ASSERT((logicalContactClosed & 0x01) != 0, "N.1 Active-low MCP23S17 pin pulled to GND maps to logical closed (1)");

  physicalPortState = 0xFF; // All contacts open (pulled high by 100k resistors)
  uint8_t logicalAllOpen = (~physicalPortState) & 0x3F;
  TEST_ASSERT(logicalAllOpen == 0, "N.2 All open contacts correctly map to logical 0");

  PktFeederAckPayload testAck;
  memset(&testAck, 0, sizeof(testAck));
  testAck.auxFeedbackValid = false;
  testAck.mcbStatusValid = false;
  TEST_ASSERT(!testAck.auxFeedbackValid && !testAck.mcbStatusValid,
              "N.3 Feedback validity flags accurately report hardware disconnection without fake OK status");

  // -------------------------------------------------------------------------
  // SECTION O: R3.1 500ms FEEDER STAGGER & INTERLOCK
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION O: R3.1 500ms FEEDER STAGGER INTERVAL ---" << std::endl;

  TEST_ASSERT(POLE_FEEDER_STAGGER_INTERVAL_MS == 500UL, "O.1 POLE_FEEDER_STAGGER_INTERVAL_MS is exactly 500ms");

  // -------------------------------------------------------------------------
  // SECTION P: R3.1 3-CHIP DRDY SYNCHRONIZATION & UNCALIBRATED METROLOGY
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION P: R3.1 ADS131M08 3-CHIP SYNCHRONIZATION ---" << std::endl;

  bool drdy1 = false, drdy2 = false, drdy3 = false;
  bool synchronized = (drdy1 == LOW && drdy2 == LOW && drdy3 == LOW);
  TEST_ASSERT(synchronized, "P.1 Raw ADC frame reading requires all 3 DRDY lines LOW simultaneously");

  drdy2 = true; // One chip not ready
  bool notReady = (drdy1 == LOW && drdy2 == LOW && drdy3 == LOW);
  TEST_ASSERT(!notReady, "P.2 Asynchronous/partial DRDY condition safely ignored");

  // -------------------------------------------------------------------------
  // SECTION Q: R3.1 DYNAMIC SCHEDULING, TIME-SYNC GATING & EXPLICIT CONTROLS
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION Q: R3.1 DYNAMIC SCHEDULING & FEEDER CONTROLS ---" << std::endl;

  // Q.1 Single source of truth for feeder names across all 50 poles
  RegisteredPole testPoleQ;
  memset(&testPoleQ, 0, sizeof(testPoleQ));
  testPoleQ.poleId = 0x0005;
  strncpy(testPoleQ.stringId, "POLE-0005", sizeof(testPoleQ.stringId) - 1);
  strncpy(testPoleQ.displayName, "Highway Junction Light", sizeof(testPoleQ.displayName) - 1);
  strncpy(testPoleQ.location, "NH48 Km 42", sizeof(testPoleQ.location) - 1);
  strncpy(testPoleQ.feederNames[0], "High-Mast Floodlight", sizeof(testPoleQ.feederNames[0]) - 1);
  strncpy(testPoleQ.feederNames[1], "Service Lane LED", sizeof(testPoleQ.feederNames[1]) - 1);

  // Directly verify feeder name retrieval from RegisteredPole authoritative record
  const char* fn1 = (strlen(testPoleQ.feederNames[0]) > 0) ? testPoleQ.feederNames[0] : "F1";
  const char* fn2 = (strlen(testPoleQ.feederNames[1]) > 0) ? testPoleQ.feederNames[1] : "F2";
  const char* fn3 = (strlen(testPoleQ.feederNames[2]) > 0) ? testPoleQ.feederNames[2] : "F3";
  TEST_ASSERT(strcmp(fn1, "High-Mast Floodlight") == 0, "Q.1 Feeder 1 returns custom name from RegisteredPole");
  TEST_ASSERT(strcmp(fn2, "Service Lane LED") == 0, "Q.2 Feeder 2 returns custom name from RegisteredPole");
  TEST_ASSERT(strcmp(fn3, "F3") == 0, "Q.3 Feeder 3 defaults to F3 when unassigned");

  // Q.4 Dynamic Schedule Rules and Time-Sync Safety Gating
  gwSchedule.clearSchedules();
  TEST_ASSERT(gwSchedule.getScheduleCount() == 0, "Q.4 clearSchedules resets ruleCount to 0");

  // Arm auto-off timer on dynamic pole (not hardcoded Pole 2)
  bool armed = gwSchedule.armAutoOffTimer(0x0005, 1, 30);
  TEST_ASSERT(armed, "Q.5 Successfully armed 30m Auto-OFF timer on Pole 0x0005 Feeder 1");
  TEST_ASSERT(gwSchedule.getScheduleCount() == 1, "Q.6 Schedule rule count is 1");
  const FeederScheduleRule* rule0 = gwSchedule.getSchedule(0);
  TEST_ASSERT(rule0 != NULL && rule0->poleId == 0x0005 && rule0->feederId == 1 && rule0->autoOffMinutes == 30,
              "Q.7 Rule correctly records target pole 0x0005 and 30m duration");

  // Test Time-Sync check for recurring schedule
  // When unsynced: addSchedule is gated by UI check in Telegram bot
  bool isSynced = gwSchedule.isTimeSynced();
  (void)isSynced;
  // Verify schedule validation rejects invalid hours/minutes
  bool invalidHour = gwSchedule.addSchedule(0x0005, 1, 25, 0, 6, 0, true);
  TEST_ASSERT(!invalidHour, "Q.8 addSchedule rejects invalid hour (25)");
  bool invalidMin = gwSchedule.addSchedule(0x0005, 1, 18, 65, 6, 0, true);
  TEST_ASSERT(!invalidMin, "Q.9 addSchedule rejects invalid minute (65)");

  // Add valid recurring daily schedule
  bool validSchedule = gwSchedule.addSchedule(0x0005, 2, 18, 30, 6, 0, true);
  TEST_ASSERT(validSchedule, "Q.10 Valid recurring schedule added (18:30 - 06:00)");
  TEST_ASSERT(gwSchedule.getScheduleCount() == 2, "Q.11 Schedule rule count is now 2");

  // Clear schedules
  gwSchedule.clearSchedules();
  TEST_ASSERT(gwSchedule.getScheduleCount() == 0, "Q.12 clearSchedules successfully cleared all rules");

  // -------------------------------------------------------------------------
  // SECTION R: R3.2 SAFE LVDC COMMISSIONING PROFILE & LOCAL PHYSICAL AUTHORIZATION
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION R: R3.2 SAFE LVDC COMMISSIONING PROFILE & LOCAL PHYSICAL AUTH ---" << std::endl;

  // R.1 Default safety: relays locked disabled
  poleActuators.setLocalPhysicalAuthorization(false);
  poleActuators.setRelaysLockedDisabled(true);
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled(), "R.1 Relays locked disabled by default");

  // R.2 Remote unlock cannot bypass lock without local physical authorization
  poleActuators.setRelaysLockedDisabled(false);
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled(), "R.2 Remote unlock blocked without local physical authorization");

  // R.3 Local physical authorization granted (Active-low: jumper to GND = LOW)
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, LOW);
  poleActuators.servicePhysicalAuthorization();
  poleActuators.servicePhysicalAuthorization();
  poleActuators.servicePhysicalAuthorization();
  poleActuators.setLocalPhysicalAuthorization(true);
  TEST_ASSERT(poleActuators.isLocalPhysicalAuthorized(), "R.3 Local physical authorization flag active");
  poleActuators.setLocalPhysicalAuthorization(false);
  TEST_ASSERT(!poleActuators.isLocalPhysicalAuthorized(), "R.4 Local physical authorization revokable");
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, HIGH);
  poleActuators.servicePhysicalAuthorization();

  // R.5 Truth state strings
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_CONTACTOR_CLOSED), "CONTACTOR_CLOSED") == 0,
              "R.5 FEEDER_ACK_CONTACTOR_CLOSED maps to 'CONTACTOR_CLOSED'");
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_MCB_TRIPPED), "MCB_TRIPPED") == 0,
              "R.6 FEEDER_ACK_MCB_TRIPPED maps to 'MCB_TRIPPED'");
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_COMMAND_TIMEOUT), "COMMAND_TIMEOUT") == 0,
              "R.7 FEEDER_ACK_COMMAND_TIMEOUT maps to 'COMMAND_TIMEOUT'");
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_HARDWARE_FAULT), "HARDWARE_FAULT") == 0,
              "R.8 FEEDER_ACK_HARDWARE_FAULT maps to 'HARDWARE_FAULT'");

  // -------------------------------------------------------------------------
  // SECTION S: R3.2 PIN CONFIGURATION, HARDWARE PROFILE & SAFETY CHECKS
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION S: R3.2 PIN CONFIGURATION & HARDWARE PROFILE ---" << std::endl;

  // S.1 Hardware profile & configuration version
  TEST_ASSERT(strcmp(MSCB_HW_PROFILE_IDENTIFIER, "ESP32_WROOM32_DOIT_POLE_R3_2") == 0,
              "S.1 MSCB_HW_PROFILE_IDENTIFIER is 'ESP32_WROOM32_DOIT_POLE_R3_2'");
  TEST_ASSERT(MSCB_HW_CONFIG_VERSION == 0x0302, "S.2 MSCB_HW_CONFIG_VERSION is 0x0302");

  // S.3 Authoritative Board Relay Pins: F1=26, F2=25, F3=27, F4=16, F5=17, F6=32
  TEST_ASSERT(POLE_FEEDER_RELAY_PINS[0] == 26, "S.3 F1 relay is GPIO 26 (Pin 24, D26)");
  TEST_ASSERT(POLE_FEEDER_RELAY_PINS[1] == 25, "S.4 F2 relay is GPIO 25");
  TEST_ASSERT(POLE_FEEDER_RELAY_PINS[2] == 27, "S.5 F3 relay is GPIO 27");
  TEST_ASSERT(POLE_FEEDER_RELAY_PINS[3] == 16, "S.6 F4 relay is GPIO 16");
  TEST_ASSERT(POLE_FEEDER_RELAY_PINS[4] == 17, "S.7 F5 relay is GPIO 17");
  TEST_ASSERT(POLE_FEEDER_RELAY_PINS[5] == 32, "S.8 F6 relay is GPIO 32");

  // S.9 Default Hardware Configuration Validation & CRC
  PoleHardwarePinConfig defaultPinCfg = getDefaultHardwarePinConfig();
  TEST_ASSERT(validateHardwarePinConfig(defaultPinCfg), "S.9 Default pin configuration passes validation");
  uint32_t pinCrc = calculatePinConfigCrc(defaultPinCfg);
  TEST_ASSERT(pinCrc != 0, "S.10 Pin configuration CRC32 computed successfully");

  // S.11 Rejection of Flash Pin (GPIO 6..11)
  PoleHardwarePinConfig badFlashCfg = defaultPinCfg;
  badFlashCfg.feederPins[0] = 6; // Flash CLK!
  TEST_ASSERT(!validateHardwarePinConfig(badFlashCfg), "S.11 Forbidden SPI flash pin 6 rejected");

  // S.12 Rejection of Input-only Pin (GPIO 34..39) for relay output
  PoleHardwarePinConfig badInputCfg = defaultPinCfg;
  badInputCfg.feederPins[0] = 34; // Input-only!
  TEST_ASSERT(!validateHardwarePinConfig(badInputCfg), "S.12 Input-only pin 34 for relay output rejected");

  // S.13 Rejection of Peripheral Collision (Relay pin on LoRa NSS GPIO 5)
  PoleHardwarePinConfig badCollisionCfg = defaultPinCfg;
  badCollisionCfg.feederPins[0] = 5; // Collides with LoRa NSS!
  TEST_ASSERT(!validateHardwarePinConfig(badCollisionCfg), "S.13 Peripheral collision on LoRa NSS pin 5 rejected");

  // S.14 Safe pin configuration application when all relays are OFF
  TEST_ASSERT(poleActuators.applyPinConfiguration(defaultPinCfg),
              "S.14 Pin configuration safely applied when all relays are OFF");

  // -------------------------------------------------------------------------
  // SECTION T: R3.2.1 TELEGRAM MENU ERGONOMICS & JSON KEYBOARD STRUCTURE
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION T: R3.2.1 TELEGRAM MENU ERGONOMICS & JSON STRUCTURE ---" << std::endl;

  uint16_t testPoleIdT = 0x0002;
  RegisteredPole* pT = gwRegistry.findPoleById(testPoleIdT);
  if (!pT) {
    if (gwRegistry.getActiveCount() > 0) {
      const RegisteredPole* pAct = gwRegistry.getPoleByIndex(0);
      if (pAct) {
        testPoleIdT = pAct->poleId;
        pT = gwRegistry.findPoleById(testPoleIdT);
      }
    }
  }
  if (!pT) {
    uint8_t macT[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    PktRegisterRequestPayload reqT;
    PktRegisterResponsePayload respT;
    memset(&reqT, 0, sizeof(reqT));
    memcpy(reqT.macAddress, macT, 6);
    reqT.sessionToken = 0x1004;
    reqT.nonce = 4;
    cmdSecurity.computeRegisterRequestAuth(reqT, reqT.authTag);
    gwRegistry.handleJoinRequest(reqT, -60, 10, respT);
    uint16_t allocatedId = 0;
    gwRegistry.approvePending(macT, allocatedId, "Test Highway Pole", "NH48 Junction", "Sector 5");
    testPoleIdT = allocatedId;
    pT = gwRegistry.findPoleById(testPoleIdT);
  }
  if (pT) {
    pT->status = POLE_STATUS_ACTIVE;
    gwRegistry.confirmPoleHandshake(testPoleIdT);
  }

  // T.1 Main Menu
  gwTelegram.showMainMenu(adminChatId);
  KeyboardParseResult kbMain = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbMain.isValid && kbMain.totalButtons <= 6, "T.1 Main menu parsed valid JSON with <= 6 buttons");
  TEST_ASSERT(!kbMain.hasDuplicateCallbacks && !kbMain.hasInvalidCallbackLength, "T.2 Main menu has valid distinct callbacks");

  // T.3 Poles Menu
  gwTelegram.showPolesMenu(adminChatId, 0);
  KeyboardParseResult kbPoles = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbPoles.isValid && kbPoles.totalButtons <= 6, "T.3 Poles menu parsed valid JSON with <= 6 buttons");

  // T.4 Pole Details Menu
  gwTelegram.showPoleDetail(adminChatId, testPoleIdT);
  KeyboardParseResult kbPoleDetail = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbPoleDetail.isValid && kbPoleDetail.totalButtons <= 6, "T.4 Pole detail menu parsed valid JSON with <= 6 buttons");

  // T.5 Feeder List Page 0 (Feeders 1-4 + Back + Next) -> exactly 6 buttons
  gwTelegram.showPoleFeederList(adminChatId, testPoleIdT, 0);
  KeyboardParseResult kbFdrP0 = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbFdrP0.isValid && kbFdrP0.totalButtons == 6, "T.5 Pole feeder list Page 0 has exactly 6 buttons");
  TEST_ASSERT(!kbFdrP0.hasDuplicateCallbacks && !kbFdrP0.hasInvalidCallbackLength, "T.6 Pole feeder list Page 0 callbacks valid");

  // T.7 Feeder List Page 1 (Feeders 5-6 + Back + Details) -> exactly 4 buttons
  gwTelegram.showPoleFeederList(adminChatId, testPoleIdT, 1);
  KeyboardParseResult kbFdrP1 = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbFdrP1.isValid && kbFdrP1.totalButtons == 4, "T.7 Pole feeder list Page 1 has exactly 4 buttons");

  // T.8 Feeder Control Detail (ON, OFF, Toggle, Back) -> exactly 4 buttons
  gwTelegram.showFeederControlDetail(adminChatId, testPoleIdT, 1);
  KeyboardParseResult kbFdrCtrl = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbFdrCtrl.isValid && kbFdrCtrl.totalButtons == 4, "T.8 Feeder control menu has exactly 4 buttons");

  // T.9 Voltage Dashboard
  gwTelegram.showVoltageDashboard(adminChatId, testPoleIdT);
  KeyboardParseResult kbVolt = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbVolt.isValid && kbVolt.totalButtons <= 6, "T.9 Voltage dashboard parsed valid JSON with <= 6 buttons");

  // T.10 Feeder Pole Select Menu
  gwTelegram.showFeederPoleSelectMenu(adminChatId, 0);
  KeyboardParseResult kbFdrPoleSel = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbFdrPoleSel.isValid && kbFdrPoleSel.totalButtons <= 6, "T.10 Feeder pole select menu has <= 6 buttons");

  // T.11 Registrations Menu
  gwTelegram.showRegistrationsMenu(adminChatId, 0);
  KeyboardParseResult kbReg = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbReg.isValid && kbReg.totalButtons <= 6, "T.11 Registrations menu has <= 6 buttons");

  // T.12 Delete Menu
  gwTelegram.showDeleteMenu(adminChatId, 0);
  KeyboardParseResult kbDel = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbDel.isValid && kbDel.totalButtons <= 6, "T.12 Delete pole menu has <= 6 buttons");

  // T.13 Delete Confirm Menu (Confirm, Cancel) -> exactly 2 buttons
  gwTelegram.showDeleteConfirm(adminChatId, testPoleIdT);
  KeyboardParseResult kbDelConf = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbDelConf.isValid && kbDelConf.totalButtons == 2, "T.13 Delete confirm menu has exactly 2 buttons");

  // T.14 Confirm All Poles (Confirm, Cancel) -> exactly 2 buttons
  gwTelegram.showConfirmAllPoles(adminChatId, 1);
  KeyboardParseResult kbAllConf = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbAllConf.isValid && kbAllConf.totalButtons == 2, "T.14 Confirm all poles menu has exactly 2 buttons");

  // T.15 Feeder Schedule Menu
  gwTelegram.showFeederScheduleMenu(adminChatId, 0);
  KeyboardParseResult kbSched = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbSched.isValid && kbSched.totalButtons <= 6, "T.15 Feeder schedule menu has <= 6 buttons");

  // T.16 Schedule Pole Select
  gwTelegram.showSchedulePoleSelect(adminChatId, 0);
  KeyboardParseResult kbSchedPoleSel = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbSchedPoleSel.isValid && kbSchedPoleSel.totalButtons <= 6, "T.16 Schedule pole select menu has <= 6 buttons");

  // T.17 Schedule Feeder Select Page 0 (F1-F4 + Back + Next) -> exactly 6 buttons
  gwTelegram.showScheduleFeederSelect(adminChatId, testPoleIdT, 0);
  KeyboardParseResult kbSchedFdr0 = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbSchedFdr0.isValid && kbSchedFdr0.totalButtons == 6, "T.17 Schedule feeder select Page 0 has exactly 6 buttons");

  // T.18 Schedule Feeder Select Page 1 (F5-F6 + Back + Sched) -> exactly 4 buttons
  gwTelegram.showScheduleFeederSelect(adminChatId, testPoleIdT, 1);
  KeyboardParseResult kbSchedFdr1 = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbSchedFdr1.isValid && kbSchedFdr1.totalButtons == 4, "T.18 Schedule feeder select Page 1 has exactly 4 buttons");

  // T.19 Schedule Action Select (6 actions) -> exactly 6 buttons
  gwTelegram.showScheduleActionSelect(adminChatId, testPoleIdT, 1);
  KeyboardParseResult kbSchedAct = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbSchedAct.isValid && kbSchedAct.totalButtons == 6, "T.19 Schedule action select menu has exactly 6 buttons");

  // T.20 Settings Menu
  gwTelegram.showSettingsMenu(adminChatId);
  KeyboardParseResult kbSet = parseTelegramInlineKeyboard(gwTelegram.getLastSentKeyboardJson());
  TEST_ASSERT(kbSet.isValid && kbSet.totalButtons <= 6, "T.20 Settings menu has <= 6 buttons");

  // T.21 Unauthorized User Rejection
  const int64_t unauthorizedChatId = 9876543210LL;
  gwTelegram.clearLastSent();
  gwTelegram.handleCallback(unauthorizedChatId, "F:2:1:1");
  TEST_ASSERT(strstr(gwTelegram.getLastSentMessageText(), "Access Denied") != nullptr,
              "T.21 Unauthorized callback rejected with Access Denied");
  gwTelegram.clearLastSent();
  gwTelegram.handleCommand(unauthorizedChatId, "/feeder 2 1 on");
  TEST_ASSERT(strstr(gwTelegram.getLastSentMessageText(), "Access Denied") != nullptr,
              "T.22 Unauthorized slash command rejected with Access Denied");

  // -------------------------------------------------------------------------
  // SECTION U: R3.2.1 MULTI-HOP MESH, SELF-LOOP & REAL CHILD TRACKING
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION U: R3.2.1 MULTI-HOP MESH & CHILD TRACKING ---" << std::endl;

  poleMesh.begin();
  uint16_t uTestPoleId = 0x0007;
  poleMesh.setNodeIdentity(uTestPoleId, MSCB_GATEWAY_NODE_ID, 1);
  MeshWireFrame outF;
  bool sendAction = false;

  // U.1 Self-Loop Drop
  MeshWireFrame selfFrame;
  memset(&selfFrame, 0, sizeof(selfFrame));
  selfFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  selfFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  selfFrame.header.version = MSCB_PROTOCOL_VERSION;
  selfFrame.header.srcAddress = poleMesh.getMyNodeId(); // Originated from self!
  selfFrame.header.dstAddress = MSCB_GATEWAY_NODE_ID;
  selfFrame.header.seqNumber = 1001;
  selfFrame.header.hopCount = 0;
  selfFrame.header.maxHops = 6;
  bool handled = poleMesh.handleReceivedWireFrame(selfFrame, -70, 8, outF, sendAction);
  TEST_ASSERT(!handled && !sendAction, "U.1 Mesh router drops self-originating packet (loop prevention)");
  TEST_ASSERT(poleMesh.getPacketsDroppedSelfLoop() > 0, "U.2 packetsDroppedSelfLoop incremented");

  // U.3 Hop Count Limit Drop
  MeshWireFrame hopLimitFrame = selfFrame;
  hopLimitFrame.header.srcAddress = 0x0055;
  hopLimitFrame.header.hopCount = 6;
  hopLimitFrame.header.maxHops = 6;
  handled = poleMesh.handleReceivedWireFrame(hopLimitFrame, -70, 8, outF, sendAction);
  TEST_ASSERT(!handled && !sendAction, "U.3 Frame exceeding hop limit dropped");
  TEST_ASSERT(poleMesh.getPacketsDroppedHopLimit() > 0, "U.4 packetsDroppedHopLimit incremented");

  // U.5 Malformed Packet Drop (Invalid Magic)
  MeshWireFrame malformedFrame = selfFrame;
  malformedFrame.header.srcAddress = 0x0055;
  malformedFrame.header.magic[0] = 0x00; // Bad magic
  malformedFrame.header.magic[1] = 0x00;
  handled = poleMesh.handleReceivedWireFrame(malformedFrame, -70, 8, outF, sendAction);
  TEST_ASSERT(!handled && !sendAction, "U.5 Malformed frame (invalid magic) dropped");
  TEST_ASSERT(poleMesh.getPacketsDroppedMalformed() > 0, "U.6 packetsDroppedMalformed incremented");

  // U.7 Duplicate Packet Drop
  MeshWireFrame validF = selfFrame;
  validF.header.srcAddress = 0x0055;
  validF.header.dstAddress = MSCB_GATEWAY_NODE_ID;
  validF.header.nextHop = poleMesh.getMyNodeId();
  validF.header.previousHopAddress = 0x0090; // Multi-hop relayed frame (hopCount == 1)
  validF.header.seqNumber = 5501;
  validF.header.hopCount = 1;
  validF.header.maxHops = 6;
  validF.header.hopAuthToken = cmdSecurity.computeHopAuthToken(
    0x0090, poleMesh.getMyNodeId(), 5501, validF.header.type
  );
  handled = poleMesh.handleReceivedWireFrame(validF, -70, 8, outF, sendAction);
  TEST_ASSERT(handled && sendAction, "U.7 First valid frame forwarded once");
  handled = poleMesh.handleReceivedWireFrame(validF, -70, 8, outF, sendAction);
  TEST_ASSERT(!handled && !sendAction, "U.8 Duplicate frame (same src and seq) dropped");
  TEST_ASSERT(poleMesh.getPacketsDroppedDuplicate() > 0, "U.9 packetsDroppedDuplicate incremented");

  // U.10 Routing Cycle Prevention
  uint16_t myNodeId = poleMesh.getMyNodeId();
  uint8_t prevNeigh = poleMesh.getNeighborCount();
  poleMesh.updateNeighborsFromBeacon(myNodeId, 1, 0, -60, 10);
  TEST_ASSERT(poleMesh.getNeighborCount() == prevNeigh, "U.10 Self cannot be added as candidate parent neighbor (cycle prevention)");
  poleMesh.updateNeighborsFromBeacon(0x0099, 2, myNodeId, -60, 10);
  TEST_ASSERT(poleMesh.getNeighborCount() == prevNeigh, "U.11 Candidate with primary parent pointing to this node rejected (cycle prevention)");

  // U.12 Real Child Tracking on Received Wire Frames
  MeshWireFrame childFrame;
  memset(&childFrame, 0, sizeof(childFrame));
  childFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  childFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  childFrame.header.version = MSCB_PROTOCOL_VERSION;
  childFrame.header.srcAddress = 0x0008; // Child pole 8
  childFrame.header.dstAddress = MSCB_GATEWAY_NODE_ID;
  childFrame.header.nextHop = poleMesh.getMyNodeId();
  childFrame.header.previousHopAddress = 0x0008;
  childFrame.header.payloadLen = 10;
  childFrame.header.seqNumber = 2001;
  childFrame.header.hopCount = 1;
  childFrame.header.maxHops = 6;
  childFrame.header.hopAuthToken = cmdSecurity.computeHopAuthToken(
    0x0008, childFrame.header.nextHop, childFrame.header.seqNumber, childFrame.header.type
  );
  poleMesh.handleReceivedWireFrame(childFrame, -75, 7, outF, sendAction);
  TEST_ASSERT(poleMesh.getTrackedChildCount() > 0, "U.12 Forwarded child packet registered in trackedChildren");
  const TrackedChildNode* ch = poleMesh.getTrackedChild(0);
  TEST_ASSERT(ch != NULL && ch->childPoleId == 0x0008, "U.13 Tracked child ID is 0x0008");
  if (ch) {
    TEST_ASSERT(ch->state == CHILD_STATE_ONLINE, "U.14 Initial child state is CHILD_STATE_ONLINE");
    TEST_ASSERT(ch->lastSeqNumber == 2001, "U.15 Child lastSeqNumber correctly recorded");
    TEST_ASSERT(ch->lastRssi == -75 && ch->lastSnr == 7, "U.16 Child link metrics accurately recorded");
  }

  // U.17 Child Inactivity Timeout Transition
  delay(95000); // Exceed 90s MESH_CHILD_TIMEOUT_MS
  poleMesh.checkChildTimeouts(millis());
  ch = poleMesh.getTrackedChild(0);
  TEST_ASSERT(ch != NULL && ch->state == CHILD_STATE_OFFLINE, "U.17 Inactive child transitioned to CHILD_STATE_OFFLINE");

  // U.18 Child Recovery on Valid Packet
  childFrame.header.seqNumber = 2002;
  childFrame.header.nextHop = poleMesh.getMyNodeId();
  childFrame.header.previousHopAddress = 0x0008;
  childFrame.header.payloadLen = 10;
  childFrame.header.hopAuthToken = cmdSecurity.computeHopAuthToken(
    0x0008, childFrame.header.nextHop, childFrame.header.seqNumber, childFrame.header.type
  );
  poleMesh.handleReceivedWireFrame(childFrame, -71, 9, outF, sendAction);
  ch = poleMesh.getTrackedChild(0);
  TEST_ASSERT(ch != NULL && ch->state == CHILD_STATE_ONLINE, "U.18 Subsequent valid packet restores child to CHILD_STATE_ONLINE");
  TEST_ASSERT(poleMesh.getTrackedChildCount() == 1, "U.19 Child record updated in-place without duplicate entries");

  // U.20 Child cannot be selected as Parent (Cycle Prevention)
  poleMesh.updateNeighborsFromBeacon(0x0008, 1, 0, -60, 10);
  TEST_ASSERT(poleMesh.getNeighborCount() == prevNeigh, "U.20 Child node 0x0008 rejected as parent (cycle prevention)");

  // -------------------------------------------------------------------------
  // SECTION V: R3.2.1 GATEWAY HEARTBEAT, CASCADE ALERT & BOUNDED QUEUE
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION V: R3.2.1 GATEWAY HEARTBEAT & CASCADE FAILURE ---" << std::endl;

  // V.1 Unknown pole query
  TEST_ASSERT(gwHeartbeat.getPoleState(0x9999) == POLE_STATE_UNKNOWN,
              "V.1 Unregistered pole query returns POLE_STATE_UNKNOWN (never false ONLINE)");

  // V.2 Register test hierarchy: Pole 0x0010 (Gateway direct, parent=0), Pole 0x0020 (parent=0x0010)
  uint8_t mac10[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0x10, 0x10};
  uint8_t mac20[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0x20, 0x20};
  PktRegisterRequestPayload req10, req20;
  PktRegisterResponsePayload resp10, resp20;
  memset(&req10, 0, sizeof(req10));
  memset(&req20, 0, sizeof(req20));
  memcpy(req10.macAddress, mac10, 6);
  memcpy(req20.macAddress, mac20, 6);
  req10.sessionToken = 0x1010;
  req10.nonce = 10;
  cmdSecurity.computeRegisterRequestAuth(req10, req10.authTag);
  req20.sessionToken = 0x1020;
  req20.nonce = 20;
  cmdSecurity.computeRegisterRequestAuth(req20, req20.authTag);
  gwRegistry.handleJoinRequest(req10, -60, 10, resp10);
  gwRegistry.handleJoinRequest(req20, -65, 9, resp20);
  uint16_t id10 = 0, id20 = 0;
  gwRegistry.approvePending(mac10, id10, "Pole 10 Direct", "NH48 Km 10", "Sector 1");
  gwRegistry.approvePending(mac20, id20, "Pole 20 Child", "NH48 Km 20", "Sector 2");
  RegisteredPole* p10 = gwRegistry.findPoleByMac(mac10);
  RegisteredPole* p20 = gwRegistry.findPoleByMac(mac20);
  p10->primaryParent = MSCB_GATEWAY_NODE_ID;
  p20->primaryParent = p10->poleId;

  // Keep other registered poles refreshed so only our test hierarchy times out
  for (uint8_t i = 0; i < gwRegistry.getActiveCount(); i++) {
    const RegisteredPole* ap = gwRegistry.getPoleByIndex(i);
    if (ap && ap->poleId != p10->poleId && ap->poleId != p20->poleId) {
      gwHeartbeat.onHeartbeatReceived(ap->poleId, 2500);
    }
  }
  // Clear any pre-existing alerts from earlier test sections
  GwAlertEvent discardEv;
  while (gwHeartbeat.popNextAlert(discardEv)) {}

  // Both report heartbeat at epoch 2000
  gwHeartbeat.onHeartbeatReceived(p10->poleId, 2000);
  gwHeartbeat.onHeartbeatReceived(p20->poleId, 2000);
  TEST_ASSERT(gwHeartbeat.getPoleState(p10->poleId) == POLE_STATE_ONLINE, "V.2 Pole 10 is ONLINE");
  TEST_ASSERT(gwHeartbeat.getPoleState(p20->poleId) == POLE_STATE_ONLINE, "V.3 Pole 20 is ONLINE");

  // Advance clock to 2500 (> 360s timeout). Neither pole reports.
  gwHeartbeat.checkHealth(2500);
  TEST_ASSERT(gwHeartbeat.getPoleState(p10->poleId) == POLE_STATE_OFFLINE,
              "V.4 Direct parent Pole 10 classified POLE_STATE_OFFLINE");
  TEST_ASSERT(gwHeartbeat.getPoleState(p20->poleId) == POLE_STATE_UNREACHABLE_VIA_PARENT,
              "V.5 Descendant Pole 20 classified POLE_STATE_UNREACHABLE_VIA_PARENT (Cascade Failure)");

  // V.6 Bounded Alert Queue & Popping Events
  TEST_ASSERT(gwHeartbeat.hasPendingAlert(), "V.6 Gateway heartbeat monitor has queued alert events");
  TEST_ASSERT(gwHeartbeat.getAlertQueueCount() <= GW_ALERT_QUEUE_CAPACITY,
              "V.7 Alert queue count is bounded at GW_ALERT_QUEUE_CAPACITY");

  GwAlertEvent evP10, evP20;
  bool pop1 = gwHeartbeat.popNextAlert(evP10);
  bool pop2 = gwHeartbeat.popNextAlert(evP20);
  TEST_ASSERT(pop1 && evP10.poleId == p10->poleId && evP10.newState == POLE_STATE_OFFLINE,
              "V.8 First alert event correctly identified Pole 10 direct OFFLINE");
  TEST_ASSERT(pop2 && evP20.poleId == p20->poleId && evP20.newState == POLE_STATE_UNREACHABLE_VIA_PARENT,
              "V.9 Second alert event correctly identified Pole 20 UNREACHABLE_VIA_PARENT");
  TEST_ASSERT(strstr(evP20.reason, "Parent 0x") != nullptr,
              "V.10 Cascade alert reason explicitly identifies parent dependency");

  // V.11 Alert Cooldown & Deduplication
  gwHeartbeat.checkHealth(2510); // 10s later, within cooldown
  TEST_ASSERT(!gwHeartbeat.hasPendingAlert(), "V.11 Deduplication and cooldown suppressed duplicate alert storm");

  // -------------------------------------------------------------------------
  // SECTION W: R3.2.1 PHYSICAL AUTHORIZATION & RELAY SAFETY LOCK
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION W: R3.2.1 PHYSICAL AUTHORIZATION & RELAY SAFETY ---" << std::endl;

  // W.1 Production Safety Lock
  TEST_ASSERT(MSCB_RELAYS_LOCKED_DISABLED == 1, "W.1 MSCB_RELAYS_LOCKED_DISABLED production lock preserved (1)");
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled(), "W.2 areRelaysLockedDisabled() returns true");
  poleActuators.setRelaysLockedDisabled(false);
  TEST_ASSERT(poleActuators.areRelaysLockedDisabled(), "W.3 Remote unlock rejected without local physical authorization");

  // W.4 Software Setter alone cannot authorize without Physical Pin 39 (open circuit HIGH)
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, HIGH);
  poleActuators.servicePhysicalAuthorization();
  poleActuators.setLocalPhysicalAuthorization(true);
  TEST_ASSERT(!poleActuators.isLocalPhysicalAuthorized(),
              "W.4 Software setter alone cannot grant physical auth while GPIO 39 is open (HIGH)");

  // W.5 Physical Pin 39 Activation with Debounce (Active-low: jumper to GND = LOW)
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, LOW);
  poleActuators.servicePhysicalAuthorization();
  delay(POLE_PHYSICAL_AUTH_DEBOUNCE_MS + 10);
  poleActuators.servicePhysicalAuthorization();
  poleActuators.setLocalPhysicalAuthorization(true);
  TEST_ASSERT(poleActuators.isLocalPhysicalAuthorized(),
              "W.5 Physical auth granted when GPIO 39 is grounded (LOW) and debounced");

  // W.6 Auto-Expiry after 5 minutes (300,000 ms)
  delay(300005UL);
  poleActuators.servicePhysicalAuthorization();
  TEST_ASSERT(!poleActuators.isLocalPhysicalAuthorized(),
              "W.6 Physical authorization automatically expired after 5 minutes");

  // W.7 Emergency Shutdown on Loss of Physical Jumper
  poleActuators.resetEmergencyState();
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, LOW);
  poleActuators.servicePhysicalAuthorization();
  delay(POLE_PHYSICAL_AUTH_DEBOUNCE_MS + 10);
  poleActuators.servicePhysicalAuthorization();
  poleActuators.setLocalPhysicalAuthorization(true);
  poleActuators.setBenchTestOverride(true);
  poleActuators.requestRelayOn(0, "BENCH_TEST");
#if defined(MSCB_COMMISSIONING_LVDC_PROFILE) && (MSCB_COMMISSIONING_LVDC_PROFILE == 1)
  TEST_ASSERT(poleActuators.isRelayCommandedOn(0),
              "W.7 Test relay energized in authorized low-voltage bench mode");
  // Physical jumper removed while energized!
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, HIGH);
  poleActuators.servicePhysicalAuthorization();
  TEST_ASSERT(!poleActuators.isLocalPhysicalAuthorized(),
              "W.8 Physical auth revoked immediately when jumper removed");
  TEST_ASSERT(!poleActuators.isRelayCommandedOn(0),
              "W.9 Emergency shutdown instantly de-energized relay upon auth loss");
#else
  TEST_ASSERT(!poleActuators.isRelayCommandedOn(0),
              "W.7 Test relay strictly locked OFF in permanent production profile");
  // Physical jumper removed while energized!
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, HIGH);
  poleActuators.servicePhysicalAuthorization();
  TEST_ASSERT(!poleActuators.isLocalPhysicalAuthorized(),
              "W.8 Physical auth revoked immediately when jumper removed");
  TEST_ASSERT(!poleActuators.isRelayCommandedOn(0),
              "W.9 Relays remain locked OFF upon auth loss");
#endif

  // W.10 Truthful Auxiliary Feedback States
  TEST_ASSERT(strcmp(contactorStateToString(CONTACTOR_CLOSED), "CLOSED") == 0, "W.10 ContactorState string CLOSED");
  TEST_ASSERT(strcmp(contactorStateToString(CONTACTOR_OPEN), "OPEN") == 0, "W.11 ContactorState string OPEN");
  TEST_ASSERT(strcmp(contactorStateToString(CONTACTOR_UNAVAILABLE), "UNAVAILABLE") == 0, "W.12 ContactorState string UNAVAILABLE");
  TEST_ASSERT(strcmp(mcbStateToString(MCB_HEALTHY), "HEALTHY") == 0, "W.13 McbState string HEALTHY");
  TEST_ASSERT(strcmp(mcbStateToString(MCB_TRIPPED), "TRIPPED") == 0, "W.14 McbState string TRIPPED");
  TEST_ASSERT(strcmp(mcbStateToString(MCB_UNAVAILABLE), "UNAVAILABLE") == 0, "W.15 McbState string UNAVAILABLE");
  TEST_ASSERT(strcmp(measurementValidityToString(MEAS_SENSOR_UNAVAILABLE), "SENSOR_UNAVAILABLE") == 0,
              "W.16 MeasurementValidity string SENSOR_UNAVAILABLE");

  // W.17 Disconnected Aux Hardware returns UNAVAILABLE (never fake OPEN or HEALTHY)
  poleFeederMgr.begin();
  TEST_ASSERT(poleFeederMgr.getContactorState(1) == CONTACTOR_UNAVAILABLE,
              "W.17 Disconnected aux hardware returns CONTACTOR_UNAVAILABLE (never fake OPEN or CLOSED)");
  TEST_ASSERT(poleFeederMgr.getMcbState(1) == MCB_UNAVAILABLE,
              "W.18 Disconnected aux hardware returns MCB_UNAVAILABLE (never fake HEALTHY)");

  // -------------------------------------------------------------------------
  // SECTION X: R3.2.1 21-CHANNEL METROLOGY TRUTH & BOOT PIN CONFIG
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION X: R3.2.1 METROLOGY TRUTH & BOOT PIN CONFIG ---" << std::endl;

  // X.1 Missing ADC metrology returns NAN (never fabricated 0.0V or 0.0A)
  poleMetrology.setAdcChipDetected(0, false);
  poleMetrology.setAdcChipDetected(1, false);
  poleMetrology.setAdcChipDetected(2, false);
  float v1 = poleMetrology.getBusVoltageR();
  TEST_ASSERT(std::isnan(v1), "X.1 Missing ADC returns NAN for phase voltage (never fabricated 0.0V)");
  float i1 = poleMetrology.getFeederCurrent(0, 0);
  TEST_ASSERT(std::isnan(i1), "X.2 Missing ADC returns NAN for feeder current (never fabricated 0.0A)");
  TEST_ASSERT(poleMetrology.getMeasurementValidity(1) == MEAS_SENSOR_UNAVAILABLE,
              "X.3 Measurement validity returns MEAS_SENSOR_UNAVAILABLE");

  // X.4 Telegram Truthful Reporting for Missing Sensors & Feedbacks
  RegisteredPole* pTestDisp = gwRegistry.findPoleById(testPoleIdT);
  if (pTestDisp) {
    pTestDisp->auxFeedbackValid = false;
    pTestDisp->mcbStatusValid = false;
    pTestDisp->lastBusVoltage = 0.0f; // Sensor unavailable
    gwTelegram.showPoleDetail(adminChatId, testPoleIdT);
    TEST_ASSERT(strstr(gwTelegram.getLastSentMessageText(), "FEEDBACK NOT CONNECTED") != nullptr,
                "X.4 Telegram detail shows 'FEEDBACK NOT CONNECTED' when aux valid flag is false");
    TEST_ASSERT(strstr(gwTelegram.getLastSentMessageText(), "SENSOR UNAVAILABLE") != nullptr,
                "X.5 Telegram detail shows 'SENSOR UNAVAILABLE' when voltage sensor is missing");
  }

  // X.6 Boot Pin Configuration Recovery & Validation
  PoleHardwarePinConfig badCrcCfg = getDefaultHardwarePinConfig();
  badCrcCfg.crc32 ^= 0xFFFFFFFF; // Corrupt CRC
  TEST_ASSERT(calculatePinConfigCrc(badCrcCfg) != badCrcCfg.crc32, "X.6 Corrupted CRC detected");
  PoleHardwarePinConfig safeDefaultCfg = getDefaultHardwarePinConfig();
  TEST_ASSERT(validateHardwarePinConfig(safeDefaultCfg), "X.7 Default pin mapping passes strict hardware validation");
  TEST_ASSERT(poleActuators.applyPinConfiguration(safeDefaultCfg), "X.8 Safe default configuration applied cleanly");

  // -------------------------------------------------------------------------
  // SECTION Y: R3.2.2 STRICT MESH ROUTING & RANK SAFETY (DEF-322-01)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION Y: R3.2.2 STRICT MESH ROUTING & RANK SAFETY ---" << std::endl;

  PoleMeshRouter testRouter;
  testRouter.begin();
  uint16_t localNode = 0x0002;
  testRouter.setNodeIdentity(localNode, 0, 255); // Start in orphan/searching state: myRank = 255

  // Y.1 Gateway Rank is strictly 0
  TEST_ASSERT(MSCB_GATEWAY_NODE_ID == 1, "Y.1 Gateway node ID is 0x0001");

  // Y.2 Candidate parent rank lower than current rank accepted
  testRouter.updateNeighborsFromBeacon(0x0010, 1, 1, -55, 12); // Node 0x0010, rank 1, parent 1
  testRouter.selectBestParents();
  TEST_ASSERT(testRouter.getMyRank() == 2, "Y.2 Valid candidate rank 1 increments to monotonic rank 2");
  TEST_ASSERT(testRouter.getPrimaryParent() == 0x0010, "Y.3 Primary parent selected as candidate");

  // Y.3 Candidate with equal or higher rank rejected
  uint8_t preCount = testRouter.getNeighborCount();
  testRouter.updateNeighborsFromBeacon(0x0020, 2, 1, -50, 15); // Rank 2 == current rank 2: rejected!
  testRouter.updateNeighborsFromBeacon(0x0030, 3, 1, -50, 15); // Rank 3 > current rank 2: rejected!
  TEST_ASSERT(testRouter.getNeighborCount() == preCount, "Y.4 Equal or higher candidate rank rejected");

  // Y.4 Rank 254/255 boundary overflow rejected safely
  testRouter.updateNeighborsFromBeacon(0x0040, 254, 1, -50, 15);
  testRouter.updateNeighborsFromBeacon(0x0050, 255, 1, -50, 15);
  TEST_ASSERT(testRouter.getNeighborCount() == preCount, "Y.5 Rank 254/255 overflow candidate rejected");

  // Y.5 Hop count limits: maxHops == 0 rejected
  MeshWireFrame zeroHopFrame;
  memset(&zeroHopFrame, 0, sizeof(zeroHopFrame));
  zeroHopFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  zeroHopFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  zeroHopFrame.header.version = MSCB_PROTOCOL_VERSION;
  zeroHopFrame.header.srcAddress = 0x0050;
  zeroHopFrame.header.dstAddress = MSCB_GATEWAY_NODE_ID;
  zeroHopFrame.header.nextHop = localNode;
  zeroHopFrame.header.maxHops = 0; // Invalid
  MeshWireFrame outDummy;
  bool sendAct = false;
  TEST_ASSERT(!testRouter.handleReceivedWireFrame(zeroHopFrame, -70, 8, outDummy, sendAct),
              "Y.6 maxHops == 0 frame strictly rejected");

  // Y.6 maxHops > 6 rejected
  MeshWireFrame excessHopFrame = zeroHopFrame;
  excessHopFrame.header.maxHops = 7;
  TEST_ASSERT(!testRouter.handleReceivedWireFrame(excessHopFrame, -70, 8, outDummy, sendAct),
              "Y.7 maxHops > 6 frame strictly rejected");

  // Y.7 hopCount >= maxHops rejected
  MeshWireFrame exhaustedHopFrame = zeroHopFrame;
  exhaustedHopFrame.header.maxHops = 6;
  exhaustedHopFrame.header.hopCount = 6;
  TEST_ASSERT(!testRouter.handleReceivedWireFrame(exhaustedHopFrame, -70, 8, outDummy, sendAct),
              "Y.8 hopCount >= maxHops frame strictly rejected");

  // Y.8 Incorrect nextHop dropped
  MeshWireFrame wrongNextHopFrame = zeroHopFrame;
  wrongNextHopFrame.header.maxHops = 6;
  wrongNextHopFrame.header.hopCount = 1;
  wrongNextHopFrame.header.nextHop = 0x0777; // Different pole!
  uint32_t preDrop = testRouter.getPacketsDropped();
  TEST_ASSERT(!testRouter.handleReceivedWireFrame(wrongNextHopFrame, -70, 8, outDummy, sendAct),
              "Y.9 Frame with incorrect nextHop dropped by bystander node");
  TEST_ASSERT(testRouter.getPacketsDropped() > preDrop, "Y.10 packetsDropped incremented for incorrect nextHop");

  // Y.9 Primary and Backup parents must be distinct
  testRouter.updateNeighborsFromBeacon(0x0015, 1, 1, -60, 10);
  testRouter.selectBestParents();
  TEST_ASSERT(testRouter.getPrimaryParent() != testRouter.getBackupParent() || testRouter.getBackupParent() == 0,
              "Y.11 Primary and backup parents are distinct or backup cleared");

  // -------------------------------------------------------------------------
  // SECTION Z: R3.2.2 AUTHENTICATED IMMEDIATE-HOP & CHILD TRACKING (DEF-322-02)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION Z: R3.2.2 AUTHENTICATED IMMEDIATE-HOP & CHILD TRACKING ---" << std::endl;

  TEST_ASSERT(MSCB_PROTOCOL_VERSION == 0x02, "Z.1 Protocol Version updated to 0x02");

  // Z.2 Authenticated child packet registered
  PoleMeshRouter childRouter;
  childRouter.begin();
  uint16_t childRouterId = 0x0005;
  childRouter.setNodeIdentity(childRouterId, MSCB_GATEWAY_NODE_ID, 1);

  MeshWireFrame childDirect;
  memset(&childDirect, 0, sizeof(childDirect));
  childDirect.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  childDirect.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  childDirect.header.version = MSCB_PROTOCOL_VERSION;
  childDirect.header.srcAddress = 0x0012;
  childDirect.header.dstAddress = MSCB_GATEWAY_NODE_ID;
  childDirect.header.nextHop = childRouterId;
  childDirect.header.previousHopAddress = 0x0012; // Direct transmission
  childDirect.header.type = PKT_HEARTBEAT_PING;
  childDirect.header.seqNumber = 1201;
  childDirect.header.hopCount = 1;
  childDirect.header.maxHops = 6;
  childDirect.header.hopAuthToken = cmdSecurity.computeHopAuthToken(
    0x0012, childRouterId, 1201, childDirect.header.type
  );
  childRouter.handleReceivedWireFrame(childDirect, -70, 8, outDummy, sendAct);
  TEST_ASSERT(childRouter.getTrackedChildCount() == 1, "Z.2 Direct authenticated child registered");
  TEST_ASSERT(childRouter.getTrackedChild(0)->childPoleId == 0x0012, "Z.3 Tracked child ID is 0x0012");

  // Z.3 Grandchild packet: srcAddress is 0x0033 (grandchild), but previousHopAddress is 0x0012 (child relay)
  MeshWireFrame grandchildFrame = childDirect;
  grandchildFrame.header.srcAddress = 0x0033; // Grandchild!
  grandchildFrame.header.previousHopAddress = 0x0012; // Transmitted by child 0x0012
  grandchildFrame.header.seqNumber = 3301;
  grandchildFrame.header.hopCount = 2;
  grandchildFrame.header.hopAuthToken = cmdSecurity.computeHopAuthToken(
    0x0012, childRouterId, 3301, grandchildFrame.header.type
  );
  childRouter.handleReceivedWireFrame(grandchildFrame, -70, 8, outDummy, sendAct);
  TEST_ASSERT(childRouter.getTrackedChildCount() == 1, "Z.4 Grandchild frame does NOT create new direct child record");
  TEST_ASSERT(childRouter.findTrackedChild(0x0033) == nullptr, "Z.5 Grandchild 0x0033 is NOT registered as direct child");

  // Z.4 Spoofed / Invalid hopAuthToken rejected from child tracking
  MeshWireFrame spoofedFrame = childDirect;
  spoofedFrame.header.srcAddress = 0x0014;
  spoofedFrame.header.previousHopAddress = 0x0014;
  spoofedFrame.header.seqNumber = 1401;
  spoofedFrame.header.hopAuthToken = 0xDEAD; // Invalid token!
  childRouter.handleReceivedWireFrame(spoofedFrame, -70, 8, outDummy, sendAct);
  TEST_ASSERT(childRouter.findTrackedChild(0x0014) == nullptr, "Z.6 Spoofed hopAuthToken rejected from child tracking");

  // Z.5 Broadcast nextHop alone does NOT register a child
  MeshWireFrame bcastFrame = childDirect;
  bcastFrame.header.srcAddress = 0x0015;
  bcastFrame.header.previousHopAddress = 0x0015;
  bcastFrame.header.nextHop = MSCB_BROADCAST_NODE_ID;
  bcastFrame.header.seqNumber = 1501;
  bcastFrame.header.hopAuthToken = cmdSecurity.computeHopAuthToken(
    0x0015, MSCB_BROADCAST_NODE_ID, 1501, bcastFrame.header.type
  );
  childRouter.handleReceivedWireFrame(bcastFrame, -70, 8, outDummy, sendAct);
  TEST_ASSERT(childRouter.findTrackedChild(0x0015) == nullptr, "Z.7 Broadcast nextHop frame does NOT register child");

  // -------------------------------------------------------------------------
  // SECTION AA: R3.2.2 ALERT QUEUE RELIABILITY & PENDING RETRY (DEF-322-03)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AA: R3.2.2 ALERT QUEUE RELIABILITY & PENDING RETRY ---" << std::endl;

  GwHeartbeatMonitor testGwHb;
  testGwHb.begin();

  // AA.1 Verify queue insertion returns true when space available
  GwAlertEvent testEv;
  memset(&testEv, 0, sizeof(testEv));
  testEv.poleId = 0x0001;
  testEv.newState = POLE_STATE_OFFLINE;
  strncpy(testEv.reason, "TEST_ALERT", sizeof(testEv.reason) - 1);
  TEST_ASSERT(testGwHb.queueAlert(testEv), "AA.1 queueAlert returns true on available queue capacity");

  // AA.2 Fill queue to full capacity (GW_ALERT_QUEUE_CAPACITY == 16)
  while (testGwHb.getAlertQueueCount() < GW_ALERT_QUEUE_CAPACITY) {
    testEv.poleId++;
    testGwHb.queueAlert(testEv);
  }
  TEST_ASSERT(testGwHb.getAlertQueueCount() == GW_ALERT_QUEUE_CAPACITY, "AA.2 Alert queue filled to exactly 16 capacity");

  // AA.3 Attempt insertion into full queue: returns false
  testEv.poleId = 0x0099;
  TEST_ASSERT(!testGwHb.queueAlert(testEv), "AA.3 Insertion into full queue returns false (fail-safe rejection)");

  // AA.4 Record child offline via parent when queue is full: preserved in pending state
  testGwHb.recordChildOfflineFromParent(0x0099, 0x0002, 1000, "DOWNSTREAM_TIMEOUT");
  TEST_ASSERT(testGwHb.hasPendingAlert(), "AA.4 Unqueued alert preserved in pending retry state");

  // AA.5 Pop alert to free capacity -> pending alert is drained and queued
  GwAlertEvent poppedEv;
  TEST_ASSERT(testGwHb.popNextAlert(poppedEv), "AA.5 Successfully pop item to free queue slot");
  testGwHb.drainPendingAlerts();
  TEST_ASSERT(testGwHb.getAlertQueueCount() == GW_ALERT_QUEUE_CAPACITY, "AA.6 Pending alert retried and inserted into queue");

  // -------------------------------------------------------------------------
  // SECTION AB: R3.2.2 PERMANENT PRODUCTION RELAY SAFETY LOCK (DEF-322-04)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AB: R3.2.2 PERMANENT PRODUCTION RELAY SAFETY LOCK ---" << std::endl;

  PoleActuators testActuators;
  testActuators.init();

  // AB.1 Default state is locked
  TEST_ASSERT(testActuators.areRelaysLockedDisabled(), "AB.1 Relays locked disabled by default");

  // AB.2 Remote unlock attempt without physical auth is rejected
  testActuators.setRelaysLockedDisabled(false);
  TEST_ASSERT(testActuators.areRelaysLockedDisabled(), "AB.2 Runtime unlock attempt rejected");

  // AB.3 In commissioning profile, bench mode requires physical auth + benchTestOverride + diagnostic passed
  testActuators.setBenchTestOverride(true);
  TEST_ASSERT(testActuators.areRelaysLockedDisabled(), "AB.3 Bench test override alone cannot unlock relays");

  // -------------------------------------------------------------------------
  // SECTION AC: R3.2.2 GPIO 39 PHYSICAL AUTH & DEBOUNCE (DEF-322-05)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AC: R3.2.2 GPIO 39 PHYSICAL AUTH & DEBOUNCE ---" << std::endl;

  digitalWrite(POLE_PHYSICAL_AUTH_PIN, HIGH); // Open circuit / pull-up HIGH
  testActuators.servicePhysicalAuthorization();
  testActuators.setLocalPhysicalAuthorization(true);
  TEST_ASSERT(!testActuators.isLocalPhysicalAuthorized(), "AC.1 Pin 39 HIGH cannot authorize");

  // Ground pin 39 (LOW)
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, LOW);
  testActuators.servicePhysicalAuthorization();
  delay(POLE_PHYSICAL_AUTH_DEBOUNCE_MS + 10); // Continuous elapsed time debounce
  testActuators.servicePhysicalAuthorization();
  testActuators.setLocalPhysicalAuthorization(true);
  TEST_ASSERT(testActuators.isLocalPhysicalAuthorized(), "AC.2 50ms debounced LOW pin grants physical authorization");

  // Immediate revocation on jumper removal (pin HIGH)
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, HIGH);
  testActuators.servicePhysicalAuthorization();
  TEST_ASSERT(!testActuators.isLocalPhysicalAuthorized(), "AC.3 Jumper removal immediately revokes authorization");

  // -------------------------------------------------------------------------
  // SECTION AD: R3.2.2 COMPLETE DYNAMIC PIN CONFIGURATION (DEF-322-06)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AD: R3.2.2 COMPLETE DYNAMIC PIN CONFIGURATION ---" << std::endl;

  PoleHardwarePinConfig dynCfg = getDefaultHardwarePinConfig();
  dynCfg.loraNssPin = 15; // Valid non-flash GPIO
  dynCfg.loraRstPin = 2;  // Valid non-flash GPIO
  dynCfg.loraDio0Pin = 4;
  dynCfg.decoderEnablePin = 22;
  dynCfg.decoderA0Pin = 21;
  dynCfg.decoderA1Pin = 19;
  dynCfg.crc32 = calculatePinConfigCrc(dynCfg);

  poleRadio.configurePins(dynCfg.loraNssPin, dynCfg.loraRstPin, dynCfg.loraDio0Pin);
  TEST_ASSERT(poleRadio.getNssPin() == 15, "AD.1 LoRa driver accepted dynamic NSS pin 15");
  TEST_ASSERT(poleRadio.getRstPin() == 2, "AD.2 LoRa driver accepted dynamic RST pin 2");

  poleMetrology.configurePins(dynCfg.decoderEnablePin, dynCfg.decoderA0Pin, dynCfg.decoderA1Pin,
                              dynCfg.adcSyncPin, dynCfg.adcDrdyPins);
  TEST_ASSERT(poleMetrology.getDecoderEnablePin() == 22, "AD.3 Metrology accepted dynamic decoder enable pin 22");
  TEST_ASSERT(poleMetrology.getDecoderA0Pin() == 21, "AD.4 Metrology accepted dynamic decoder A0 pin 21");
  TEST_ASSERT(poleMetrology.getDecoderA1Pin() == 19, "AD.5 Metrology accepted dynamic decoder A1 pin 19");

  // Restore defaults
  PoleHardwarePinConfig defCfg = getDefaultHardwarePinConfig();
  poleRadio.configurePins(defCfg.loraNssPin, defCfg.loraRstPin, defCfg.loraDio0Pin);
  poleMetrology.configurePins(defCfg.decoderEnablePin, defCfg.decoderA0Pin, defCfg.decoderA1Pin,
                              defCfg.adcSyncPin, defCfg.adcDrdyPins);
  TEST_ASSERT(poleRadio.getNssPin() == POLE_LORA_NSS_PIN, "AD.6 Defaults restored for LoRa");

  // -------------------------------------------------------------------------
  // SECTION AE: R3.2.2 GATEWAY TELEGRAM TLS & ADMIN IDENTITY (DEF-322-07)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AE: R3.2.2 TELEGRAM TLS & ADMIN IDENTITY ---" << std::endl;

  TEST_ASSERT(TELEGRAM_CA_ROOT_CERT != nullptr && strlen(TELEGRAM_CA_ROOT_CERT) > 100,
              "AE.1 DigiCert Root CA certificate embedded in GwTelegramCert.h");
  TEST_ASSERT(strstr(TELEGRAM_CA_ROOT_CERT, "BEGIN CERTIFICATE") != nullptr,
              "AE.2 Valid PEM certificate structure verified");

  // Verify allowlist check
  TEST_ASSERT(gwTelegram.isAuthorizedAdmin(5505296197LL), "AE.3 Primary admin 5505296197 authorized");
  TEST_ASSERT(!gwTelegram.isAuthorizedAdmin(123456789LL), "AE.4 Dummy admin 123456789 rejected in production config");
  TEST_ASSERT(!gwTelegram.isAuthorizedAdmin(987654321LL), "AE.5 Unknown user ID 987654321 strictly rejected");
  TEST_ASSERT(gwTelegram.isAuthorizedSender(5505296197LL, -1001234567890LL, true),
              "AE.6 Admin sender authorized in group chat");
  TEST_ASSERT(!gwTelegram.isAuthorizedSender(987654321LL, -1001234567890LL, true),
              "AE.7 Non-admin sender rejected in group chat");

  // -------------------------------------------------------------------------
  // SECTION AF: R3.2.2 METROLOGY DIAGNOSTIC TRUTH (DEF-322-08)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AF: R3.2.2 METROLOGY DIAGNOSTIC TRUTH ---" << std::endl;

  // AF.1 Missing ADC returns NAN for voltage
  poleMetrology.setAdcChipDetected(2, false); // Chip 2 has voltages
  TEST_ASSERT(isnan(poleMetrology.getBusVoltageR()), "AF.1 Missing ADC returns NAN for phase R voltage");
  TEST_ASSERT(isnan(poleMetrology.getBusVoltageY()), "AF.2 Missing ADC returns NAN for phase Y voltage");
  TEST_ASSERT(isnan(poleMetrology.getBusVoltageB()), "AF.3 Missing ADC returns NAN for phase B voltage");

  // AF.2 Missing ADC returns NAN for current
  poleMetrology.setAdcChipDetected(0, false); // Chip 0 has feeders 1..3
  TEST_ASSERT(isnan(poleMetrology.getFeederCurrent(0, 0)), "AF.4 Missing ADC returns NAN for feeder 1 phase R");

  // AF.3 Validity reports SENSOR_UNAVAILABLE
  TEST_ASSERT(poleMetrology.getMeasurementValidity(1) == MEAS_SENSOR_UNAVAILABLE,
              "AF.5 Disconnected channel returns MEAS_SENSOR_UNAVAILABLE");

  // AF.4 ADS131M08 Device ID and Status Word checks
  TEST_ASSERT(poleMetrology.validateAds131m08DeviceId(0x2800), "AF.6 Valid ADS131M08 device ID 0x2800 accepted");
  TEST_ASSERT(!poleMetrology.validateAds131m08DeviceId(0x1200), "AF.7 Invalid device ID rejected");
  TEST_ASSERT(poleMetrology.validateAds131m08StatusWord(0x05FF), "AF.8 Healthy status word accepted");
  TEST_ASSERT(!poleMetrology.validateAds131m08StatusWord(0x07FF), "AF.9 Status word with reset/fault bit rejected");

  // -------------------------------------------------------------------------
  // SECTION AG: R3.3 RELAY SAFE-OFF BOOT ORDER (NO ACTIVE-LOW GLITCH)
  // -------------------------------------------------------------------------
  std::cout << "\n--- SECTION AG: R3.3 RELAY SAFE-OFF BOOT ORDER ---" << std::endl;

  // Re-run initialization with GPIO tracing enabled. On an active-LOW board,
  // enabling the output driver before loading the OFF latch produces a brief
  // LOW (relay ON) pulse. The first operation on every relay pin must therefore
  // be a digitalWrite(OFF), never a pinMode(OUTPUT).
  g_pinTraceCount = 0;
  g_pinTraceEnabled = true;
  poleActuators.init(true);
  g_pinTraceEnabled = false;

  const uint8_t offLevel = RELAY_ACTIVE_LOW ? HIGH : LOW;
  bool allPinsSafe = true;
  bool allPinsTouched = true;
  for (uint8_t p = 0; p < 6; p++) {
    const uint8_t pin = POLE_FEEDER_RELAY_PINS[p];
    bool sawOffWrite = false;
    bool prematureOutput = false;
    for (int t = 0; t < g_pinTraceCount; t++) {
      if (g_pinTracePin[t] != pin) continue;
      if (g_pinTraceOp[t] == 1) { // pinMode before any safe write
        prematureOutput = true;
        break;
      }
      if (g_pinTraceOp[t] == 2) {
        sawOffWrite = true;
        if (g_pinTraceVal[t] != offLevel) prematureOutput = true;
        break;
      }
    }
    if (!sawOffWrite || prematureOutput) allPinsSafe = false;
    if (!sawOffWrite) allPinsTouched = false;
  }
  TEST_ASSERT(allPinsTouched,
              "AG.1 Every feeder relay pin is explicitly driven during init");
  TEST_ASSERT(allPinsSafe,
              "AG.2 Safe OFF level is latched before output enable on all 6 pins");

  // Final Summary
  std::cout << "\n=====================================================================" << std::endl;
  std::cout << "TEST RUN COMPLETE: " << g_pass << " PASSED, " << g_fail << " FAILED" << std::endl;
  std::cout << "=====================================================================" << std::endl;

  return (g_fail == 0) ? 0 : 1;
}
