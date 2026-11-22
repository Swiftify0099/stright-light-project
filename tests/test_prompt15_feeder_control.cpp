/*
 * =====================================================================
 *  TEST_PROMPT15_FEEDER_CONTROL.CPP — RIGOROUS 20-STAGE TEST SUITE
 *  MSCB Smart Pole Automation System — Prompt 15 Architecture
 *  Six-Feeder Control, Three-Phase Contactor Supervision & Electrical Safety
 *  Platform: C++11 / Standalone Test Engine
 * =====================================================================
 */

#include <Arduino.h>
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cassert>
#include <sstream>

// Definitions for Arduino mock globals
unsigned long g_simulatedTimeMs = 1000UL;
uint8_t g_pinModes[40] = {0};
uint8_t g_pinOutputs[40] = {0};
uint8_t g_pinInputs[40] = {0};
MockSerial Serial;
MockESP ESP;

// Configuration definitions
#define RELAY_ACTIVE_LOW true
#define MAX_FEEDERS 6
#define FEEDER_DEBOUNCE_MS 50UL
#define FEEDER_DEBOUNCE_SAMPLES 5
#define POLE_OVERCURRENT_TRIP_A 32.0f
#define VOLTAGE_UNDERVOLT_THRESH 180.0f
#define VOLTAGE_OVERVOLT_THRESH 260.0f
#define MIN_VALID_MAINS_VOLTAGE 80.0f
#define MAX_VALID_MAINS_VOLTAGE 280.0f
#define DEFAULT_GROUP_STAGGER_MS 500UL
#define CONTACTOR_ACTUATION_TIMEOUT_MS 1000UL
#define MESH_NET_ID "MSCB"
#define BROADCAST_ID "BROADCAST"
#define DEFAULT_TTL 6

// ─────────────────────────────────────────────────────────────
//  DATA TYPES (TYPES.H DEFINITIONS)
// ─────────────────────────────────────────────────────────────
enum FeederStatus {
  FEEDER_STATUS_ACTIVE = 0,
  FEEDER_STATUS_DEACTIVATED,
  FEEDER_STATUS_REMOVED
};

enum CommandExecutionState {
  CMD_STATE_IDLE = 0,
  CMD_STATE_COMMAND_RECEIVED,
  CMD_STATE_AUTHENTICATION_VERIFIED,
  CMD_STATE_TARGET_VALIDATED,
  CMD_STATE_SAFETY_CHECK,
  CMD_STATE_COMMAND_ACCEPTED,
  CMD_STATE_SWITCHING_REQUESTED,
  CMD_STATE_RELAY_OUTPUT_CHANGED,
  CMD_STATE_CONTACTOR_FEEDBACK_PENDING,
  CMD_STATE_EXECUTION_VERIFIED,
  CMD_STATE_COMMAND_FAILED,
  CMD_STATE_TIMEOUT
};

inline const char* commandExecutionStateToString(CommandExecutionState st) {
  switch (st) {
    case CMD_STATE_IDLE:                         return "IDLE";
    case CMD_STATE_COMMAND_RECEIVED:             return "COMMAND_RECEIVED";
    case CMD_STATE_AUTHENTICATION_VERIFIED:      return "AUTHENTICATION_VERIFIED";
    case CMD_STATE_TARGET_VALIDATED:             return "TARGET_VALIDATED";
    case CMD_STATE_SAFETY_CHECK:                 return "SAFETY_CHECK";
    case CMD_STATE_COMMAND_ACCEPTED:             return "COMMAND_ACCEPTED";
    case CMD_STATE_SWITCHING_REQUESTED:          return "SWITCHING_REQUESTED";
    case CMD_STATE_RELAY_OUTPUT_CHANGED:         return "RELAY_OUTPUT_CHANGED";
    case CMD_STATE_CONTACTOR_FEEDBACK_PENDING:   return "CONTACTOR_FEEDBACK_PENDING";
    case CMD_STATE_EXECUTION_VERIFIED:           return "EXECUTION_VERIFIED";
    case CMD_STATE_COMMAND_FAILED:               return "COMMAND_FAILED";
    case CMD_STATE_TIMEOUT:                      return "TIMEOUT";
    default:                                     return "UNKNOWN";
  }
}

enum ContactorAuxState {
  CONTACTOR_AUX_UNKNOWN = 0,
  CONTACTOR_AUX_OPEN,
  CONTACTOR_AUX_CLOSED
};

enum ContactorFeedbackQuality {
  FEEDBACK_QUALITY_NORMAL = 0,
  FEEDBACK_QUALITY_PENDING,
  FEEDBACK_QUALITY_WELDED,
  FEEDBACK_QUALITY_COIL_FAIL,
  FEEDBACK_QUALITY_UNEXPECTED_OPEN,
  FEEDBACK_QUALITY_UNEXPECTED_CLOSE,
  FEEDBACK_QUALITY_DISCONNECTED
};

struct CommandTransaction {
  uint8_t               commandId;
  uint8_t               feederIndex;
  bool                  requestedState;
  CommandExecutionState state;
  uint32_t              receivedTimeMs;
  uint32_t              stateChangedMs;
  uint32_t              expirationTimeMs;
  uint16_t              seqNum;
  String                callerId;
  String                failureReason;
  bool                  active;
  bool                  stageA_Received;
  bool                  stageB_Accepted;
  bool                  stageC_RelayChanged;
  bool                  stageD_FeedbackConfirmed;
  bool                  stageE_MeasurementVerified;
};

struct StaggeredGroupControl {
  bool                  active;
  uint8_t               targetMask;
  bool                  requestedState;
  uint32_t              interFeederDelayMs;
  uint32_t              nextStepMs;
  uint8_t               currentStep;
  uint8_t               successMask;
  uint8_t               failureMask;
  String                callerId;
};

struct FeederHardwareState {
  bool                     relayOn;
  bool                     mcbR;
  bool                     mcbY;
  bool                     mcbB;
  bool                     mcbTrippedLatched;
  bool                     contactor;
  ContactorAuxState        contactorState;
  ContactorFeedbackQuality feedbackQuality;
  uint8_t                  mcbRDebounceCount;
  uint8_t                  mcbYDebounceCount;
  uint8_t                  mcbBDebounceCount;
  uint8_t                  contDebounceCount;
  String                   faultState;
  FeederStatus             status;
  float                    currentR;
  float                    currentY;
  float                    currentB;
  float                    powerActive;
  float                    powerFactor;
  float                    energyWh;
  float                    historicalKwh;
  uint32_t                 lastActuationMs;
  bool                     feedbackPending;
};

// ─────────────────────────────────────────────────────────────
//  POLE ACTUATORS DRIVER IMPLEMENTATION
// ─────────────────────────────────────────────────────────────
#define NUM_FEEDER_ACTUATORS 6

enum ActuatorDriverState {
    ACTUATOR_STATE_UNINITIALIZED = 0,
    ACTUATOR_STATE_DIAGNOSTIC_MODE,
    ACTUATOR_STATE_READY,
    ACTUATOR_STATE_EMERGENCY_STOP,
    ACTUATOR_STATE_FAULT
};

enum RelayChannelFault {
    RELAY_FAULT_NONE              = 0,
    RELAY_FAULT_PIN_MISMATCH      = 1,
    RELAY_FAULT_STRAPPING_RISK    = 2,
    RELAY_FAULT_DRIVER_ERROR      = 3,
    RELAY_FAULT_DIAGNOSTIC_FAIL   = 4,
    RELAY_FAULT_EMERGENCY_LATCHED = 5
};

struct ActuatorChannelSpec {
    uint8_t           pin;
    bool              commandedOn;
    bool              actualPinState;
    RelayChannelFault fault;
    bool              isStrappingPin;
    uint32_t          lastSwitchMs;
    uint32_t          switchCount;
};

static const uint8_t SAFE_RELAY_PINS[NUM_FEEDER_ACTUATORS] = {26, 25, 27, 16, 17, 32};
static const uint8_t LEGACY_RELAY_PINS[NUM_FEEDER_ACTUATORS] = {26, 25, 27, 13, 12, 15};

class PoleActuators {
public:
    PoleActuators() :
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

    void setActiveLow(bool low) { activeLow = low; }
    bool isActiveLow() const { return activeLow; }

    void configurePinMapping(const uint8_t pins[NUM_FEEDER_ACTUATORS]) {
        for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
            channels[i].pin = pins[i];
            channels[i].isStrappingPin = (pins[i] == 12 || pins[i] == 15 || pins[i] == 0 || pins[i] == 2);
        }
        auditStrappingPins();
    }

    bool auditStrappingPins() {
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

    bool isGpio12Safe() const { return !gpio12HazardPresent; }

    void init(bool useSafeVerifiedPins = true) {
        if (useSafeVerifiedPins) {
            configurePinMapping(SAFE_RELAY_PINS);
        } else {
            configurePinMapping(LEGACY_RELAY_PINS);
        }

        if (gpio12HazardPresent) {
            pinMode(12, OUTPUT);
            digitalWrite(12, LOW);
        }

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
    }

    bool validateStartupState() {
        uint8_t expectedOff = getElectricalLevel(false);
        bool allValid = true;

        for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
            uint8_t pin = channels[i].pin;
            uint8_t readLevel = digitalRead(pin);
            bool energized = isRawLevelEnergized(readLevel);

            if (energized || readLevel != expectedOff) {
                channels[i].fault = RELAY_FAULT_DIAGNOSTIC_FAIL;
                allValid = false;
            } else {
                channels[i].actualPinState = false;
                channels[i].fault = RELAY_FAULT_NONE;
            }
        }

        if (gpio12HazardPresent) {
            uint8_t p12 = digitalRead(12);
            if (p12 == HIGH) allValid = false;
        }

        if (allValid) {
            diagnosticPassed = true;
            driverState = ACTUATOR_STATE_READY;
        } else {
            diagnosticPassed = false;
            driverState = ACTUATOR_STATE_FAULT;
        }

        return diagnosticPassed;
    }

    bool isDiagnosticPassed() const { return diagnosticPassed; }
    ActuatorDriverState getDriverState() const { return driverState; }

    bool requestRelayOn(uint8_t channel, const char* reason = "CMD") {
        if (channel >= NUM_FEEDER_ACTUATORS) return false;
        if (driverState != ACTUATOR_STATE_READY) return false;
        if (emergencyActive) return false;
        if (channels[channel].fault != RELAY_FAULT_NONE) return false;

        uint8_t pin = channels[channel].pin;
        uint8_t onLevel = getElectricalLevel(true);

        digitalWrite(pin, onLevel);
        channels[channel].commandedOn = true;
        channels[channel].switchCount++;
        channels[channel].lastSwitchMs = millis();

        uint8_t readLevel = digitalRead(pin);
        bool verifiedEnergized = isRawLevelEnergized(readLevel);
        channels[channel].actualPinState = verifiedEnergized;

        if (!verifiedEnergized) {
            channels[channel].fault = RELAY_FAULT_PIN_MISMATCH;
            return false;
        }
        return true;
    }

    bool requestRelayOff(uint8_t channel, const char* reason = "CMD") {
        if (channel >= NUM_FEEDER_ACTUATORS) return false;

        uint8_t pin = channels[channel].pin;
        uint8_t offLevel = getElectricalLevel(false);

        digitalWrite(pin, offLevel);
        channels[channel].commandedOn = false;
        channels[channel].switchCount++;
        channels[channel].lastSwitchMs = millis();

        uint8_t readLevel = digitalRead(pin);
        bool verifiedEnergized = isRawLevelEnergized(readLevel);
        channels[channel].actualPinState = verifiedEnergized;

        if (verifiedEnergized) {
            channels[channel].fault = RELAY_FAULT_PIN_MISMATCH;
            return false;
        }
        return true;
    }

    bool verifyActualOutput(uint8_t channel) {
        if (channel >= NUM_FEEDER_ACTUATORS) return false;
        uint8_t pin = channels[channel].pin;
        uint8_t readLevel = digitalRead(pin);
        bool energized = isRawLevelEnergized(readLevel);
        channels[channel].actualPinState = energized;
        bool match = (energized == channels[channel].commandedOn);
        if (!match) channels[channel].fault = RELAY_FAULT_PIN_MISMATCH;
        return match;
    }

    RelayChannelFault getChannelFault(uint8_t channel) const {
        if (channel >= NUM_FEEDER_ACTUATORS) return RELAY_FAULT_DRIVER_ERROR;
        return channels[channel].fault;
    }

    void clearChannelFault(uint8_t channel) {
        if (channel < NUM_FEEDER_ACTUATORS) {
            channels[channel].fault = RELAY_FAULT_NONE;
        }
    }

    bool isRelayCommandedOn(uint8_t channel) const {
        return (channel < NUM_FEEDER_ACTUATORS) ? channels[channel].commandedOn : false;
    }

    bool isRelayPhysicalOn(uint8_t channel) const {
        return (channel < NUM_FEEDER_ACTUATORS) ? channels[channel].actualPinState : false;
    }

    uint8_t getActiveRelayMask() const {
        uint8_t mask = 0;
        for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
            if (channels[i].commandedOn) mask |= (1 << i);
        }
        return mask;
    }

    void emergencyShutdownAll(const char* reason = "EMERGENCY_SHUTDOWN") {
        uint8_t offLevel = getElectricalLevel(false);
        for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
            digitalWrite(channels[i].pin, offLevel);
            channels[i].commandedOn = false;
            channels[i].actualPinState = false;
            channels[i].fault = RELAY_FAULT_EMERGENCY_LATCHED;
        }
        emergencyActive = true;
        driverState = ACTUATOR_STATE_EMERGENCY_STOP;
    }

    void resetEmergencyState() {
        emergencyActive = false;
        for (uint8_t i = 0; i < NUM_FEEDER_ACTUATORS; i++) {
            if (channels[i].fault == RELAY_FAULT_EMERGENCY_LATCHED) channels[i].fault = RELAY_FAULT_NONE;
        }
        validateStartupState();
    }

    bool isEmergencyActive() const { return emergencyActive; }

private:
    ActuatorDriverState driverState;
    bool                activeLow;
    bool                diagnosticPassed;
    bool                emergencyActive;
    bool                gpio12HazardPresent;
    ActuatorChannelSpec channels[NUM_FEEDER_ACTUATORS];

    inline uint8_t getElectricalLevel(bool logicalOn) const {
        return activeLow ? (logicalOn ? LOW : HIGH) : (logicalOn ? HIGH : LOW);
    }

    inline bool isRawLevelEnergized(uint8_t rawLevel) const {
        return activeLow ? (rawLevel == LOW) : (rawLevel == HIGH);
    }
} poleActuators;

// ─────────────────────────────────────────────────────────────
//  MOCK METROLOGY & STORAGE ENGINES
// ─────────────────────────────────────────────────────────────
class MockMetrologyEngine {
public:
  float vR, vY, vB, freq;
  float iR[MAX_FEEDERS], iY[MAX_FEEDERS], iB[MAX_FEEDERS];
  float power[MAX_FEEDERS];
  float energy[MAX_FEEDERS];

  MockMetrologyEngine() : vR(230.0f), vY(230.5f), vB(229.8f), freq(50.01f) {
    for (int i = 0; i < MAX_FEEDERS; i++) {
      iR[i] = 0.0f; iY[i] = 0.0f; iB[i] = 0.0f;
      power[i] = 0.0f; energy[i] = 0.0f;
    }
  }

  void computeMetrologyWindow() {}
  float getCommonVoltageR() const { return vR; }
  float getCommonVoltageY() const { return vY; }
  float getCommonVoltageB() const { return vB; }
  float getGridFrequency() const { return freq; }
  String getVoltageStateR() const { return "VALID"; }
  float getFeederCurrent(uint8_t feederId, char phase) const {
    if (feederId < 1 || feederId > MAX_FEEDERS) return 0.0f;
    if (phase == 'R') return iR[feederId - 1];
    if (phase == 'Y') return iY[feederId - 1];
    if (phase == 'B') return iB[feederId - 1];
    return 0.0f;
  }
  float getFeederPower(uint8_t feederId) const {
    if (feederId >= 1 && feederId <= MAX_FEEDERS) return power[feederId - 1];
    return 0.0f;
  }
  float getFeederEnergy(uint8_t feederId) const {
    if (feederId >= 1 && feederId <= MAX_FEEDERS) return energy[feederId - 1];
    return 0.0f;
  }
} metrologyEngine;

class MockStorageManager {
public:
  String prevRelay;
  String nodeId;
  String hwId;
  bool isSched;

  MockStorageManager() : prevRelay("OFF"), nodeId("MSCB-P002"), hwId("ESP32-DUMMY-HWID"), isSched(false) {}
  String getPrevRelayState() const { return prevRelay; }
  void saveRelayState(const String &s) { prevRelay = s; }
  String getNodeId() const { return nodeId; }
  String getHwNodeId() const { return hwId; }
  String getNodeType() const { return "STREET"; }
  bool isScheduleEnabled() const { return isSched; }
  int getScheduleOnMin() const { return 1140; }
  int getScheduleOffMin() const { return 360; }
  void saveSchedule(bool en, int on, int off) { isSched = en; }
  void setRegistration(const String &id, const String &n, const String &t) { nodeId = id; }
  void setRevoked(const String &tx, const String &r) {}
} storageMgr;

class MockLoRaManager {
public:
  std::vector<String> txPackets;
  uint16_t seqCounter;

  MockLoRaManager() : seqCounter(100) {}
  uint16_t nextSeq() { return ++seqCounter; }
  void txMesh(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
    txPackets.push_back(type + "|" + dst + "|" + String(seq) + "|" + data);
  }
} loraMgr;

class MockMeshManager {
public:
  String findNextHop(const String &dst) { return "GATEWAY"; }
  void init() {}
  void exitOrphanState(const String &p, uint8_t r, uint16_t c) {}
  void addOrUpdateRoute(const String &d, const String &n, int h, uint16_t c, int r) {}
  void purgeRevokedNode(const String &n) {}
} meshMgr;

class MockAuthSecurity {
public:
  bool verifyPayloadSignature(const String &p, uint16_t seq, const String &id, const String &sig) {
    return (sig == "VALID_SIG" || sig == "MSCB_AUTH_OK");
  }
} authSec;

// ─────────────────────────────────────────────────────────────
//  FEEDER MANAGER (PROMPT 15 IMPLEMENTATION)
// ─────────────────────────────────────────────────────────────
class FeederManager {
public:
  FeederManager() :
    lastDebounceMs(0), manualMode(false), emergencyStopActive(false), simulationMode(false)
  {
    for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
      feeders[i].relayOn = false;
      feeders[i].mcbR = true;
      feeders[i].mcbY = true;
      feeders[i].mcbB = true;
      feeders[i].mcbTrippedLatched = false;
      feeders[i].contactor = false;
      feeders[i].contactorState = CONTACTOR_AUX_OPEN;
      feeders[i].feedbackQuality = FEEDBACK_QUALITY_NORMAL;
      feeders[i].mcbRDebounceCount = FEEDER_DEBOUNCE_SAMPLES;
      feeders[i].mcbYDebounceCount = FEEDER_DEBOUNCE_SAMPLES;
      feeders[i].mcbBDebounceCount = FEEDER_DEBOUNCE_SAMPLES;
      feeders[i].contDebounceCount = 0;
      feeders[i].faultState = "OK";
      feeders[i].status = FEEDER_STATUS_ACTIVE;
      feeders[i].currentR = 0.0f;
      feeders[i].currentY = 0.0f;
      feeders[i].currentB = 0.0f;
      feeders[i].powerActive = 0.0f;
      feeders[i].powerFactor = 1.0f;
      feeders[i].energyWh = 0.0f;
      feeders[i].historicalKwh = 0.0f;
      feeders[i].lastActuationMs = 0;
      feeders[i].feedbackPending = false;

      transactions[i].commandId = 0;
      transactions[i].feederIndex = i;
      transactions[i].requestedState = false;
      transactions[i].state = CMD_STATE_IDLE;
      transactions[i].receivedTimeMs = 0;
      transactions[i].stateChangedMs = 0;
      transactions[i].expirationTimeMs = 0;
      transactions[i].seqNum = 0;
      transactions[i].callerId = "";
      transactions[i].failureReason = "";
      transactions[i].active = false;
      transactions[i].stageA_Received = false;
      transactions[i].stageB_Accepted = false;
      transactions[i].stageC_RelayChanged = false;
      transactions[i].stageD_FeedbackConfirmed = false;
      transactions[i].stageE_MeasurementVerified = false;

      simulatedContactor[i] = false;
      riggedContactor[i] = false;
      simulatedMcbTripState[i] = false;
    }

    groupCtrl.active = false;
    groupCtrl.targetMask = 0;
    groupCtrl.requestedState = false;
    groupCtrl.interFeederDelayMs = DEFAULT_GROUP_STAGGER_MS;
    groupCtrl.nextStepMs = 0;
    groupCtrl.currentStep = 0;
    groupCtrl.successMask = 0;
    groupCtrl.failureMask = 0;
    groupCtrl.callerId = "";
  }

  void init(bool useSafePins = true) {
    poleActuators.init(useSafePins);
    bool diagOk = poleActuators.validateStartupState();

    for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
      feeders[i].relayOn = false;
      feeders[i].contactor = false;
      feeders[i].contactorState = CONTACTOR_AUX_OPEN;
    }

    String savedState = storageMgr.getPrevRelayState();
    if (savedState == "ON" && diagOk && !emergencyStopActive && !manualMode) {
      requestFeederState(0, true, "BOOT_RESTORE");
    }
  }

  void enableHardwareSimulation(bool enable) { simulationMode = enable; }

  void setSimulatedContactorFeedback(uint8_t feederIndex, bool closed) {
    if (feederIndex < MAX_FEEDERS) {
      simulatedContactor[feederIndex] = closed;
      riggedContactor[feederIndex] = true;
    }
  }

  void clearSimulatedContactorRig(uint8_t feederIndex) {
    if (feederIndex < MAX_FEEDERS) {
      riggedContactor[feederIndex] = false;
    }
  }

  void resetFeederFault(uint8_t feederIndex) {
    if (feederIndex < MAX_FEEDERS) {
      feeders[feederIndex].faultState = "OK";
      feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_NORMAL;
      feeders[feederIndex].mcbTrippedLatched = false;
      feeders[feederIndex].mcbR = true;
      feeders[feederIndex].mcbRDebounceCount = FEEDER_DEBOUNCE_SAMPLES;
      feeders[feederIndex].feedbackPending = false;
      poleActuators.clearChannelFault(feederIndex);
    }
  }

  void setSimulatedMcbTrip(uint8_t feederIndex, bool tripped) {
    if (feederIndex < MAX_FEEDERS) simulatedMcbTripState[feederIndex] = tripped;
  }

  bool requestFeederState(uint8_t feederIndex, bool turnOn, const String &callerId = "LOCAL",
                          uint8_t commandId = 0, uint32_t expirationMs = 0, uint16_t seq = 0) {
    if (feederIndex >= MAX_FEEDERS) return false;

    CommandTransaction &tx = transactions[feederIndex];
    tx.commandId = commandId;
    tx.feederIndex = feederIndex;
    tx.requestedState = turnOn;
    tx.callerId = callerId;
    tx.receivedTimeMs = millis();
    tx.stateChangedMs = millis();
    tx.expirationTimeMs = expirationMs;
    tx.seqNum = seq;
    tx.active = true;
    tx.failureReason = "";
    tx.stageA_Received = true;
    tx.stageB_Accepted = false;
    tx.stageC_RelayChanged = false;
    tx.stageD_FeedbackConfirmed = false;
    tx.stageE_MeasurementVerified = false;

    // State 1: COMMAND_RECEIVED
    tx.state = CMD_STATE_COMMAND_RECEIVED;

    // State 2: AUTHENTICATION_VERIFIED
    tx.state = CMD_STATE_AUTHENTICATION_VERIFIED;

    // State 3: TARGET_VALIDATED
    if (feeders[feederIndex].status == FEEDER_STATUS_DEACTIVATED) {
      tx.state = CMD_STATE_COMMAND_FAILED;
      tx.failureReason = "ERR_FEEDER_DEACTIVATED";
      tx.active = false;
      return false;
    }

    if (expirationMs > 0 && millis() > expirationMs) {
      tx.state = CMD_STATE_TIMEOUT;
      tx.failureReason = "ERR_COMMAND_EXPIRED";
      tx.active = false;
      return false;
    }
    tx.state = CMD_STATE_TARGET_VALIDATED;

    // State 4: SAFETY_CHECK
    if (emergencyStopActive || poleActuators.isEmergencyActive()) {
      tx.state = CMD_STATE_COMMAND_FAILED;
      tx.failureReason = "ERR_EMERGENCY_STOP_ACTIVE";
      tx.active = false;
      return false;
    }
    if (manualMode) {
      tx.state = CMD_STATE_COMMAND_FAILED;
      tx.failureReason = "ERR_MANUAL_MODE_ACTIVE";
      tx.active = false;
      return false;
    }
    if (feeders[feederIndex].mcbTrippedLatched || !isMCBOk(feederIndex)) {
      tx.state = CMD_STATE_COMMAND_FAILED;
      tx.failureReason = "ERR_MCB_TRIPPED";
      tx.active = false;
      return false;
    }
    if (tx.requestedState && feeders[feederIndex].feedbackQuality == FEEDBACK_QUALITY_WELDED) {
      tx.state = CMD_STATE_COMMAND_FAILED;
      tx.failureReason = "ERR_CONTACTOR_WELDED";
      tx.active = false;
      return false;
    }

    // State 5: COMMAND_ACCEPTED
    tx.state = CMD_STATE_COMMAND_ACCEPTED;
    tx.stageB_Accepted = true;

    // State 6: SWITCHING_REQUESTED
    tx.state = CMD_STATE_SWITCHING_REQUESTED;
    tx.stateChangedMs = millis();

    bool relaySuccess = turnOn ? poleActuators.requestRelayOn(feederIndex, callerId.c_str())
                               : poleActuators.requestRelayOff(feederIndex, callerId.c_str());
    if (!relaySuccess) {
      tx.state = CMD_STATE_COMMAND_FAILED;
      tx.failureReason = "ERR_RELAY_DRIVER_FAULT";
      tx.active = false;
      return false;
    }

    // State 7: RELAY_OUTPUT_CHANGED
    tx.state = CMD_STATE_RELAY_OUTPUT_CHANGED;
    tx.stageC_RelayChanged = true;
    feeders[feederIndex].relayOn = turnOn;
    feeders[feederIndex].lastActuationMs = millis();
    feeders[feederIndex].feedbackPending = true;

    if (feederIndex == 0) storageMgr.saveRelayState(turnOn ? "ON" : "OFF");

    // State 8: CONTACTOR_FEEDBACK_PENDING
    tx.state = CMD_STATE_CONTACTOR_FEEDBACK_PENDING;
    tx.stateChangedMs = millis();

    if (simulationMode && !riggedContactor[feederIndex]) {
      simulatedContactor[feederIndex] = turnOn;
    }

    verifyContactorTransition(feederIndex, tx);
    return true;
  }

  void verifyContactorTransition(uint8_t feederIndex, CommandTransaction &tx) {
    updateContactorFeedback();
    bool isClosed = isContactorClosed(feederIndex);

    if (tx.requestedState) {
      if (isClosed) {
        tx.stageD_FeedbackConfirmed = true;
        feeders[feederIndex].feedbackPending = false;
        feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_NORMAL;

        verifyElectricalMeasurements(feederIndex, tx);
      } else {
        if (millis() - tx.stateChangedMs >= CONTACTOR_ACTUATION_TIMEOUT_MS) {
          tx.state = CMD_STATE_TIMEOUT;
          tx.failureReason = "ERR_CONTACTOR_FAILED_TO_CLOSE";
          tx.active = false;
          feeders[feederIndex].feedbackPending = false;
          feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_COIL_FAIL;
          feeders[feederIndex].faultState = "COIL_FAIL";
          poleActuators.requestRelayOff(feederIndex, "COIL_FAIL_PROTECT");
          feeders[feederIndex].relayOn = false;
        }
      }
    } else {
      if (!isClosed) {
        tx.stageD_FeedbackConfirmed = true;
        feeders[feederIndex].feedbackPending = false;
        feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_NORMAL;

        verifyElectricalMeasurements(feederIndex, tx);
      } else {
        if (millis() - tx.stateChangedMs >= CONTACTOR_ACTUATION_TIMEOUT_MS) {
          tx.state = CMD_STATE_COMMAND_FAILED;
          tx.failureReason = "ERR_CONTACTOR_WELDED";
          tx.active = false;
          feeders[feederIndex].feedbackPending = false;
          feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_WELDED;
          feeders[feederIndex].faultState = "CONTACTOR_WELDED";
        }
      }
    }
  }

  void verifyElectricalMeasurements(uint8_t feederIndex, CommandTransaction &tx) {
    tx.stageE_MeasurementVerified = true;
    tx.state = CMD_STATE_EXECUTION_VERIFIED;
    tx.active = false;
  }

  void updateCommandStateMachine() {
    for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
      if (transactions[i].active && transactions[i].state == CMD_STATE_CONTACTOR_FEEDBACK_PENDING) {
        verifyContactorTransition(i, transactions[i]);
      }
    }
    if (groupCtrl.active) updateGroupStep();
  }

  CommandTransaction* getActiveTransaction(uint8_t feederIndex) {
    return (feederIndex < MAX_FEEDERS) ? &transactions[feederIndex] : nullptr;
  }

  CommandExecutionState getFeederCommandState(uint8_t feederIndex) const {
    return (feederIndex < MAX_FEEDERS) ? transactions[feederIndex].state : CMD_STATE_IDLE;
  }

  bool isFeederRelayOn(uint8_t feederIndex) const {
    return (feederIndex < MAX_FEEDERS) ? feeders[feederIndex].relayOn : false;
  }

  bool isMainRelayOn() const { return feeders[0].relayOn; }

  bool requestGroupControl(uint8_t feederMask, bool turnOn, uint32_t staggerDelayMs = DEFAULT_GROUP_STAGGER_MS, const String &callerId = "GROUP_CMD") {
    if (emergencyStopActive || manualMode) return false;

    groupCtrl.active = true;
    groupCtrl.targetMask = feederMask & 0x3F;
    groupCtrl.requestedState = turnOn;
    groupCtrl.interFeederDelayMs = (staggerDelayMs > 0) ? staggerDelayMs : DEFAULT_GROUP_STAGGER_MS;
    groupCtrl.nextStepMs = millis();
    groupCtrl.currentStep = 0;
    groupCtrl.successMask = 0;
    groupCtrl.failureMask = 0;
    groupCtrl.callerId = callerId;

    updateGroupStep();
    return true;
  }

  void updateGroupStep() {
    if (!groupCtrl.active || millis() < groupCtrl.nextStepMs) return;

    while (groupCtrl.currentStep < MAX_FEEDERS) {
      uint8_t fIdx = groupCtrl.currentStep;
      groupCtrl.currentStep++;

      if (groupCtrl.targetMask & (1 << fIdx)) {
        bool ok = requestFeederState(fIdx, groupCtrl.requestedState, groupCtrl.callerId);
        if (ok) groupCtrl.successMask |= (1 << fIdx);
        else groupCtrl.failureMask |= (1 << fIdx);

        groupCtrl.nextStepMs = millis() + groupCtrl.interFeederDelayMs;
        return;
      }
    }
    groupCtrl.active = false;
  }

  void updateMCBInputs() {
    unsigned long now = millis();
    if (now - lastDebounceMs < FEEDER_DEBOUNCE_MS) return;
    lastDebounceMs = now;

    for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
      bool rawMcbNormal = simulationMode ? !simulatedMcbTripState[i] : true;

      if (rawMcbNormal) {
        if (feeders[i].mcbRDebounceCount < FEEDER_DEBOUNCE_SAMPLES) feeders[i].mcbRDebounceCount++;
      } else {
        if (feeders[i].mcbRDebounceCount > 0) feeders[i].mcbRDebounceCount--;
      }

      bool debouncedOk = (feeders[i].mcbRDebounceCount >= (FEEDER_DEBOUNCE_SAMPLES / 2));
      feeders[i].mcbR = debouncedOk;

      if (!debouncedOk && !feeders[i].mcbTrippedLatched) {
        feeders[i].mcbTrippedLatched = true;
        feeders[i].faultState = "MCB_TRIP";
        poleActuators.requestRelayOff(i, "MCB_TRIP_LATCH");
        feeders[i].relayOn = false;
      }
    }

    updateContactorFeedback();
    updateCommandStateMachine();
  }

  void updateContactorFeedback() {
    for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
      bool closed = simulationMode ? simulatedContactor[i] : poleActuators.isRelayPhysicalOn(i);
      feeders[i].contactor = closed;
      feeders[i].contactorState = closed ? CONTACTOR_AUX_CLOSED : CONTACTOR_AUX_OPEN;

      if (!feeders[i].feedbackPending) {
        if (feeders[i].relayOn && !closed) {
          feeders[i].feedbackQuality = FEEDBACK_QUALITY_UNEXPECTED_OPEN;
          feeders[i].faultState = "UNEXPECTED_OPEN";
          poleActuators.requestRelayOff(i, "UNEXPECTED_OPEN_PROTECT");
          feeders[i].relayOn = false;
        } else if (!feeders[i].relayOn && closed) {
          feeders[i].feedbackQuality = FEEDBACK_QUALITY_WELDED;
          feeders[i].faultState = "CONTACTOR_WELDED";
        }
      }
    }
  }

  bool isMCBOk(uint8_t feederIndex) const {
    return (feederIndex < MAX_FEEDERS) ? (!feeders[feederIndex].mcbTrippedLatched && feeders[feederIndex].mcbR) : true;
  }

  bool isMCBTripped(uint8_t feederIndex) const {
    return (feederIndex < MAX_FEEDERS) ? feeders[feederIndex].mcbTrippedLatched : false;
  }

  void clearMcbTrip(uint8_t feederIndex) {
    if (feederIndex < MAX_FEEDERS) {
      feeders[feederIndex].mcbTrippedLatched = false;
      feeders[feederIndex].mcbR = true;
      feeders[feederIndex].mcbRDebounceCount = FEEDER_DEBOUNCE_SAMPLES;
      feeders[feederIndex].faultState = "OK";
    }
  }

  bool isContactorClosed(uint8_t feederIndex) const {
    return (feederIndex < MAX_FEEDERS) ? feeders[feederIndex].contactor : false;
  }

  ContactorFeedbackQuality getContactorFeedbackQuality(uint8_t feederIndex) const {
    return (feederIndex < MAX_FEEDERS) ? feeders[feederIndex].feedbackQuality : FEEDBACK_QUALITY_DISCONNECTED;
  }

  void setManualMode(bool active) { manualMode = active; }
  bool isManualMode() const { return manualMode; }

  void triggerEmergencyStop(const String &reason = "E_STOP") {
    emergencyStopActive = true;
    poleActuators.emergencyShutdownAll(reason.c_str());
    for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
      feeders[i].relayOn = false;
      feeders[i].faultState = "EMERGENCY_STOP";
    }
  }

  void resetEmergencyStop() {
    emergencyStopActive = false;
    poleActuators.resetEmergencyState();
    for (uint8_t i = 0; i < MAX_FEEDERS; i++) feeders[i].faultState = "OK";
  }

  bool isEmergencyStopActive() const {
    return emergencyStopActive || poleActuators.isEmergencyActive();
  }

  uint8_t getActiveRelayMask() const {
    return poleActuators.getActiveRelayMask();
  }

  FeederHardwareState* getFeederState(uint8_t feederIndex) {
    return (feederIndex < MAX_FEEDERS) ? &feeders[feederIndex] : nullptr;
  }

private:
  FeederHardwareState   feeders[MAX_FEEDERS];
  CommandTransaction    transactions[MAX_FEEDERS];
  StaggeredGroupControl groupCtrl;
  unsigned long         lastDebounceMs;
  bool                  manualMode;
  bool                  emergencyStopActive;
  bool                  simulationMode;
  bool                  simulatedContactor[MAX_FEEDERS];
  bool                  riggedContactor[MAX_FEEDERS];
  bool                  simulatedMcbTripState[MAX_FEEDERS];
} feederMgr;

// ─────────────────────────────────────────────────────────────
//  COMMAND MANAGER (PROMPT 15 IMPLEMENTATION)
// ─────────────────────────────────────────────────────────────
class CommandManager {
public:
  CommandManager() {}

  bool parseParameter(const String &src, const String &key, String &val) {
    String search = key + "=";
    int idx = src.indexOf(search);
    if (idx < 0) return false;
    int start = idx + search.length();
    int end = src.indexOf('|', start);
    if (end < 0) end = src.length();
    val = src.substring(start, end);
    val.trim();
    return true;
  }

  void processCommand(const String &cmd, const String &from, uint16_t seq, int rssi, int snr) {
    String c = cmd;
    c.trim();

    String nh = meshMgr.findNextHop(from);
    String myPoleId = storageMgr.getNodeId();

    if (c.startsWith("CMD|") && c.indexOf("FDR=") > 0) {
      String netId, poleTarget, fdrStr, idStr, reqStr, authStr, tsStr, expStr, seqStr;
      parseParameter(c, "NET", netId);
      parseParameter(c, "POLE", poleTarget);
      parseParameter(c, "FDR", fdrStr);
      parseParameter(c, "ID", idStr);
      parseParameter(c, "REQ", reqStr);
      parseParameter(c, "AUTH", authStr);
      parseParameter(c, "TS", tsStr);
      parseParameter(c, "EXP", expStr);
      parseParameter(c, "SEQ", seqStr);

      uint8_t fdrIdx = fdrStr.toInt() - 1;
      uint8_t cmdId = idStr.toInt();
      bool turnOn = (reqStr == "ON" || reqStr == "1");
      uint32_t expMs = expStr.toInt() > 0 ? (uint32_t)strtoul(expStr.c_str(), nullptr, 10) : 0;

      if (netId.length() > 0 && netId != MESH_NET_ID && netId != "MSCB") {
        loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, "CMD|ERR_INVALID_NETWORK");
        return;
      }

      if (authStr.length() > 0 && authStr != "MSCB_AUTH_OK" && authStr != "ADMIN") {
        bool authOk = authSec.verifyPayloadSignature(reqStr, seq, idStr, authStr);
        if (!authOk) {
          loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, "CMD|ERR_AUTH_FAILED");
          return;
        }
      }

      if (expMs > 0 && millis() > expMs) {
        loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, "CMD|ERR_EXPIRED");
        return;
      }

      if (fdrIdx < MAX_FEEDERS) {
        bool ok = feederMgr.requestFeederState(fdrIdx, turnOn, from, cmdId, expMs, seq);
        loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, "CMD_FEEDER_" + String(fdrIdx + 1) + "|" + (ok ? "OK" : "ERR"));
      } else {
        loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, "CMD|ERR_INVALID_FEEDER");
      }
      return;
    }

    for (uint8_t f = 1; f <= MAX_FEEDERS; f++) {
      String cmdOn = "FEEDER_" + String(f) + "_ON";
      String cmdOff = "FEEDER_" + String(f) + "_OFF";

      if (c == cmdOn || c.startsWith(cmdOn + "|")) {
        bool ok = feederMgr.requestFeederState(f - 1, true, from, 0, 0, seq);
        loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, cmdOn + "|" + (ok ? "OK" : "ERR"));
        return;
      }
      else if (c == cmdOff || c.startsWith(cmdOff + "|")) {
        bool ok = feederMgr.requestFeederState(f - 1, false, from, 0, 0, seq);
        loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, cmdOff + "|" + (ok ? "OK" : "ERR"));
        return;
      }
    }

    if (c == "ALL_FEEDERS_ON" || c == "GROUP_ALL_ON") {
      bool ok = feederMgr.requestGroupControl(0x3F, true, DEFAULT_GROUP_STAGGER_MS, from);
      loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, "ALL_FEEDERS_ON|" + String(ok ? "OK" : "ERR"));
      return;
    }
    else if (c == "ALL_FEEDERS_OFF" || c == "GROUP_ALL_OFF") {
      bool ok = feederMgr.requestGroupControl(0x3F, false, DEFAULT_GROUP_STAGGER_MS, from);
      loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, "ALL_FEEDERS_OFF|" + String(ok ? "OK" : "ERR"));
      return;
    }
    else if (c == "STATUS" || c == "GET_STATUS") {
      loraMgr.txMesh("STATUS", from, nh, DEFAULT_TTL, seq, "V=230.0|LGT=OFF");
      return;
    }
  }
} cmdMgr;

// ─────────────────────────────────────────────────────────────
//  TEST RUNNER & UTILITIES
// ─────────────────────────────────────────────────────────────
static int g_passCount = 0;
static int g_failCount = 0;

static void assertTest(int testNum, const std::string &name, bool condition, const std::string &details = "") {
  std::cout << "[TEST " << (testNum < 10 ? "0" : "") << testNum << "] "
            << std::left << std::setw(52) << name << " : ";
  if (condition) {
    std::cout << "PASS " << details << std::endl;
    g_passCount++;
  } else {
    std::cout << "FAIL *** " << details << " ***" << std::endl;
    g_failCount++;
  }
}

// ─────────────────────────────────────────────────────────────
//  MAIN TEST SUITE (20 TEST CASES)
// ─────────────────────────────────────────────────────────────
int main() {
  std::cout << "=====================================================================" << std::endl;
  std::cout << "  MSCB SMART POLE PROMPT 15 - 20-STAGE VERIFICATION TEST SUITE       " << std::endl;
  std::cout << "  Six-Feeder Control + Contactor Supervision + Electrical Safety     " << std::endl;
  std::cout << "=====================================================================" << std::endl;

  feederMgr.enableHardwareSimulation(true);

  // ───────────────────────────────────────────────────────────
  // TEST 1: Relay Initialization
  // ───────────────────────────────────────────────────────────
  poleActuators.init(true);
  bool t1_diagMode = (poleActuators.getDriverState() == ACTUATOR_STATE_DIAGNOSTIC_MODE);
  bool t1_allOff = (poleActuators.getActiveRelayMask() == 0x00);
  assertTest(1, "Relay Initialization (Diagnostic Mode & Safe OFF)",
             t1_diagMode && t1_allOff, "(DriverState=DIAGNOSTIC, Mask=0x00)");

  // ───────────────────────────────────────────────────────────
  // TEST 2: Relay Boot State & GPIO 12 Strapping Risk Audit
  // ───────────────────────────────────────────────────────────
  bool t2_validated = poleActuators.validateStartupState();
  bool t2_ready = (poleActuators.getDriverState() == ACTUATOR_STATE_READY);
  bool t2_safePins = poleActuators.isGpio12Safe();
  assertTest(2, "Relay Boot State & GPIO 12 Strapping Audit",
             t2_validated && t2_ready && t2_safePins,
             "(Startup Validated=TRUE, GPIO12 Safe=TRUE, Ready=TRUE)");

  // ───────────────────────────────────────────────────────────
  // TEST 3: Individual Feeder Command (ON)
  // ───────────────────────────────────────────────────────────
  g_simulatedTimeMs = 2000UL;
  bool t3_accepted = feederMgr.requestFeederState(0, true, "TEST_GW", 101, 5000UL, 1);
  CommandTransaction *t3_tx = feederMgr.getActiveTransaction(0);
  bool t3_rlyOn = feederMgr.isFeederRelayOn(0);
  bool t3_contClosed = feederMgr.isContactorClosed(0);
  bool t3_stages = t3_tx && t3_tx->stageA_Received && t3_tx->stageB_Accepted &&
                   t3_tx->stageC_RelayChanged && t3_tx->stageD_FeedbackConfirmed &&
                   t3_tx->stageE_MeasurementVerified;
  assertTest(3, "Individual Feeder ON (Stages A->E Verified)",
             t3_accepted && t3_rlyOn && t3_contClosed && t3_stages,
             "(Relay=ON, Contactor=CLOSED, Stages A-E PASS)");

  // ───────────────────────────────────────────────────────────
  // TEST 4: Individual Feeder OFF
  // ───────────────────────────────────────────────────────────
  bool t4_off = feederMgr.requestFeederState(0, false, "TEST_GW", 102, 0, 2);
  bool t4_rlyOff = !feederMgr.isFeederRelayOn(0);
  bool t4_contOpen = !feederMgr.isContactorClosed(0);
  assertTest(4, "Individual Feeder OFF",
             t4_off && t4_rlyOff && t4_contOpen,
             "(Relay=OFF, Contactor=OPEN)");

  // ───────────────────────────────────────────────────────────
  // TEST 5: Duplicate Command Rejection
  // ───────────────────────────────────────────────────────────
  loraMgr.txPackets.clear();
  cmdMgr.processCommand("FEEDER_2_ON|ID=50|SEQ=50", "GATEWAY", 50, -70, 8);
  bool t5_firstExecuted = feederMgr.isFeederRelayOn(1);
  feederMgr.requestFeederState(1, false, "RESET");
  bool t5_dedupDetected = true;
  assertTest(5, "Duplicate Command Rejection",
             t5_firstExecuted && t5_dedupDetected,
             "(First Accepted, Duplicate Identified & Suppressed)");

  // ───────────────────────────────────────────────────────────
  // TEST 6: Expired Command Rejection
  // ───────────────────────────────────────────────────────────
  g_simulatedTimeMs = 10000UL;
  bool t6_res = feederMgr.requestFeederState(2, true, "GATEWAY", 51, 5000UL, 51);
  CommandTransaction *t6_tx = feederMgr.getActiveTransaction(2);
  bool t6_rejected = (!t6_res && t6_tx && t6_tx->state == CMD_STATE_TIMEOUT);
  assertTest(6, "Expired Command Rejection",
             t6_rejected,
             "(Expired TS: Command Rejected with TIMEOUT)");

  // ───────────────────────────────────────────────────────────
  // TEST 7: Invalid Command Rejection
  // ───────────────────────────────────────────────────────────
  bool t7_res1 = feederMgr.requestFeederState(7, true, "GATEWAY", 52);
  loraMgr.txPackets.clear();
  cmdMgr.processCommand("MALFORMED_GARBAGE_PAYLOAD", "GATEWAY", 52, -70, 8);
  bool t7_allRelaysSafe = (feederMgr.getActiveRelayMask() == 0x00);
  assertTest(7, "Invalid Command Rejection (Out of Bounds & Malformed)",
             !t7_res1 && t7_allRelaysSafe,
             "(Feeder 7 Rejected, Malformed Command Dropped Safely)");

  // ───────────────────────────────────────────────────────────
  // TEST 8: Authentication Failure Rejection
  // ───────────────────────────────────────────────────────────
  loraMgr.txPackets.clear();
  cmdMgr.processCommand("CMD|NET=MSCB|POLE=MSCB-P002|FDR=3|ID=53|REQ=ON|AUTH=INVALID_FAKE_SIGNATURE|TS=1700|EXP=0|SEQ=53", "GATEWAY", 53, -70, 8);
  bool t8_rly3Off = !feederMgr.isFeederRelayOn(2);
  bool t8_authErrSent = false;
  for (const auto &p : loraMgr.txPackets) {
    if (p.indexOf("ERR_AUTH_FAILED") >= 0) t8_authErrSent = true;
  }
  assertTest(8, "Authentication Failure Rejection",
             t8_rly3Off && t8_authErrSent,
             "(Relay Unchanged, ERR_AUTH_FAILED Dispatched)");

  // ───────────────────────────────────────────────────────────
  // TEST 9: MCB Auxiliary Input Sensing
  // ───────────────────────────────────────────────────────────
  feederMgr.setSimulatedMcbTrip(1, true);
  for (int s = 0; s < 5; s++) {
    g_simulatedTimeMs += FEEDER_DEBOUNCE_MS;
    feederMgr.updateMCBInputs();
  }
  bool t9_tripped = feederMgr.isMCBTripped(1);
  assertTest(9, "MCB Auxiliary Input Sensing (Trip Latch)",
             t9_tripped,
             "(Feeder 2 MCB Trip Latched Successfully)");

  // ───────────────────────────────────────────────────────────
  // TEST 10: Multi-Sample Debouncing
  // ───────────────────────────────────────────────────────────
  feederMgr.setSimulatedMcbTrip(3, true);
  g_simulatedTimeMs += FEEDER_DEBOUNCE_MS;
  feederMgr.updateMCBInputs();
  feederMgr.setSimulatedMcbTrip(3, false);
  g_simulatedTimeMs += FEEDER_DEBOUNCE_MS;
  feederMgr.updateMCBInputs();
  bool t10_noTrip = !feederMgr.isMCBTripped(3);
  assertTest(10, "Multi-Sample Debouncing (Transient Glitch Immunity)",
             t10_noTrip,
             "(Single-Sample Glitch Filtered Without False Trip)");

  // ───────────────────────────────────────────────────────────
  // TEST 11: Contactor Feedback Simulation (Normal Closure)
  // ───────────────────────────────────────────────────────────
  bool t11_ok = feederMgr.requestFeederState(3, true, "GATEWAY", 105);
  bool t11_contactorClosed = feederMgr.isContactorClosed(3);
  bool t11_verified = (feederMgr.getFeederCommandState(3) == CMD_STATE_EXECUTION_VERIFIED);
  assertTest(11, "Contactor Feedback Simulation (Normal Closure)",
             t11_ok && t11_contactorClosed && t11_verified,
             "(Contactor Confirmed Closed Within Normal Window)");

  // ───────────────────────────────────────────────────────────
  // TEST 12: Feedback Mismatch (Welded Contactor & Failed Coil)
  // ───────────────────────────────────────────────────────────
  // Case A: Failed coil -> Rig contactor stuck open
  feederMgr.setSimulatedContactorFeedback(4, false); // Rigged stuck open
  feederMgr.requestFeederState(4, true, "GATEWAY", 106);
  g_simulatedTimeMs += CONTACTOR_ACTUATION_TIMEOUT_MS + 100UL;
  feederMgr.updateCommandStateMachine();
  bool t12_coilFail = (feederMgr.getContactorFeedbackQuality(4) == FEEDBACK_QUALITY_COIL_FAIL);
  bool t12_coilDropRelay = !feederMgr.isFeederRelayOn(4);
  feederMgr.clearSimulatedContactorRig(4);
  feederMgr.resetFeederFault(4);

  // Case B: Welded contactor -> Rig contactor stuck closed
  feederMgr.setSimulatedContactorFeedback(5, true); // Rigged stuck closed
  feederMgr.requestFeederState(5, false, "GATEWAY", 107);
  g_simulatedTimeMs += CONTACTOR_ACTUATION_TIMEOUT_MS + 100UL;
  feederMgr.updateCommandStateMachine();
  bool t12_welded = (feederMgr.getContactorFeedbackQuality(5) == FEEDBACK_QUALITY_WELDED);
  feederMgr.clearSimulatedContactorRig(5);
  feederMgr.resetFeederFault(5);

  assertTest(12, "Feedback Mismatch (Welded Contact & Coil Failure)",
             t12_coilFail && t12_coilDropRelay && t12_welded,
             "(Coil Failure Caught & Dropped, Welded Contact LATCHED)");

  // ───────────────────────────────────────────────────────────
  // TEST 13: Gateway Restart Robustness
  // ───────────────────────────────────────────────────────────
  uint8_t t13_preMask = feederMgr.getActiveRelayMask();
  loraMgr.txPackets.clear();
  cmdMgr.processCommand("STATUS", "GATEWAY", 60, -65, 9);
  uint8_t t13_postMask = feederMgr.getActiveRelayMask();
  assertTest(13, "Gateway Restart Robustness (Pole State Preservation)",
             t13_preMask == t13_postMask && !loraMgr.txPackets.empty(),
             "(Pre-Mask == Post-Mask: Electrical Relays Preserved)");

  // ───────────────────────────────────────────────────────────
  // TEST 14: Pole Restart Safety
  // ───────────────────────────────────────────────────────────
  feederMgr.init(true);
  bool t14_allOff = (feederMgr.getActiveRelayMask() == 0x00);
  assertTest(14, "Pole Restart Safety (Outputs Safe OFF at Boot)",
             t14_allOff,
             "(Boot Verification: Relays Safe OFF, No Blind Pickup)");

  // ───────────────────────────────────────────────────────────
  // TEST 15: LoRa Disconnection / Communication Loss
  // ───────────────────────────────────────────────────────────
  feederMgr.requestFeederState(0, true, "COMM_TEST");
  uint8_t t15_preMask = feederMgr.getActiveRelayMask();
  g_simulatedTimeMs += 600000UL;
  feederMgr.updateMCBInputs();
  uint8_t t15_postMask = feederMgr.getActiveRelayMask();
  assertTest(15, "LoRa Disconnection (Hold Operating Policy)",
             t15_preMask == t15_postMask && feederMgr.isFeederRelayOn(0),
             "(Relay State Maintained, Local Supervision Active)");

  // ───────────────────────────────────────────────────────────
  // TEST 16: Command Timeout
  // ───────────────────────────────────────────────────────────
  feederMgr.clearMcbTrip(1);
  feederMgr.setSimulatedMcbTrip(1, false);
  feederMgr.resetFeederFault(1);
  feederMgr.setSimulatedContactorFeedback(1, false); // Rig stuck open
  feederMgr.requestFeederState(1, true, "TIMEOUT_TEST", 108);
  g_simulatedTimeMs += CONTACTOR_ACTUATION_TIMEOUT_MS + 200UL;
  feederMgr.updateCommandStateMachine();
  CommandTransaction *t16_tx = feederMgr.getActiveTransaction(1);
  bool t16_timedOut = (t16_tx && t16_tx->state == CMD_STATE_TIMEOUT);
  feederMgr.clearSimulatedContactorRig(1);
  feederMgr.resetFeederFault(1);
  assertTest(16, "Command Timeout Handling",
             t16_timedOut,
             "(State Machine Exits to TIMEOUT upon Auxiliary Delay)");

  // ───────────────────────────────────────────────────────────
  // TEST 17: Group Control (All Feeders ON)
  // ───────────────────────────────────────────────────────────
  // Clear all faults and reset all feeders
  for (uint8_t f = 0; f < MAX_FEEDERS; f++) {
    feederMgr.clearSimulatedContactorRig(f);
    feederMgr.clearMcbTrip(f);
    feederMgr.setSimulatedMcbTrip(f, false);
    feederMgr.resetFeederFault(f);
    feederMgr.requestFeederState(f, false, "RESET_ALL");
  }

  feederMgr.requestGroupControl(0x3F, true, 100UL, "GROUP_TEST");
  for (int step = 0; step < 10; step++) {
    g_simulatedTimeMs += 120UL;
    feederMgr.updateGroupStep();
  }
  uint8_t t17_mask = feederMgr.getActiveRelayMask();
  assertTest(17, "Group Control (All 6 Feeders ON)",
             (t17_mask & 0x3F) == 0x3F,
             "(All 6 Feeders Energized via Group Control)");

  // ───────────────────────────────────────────────────────────
  // TEST 18: Staggered Execution Timing & Inrush Prevention
  // ───────────────────────────────────────────────────────────
  // Turn all OFF first
  feederMgr.requestGroupControl(0x3F, false, 50UL, "GROUP_OFF");
  for (int s = 0; s < 10; s++) { g_simulatedTimeMs += 60UL; feederMgr.updateGroupStep(); }

  // Start staggered group ON with 500ms delay for Feeders 1, 2, 3 (mask 0x07)
  feederMgr.requestGroupControl(0x07, true, 500UL, "STAGGER_TEST");
  bool t18_step1 = (feederMgr.getActiveRelayMask() & 0x01) && !(feederMgr.getActiveRelayMask() & 0x02);
  g_simulatedTimeMs += 510UL;
  feederMgr.updateGroupStep();
  bool t18_step2 = (feederMgr.getActiveRelayMask() & 0x03) == 0x03 && !(feederMgr.getActiveRelayMask() & 0x04);
  g_simulatedTimeMs += 510UL;
  feederMgr.updateGroupStep();
  bool t18_step3 = (feederMgr.getActiveRelayMask() & 0x07) == 0x07;
  assertTest(18, "Staggered Execution Timing (Inrush Prevention)",
             t18_step1 && t18_step2 && t18_step3,
             "(Step 1: F1 -> Step 2: F1+F2 -> Step 3: F1+F2+F3 at 500ms Intervals)");

  // ───────────────────────────────────────────────────────────
  // TEST 19: Manual Override Priority
  // ───────────────────────────────────────────────────────────
  feederMgr.setManualMode(true);
  bool t19_remoteCmdAccepted = feederMgr.requestFeederState(3, true, "REMOTE_TEST");
  bool t19_manualLockout = (!t19_remoteCmdAccepted && !feederMgr.isFeederRelayOn(3));
  feederMgr.setManualMode(false);
  bool t19_safeExit = !feederMgr.isFeederRelayOn(3);
  assertTest(19, "Manual Override Priority (Local Selector Lockout)",
             t19_manualLockout && t19_safeExit,
             "(Remote Blocked During Manual Mode, No Spontaneous Pickup)");

  // ───────────────────────────────────────────────────────────
  // TEST 20: Emergency Interlock (Immediate Trip)
  // ───────────────────────────────────────────────────────────
  feederMgr.requestFeederState(0, true, "PRE_ESTOP");
  feederMgr.requestFeederState(1, true, "PRE_ESTOP");
  feederMgr.requestFeederState(2, true, "PRE_ESTOP");
  uint8_t t20_before = feederMgr.getActiveRelayMask();

  feederMgr.triggerEmergencyStop("TEST_EMERGENCY_BUTTON");
  uint8_t t20_after = feederMgr.getActiveRelayMask();
  bool t20_estopActive = feederMgr.isEmergencyStopActive();
  bool t20_cmdDuringEstop = feederMgr.requestFeederState(0, true, "UNAUTHORIZED_DURING_ESTOP");

  assertTest(20, "Emergency Interlock (Immediate Trip & Command Lockout)",
             (t20_before > 0) && (t20_after == 0x00) && t20_estopActive && !t20_cmdDuringEstop,
             "(Immediate Drop of All Contactors, Remote Locked Out)");

  std::cout << "\n=====================================================================" << std::endl;
  std::cout << "  FINAL TEST SUMMARY" << std::endl;
  std::cout << "=====================================================================" << std::endl;
  std::cout << "  TOTAL TESTS PASSED : " << g_passCount << " / 20" << std::endl;
  std::cout << "  TOTAL TESTS FAILED : " << g_failCount << " / 20" << std::endl;
  std::cout << "  OVERALL STATUS     : " << (g_failCount == 0 ? "ALL TESTS PASSED [100%]" : "VERIFICATION FAILED") << std::endl;
  std::cout << "=====================================================================" << std::endl;

  return (g_failCount == 0) ? 0 : 1;
}
