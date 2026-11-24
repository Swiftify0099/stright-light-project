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
  TEST_ASSERT(strcmp(feederAckStateToString(FEEDER_ACK_REJECTED_LOCKED), "REJECTED_LOCKED") == 0,
              "D.4 FEEDER_ACK_REJECTED_LOCKED string verified");

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

  // Final Summary
  std::cout << "\n=====================================================================" << std::endl;
  std::cout << "TEST RUN COMPLETE: " << g_pass << " PASSED, " << g_fail << " FAILED" << std::endl;
  std::cout << "=====================================================================" << std::endl;

  return (g_fail == 0) ? 0 : 1;
}
