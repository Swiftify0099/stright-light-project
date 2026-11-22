/*
 * =====================================================================
 *  POLEACTUATORS.CPP — SIX-FEEDER RELAY & CONTACTOR ACTUATION DRIVER
 *  MSCB Smart Pole Automation System — Prompt 15 Architecture
 * =====================================================================
 */

#include "PoleActuators.h"

PoleActuators poleActuators;

// Default Safe Verified GPIO mapping (Free of strapping pins 12, 15, 0, 2)
static const uint8_t SAFE_RELAY_PINS[NUM_FEEDER_ACTUATORS] = {
    26, // Feeder 1 / Main Street Light Contactor Coil (DAC2)
    25, // Feeder 2 Contactor Coil (DAC1)
    27, // Feeder 3 Contactor Coil (General IO)
    16, // Feeder 4 Contactor Coil (UART2 RX / General IO)
    17, // Feeder 5 Contactor Coil (UART2 TX / General IO)
    32  // Feeder 6 Contactor Coil (General IO)
};

// Legacy hardware mapping (Includes GPIO 13, 12, 15)
static const uint8_t LEGACY_RELAY_PINS[NUM_FEEDER_ACTUATORS] = {
    26, // Feeder 1
    25, // Feeder 2
    27, // Feeder 3
    13, // Feeder 4
    12, // Feeder 5 (MTDI Strapping Pin)
    15  // Feeder 6 (MTDO Strapping Pin)
};

PoleActuators::PoleActuators() :
    driverState(ACTUATOR_STATE_UNINITIALIZED),
    activeLow(RELAY_ACTIVE_LOW),
    diagnosticPassed(false),
    emergencyActive(false),
    gpio12HazardPresent(false)
{
    for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
        channels[i].pin = SAFE_RELAY_PINS[i];
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

void PoleActuators::init(bool useSafeVerifiedPins) {
    Serial.println(F("[ACTUATOR] Initializing 6-Channel Feeder Relay Actuation Driver..."));

    // 1. Establish pin mapping
    if (useSafeVerifiedPins) {
        configurePinMapping(SAFE_RELAY_PINS);
        Serial.println(F("[ACTUATOR] Applied Verified Safe Pin Mapping (GPIOs: 26, 25, 27, 16, 17, 32)."));
    } else {
        configurePinMapping(FEEDER_RELAY_PINS);
        Serial.printf("[ACTUATOR] Applied Configured Pin Mapping (GPIOs: %d, %d, %d, %d, %d, %d).\n",
                      channels[0].pin, channels[1].pin, channels[2].pin,
                      channels[3].pin, channels[4].pin, channels[5].pin);
    }

    // 2. Critical Hardware Strapping Mitigation for GPIO 12
    if (gpio12HazardPresent) {
        Serial.println(F("[ACTUATOR] ⚠️ WARNING: GPIO 12 (MTDI) detected in relay mapping!"));
        Serial.println(F("[ACTUATOR] Clamping GPIO 12 LOW to avoid 1.8V VDD_SDIO boot brownout."));
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
        pinMode(pin, OUTPUT);
        digitalWrite(pin, offLevel);
        channels[i].commandedOn = false;
        channels[i].actualPinState = false;
        channels[i].fault = RELAY_FAULT_NONE;
    }

    Serial.printf("[ACTUATOR] Mode: %s | Polarity: Active-%s (OFF Level: %s)\n",
                  "DIAGNOSTIC_MODE", activeLow ? "LOW" : "HIGH", offLevel == HIGH ? "HIGH" : "LOW");
    Serial.println(F("[ACTUATOR] All relay outputs forced SAFE OFF during boot diagnostics."));
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

bool PoleActuators::requestRelayOn(uint8_t channel, const char* reason) {
    if (channel >= NUM_FEEDER_ACTUATORS) return false;

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
