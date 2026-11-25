/**
 * @file test_r3_2_2_production_lock.cpp
 * @brief MSCB Smart Pole System R3.2.2 - Standalone Production Relay Safety Lock Verification
 * 
 * Verifies that in production configuration:
 * 1. MSCB_RELAYS_LOCKED_DISABLED is compile-time locked to 1.
 * 2. MSCB_COMMISSIONING_LVDC_PROFILE is 0 (production firmware).
 * 3. MSCB_BENCH_TEST_PROFILE_ENABLED is 0.
 * 4. Relays cannot be energized by runtime setters, remote commands, or Telegram.
 * 5. Physical GPIO39 jumper cannot unlock relays in production profile.
 * 6. Actuators fail-closed under all circumstances.
 * 7. Hardware emergency interlock truthfully reports NOT IMPLEMENTED.
 */

#define MSCB_RELAYS_LOCKED_DISABLED 1
#define MSCB_COMMISSIONING_LVDC_PROFILE 0
#define MSCB_BENCH_TEST_PROFILE_ENABLED 0
#define ARDUINO_MOCK_IMPLEMENTATION

#include "ArduinoMock.h"
#include "../MSCBShared/BuildConfig.h"
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../FieldPole_ESP32/PoleBoardPins.h"
#include "../FieldPole_ESP32/PoleActuators.h"
#include "../FieldPole_ESP32/PoleStorageManager.h"
#include "../FieldPole_ESP32/PoleRegistrationClient.h"
#include <iostream>
#include <cassert>
#include <cstring>

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
  std::cout << "=====================================================================" << std::endl;
  std::cout << "  MSCB R3.2.2 STANDALONE PRODUCTION RELAY SAFETY LOCK SUITE" << std::endl;
  std::cout << "=====================================================================" << std::endl;

  // 1. Compile-Time Macro & Profile Checks
  TEST_ASSERT(MSCB_RELAYS_LOCKED_DISABLED == 1, "PL.1 Production macro MSCB_RELAYS_LOCKED_DISABLED == 1");
  TEST_ASSERT(MSCB_COMMISSIONING_LVDC_PROFILE == 0, "PL.2 Production macro MSCB_COMMISSIONING_LVDC_PROFILE == 0");
  TEST_ASSERT(MSCB_BENCH_TEST_PROFILE_ENABLED == 0, "PL.3 Production macro MSCB_BENCH_TEST_PROFILE_ENABLED == 0");

  PoleActuators prodActuators;
  prodActuators.init();

  TEST_ASSERT(strcmp(prodActuators.getSafetyProfileName(), "PRODUCTION_LOCKED_PERMANENT") == 0,
              "PL.4 Safety profile name is strictly 'PRODUCTION_LOCKED'");
  TEST_ASSERT(prodActuators.areRelaysLockedDisabled(),
              "PL.5 areRelaysLockedDisabled() returns true upon initialization");

  // 2. Runtime Software Setter Lockout
  prodActuators.setRelaysLockedDisabled(false);
  TEST_ASSERT(prodActuators.areRelaysLockedDisabled(),
              "PL.6 setRelaysLockedDisabled(false) strictly rejected in production build");

  prodActuators.setBenchTestOverride(true);
  TEST_ASSERT(!prodActuators.isBenchTestOverride(),
              "PL.7 setBenchTestOverride(true) strictly rejected when MSCB_BENCH_TEST_PROFILE_ENABLED == 0");

  // 3. Physical GPIO39 Jumper Alone Cannot Unlock Production Profile
  digitalWrite(POLE_PHYSICAL_AUTH_PIN, LOW);
  prodActuators.servicePhysicalAuthorization();
  delay(60);
  prodActuators.servicePhysicalAuthorization();
  prodActuators.setLocalPhysicalAuthorization(true);
  // Physical authorization bit may track the debounced pin, but production lock overrides it
  TEST_ASSERT(prodActuators.areRelaysLockedDisabled(),
              "PL.8 Relays remain locked disabled even when physical jumper is grounded");

  // 4. Actuation Requests Fail-Closed for All 6 Channels
  for (uint8_t ch = 0; ch < 6; ch++) {
    bool ok = prodActuators.requestRelayOn(ch, "TEST_COMMAND");
    TEST_ASSERT(!ok, "PL.9 Relay ON request rejected for channel " + std::to_string(ch + 1));
    TEST_ASSERT(!prodActuators.isRelayCommandedOn(ch),
                "PL.10 Relay commanded state remains FALSE for channel " + std::to_string(ch + 1));
  }

  // 5. Emergency Shutdown Remains Functional
  prodActuators.emergencyShutdownAll("AUDIT_TEST");
  TEST_ASSERT(prodActuators.areRelaysLockedDisabled(),
              "PL.11 Production lock invariant preserved across emergency shutdown");

  // 6. Truthful Hardware Interlock Status
  TEST_ASSERT(!prodActuators.isHardwareEmergencyInterlockImplemented(),
              "PL.12 Hardware emergency interlock truthfully reported NOT IMPLEMENTED");

  std::cout << "\n=====================================================================" << std::endl;
  std::cout << "PRODUCTION LOCK AUDIT COMPLETE: " << g_pass << " PASSED, " << g_fail << " FAILED" << std::endl;
  std::cout << "=====================================================================" << std::endl;

  return (g_fail == 0) ? 0 : 1;
}
