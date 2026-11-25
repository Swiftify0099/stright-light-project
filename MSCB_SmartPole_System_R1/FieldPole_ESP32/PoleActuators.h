/*
 * =====================================================================
 *  POLEACTUATORS.H — SIX-FEEDER RELAY & CONTACTOR ACTUATION DRIVER
 *  MSCB Smart Pole Automation System — Prompt 15 Architecture
 *  Target: ESP32-WROOM-32 | 6 Feeder Relay Outputs + Safety Supervision
 * =====================================================================
 */

#ifndef POLE_ACTUATORS_H
#define POLE_ACTUATORS_H

#include <Arduino.h>
#include <stdint.h>
#include <stdbool.h>
#include "PoleConfig.h"

#define NUM_FEEDER_ACTUATORS 6

// ─────────────────────────────────────────────────────────────
//  ACTUATOR DRIVER OPERATIONAL STATES
// ─────────────────────────────────────────────────────────────
enum ActuatorDriverState {
    ACTUATOR_STATE_UNINITIALIZED = 0,
    ACTUATOR_STATE_DIAGNOSTIC_MODE,     // Relays forced SAFE OFF during boot diagnostics
    ACTUATOR_STATE_READY,               // Operational, authorized switching permitted
    ACTUATOR_STATE_EMERGENCY_STOP,      // Hardwired or supervisory emergency shutdown
    ACTUATOR_STATE_FAULT                // Hardware driver or strapping pin fault
};

// ─────────────────────────────────────────────────────────────
//  RELAY CHANNEL FAULT CODES
// ─────────────────────────────────────────────────────────────
enum RelayChannelFault {
    RELAY_FAULT_NONE              = 0,
    RELAY_FAULT_PIN_MISMATCH      = 1,  // Output readback disagrees with commanded register
    RELAY_FAULT_STRAPPING_RISK    = 2,  // Strapping pin conflict (GPIO 12 hazard detected)
    RELAY_FAULT_DRIVER_ERROR      = 3,  // Optocoupler / driver circuit open or unverified
    RELAY_FAULT_DIAGNOSTIC_FAIL   = 4,  // Boot-time self-test failed
    RELAY_FAULT_EMERGENCY_LATCHED = 5   // Interlocked by emergency stop
};

// ─────────────────────────────────────────────────────────────
//  CHANNEL HARDWARE SPECIFICATION RECORD
// ─────────────────────────────────────────────────────────────
struct ActuatorChannelSpec {
    uint8_t           pin;              // ESP32 GPIO pin
    bool              commandedOn;      // Commanded logical state (true = ON / energized)
    bool              actualPinState;   // Physical pin state readback
    RelayChannelFault fault;            // Current channel fault
    bool              isStrappingPin;   // Flagged if mapped to GPIO 12, 15, 0, 2
    uint32_t          lastSwitchMs;     // Millis timestamp of last state transition
    uint32_t          switchCount;      // Total operations counter (wear tracking)
};

enum PhysicalAuthStatus {
    AUTH_STATUS_LOCKED       = 0,  // Production lock or unauthorized
    AUTH_STATUS_UNAUTHORIZED = 1,  // Missing physical authorization
    AUTH_STATUS_BENCH_ACTIVE = 2   // Authorized and low-voltage bench test active
};

// ─────────────────────────────────────────────────────────────
//  POLE ACTUATORS CLASS
// ─────────────────────────────────────────────────────────────
class PoleActuators {
public:
    PoleActuators();

    // 1. Relay Initialization & Safe Configuration
    void init(bool useSafeVerifiedPins = false);
    void configurePinMapping(const uint8_t pins[NUM_FEEDER_ACTUATORS]);
    bool applyPinConfiguration(const PoleHardwarePinConfig &cfg);
    void setActiveLow(bool activeLow);
    bool isActiveLow() const;

    // 2. Startup State Validation & Diagnostics
    bool validateStartupState();
    bool isDiagnosticPassed() const;
    ActuatorDriverState getDriverState() const;
    bool areRelaysLockedDisabled() const;
    void setRelaysLockedDisabled(bool locked);
    void setBenchTestOverride(bool override);
    bool getBenchTestOverride() const { return benchTestOverride; }

    // 3. Relay Switching Requests
    bool requestRelayOn(uint8_t channel, const char* reason = "CMD");
    bool requestRelayOff(uint8_t channel, const char* reason = "CMD");
    bool toggleRelay(uint8_t channel, const char* reason = "CMD");

    // 4. Output Verification & Fault Detection
    bool verifyActualOutput(uint8_t channel);
    bool verifyAllOutputs();
    RelayChannelFault getChannelFault(uint8_t channel) const;
    void clearChannelFault(uint8_t channel);

    // 5. Query Actuator States
    bool isRelayCommandedOn(uint8_t channel) const;
    bool isRelayPhysicalOn(uint8_t channel) const;
    uint8_t getActiveRelayMask() const;
    uint8_t getFaultMask() const;
    uint8_t getPinForChannel(uint8_t channel) const;
    inline uint8_t getFeederPin(uint8_t channel) const { return getPinForChannel(channel); }

    // 6. Emergency & Safety Controls
    void emergencyShutdownAll(const char* reason = "EMERGENCY_SHUTDOWN");
    void resetEmergencyState();
    bool isEmergencyActive() const;

    // 7. Low-Voltage Isolated DC Commissioning Profile & Local Physical Authorization
    bool isBenchTestOverride() const { return benchTestOverride; }
    void setLocalPhysicalAuthorization(bool authorized);
    bool isLocalPhysicalAuthorized() const;
    void servicePhysicalAuthorization();
    PhysicalAuthStatus getPhysicalAuthStatus() const;
    const char* getPhysicalAuthStatusString() const;

    // 8. GPIO 12 Strapping Risk Audit & Safety Profile Introspection
    bool auditStrappingPins();
    bool isGpio12Safe() const;
    const char* getSafetyProfileName() const;
    bool isHardwareEmergencyInterlockImplemented() const { return false; }

private:
    ActuatorDriverState driverState;
    bool                activeLow;
    bool                diagnosticPassed;
    bool                emergencyActive;
    bool                gpio12HazardPresent;
    bool                relaysLockedDisabled;
    bool                benchTestOverride;
    bool                softwareAuthRequested;
    bool                physicalPinState;
    uint32_t            physicalAuthExpiryMs;
    bool                physicalLowTracking;
    uint32_t            physicalLowStartMs;
    uint8_t             physicalAuthDebounceCount;
    bool                physicalAuthSessionActive;
    uint32_t            physicalAuthGrantedMs;

    ActuatorChannelSpec channels[NUM_FEEDER_ACTUATORS];

    // Helper to get raw electrical level for desired logical state
    inline uint8_t getElectricalLevel(bool logicalOn) const {
        if (activeLow) {
            return logicalOn ? LOW : HIGH;
        } else {
            return logicalOn ? HIGH : LOW;
        }
    }

    // Helper to check if raw pin level corresponds to energized relay
    inline bool isRawLevelEnergized(uint8_t rawLevel) const {
        return activeLow ? (rawLevel == LOW) : (rawLevel == HIGH);
    }
};

extern PoleActuators poleActuators;

#endif // POLE_ACTUATORS_H
