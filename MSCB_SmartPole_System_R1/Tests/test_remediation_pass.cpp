/**
 * @file test_remediation_pass.cpp
 * @brief MSCB Smart Pole System R1 - First Controlled Remediation Pass Verification Suite
 * 
 * Tests:
 * 1. Relay Safety & MSCB_RELAYS_LOCKED_DISABLED Enforcement
 * 2. ADC Safety, Zero-Simulation Prevention & Stale Frame Rejection
 * 3. Authoritative 24-Raw / 21-Active Channel Metrology Mapping & Per-Channel Calibration
 * 4. Fail-Closed Cryptographic Command Authentication & Anti-Replay Nonce Verification
 */

#include "ArduinoMock.h"
#include "../FieldPole_ESP32/PoleActuators.h"
#include "../FieldPole_ESP32/PoleMetrology.h"
#include "../MSCBShared/CommandSecurity.h"
#include "../MSCBShared/ReplayProtection.h"

MockSerial Serial;

static int g_passedTests = 0;
static int g_failedTests = 0;

#define TEST_ASSERT(cond, msg) \
  do { \
    if (cond) { \
      std::cout << "  [PASS] " << msg << std::endl; \
      g_passedTests++; \
    } else { \
      std::cout << "  [FAIL] " << msg << " (Line " << __LINE__ << ")" << std::endl; \
      g_failedTests++; \
    } \
  } while (0)

void test_section_relay_safety() {
  std::cout << "\n========================================================" << std::endl;
  std::cout << "TEST SECTION 1: RELAY SAFETY & ACTUATOR LOCKOUT" << std::endl;
  std::cout << "========================================================" << std::endl;

  PoleActuators actuators;
  actuators.init(true);

  // 1. Verify lockout is enabled by default (MSCB_RELAYS_LOCKED_DISABLED)
  TEST_ASSERT(actuators.areRelaysLockedDisabled() == true,
              "actuators.areRelaysLockedDisabled() must be TRUE under MSCB_RELAYS_LOCKED_DISABLED");

  // 2. Attempt to request relay ON while locked
  bool resOn = actuators.requestRelayOn(0, "TEST_ON");
  TEST_ASSERT(resOn == false, "requestRelayOn(0) must return FALSE when lock is active");
  TEST_ASSERT(actuators.isRelayCommandedOn(0) == false, "Relay commandedOn state must remain FALSE");

  // 3. Attempt toggle while locked
  bool resToggle = actuators.toggleRelay(0, "TEST_TOGGLE");
  TEST_ASSERT(resToggle == false, "toggleRelay(0) must return FALSE when lock is active");

  // 4. Validate startup state
  bool diagPassed = actuators.validateStartupState();
  TEST_ASSERT(diagPassed == true, "validateStartupState() succeeds on clean initial low states");
  TEST_ASSERT(actuators.getDriverState() == ACTUATOR_STATE_READY, "Driver state transitions to READY");

  // 5. Emergency shutdown interlock
  actuators.emergencyShutdownAll("TEST_ESTOP");
  TEST_ASSERT(actuators.isEmergencyActive() == true, "Emergency shutdown latches active state");
  TEST_ASSERT(actuators.getDriverState() == ACTUATOR_STATE_EMERGENCY_STOP, "Driver state is EMERGENCY_STOP");
  TEST_ASSERT(actuators.requestRelayOn(0) == false, "Relay ON rejected under emergency shutdown");

  actuators.resetEmergencyState();
  TEST_ASSERT(actuators.isEmergencyActive() == false, "Emergency state cleared");
}

void test_section_adc_safety_and_metrology() {
  std::cout << "\n========================================================" << std::endl;
  std::cout << "TEST SECTION 2: ADC SAFETY, ZERO-SIMULATION & METROLOGY" << std::endl;
  std::cout << "========================================================" << std::endl;

  PoleMetrology metrology;

  // 1. Verify Hardware Detection Failure (Zero-Simulation Mandate)
  bool det = metrology.detectHardware();
  TEST_ASSERT(det == false, "detectHardware() returns FALSE when physical ADC chips are missing");
  TEST_ASSERT(metrology.areAllAdcsDetected() == false, "areAllAdcsDetected() is FALSE");
  TEST_ASSERT(metrology.getChannelState(1) == CHANNEL_ADC_NOT_CONNECTED,
              "Channel 1 reports CHANNEL_ADC_NOT_CONNECTED when hardware absent");
  TEST_ASSERT(metrology.getChannelValue(1) == 0.0f,
              "Channel 1 RMS value is 0.0f (strictly no simulated sine wave!)");

  // 2. Authoritative Physical 24-Raw-Channel Mapping Verification
  std::cout << "\n  Verifying Authoritative 24-Raw-Channel Physical Allocation:" << std::endl;
  // Bus Voltages R, Y, B on ADC3 Ch 3, 4, 5
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[19].logicalChId == 1 && AUTHORITATIVE_ADC_MAP[19].chipIndex == 2 && AUTHORITATIVE_ADC_MAP[19].chipChannel == 3,
              "Raw index 19 maps to Bus Voltage Phase R on ADC3 Ch 3");
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[20].logicalChId == 2 && AUTHORITATIVE_ADC_MAP[20].chipIndex == 2 && AUTHORITATIVE_ADC_MAP[20].chipChannel == 4,
              "Raw index 20 maps to Bus Voltage Phase Y on ADC3 Ch 4");
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[21].logicalChId == 3 && AUTHORITATIVE_ADC_MAP[21].chipIndex == 2 && AUTHORITATIVE_ADC_MAP[21].chipChannel == 5,
              "Raw index 21 maps to Bus Voltage Phase B on ADC3 Ch 5");

  // Feeder 1 on ADC1 Ch 0, 1, 2
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[0].logicalChId == 4 && AUTHORITATIVE_ADC_MAP[0].chipIndex == 0 && AUTHORITATIVE_ADC_MAP[0].chipChannel == 0,
              "Raw index 0 maps to Feeder 1 Phase R on ADC1 Ch 0");
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[1].logicalChId == 5 && AUTHORITATIVE_ADC_MAP[1].chipIndex == 0 && AUTHORITATIVE_ADC_MAP[1].chipChannel == 1,
              "Raw index 1 maps to Feeder 1 Phase Y on ADC1 Ch 1");
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[2].logicalChId == 6 && AUTHORITATIVE_ADC_MAP[2].chipIndex == 0 && AUTHORITATIVE_ADC_MAP[2].chipChannel == 2,
              "Raw index 2 maps to Feeder 1 Phase B on ADC1 Ch 2");

  // Balanced Spares (ADC2 Ch 7, ADC3 Ch 6, 7)
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[15].isSpare == true && AUTHORITATIVE_ADC_MAP[15].chipIndex == 1 && AUTHORITATIVE_ADC_MAP[15].chipChannel == 7,
              "Raw index 15 is Spare 1 (ADC2 Ch 7 tied to AGND)");
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[22].isSpare == true && AUTHORITATIVE_ADC_MAP[22].chipIndex == 2 && AUTHORITATIVE_ADC_MAP[22].chipChannel == 6,
              "Raw index 22 is Spare 2 (ADC3 Ch 6 tied to AGND)");
  TEST_ASSERT(AUTHORITATIVE_ADC_MAP[23].isSpare == true && AUTHORITATIVE_ADC_MAP[23].chipIndex == 2 && AUTHORITATIVE_ADC_MAP[23].chipChannel == 7,
              "Raw index 23 is Spare 3 (ADC3 Ch 7 tied to AGND)");

  // 3. Zero-Frame / Dead-Bus Rejection
  int32_t zeroFrame[24] = {0};
  bool zeroRes = metrology.processRawAdcFrame(zeroFrame, 1000);
  TEST_ASSERT(zeroRes == false, "processRawAdcFrame() rejects all-zero frame");
  TEST_ASSERT(metrology.getChannelState(1) == CHANNEL_ZERO_FRAME_REJECTED,
              "Channel state marked CHANNEL_ZERO_FRAME_REJECTED");

  // 4. Class 1.0 Accuracy Disclaimer
  TEST_ASSERT(metrology.isClass1_0Certified() == false,
              "Class 1.0 certification is strictly FALSE without laboratory test evidence");

  // 5. Per-Channel Calibration Verification
  metrology.setChannelCalibration(1, 0.001f, 1.002f, 400.0f, 0.5f);
  const ChannelCalibRecord &cal1 = metrology.getChannelCalibration(1);
  TEST_ASSERT(cal1.isValid == true, "Channel 1 calibration marked valid after trim");
  TEST_ASSERT(std::abs(cal1.gainMultiplier - 1.002f) < 0.0001f, "Gain multiplier applied correctly");
}

void test_section_cryptographic_security() {
  std::cout << "\n========================================================" << std::endl;
  std::cout << "TEST SECTION 3: CRYPTOGRAPHIC SECURITY & FAIL-CLOSED AUTH" << std::endl;
  std::cout << "========================================================" << std::endl;

  CommandSecurity sec;
  PersistentNonceRecord b0, b1;
  replayProtector.reprovisionStorage(1, &b0, &b1);

  AuthenticatedCommandEnvelope env;

  // 1. Fail-Closed Check: No provisioned key
  TEST_ASSERT(sec.isKeyProvisioned() == false, "Key is NOT provisioned initially");
  bool createNoKey = sec.createCommand(0x0001, 0x0002, 1, 0x20, 1, 1800000000UL, env);
  TEST_ASSERT(createNoKey == false, "createCommand() FAILS CLOSED when no key is provisioned");

  CommandAuthResult verifyNoKey = sec.verifyCommand(env, 0x0002, 1750000000UL);
  TEST_ASSERT(verifyNoKey == AUTH_ERR_NO_PROVISIONED_KEY,
              "verifyCommand() FAILS CLOSED with AUTH_ERR_NO_PROVISIONED_KEY");

  // 2. Key Provisioning
  uint8_t testKey[32];
  for (int i = 0; i < 32; i++) testKey[i] = (uint8_t)(i * 7 + 0x33);
  bool provOk = sec.provisionKey(testKey, 32, 1);
  TEST_ASSERT(provOk == true, "provisionKey() successfully provisions 32-byte key");
  TEST_ASSERT(sec.isKeyProvisioned() == true, "isKeyProvisioned() is now TRUE");

  // 3. Successful Envelope Creation & Verification
  uint32_t expirySec = 1800000000UL;
  uint32_t currentSec = 1750000000UL;
  bool createOk = sec.createCommand(0x0001, 0x0002, 1, 0x20, 1, expirySec, env);
  TEST_ASSERT(createOk == true, "createCommand() succeeds with provisioned key and persistent nonce");

  CommandAuthResult vOk = sec.verifyCommand(env, 0x0002, currentSec);
  TEST_ASSERT(vOk == AUTH_SUCCESS, "verifyCommand() authenticates valid envelope with AUTH_SUCCESS");

  // 4. Replay Attack Prevention
  CommandAuthResult vReplay = sec.verifyCommand(env, 0x0002, currentSec);
  TEST_ASSERT(vReplay == AUTH_ERR_REPLAY_DETECTED, "Replay of same envelope is REJECTED");

  // 5. Expiry Check (Current time > Expiry timestamp)
  AuthenticatedCommandEnvelope envExpired;
  sec.createCommand(0x0001, 0x0002, 2, 0x20, 0, expirySec, envExpired);
  CommandAuthResult vExpired = sec.verifyCommand(envExpired, 0x0002, expirySec + 10);
  TEST_ASSERT(vExpired == AUTH_ERR_COMMAND_EXPIRED, "Expired envelope is rejected with AUTH_ERR_COMMAND_EXPIRED");

  // 6. Envelope Tampering / HMAC Integrity Check
  AuthenticatedCommandEnvelope envTampered;
  sec.createCommand(0x0001, 0x0002, 3, 0x20, 1, expirySec, envTampered);
  envTampered.feederId = 4; // Tamper with feeder ID!
  CommandAuthResult vTampered = sec.verifyCommand(envTampered, 0x0002, currentSec);
  TEST_ASSERT(vTampered == AUTH_ERR_HMAC_INVALID, "Tampered envelope rejected with AUTH_ERR_HMAC_INVALID");

  // 7. Network ID Mismatch Check
  AuthenticatedCommandEnvelope envWrongNet;
  sec.createCommand(0x0001, 0x0002, 4, 0x20, 1, expirySec, envWrongNet);
  envWrongNet.networkId = 0x1234; // Wrong network ID!
  CommandAuthResult vNet = sec.verifyCommand(envWrongNet, 0x0002, currentSec);
  TEST_ASSERT(vNet == AUTH_ERR_NETWORK_ID_MISMATCH, "Wrong network ID rejected with AUTH_ERR_NETWORK_ID_MISMATCH");

  // 8. Key Revocation (Fail-Closed)
  sec.revokeKey();
  TEST_ASSERT(sec.isKeyProvisioned() == false, "revokeKey() clears key and sets provisioned to FALSE");
  TEST_ASSERT(sec.verifyCommand(env, 0x0002, currentSec) == AUTH_ERR_NO_PROVISIONED_KEY,
              "After revocation, verifyCommand() immediately fails closed");
}

int main() {
  std::cout << "========================================================" << std::endl;
  std::cout << " MSCB SYSTEM R1 - FIRST CONTROLLED REMEDIATION TEST SUITE" << std::endl;
  std::cout << "========================================================" << std::endl;

  test_section_relay_safety();
  test_section_adc_safety_and_metrology();
  test_section_cryptographic_security();

  std::cout << "\n========================================================" << std::endl;
  std::cout << "REMEDIATION PASS TEST SUMMARY:" << std::endl;
  std::cout << "  Passed : " << g_passedTests << std::endl;
  std::cout << "  Failed : " << g_failedTests << std::endl;
  std::cout << "========================================================" << std::endl;

  return (g_failedTests == 0) ? 0 : 1;
}
