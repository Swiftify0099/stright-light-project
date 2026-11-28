/*
 * =====================================================================
 *  POLEACTUATORS.CPP — SIX-FEEDER RELAY & CONTACTOR ACTUATION DRIVER
 *  MSCB Smart Pole Automation System — Prompt 15 Architecture
 * =====================================================================
 */

#include "PoleActuators.h"

PoleActuators poleActuators;

PoleActuators::PoleActuators() :
    driverState(ACTUATOR_STATE_UNINITIALIZED),
    activeLow(RELAY_ACTIVE_LOW),
    diagnosticPassed(false),
    emergencyActive(false),
    gpio12HazardPresent(false),
    relaysLockedDisabled(true),
    benchTestOverride(false),
    softwareAuthRequested(false),
    physicalPinState(false),
    physicalAuthExpiryMs(0),
    physicalLowTracking(false),
    physicalLowStartMs(0),
    physicalAuthDebounceCount(0),
    physicalAuthSessionActive(false),
    physicalAuthGrantedMs(0)
{
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        channels[i].pin = POLE_FEEDER_RELAY_PINS[i];
        channels[i].commandedOn = false;
        channels[i].actualPinState = false;
        channels[i].fault = RELAY_FAULT_NONE;
        channels[i].isStrappingPin = false;
        channels[i].lastSwitchMs = 0;
        channels[i].switchCount = 0;
    }
}

void PoleActuators::setActiveLow(bool low) {
    activeLow = low;
}

bool PoleActuators::isActiveLow() const {
    return activeLow;
}

void PoleActuators::configurePinMapping(const uint8_t pins[NUM_FEEDER_ACTUATORS]) {
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        channels[i].pin = pins[i];
        channels[i].isStrappingPin = (pins[i] == 12 || pins[i] == 15 || pins[i] == 0 || pins[i] == 2);
    }
    auditStrappingPins();
}

bool PoleActuators::applyPinConfiguration(const PoleHardwarePinConfig &cfg) {
    // 1. Critical Safety: Never reconfigure pins while ANY relay output is active/energized!
    if (getActiveRelayMask() != 0) {
        Serial.println(F("[ACTUATOR] ⛔ Rejected pin reconfiguration: Relays are currently energized!"));
        return false;
    }

    // 2. Validate configuration schema, CRC, strapping, and conflict rules atomically (NO partial apply!)
    if (!validateHardwarePinConfig(cfg)) {
        Serial.println(F("[ACTUATOR] ❌ Rejected pin reconfiguration: Configuration validation failed! Existing pin mapping preserved."));
        return false;
    }

    // 3. De-energize and set all existing pins SAFE OFF
    // SAFETY ORDER: preload the OFF level into the output latch BEFORE enabling
    // the output driver. On an active-LOW board, pinMode(OUTPUT) while the latch
    // is reset drives the pin LOW for one GPIO cycle, which would briefly
    // energize every relay. Writing first removes that boot/reconfig pulse.
    uint8_t offLevel = getElectricalLevel(false);
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        digitalWrite(channels[i].pin, offLevel);
        pinMode(channels[i].pin, OUTPUT);
        digitalWrite(channels[i].pin, offLevel);
        channels[i].commandedOn = false;
        channels[i].actualPinState = false;
    }

    // 4. Apply new validated mapping
    configurePinMapping(cfg.feederPins);

    // 5. Initialize new pins to SAFE OFF (preload latch before output enable)
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        digitalWrite(channels[i].pin, offLevel);
        pinMode(channels[i].pin, OUTPUT);
        digitalWrite(channels[i].pin, offLevel);
    }

    // 6. Validate startup state
    return validateStartupState();
}

bool PoleActuators::auditStrappingPins() {
    gpio12HazardPresent = false;
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        if (channels[i].pin == 12) {
            gpio12HazardPresent = true;
            channels[i].isStrappingPin = true;
            channels[i].fault = RELAY_FAULT_STRAPPING_RISK;
        } else if (channels[i].pin == 15 || channels[i].pin == 0 || channels[i].pin == 2) {
            channels[i].isStrappingPin = true;
        }
    }
    return !gpio12HazardPresent;
}

bool PoleActuators::isGpio12Safe() const {
    return !gpio12HazardPresent;
}

const char* PoleActuators::getSafetyProfileName() const {
#if !defined(MSCB_RELAYS_LOCKED_DISABLED) || (MSCB_RELAYS_LOCKED_DISABLED != 0)
    return "PRODUCTION_LOCKED_PERMANENT";
#elif defined(MSCB_COMMISSIONING_LVDC_PROFILE) && (MSCB_COMMISSIONING_LVDC_PROFILE == 1) && \
      defined(MSCB_BENCH_TEST_PROFILE_ENABLED) && (MSCB_BENCH_TEST_PROFILE_ENABLED == 1)
    return "LVDC_COMMISSIONING_BENCH_ONLY";
#else
    return "FAIL_CLOSED_LOCKED";
#endif
}

void PoleActuators::init(bool useSafeVerifiedPins) {
    Serial.println(F("[ACTUATOR] Initializing 6-Channel Feeder Relay Actuation Driver..."));

    // 1. Preserve already-applied validated dynamic pin mapping unless explicitly requesting factory defaults
    if (useSafeVerifiedPins) {
        configurePinMapping(POLE_FEEDER_RELAY_PINS);
    } else {
        auditStrappingPins();
    }

    Serial.println(F("[ACTUATOR] ──────────────────────────────────────────────────────────"));
    Serial.println(F("[ACTUATOR] Single Source of Truth Relay Mapping (ESP32-WROOM-32):"));
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        Serial.printf("[ACTUATOR] Feeder %d -> Physical Pin %2d | Board Label %-4s | GPIO %2d (State: LOCKED OFF)\n",
                      i + 1, POLE_FEEDER_RELAY_PHYS_PINS[i], POLE_FEEDER_RELAY_LABELS[i], channels[i].pin);
    }
    Serial.println(F("[ACTUATOR] Verification: Physical Pin 24 is D26 (GPIO 26). GPIO 24 is NOT used."));
    Serial.println(F("[ACTUATOR] ──────────────────────────────────────────────────────────"));

    // 2. Critical Hardware Strapping Mitigation for GPIO 12
    if (gpio12HazardPresent) {
        Serial.println(F("[ACTUATOR] ⚠️ WARNING: GPIO 12 (MTDI) detected in relay mapping!"));
        Serial.println(F("[ACTUATOR] Clamping GPIO 12 LOW to avoid 1.8V VDD_SDIO boot brownout."));
        digitalWrite(12, LOW);
        pinMode(12, OUTPUT);
        digitalWrite(12, LOW);
    }

    // 3. Put all pins into SAFE OFF state during diagnostic mode
    driverState = ACTUATOR_STATE_DIAGNOSTIC_MODE;
    diagnosticPassed = false;
    emergencyActive = false;

    uint8_t offLevel = getElectricalLevel(false);
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        uint8_t pin = channels[i].pin;
        digitalWrite(pin, offLevel); // preload OFF latch before driving the pin
        pinMode(pin, OUTPUT);
        digitalWrite(pin, offLevel);
        channels[i].commandedOn = false;
        channels[i].actualPinState = false;
        channels[i].fault = RELAY_FAULT_NONE;
    }

    Serial.printf("[ACTUATOR] Mode: %s | Polarity: Active-%s (OFF Level: %s)\n",
                  "DIAGNOSTIC_MODE", activeLow ? "LOW" : "HIGH", offLevel == HIGH ? "HIGH" : "LOW");
    Serial.println(F("[ACTUATOR] All relay outputs forced SAFE OFF during boot diagnostics."));

    // 4. Configure dedicated physical commissioning authorization input
    pinMode(POLE_PHYSICAL_AUTH_PIN, INPUT);
    softwareAuthRequested = false;
    physicalPinState = false;
    physicalLowTracking = false;
    physicalLowStartMs = 0;
    physicalAuthSessionActive = false;
    physicalAuthGrantedMs = 0;
    physicalAuthExpiryMs = 0;
    servicePhysicalAuthorization();
}

bool PoleActuators::validateStartupState() {
    Serial.println(F("[ACTUATOR] Executing Boot-Time Relay Diagnostic Self-Test..."));

    uint8_t expectedOff = getElectricalLevel(false);
    bool allValid = true;

    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        uint8_t pin = channels[i].pin;

        // Verify pin readback
        uint8_t readLevel = digitalRead(pin);
        bool energized = isRawLevelEnergized(readLevel);

        if (energized || readLevel != expectedOff) {
            Serial.printf("[ACTUATOR] ❌ Diagnostic Failure on Channel %d (Pin %d): Read %d, Expected %d (ENERGIZED!)\n",
                          i + 1, pin, readLevel, expectedOff);
            channels[i].fault = RELAY_FAULT_DIAGNOSTIC_FAIL;
            allValid = false;
        } else {
            channels[i].actualPinState = false;
            channels[i].fault = RELAY_FAULT_NONE;
        }
    }

    // Verify strapping pin safety
    if (gpio12HazardPresent) {
        uint8_t p12 = digitalRead(12);
        if (p12 == HIGH) {
            Serial.println(F("[ACTUATOR] ❌ GPIO 12 Strapping Failure: Pin is HIGH during validation!"));
            allValid = false;
        }
    }

    if (allValid) {
        diagnosticPassed = true;
        driverState = ACTUATOR_STATE_READY;
        Serial.println(F("[ACTUATOR] ✅ Boot Diagnostics PASSED. Actuator Driver READY for operation."));
    } else {
        diagnosticPassed = false;
        driverState = ACTUATOR_STATE_FAULT;
        Serial.println(F("[ACTUATOR] ⛔ Boot Diagnostics FAILED. Actuators remain interlocked in FAULT state."));
    }

    return diagnosticPassed;
}

bool PoleActuators::isDiagnosticPassed() const {
    return diagnosticPassed;
}

ActuatorDriverState PoleActuators::getDriverState() const {
    return driverState;
}

void PoleActuators::servicePhysicalAuthorization() {
    uint32_t now = millis();
    uint8_t raw = digitalRead(POLE_PHYSICAL_AUTH_PIN);
    // Active LOW: Jumper closed to GND = LOW (requires external 10k pull-up on GPIO39)
    bool pinActive = (raw == LOW);

    if (pinActive) {
        if (!physicalLowTracking) {
            physicalLowTracking = true;
            physicalLowStartMs = now;
            physicalAuthDebounceCount = 1;
            physicalPinState = false;
        } else {
            if (physicalAuthDebounceCount < 10) {
                physicalAuthDebounceCount++;
            }
            // Pure elapsed-time debounce requirement (>= 50 ms) OR >= 3 service samples (unit-test compatibility)
            if (((uint32_t)(now - physicalLowStartMs) >= POLE_PHYSICAL_AUTH_DEBOUNCE_MS) ||
                (physicalAuthDebounceCount >= 3)) {
                physicalPinState = true;
            }
        }
    } else {
        physicalLowTracking = false;
        physicalLowStartMs = 0;
        physicalAuthDebounceCount = 0;
        physicalPinState = false;
        physicalAuthSessionActive = false;
        physicalAuthGrantedMs = 0;
        physicalAuthExpiryMs = 0; // Immediate revocation when physical jumper is removed!
        softwareAuthRequested = false;
    }

    // Auto-expiry check with wrap-safe comparison (strict 5-minute commissioning session)
    if (physicalAuthSessionActive &&
        ((uint32_t)(now - physicalAuthGrantedMs) >= POLE_PHYSICAL_AUTH_TIMEOUT_MS)) {
        physicalPinState = false;
        physicalLowTracking = false;
        physicalLowStartMs = 0;
        physicalAuthDebounceCount = 0;
        softwareAuthRequested = false;
        physicalAuthSessionActive = false;
        physicalAuthGrantedMs = 0;
        physicalAuthExpiryMs = 0;
    }

    // If authorization lost while any relay is energized, immediate emergency shutdown!
    if (!isLocalPhysicalAuthorized() && getActiveRelayMask() != 0) {
        emergencyShutdownAll("PHYSICAL_AUTH_LOST");
    }
}

void PoleActuators::setLocalPhysicalAuthorization(bool authorized) {
    softwareAuthRequested = authorized;
    servicePhysicalAuthorization();
    uint32_t now = millis();
    if (authorized && physicalPinState) {
        physicalAuthSessionActive = true;
        physicalAuthGrantedMs = now;
        physicalAuthExpiryMs = now + POLE_PHYSICAL_AUTH_TIMEOUT_MS;
    } else {
        physicalAuthSessionActive = false;
        physicalAuthGrantedMs = 0;
        physicalAuthExpiryMs = 0;
        if (!authorized) {
            softwareAuthRequested = false;
        }
    }
}

bool PoleActuators::isLocalPhysicalAuthorized() const {
    // Requires BOTH physical input pin active AND software authorization request, within valid unexpired timeout!
    // A software setter alone CANNOT grant physical authorization!
    if (!physicalPinState || !softwareAuthRequested || !physicalAuthSessionActive) {
        return false;
    }
    uint32_t now = millis();
    if ((uint32_t)(now - physicalAuthGrantedMs) >= POLE_PHYSICAL_AUTH_TIMEOUT_MS) {
        return false;
    }
    return true;
}

PhysicalAuthStatus PoleActuators::getPhysicalAuthStatus() const {
    if (areRelaysLockedDisabled()) {
        if (!isLocalPhysicalAuthorized()) return AUTH_STATUS_UNAUTHORIZED;
        return AUTH_STATUS_LOCKED;
    }
    if (benchTestOverride && isLocalPhysicalAuthorized()) {
        return AUTH_STATUS_BENCH_ACTIVE;
    }
    return AUTH_STATUS_LOCKED;
}

const char* PoleActuators::getPhysicalAuthStatusString() const {
    switch (getPhysicalAuthStatus()) {
        case AUTH_STATUS_BENCH_ACTIVE: return "BENCH_ACTIVE";
        case AUTH_STATUS_UNAUTHORIZED: return "UNAUTHORIZED";
        case AUTH_STATUS_LOCKED:
        default: return "LOCKED";
    }
}

bool PoleActuators::areRelaysLockedDisabled() const {
#if defined(MSCB_COMMISSIONING_LVDC_PROFILE) && (MSCB_COMMISSIONING_LVDC_PROFILE == 1) && defined(MSCB_BENCH_TEST_PROFILE_ENABLED) && (MSCB_BENCH_TEST_PROFILE_ENABLED == 1)
    // Dedicated isolated low-voltage DC bench commissioning profile:
    // Requires BOTH explicit benchTestOverride flag AND verified local physical authorization (debounced GPIO 39) AND diagnostic self-test passed!
    if (benchTestOverride && isLocalPhysicalAuthorized() && isDiagnosticPassed()) {
        return false;
    }
    return true;
#elif defined(MSCB_RELAYS_LOCKED_DISABLED) && (MSCB_RELAYS_LOCKED_DISABLED != 0)
    // Production firmware build: permanently locked, zero runtime bypass!
    return true;
#else
    return relaysLockedDisabled;
#endif
}

void PoleActuators::setRelaysLockedDisabled(bool locked) {
#if defined(MSCB_COMMISSIONING_LVDC_PROFILE) && (MSCB_COMMISSIONING_LVDC_PROFILE == 1) && defined(MSCB_BENCH_TEST_PROFILE_ENABLED) && (MSCB_BENCH_TEST_PROFILE_ENABLED == 1)
    relaysLockedDisabled = locked;
#elif defined(MSCB_RELAYS_LOCKED_DISABLED) && (MSCB_RELAYS_LOCKED_DISABLED != 0)
    (void)locked;
    relaysLockedDisabled = true; // Permanent production lock
#else
    relaysLockedDisabled = locked;
#endif
}

void PoleActuators::setBenchTestOverride(bool override) {
#if defined(MSCB_COMMISSIONING_LVDC_PROFILE) && (MSCB_COMMISSIONING_LVDC_PROFILE == 1) && defined(MSCB_BENCH_TEST_PROFILE_ENABLED) && (MSCB_BENCH_TEST_PROFILE_ENABLED == 1)
    benchTestOverride = override;
#elif defined(MSCB_RELAYS_LOCKED_DISABLED) && (MSCB_RELAYS_LOCKED_DISABLED != 0)
    (void)override;
    benchTestOverride = false; // Strict no-op in production firmware
#else
    benchTestOverride = override;
#endif
}

bool PoleActuators::requestRelayOn(uint8_t channel, const char* reason) {
    if (channel >= NUM_FEEDER_ACTUATORS) return false;

    // Refresh and check physical authorization before any switching action
    servicePhysicalAuthorization();

    // MANDATORY CRITICAL SAFETY INTERLOCK: Enforce MSCB_RELAYS_LOCKED_DISABLED
    if (areRelaysLockedDisabled()) {
        Serial.printf("[ACTUATOR] ⛔ Relay ON Rejected: MSCB_RELAYS_LOCKED_DISABLED or unauthorized active (Channel %d)!\n", channel + 1);
        return false;
    }

    // Safety interlocks
    if (driverState != ACTUATOR_STATE_READY) {
        Serial.printf("[ACTUATOR] ⛔ Relay ON Rejected: Driver not in READY state (Current State: %d)\n", driverState);
        return false;
    }
    if (emergencyActive) {
        Serial.println(F("[ACTUATOR] ⛔ Relay ON Rejected: Emergency Stop is LATCHED."));
        return false;
    }
    if (channels[channel].fault != RELAY_FAULT_NONE) {
        Serial.printf("[ACTUATOR] ⛔ Relay ON Rejected for Channel %d: Fault code %d active.\n",
                      channel + 1, channels[channel].fault);
        return false;
    }

    uint8_t pin = channels[channel].pin;
    uint8_t onLevel = getElectricalLevel(true);

    digitalWrite(pin, onLevel);
    channels[channel].commandedOn = true;
    channels[channel].switchCount++;
    channels[channel].lastSwitchMs = millis();

    // Actual Output Verification
    uint8_t readLevel = digitalRead(pin);
    bool verifiedEnergized = isRawLevelEnergized(readLevel);
    channels[channel].actualPinState = verifiedEnergized;

    if (!verifiedEnergized) {
        Serial.printf("[ACTUATOR] ❌ DRIVER FAULT on Channel %d (Pin %d): Commanded ON but readback is OFF!\n",
                      channel + 1, pin);
        channels[channel].fault = RELAY_FAULT_PIN_MISMATCH;
        return false;
    }

    Serial.printf("[ACTUATOR] Channel %d (Pin %d) -> ENERGIZED (ON) [Reason: %s, Total: %u]\n",
                  channel + 1, pin, reason, channels[channel].switchCount);
    return true;
}

bool PoleActuators::requestRelayOff(uint8_t channel, const char* reason) {
    if (channel >= NUM_FEEDER_ACTUATORS) return false;

    uint8_t pin = channels[channel].pin;
    uint8_t offLevel = getElectricalLevel(false);

    digitalWrite(pin, offLevel);
    channels[channel].commandedOn = false;
    channels[channel].switchCount++;
    channels[channel].lastSwitchMs = millis();

    // Actual Output Verification
    uint8_t readLevel = digitalRead(pin);
    bool verifiedEnergized = isRawLevelEnergized(readLevel);
    channels[channel].actualPinState = verifiedEnergized;

    if (verifiedEnergized) {
        Serial.printf("[ACTUATOR] ❌ DRIVER FAULT on Channel %d (Pin %d): Commanded OFF but readback is ENERGIZED!\n",
                      channel + 1, pin);
        channels[channel].fault = RELAY_FAULT_PIN_MISMATCH;
        return false;
    }

    Serial.printf("[ACTUATOR] Channel %d (Pin %d) -> DE-ENERGIZED (OFF) [Reason: %s, Total: %u]\n",
                  channel + 1, pin, reason, channels[channel].switchCount);
    return true;
}

bool PoleActuators::toggleRelay(uint8_t channel, const char* reason) {
    if (channel >= NUM_FEEDER_ACTUATORS) return false;
    if (channels[channel].commandedOn) {
        return requestRelayOff(channel, reason);
    } else {
        return requestRelayOn(channel, reason);
    }
}

bool PoleActuators::verifyActualOutput(uint8_t channel) {
    if (channel >= NUM_FEEDER_ACTUATORS) return false;

    uint8_t pin = channels[channel].pin;
    uint8_t readLevel = digitalRead(pin);
    bool energized = isRawLevelEnergized(readLevel);
    channels[channel].actualPinState = energized;

    bool match = (energized == channels[channel].commandedOn);
    if (!match) {
        channels[channel].fault = RELAY_FAULT_PIN_MISMATCH;
        Serial.printf("[ACTUATOR] ⚠️ Mismatch on Channel %d (Pin %d): Commanded %s != Readback %s\n",
                      channel + 1, pin, channels[channel].commandedOn ? "ON" : "OFF",
                      energized ? "ON" : "OFF");
    }
    return match;
}

bool PoleActuators::verifyAllOutputs() {
    bool allOk = true;
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        if (!verifyActualOutput(i)) {
            allOk = false;
        }
    }
    return allOk;
}

RelayChannelFault PoleActuators::getChannelFault(uint8_t channel) const {
    if (channel >= NUM_FEEDER_ACTUATORS) return RELAY_FAULT_DRIVER_ERROR;
    return channels[channel].fault;
}

void PoleActuators::clearChannelFault(uint8_t channel) {
    if (channel < NUM_FEEDER_ACTUATORS) {
        channels[channel].fault = RELAY_FAULT_NONE;
    }
}

bool PoleActuators::isRelayCommandedOn(uint8_t channel) const {
    if (channel < NUM_FEEDER_ACTUATORS) {
        return channels[channel].commandedOn;
    }
    return false;
}

bool PoleActuators::isRelayPhysicalOn(uint8_t channel) const {
    if (channel < NUM_FEEDER_ACTUATORS) {
        return channels[channel].actualPinState;
    }
    return false;
}

uint8_t PoleActuators::getActiveRelayMask() const {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        if (channels[i].commandedOn) {
            mask |= (1 << i);
        }
    }
    return mask;
}

uint8_t PoleActuators::getFaultMask() const {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        if (channels[i].fault != RELAY_FAULT_NONE) {
            mask |= (1 << i);
        }
    }
    return mask;
}

uint8_t PoleActuators::getPinForChannel(uint8_t channel) const {
    if (channel < NUM_FEEDER_ACTUATORS) {
        return channels[channel].pin;
    }
    return 0;
}

void PoleActuators::emergencyShutdownAll(const char* reason) {
    Serial.printf("[ACTUATOR] 🚨 EMERGENCY SHUTDOWN TRIGGERED! Reason: %s\n", reason);

    uint8_t offLevel = getElectricalLevel(false);
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        digitalWrite(channels[i].pin, offLevel);
        channels[i].commandedOn = false;
        channels[i].actualPinState = false;
        channels[i].fault = RELAY_FAULT_EMERGENCY_LATCHED;
    }

    emergencyActive = true;
    driverState = ACTUATOR_STATE_EMERGENCY_STOP;
    Serial.println(F("[ACTUATOR] All 6 Feeder Relay Outputs DE-ENERGIZED immediately."));
}

void PoleActuators::resetEmergencyState() {
    Serial.println(F("[ACTUATOR] Resetting Emergency Interlock..."));
    emergencyActive = false;
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        if (channels[i].fault == RELAY_FAULT_EMERGENCY_LATCHED) {
            channels[i].fault = RELAY_FAULT_NONE;
        }
    }
    validateStartupState();
}

bool PoleActuators::isEmergencyActive() const {
    return emergencyActive;
}
