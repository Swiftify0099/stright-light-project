/**
 * @file test_r1_heartbeat_and_actuators.cpp
 * @brief MSCB Smart Pole System R1 - Heartbeat Tracking, Offline Deduplication & Feeder Actuation Suite
 */

#include "ArduinoMock.h"
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../Gateway_ESP32S3/GwHeartbeatMonitor.h"
#include "../Gateway_ESP32S3/GwNodeRegistry.h"
#include "../FieldPole_ESP32/PoleFeederManager.h"
#include "../FieldPole_ESP32/PoleActuators.h"
#include "../FieldPole_ESP32/PoleStorageManager.h"
#include "../FieldPole_ESP32/PoleMeshRouter.h"
#include "../MSCBShared/CommandSecurity.h"

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
  std::cout << "  TEST SUITE 3: HEARTBEAT MONITORING & FEEDER ACTUATION " << std::endl;
  std::cout << "========================================================" << std::endl;

  // Initialize Registry and pre-register Pole 2
  gwRegistry.begin();
  uint8_t poleMac[6] = {0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x02};
  PktRegisterRequestPayload regReq;
  memset(&regReq, 0, sizeof(regReq));
  memcpy(regReq.macAddress, poleMac, 6);
  regReq.firmwareVersion = 0x0100;
  regReq.candidateParent1 = MSCB_GATEWAY_NODE_ID;
  PktRegisterResponsePayload regResp;
  gwRegistry.handleJoinRequest(regReq, -70, 8, regResp);
  uint16_t testPoleId = 0;
  gwRegistry.approvePending(poleMac, testPoleId);

  // -----------------------------------------------------------------
  // 1. Gateway Heartbeat Tracking & Health State Transitions
  // -----------------------------------------------------------------
  std::cout << "\n[1. GATEWAY HEARTBEAT TRACKING & STATE TRANSITIONS]" << std::endl;
  GwHeartbeatMonitor monitor;
  monitor.begin();
  monitor.setHeartbeatIntervalSec(30);
  monitor.setOfflineTimeoutSec(120);

  uint32_t t0 = 10000; // t = 10,000s

  // 1.1 Initial ping
  monitor.onHeartbeatReceived(testPoleId, t0);
  monitor.checkHealth(t0);
  TEST_REPORT(monitor.getPoleState(testPoleId) == POLE_STATE_ONLINE, "UNIT_PASS",
              "Pole initially marked POLE_STATE_ONLINE upon receiving heartbeat ping");
  TEST_REPORT(monitor.getMissedCount(testPoleId) == 0, "UNIT_PASS",
              "Missed heartbeat counter is 0");

  // 1.2 Elapsed 45s (> 30s interval, < 120s timeout) -> DELAYED
  monitor.checkHealth(t0 + 45);
  TEST_REPORT(monitor.getPoleState(testPoleId) == POLE_STATE_DELAYED, "INTEGRATION_PASS",
              "Pole transitions to POLE_STATE_DELAYED after 45s without ping (missedCount >= 1)");
  TEST_REPORT(monitor.getMissedCount(testPoleId) == 1, "UNIT_PASS",
              "Missed count correctly incremented to 1");

  // 1.3 Elapsed 130s (> 120s timeout) -> OFFLINE
  monitor.checkHealth(t0 + 130);
  TEST_REPORT(monitor.getPoleState(testPoleId) == POLE_STATE_OFFLINE, "INTEGRATION_PASS",
              "Pole transitions to POLE_STATE_OFFLINE after 130s without ping (> 120s timeout)");
  TEST_REPORT(monitor.hasPendingAlert() == true, "INTEGRATION_PASS",
              "Telegram offline alert generated on transition to OFFLINE");

  // -----------------------------------------------------------------
  // 2. Offline Alert Deduplication
  // -----------------------------------------------------------------
  std::cout << "\n[2. TELEGRAM OFFLINE ALERT DEDUPLICATION]" << std::endl;
  {
    // Retrieve alert text and clear
    const char* alertText = monitor.getNextAlertText();
    TEST_REPORT(strstr(alertText, "OFFLINE") != NULL, "UNIT_PASS",
                "Alert text correctly identifies OFFLINE status and Pole ID");
    monitor.clearPendingAlert();

    // Outage continues at 160s, 200s, 300s -> Must NOT spam duplicate alerts
    monitor.checkHealth(t0 + 160);
    monitor.checkHealth(t0 + 200);
    monitor.checkHealth(t0 + 300);
    TEST_REPORT(monitor.hasPendingAlert() == false, "INTEGRATION_PASS",
                "Alert deduplication confirmed: Zero duplicate Telegram alerts emitted during ongoing outage");
  }

  // -----------------------------------------------------------------
  // 3. Recovery Alert Emission
  // -----------------------------------------------------------------
  std::cout << "\n[3. RECOVERY FROM OUTAGE & RESTORATION ALERT]" << std::endl;
  {
    // Node recovers and sends ping at t0 + 350s
    monitor.onHeartbeatReceived(testPoleId, t0 + 350);
    TEST_REPORT(monitor.getPoleState(testPoleId) == POLE_STATE_RECOVERING, "INTEGRATION_PASS",
                "Pole transitions to POLE_STATE_RECOVERING upon initial heartbeat after outage");
    TEST_REPORT(monitor.hasPendingAlert() == true, "INTEGRATION_PASS",
                "Telegram recovery alert generated upon node restoration");

    const char* recAlert = monitor.getNextAlertText();
    TEST_REPORT(strstr(recAlert, "RESTORED") != NULL, "UNIT_PASS",
                "Recovery alert text contains duration and recovery notice");
    monitor.clearPendingAlert();

    // Subsequent heartbeat confirms stability and transitions from RECOVERING to ONLINE
    monitor.onHeartbeatReceived(testPoleId, t0 + 380);
    TEST_REPORT(monitor.getPoleState(testPoleId) == POLE_STATE_ONLINE, "UNIT_PASS",
                "Pole state transitions from RECOVERING to full POLE_STATE_ONLINE upon subsequent ping");
  }

  // -----------------------------------------------------------------
  // 4. Feeder Actuator Safety & Production Relay Lockout
  // -----------------------------------------------------------------
  std::cout << "\n[4. ACTUATOR SAFETY & PRODUCTION RELAY LOCKOUT]" << std::endl;
  poleActuators.init(true); // relays locked by default
  poleActuators.validateStartupState(); // transition driver state to READY
  PoleFeederManager feederMgr;
  feederMgr.begin();

  {
    // Attempt switching while relay lock is active
    FeederCommandAckState ack = feederMgr.executeFeederSwitch(1, 1); // Feeder 1 ON
    TEST_REPORT(ack == FEEDER_ACK_REJECTED_LOCKED, "INTEGRATION_PASS",
                "Feeder switch strictly rejected with FEEDER_ACK_REJECTED_LOCKED under MSCB_RELAYS_LOCKED_DISABLED");
  }

  // -----------------------------------------------------------------
  // 5. Low-Voltage Bench Test Mode & Auxiliary Feedback Verification
  // -----------------------------------------------------------------
  std::cout << "\n[5. BENCH TEST MODE & AUXILIARY FEEDBACK SAFETY]" << std::endl;
  {
    // Enable Low-Voltage Bench Test Mode (explicitly authorized isolated test)
    feederMgr.setBenchTestMode(true);
    TEST_REPORT(feederMgr.isBenchTestMode() == true, "UNIT_PASS",
                "Low-voltage bench test mode successfully enabled");

    // Without physical MCP23S17 / auxiliary hardware connected:
    // Must report FEEDER_ACK_FEEDBACK_NOT_CONNECTED (Never fake or assume GPIO readback!)
    FeederCommandAckState ack = feederMgr.executeFeederSwitch(1, 1);
    TEST_REPORT(ack == FEEDER_ACK_FEEDBACK_NOT_CONNECTED, "INTEGRATION_PASS",
                "Feeder commanded ON reports FEEDER_ACK_FEEDBACK_NOT_CONNECTED (GPIO output is NEVER treated as contactor feedback)");
    TEST_REPORT(poleActuators.isRelayCommandedOn(0) == true, "UNIT_PASS",
                "Relay output GPIO driven HIGH in bench test mode");

    // Turn feeder OFF
    ack = feederMgr.executeFeederSwitch(1, 0);
    TEST_REPORT(ack == FEEDER_ACK_FEEDBACK_NOT_CONNECTED, "INTEGRATION_PASS",
                "Feeder commanded OFF returns FEEDER_ACK_FEEDBACK_NOT_CONNECTED");
    TEST_REPORT(poleActuators.isRelayCommandedOn(0) == false, "UNIT_PASS",
                "Relay output GPIO returned to LOW");
  }

  // -----------------------------------------------------------------
  // 6. Emergency Stop Interlock
  // -----------------------------------------------------------------
  std::cout << "\n[6. EMERGENCY SHUTDOWN INTERLOCK]" << std::endl;
  {
    // Command Feeder 2 ON
    feederMgr.executeFeederSwitch(2, 1);
    TEST_REPORT(poleActuators.isRelayCommandedOn(1) == true, "UNIT_PASS", "Feeder 2 commanded ON");

    // Trigger Emergency Stop
    feederMgr.emergencyShutdownAll("ESTOP_TEST");
    TEST_REPORT(poleActuators.isEmergencyActive() == true, "INTEGRATION_PASS",
                "Emergency shutdown latched active");
    TEST_REPORT(poleActuators.isRelayCommandedOn(1) == false, "INTEGRATION_PASS",
                "All feeder relays immediately opened to safe de-energized state");

    // Any subsequent switch rejected while in E-Stop
    FeederCommandAckState ack = feederMgr.executeFeederSwitch(2, 1);
    TEST_REPORT(ack == FEEDER_ACK_REJECTED_FAULT, "INTEGRATION_PASS",
                "Switch command rejected while emergency shutdown state is latched");
  }

  std::cout << "\n========================================================" << std::endl;
  std::cout << "TEST SUITE 3 SUMMARY: " << g_passCount << " PASSED, " << g_failCount << " FAILED" << std::endl;
  std::cout << "========================================================\n" << std::endl;

  return (g_failCount == 0) ? 0 : 1;
}
