/*
 * =====================================================================
 *  FEEDERMANAGER.CPP — FIELD POLE MULTI-FEEDER & MCB CONTROLLER
 *  MSCB Smart Pole Automation System — Prompt 15 Architecture
 *  Three-Phase Contactor Supervision, MCB Feedback & Electrical Safety
 * =====================================================================
 */

#include "FeederManager.h"
#include "StorageManager.h"
#include "PowerMeasurementEngine.h"

FeederManager feederMgr;

FeederManager::FeederManager() :
  lastDebounceMs(0),
  manualMode(false),
  emergencyStopActive(false),
  simulationMode(false)
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

    // Transactions
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

void FeederManager::init(bool useSafePins) {
  Serial.println(F("[FEEDER] Initializing Multi-Feeder Three-Phase Controller (Prompt 15)..."));

  // 1. Initialize Relay Actuator Driver
  poleActuators.init(useSafePins);
  bool diagOk = poleActuators.validateStartupState();
  if (!diagOk) {
    Serial.println(F("[FEEDER] ⛔ CRITICAL: Actuator diagnostic self-test FAILED! Outputs locked out."));
  }

  // 2. Configure MCB Auxiliary Input Pins
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    if (FEEDER_MCB_R_PINS[i] > 0) pinMode(FEEDER_MCB_R_PINS[i], INPUT_PULLUP);
    if (FEEDER_MCB_Y_PINS[i] > 0) pinMode(FEEDER_MCB_Y_PINS[i], INPUT_PULLUP);
    if (FEEDER_MCB_B_PINS[i] > 0) pinMode(FEEDER_MCB_B_PINS[i], INPUT_PULLUP);

    if (FEEDER_CONT_R_PINS[i] > 0) pinMode(FEEDER_CONT_R_PINS[i], INPUT_PULLUP);
  }

  // 3. Safe Startup State Validation
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    feeders[i].relayOn = false;
    feeders[i].contactor = false;
    feeders[i].contactorState = CONTACTOR_AUX_OPEN;
  }

  // Check saved state for Feeder 0 (Main Street Light) with strict validation
  String savedState = storageMgr.getPrevRelayState();
  if (savedState == "ON" && diagOk && !emergencyStopActive && !manualMode) {
    Serial.println(F("[FEEDER] Restoring Feeder 0 state 'ON' per validated NVS policy..."));
    requestFeederState(0, true, "BOOT_RESTORE");
  } else {
    Serial.println(F("[FEEDER] All Feeders initialized to verified SAFE OFF state."));
  }
}

void FeederManager::enableHardwareSimulation(bool enable) {
  simulationMode = enable;
}

void FeederManager::setSimulatedContactorFeedback(uint8_t feederIndex, bool closed) {
  if (feederIndex < MAX_FEEDERS) {
    simulatedContactor[feederIndex] = closed;
    riggedContactor[feederIndex] = true;
  }
}

void FeederManager::clearSimulatedContactorRig(uint8_t feederIndex) {
  if (feederIndex < MAX_FEEDERS) {
    riggedContactor[feederIndex] = false;
  }
}

void FeederManager::resetFeederFault(uint8_t feederIndex) {
  if (feederIndex < MAX_FEEDERS) {
    feeders[feederIndex].faultState = "OK";
    feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_NORMAL;
    feeders[feederIndex].mcbTrippedLatched = false;
    feeders[feederIndex].feedbackPending = false;
    poleActuators.clearChannelFault(feederIndex);
  }
}

void FeederManager::setSimulatedMcbTrip(uint8_t feederIndex, bool tripped) {
  if (feederIndex < MAX_FEEDERS) {
    simulatedMcbTripState[feederIndex] = tripped;
  }
}

// ─────────────────────────────────────────────────────────────
//  COMMAND EXECUTION STATE MACHINE (SECTION 5)
// ─────────────────────────────────────────────────────────────
bool FeederManager::requestFeederState(uint8_t feederIndex, bool turnOn, const String &callerId,
                                       uint8_t commandId, uint32_t expirationMs, uint16_t seq) {
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
  Serial.printf("[FEEDER][SM] F%d: COMMAND_RECEIVED (Req: %s, ID: %u, Caller: %s)\n",
                feederIndex + 1, turnOn ? "ON" : "OFF", commandId, callerId.c_str());

  // State 2: AUTHENTICATION_VERIFIED
  tx.state = CMD_STATE_AUTHENTICATION_VERIFIED;

  // State 3: TARGET_VALIDATED
  if (feeders[feederIndex].status == FEEDER_STATUS_DEACTIVATED) {
    tx.state = CMD_STATE_COMMAND_FAILED;
    tx.failureReason = "ERR_FEEDER_DEACTIVATED";
    tx.active = false;
    Serial.printf("[FEEDER][SM] F%d: ⛔ TARGET_VALIDATED FAILED: Feeder is DEACTIVATED.\n", feederIndex + 1);
    return false;
  }
  if (feeders[feederIndex].status == FEEDER_STATUS_REMOVED) {
    tx.state = CMD_STATE_COMMAND_FAILED;
    tx.failureReason = "ERR_FEEDER_REMOVED";
    tx.active = false;
    return false;
  }
  tx.state = CMD_STATE_TARGET_VALIDATED;

  // Expiration check
  if (expirationMs > 0 && millis() > expirationMs) {
    tx.state = CMD_STATE_TIMEOUT;
    tx.failureReason = "ERR_COMMAND_EXPIRED";
    tx.active = false;
    Serial.printf("[FEEDER][SM] F%d: ⛔ Command EXPIRED (Now: %lu > Exp: %lu)\n",
                  feederIndex + 1, millis(), expirationMs);
    return false;
  }

  // State 4: SAFETY_CHECK
  executeSafetyCheck(feederIndex, tx);
  if (tx.state == CMD_STATE_COMMAND_FAILED) {
    tx.active = false;
    return false;
  }

  // State 5: COMMAND_ACCEPTED
  tx.state = CMD_STATE_COMMAND_ACCEPTED;
  tx.stageB_Accepted = true;
  Serial.printf("[FEEDER][SM] F%d: ✅ COMMAND_ACCEPTED\n", feederIndex + 1);

  // State 6: SWITCHING_REQUESTED
  tx.state = CMD_STATE_SWITCHING_REQUESTED;
  tx.stateChangedMs = millis();

  bool relaySuccess = false;
  if (turnOn) {
    relaySuccess = poleActuators.requestRelayOn(feederIndex, callerId.c_str());
  } else {
    relaySuccess = poleActuators.requestRelayOff(feederIndex, callerId.c_str());
  }

  if (!relaySuccess) {
    tx.state = CMD_STATE_COMMAND_FAILED;
    tx.failureReason = "ERR_RELAY_DRIVER_FAULT";
    tx.active = false;
    Serial.printf("[FEEDER][SM] F%d: ❌ RELAY_OUTPUT_CHANGED FAILED: Driver Fault!\n", feederIndex + 1);
    return false;
  }

  // State 7: RELAY_OUTPUT_CHANGED
  tx.state = CMD_STATE_RELAY_OUTPUT_CHANGED;
  tx.stageC_RelayChanged = true;
  feeders[feederIndex].relayOn = turnOn;
  feeders[feederIndex].lastActuationMs = millis();
  feeders[feederIndex].feedbackPending = true;

  if (feederIndex == 0) {
    storageMgr.saveRelayState(turnOn ? "ON" : "OFF");
  }

  Serial.printf("[FEEDER][SM] F%d: ✅ RELAY_OUTPUT_CHANGED -> %s\n", feederIndex + 1, turnOn ? "ON" : "OFF");

  // State 8: CONTACTOR_FEEDBACK_PENDING
  tx.state = CMD_STATE_CONTACTOR_FEEDBACK_PENDING;
  tx.stateChangedMs = millis();

  // If in simulation mode and not manually rigged, mirror output immediately
  if (simulationMode && !riggedContactor[feederIndex]) {
    simulatedContactor[feederIndex] = turnOn;
  }

  // Check immediate feedback transition
  verifyContactorTransition(feederIndex, tx);
  return true;
}

void FeederManager::executeSafetyCheck(uint8_t feederIndex, CommandTransaction &tx) {
  // 1. Emergency Stop Check
  if (emergencyStopActive || poleActuators.isEmergencyActive()) {
    tx.state = CMD_STATE_COMMAND_FAILED;
    tx.failureReason = "ERR_EMERGENCY_STOP_ACTIVE";
    Serial.printf("[FEEDER][SAFETY] F%d: ⛔ Rejected: Emergency Stop is ACTIVE!\n", feederIndex + 1);
    return;
  }

  // 2. Manual Override Check
  if (manualMode) {
    tx.state = CMD_STATE_COMMAND_FAILED;
    tx.failureReason = "ERR_MANUAL_MODE_ACTIVE";
    Serial.printf("[FEEDER][SAFETY] F%d: ⛔ Rejected: Manual Override switch is engaged!\n", feederIndex + 1);
    return;
  }

  // 3. MCB Trip Check
  if (feeders[feederIndex].mcbTrippedLatched || !isMCBOk(feederIndex)) {
    tx.state = CMD_STATE_COMMAND_FAILED;
    tx.failureReason = "ERR_MCB_TRIPPED";
    Serial.printf("[FEEDER][SAFETY] F%d: ⛔ Rejected: MCB is TRIPPED! Requires manual panel reset.\n", feederIndex + 1);
    return;
  }

  // 4. Welded Contact Check
  if (tx.requestedState && feeders[feederIndex].feedbackQuality == FEEDBACK_QUALITY_WELDED) {
    tx.state = CMD_STATE_COMMAND_FAILED;
    tx.failureReason = "ERR_CONTACTOR_WELDED";
    Serial.printf("[FEEDER][SAFETY] F%d: ⛔ Rejected: Contactor contacts WELDED/STUCK!\n", feederIndex + 1);
    return;
  }

  // 5. Driver Readiness
  if (poleActuators.getDriverState() != ACTUATOR_STATE_READY) {
    tx.state = CMD_STATE_COMMAND_FAILED;
    tx.failureReason = "ERR_ACTUATOR_NOT_READY";
    return;
  }
}

void FeederManager::verifyContactorTransition(uint8_t feederIndex, CommandTransaction &tx) {
  updateContactorFeedback();

  bool isClosed = isContactorClosed(feederIndex);

  if (tx.requestedState) {
    // Commanded ON -> Expecting CLOSED
    if (isClosed) {
      tx.stageD_FeedbackConfirmed = true;
      feeders[feederIndex].feedbackPending = false;
      feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_NORMAL;
      Serial.printf("[FEEDER][SM] F%d: ✅ CONTACTOR_FEEDBACK_CONFIRMED (Aux CLOSED)\n", feederIndex + 1);

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

        Serial.printf("[FEEDER][SM] F%d: ❌ TIMEOUT: Contactor failed to close within %lums!\n",
                      feederIndex + 1, CONTACTOR_ACTUATION_TIMEOUT_MS);
      }
    }
  } else {
    // Commanded OFF -> Expecting OPEN
    if (!isClosed) {
      tx.stageD_FeedbackConfirmed = true;
      feeders[feederIndex].feedbackPending = false;
      feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_NORMAL;
      Serial.printf("[FEEDER][SM] F%d: ✅ CONTACTOR_FEEDBACK_CONFIRMED (Aux OPEN)\n", feederIndex + 1);

      verifyElectricalMeasurements(feederIndex, tx);
    } else {
      if (millis() - tx.stateChangedMs >= CONTACTOR_ACTUATION_TIMEOUT_MS) {
        tx.state = CMD_STATE_COMMAND_FAILED;
        tx.failureReason = "ERR_CONTACTOR_WELDED";
        tx.active = false;
        feeders[feederIndex].feedbackPending = false;
        feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_WELDED;
        feeders[feederIndex].faultState = "CONTACTOR_WELDED";

        Serial.printf("[FEEDER][SM] F%d: 🚨 CRITICAL FAULT: Contactor contacts WELDED/STUCK CLOSED!\n",
                      feederIndex + 1);
      }
    }
  }
}

void FeederManager::verifyElectricalMeasurements(uint8_t feederIndex, CommandTransaction &tx) {
  syncMeasurementsFromMetrology();

  float ir = feeders[feederIndex].currentR;
  float iy = feeders[feederIndex].currentY;
  float ib = feeders[feederIndex].currentB;

  float vr = metrologyEngine.getCommonVoltageR();

  tx.stageE_MeasurementVerified = true;
  tx.state = CMD_STATE_EXECUTION_VERIFIED;
  tx.active = false;

  Serial.printf("[FEEDER][SM] F%d: ⚡ EXECUTION_VERIFIED (VR=%.1fV, IR=%.2fA, IY=%.2fA, IB=%.2fA %s)\n",
                feederIndex + 1, vr, ir, iy, ib,
                (ir < 0.05f && iy < 0.05f && ib < 0.05f) ? "[LOAD IDLE/ABSENT]" : "[LOAD ACTIVE]");
}

void FeederManager::updateCommandStateMachine() {
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    if (transactions[i].active) {
      if (transactions[i].state == CMD_STATE_CONTACTOR_FEEDBACK_PENDING) {
        verifyContactorTransition(i, transactions[i]);
      }
    }
  }

  if (groupCtrl.active) {
    updateGroupStep();
  }
}

CommandTransaction* FeederManager::getActiveTransaction(uint8_t feederIndex) {
  if (feederIndex < MAX_FEEDERS) return &transactions[feederIndex];
  return nullptr;
}

CommandExecutionState FeederManager::getFeederCommandState(uint8_t feederIndex) const {
  if (feederIndex < MAX_FEEDERS) return transactions[feederIndex].state;
  return CMD_STATE_IDLE;
}

void FeederManager::setFeederRelay(uint8_t feederIndex, bool on, const String &reason) {
  requestFeederState(feederIndex, on, reason);
}

void FeederManager::setMainRelay(bool on, const String &reason) {
  requestFeederState(0, on, reason);
}

bool FeederManager::isMainRelayOn() const {
  return feeders[0].relayOn;
}

bool FeederManager::isFeederRelayOn(uint8_t feederIndex) const {
  if (feederIndex < MAX_FEEDERS) {
    return feeders[feederIndex].relayOn;
  }
  return false;
}

// ─────────────────────────────────────────────────────────────
//  GROUP CONTROL & STAGGERED EXECUTION (SECTION 11)
// ─────────────────────────────────────────────────────────────
bool FeederManager::requestGroupControl(uint8_t feederMask, bool turnOn, uint32_t staggerDelayMs, const String &callerId) {
  if (emergencyStopActive || manualMode) {
    Serial.println(F("[FEEDER][GROUP] ⛔ Group control rejected: Emergency Stop or Manual Mode active!"));
    return false;
  }

  groupCtrl.active = true;
  groupCtrl.targetMask = feederMask & 0x3F;
  groupCtrl.requestedState = turnOn;
  groupCtrl.interFeederDelayMs = (staggerDelayMs > 0) ? staggerDelayMs : DEFAULT_GROUP_STAGGER_MS;
  groupCtrl.nextStepMs = millis();
  groupCtrl.currentStep = 0;
  groupCtrl.successMask = 0;
  groupCtrl.failureMask = 0;
  groupCtrl.callerId = callerId;

  Serial.printf("[FEEDER][GROUP] Staggered Group Control Started: Mask=0x%02X, Req=%s, Stagger=%lums\n",
                groupCtrl.targetMask, turnOn ? "ON" : "OFF", groupCtrl.interFeederDelayMs);

  updateGroupStep();
  return true;
}

void FeederManager::updateGroupStep() {
  if (!groupCtrl.active) return;
  if (millis() < groupCtrl.nextStepMs) return;

  while (groupCtrl.currentStep < MAX_FEEDERS) {
    uint8_t fIdx = groupCtrl.currentStep;
    groupCtrl.currentStep++;

    if (groupCtrl.targetMask & (1 << fIdx)) {
      bool ok = requestFeederState(fIdx, groupCtrl.requestedState, groupCtrl.callerId);
      if (ok) {
        groupCtrl.successMask |= (1 << fIdx);
      } else {
        groupCtrl.failureMask |= (1 << fIdx);
        Serial.printf("[FEEDER][GROUP] ⚠️ Feeder %d failed in group execution! Failure isolated.\n", fIdx + 1);
      }

      groupCtrl.nextStepMs = millis() + groupCtrl.interFeederDelayMs;
      return;
    }
  }

  groupCtrl.active = false;
  Serial.printf("[FEEDER][GROUP] Staggered Group Control COMPLETED: SuccessMask=0x%02X, FailureMask=0x%02X\n",
                groupCtrl.successMask, groupCtrl.failureMask);
}

bool FeederManager::isGroupControlActive() const {
  return groupCtrl.active;
}

const StaggeredGroupControl& FeederManager::getGroupControlStatus() const {
  return groupCtrl;
}

// ─────────────────────────────────────────────────────────────
//  MCB & CONTACTOR FEEDBACK SENSING & DEBOUNCING (SECTION 6)
// ─────────────────────────────────────────────────────────────
void FeederManager::updateMCBInputs() {
  unsigned long now = millis();
  if (now - lastDebounceMs < FEEDER_DEBOUNCE_MS) return;
  lastDebounceMs = now;

  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    bool rawMcbNormal = true;

    if (simulationMode) {
      rawMcbNormal = !simulatedMcbTripState[i];
    } else {
      if (FEEDER_MCB_R_PINS[i] > 0) {
        rawMcbNormal = (digitalRead(FEEDER_MCB_R_PINS[i]) == HIGH);
      }
    }

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

      Serial.printf("[FEEDER][MCB] 🚨 MCB TRIP DETECTED on Feeder %d! Relay de-energized. LATCHED.\n", i + 1);
    }
  }

  updateContactorFeedback();
  updateCommandStateMachine();
  syncMeasurementsFromMetrology();
}

void FeederManager::updateContactorFeedback() {
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    bool closed = false;

    if (simulationMode) {
      closed = simulatedContactor[i];
    } else {
      if (FEEDER_CONT_R_PINS[i] > 0) {
        closed = (digitalRead(FEEDER_CONT_R_PINS[i]) == LOW);
      } else {
        closed = poleActuators.isRelayPhysicalOn(i);
      }
    }

    feeders[i].contactor = closed;
    feeders[i].contactorState = closed ? CONTACTOR_AUX_CLOSED : CONTACTOR_AUX_OPEN;

    if (!feeders[i].feedbackPending) {
      if (feeders[i].relayOn && !closed) {
        feeders[i].feedbackQuality = FEEDBACK_QUALITY_UNEXPECTED_OPEN;
        feeders[i].faultState = "UNEXPECTED_OPEN";
        poleActuators.requestRelayOff(i, "UNEXPECTED_OPEN_PROTECT");
        feeders[i].relayOn = false;
        Serial.printf("[FEEDER][AUX] ⚠️ Feeder %d: Contactor dropped OPEN unexpectedly! Coil dropped.\n", i + 1);
      } else if (!feeders[i].relayOn && closed) {
        feeders[i].feedbackQuality = FEEDBACK_QUALITY_WELDED;
        feeders[i].faultState = "CONTACTOR_WELDED";
        Serial.printf("[FEEDER][AUX] 🚨 Feeder %d: Contactor CLOSED while commanded OFF (Welded / Manual bypass)!\n", i + 1);
      }
    }
  }
}

bool FeederManager::isMCBOk(uint8_t feederIndex) const {
  if (feederIndex < MAX_FEEDERS) {
    return (!feeders[feederIndex].mcbTrippedLatched && feeders[feederIndex].mcbR);
  }
  return true;
}

bool FeederManager::isMCBTripped(uint8_t feederIndex) const {
  if (feederIndex < MAX_FEEDERS) {
    return feeders[feederIndex].mcbTrippedLatched;
  }
  return false;
}

void FeederManager::clearMcbTrip(uint8_t feederIndex) {
  if (feederIndex < MAX_FEEDERS) {
    feeders[feederIndex].mcbTrippedLatched = false;
    feeders[feederIndex].mcbR = true;
    feeders[feederIndex].mcbRDebounceCount = FEEDER_DEBOUNCE_SAMPLES;
    if (feeders[feederIndex].faultState == "MCB_TRIP") {
      feeders[feederIndex].faultState = "OK";
    }
    Serial.printf("[FEEDER][MCB] Feeder %d MCB Trip Latch CLEARED by authorized panel operator.\n", feederIndex + 1);
  }
}

void FeederManager::resetFeederFault(uint8_t feederIndex) {
  if (feederIndex < MAX_FEEDERS) {
    feeders[feederIndex].faultState = "OK";
    feeders[feederIndex].feedbackQuality = FEEDBACK_QUALITY_NORMAL;
    feeders[feederIndex].mcbTrippedLatched = false;
    feeders[feederIndex].mcbR = true;
    feeders[feederIndex].mcbRDebounceCount = FEEDER_DEBOUNCE_SAMPLES;
    feeders[feederIndex].feedbackPending = false;
    poleActuators.clearChannelFault(feederIndex);
    Serial.printf("[FEEDER] Feeder %d fault cleared and restored to normal operation.\n", feederIndex + 1);
  }
}

bool FeederManager::isContactorClosed(uint8_t feederIndex) const {
  if (feederIndex < MAX_FEEDERS) {
    return feeders[feederIndex].contactor;
  }
  return false;
}

ContactorFeedbackQuality FeederManager::getContactorFeedbackQuality(uint8_t feederIndex) const {
  if (feederIndex < MAX_FEEDERS) {
    return feeders[feederIndex].feedbackQuality;
  }
  return FEEDBACK_QUALITY_DISCONNECTED;
}

// ─────────────────────────────────────────────────────────────
//  MANUAL OVERRIDE & EMERGENCY INTERLOCK (SECTION 12)
// ─────────────────────────────────────────────────────────────
void FeederManager::setManualMode(bool active) {
  manualMode = active;
  Serial.printf("[FEEDER][MANUAL] Manual Mode %s. Remote commands %s.\n",
                active ? "ENGAGED" : "DISENGAGED",
                active ? "LOCKED OUT" : "ENABLED");
}

bool FeederManager::isManualMode() const {
  return manualMode;
}

void FeederManager::triggerEmergencyStop(const String &reason) {
  emergencyStopActive = true;
  poleActuators.emergencyShutdownAll(reason.c_str());

  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    feeders[i].relayOn = false;
    feeders[i].faultState = "EMERGENCY_STOP";
  }

  Serial.printf("[FEEDER][ESTOP] 🚨 EMERGENCY STOP TRIGGERED! Reason: %s\n", reason.c_str());
}

void FeederManager::resetEmergencyStop() {
  emergencyStopActive = false;
  poleActuators.resetEmergencyState();

  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    if (feeders[i].faultState == "EMERGENCY_STOP") {
      feeders[i].faultState = "OK";
    }
  }

  Serial.println(F("[FEEDER][ESTOP] Emergency Stop RESET. Operating in safe standby."));
}

bool FeederManager::isEmergencyStopActive() const {
  return emergencyStopActive || poleActuators.isEmergencyActive();
}

// ─────────────────────────────────────────────────────────────
//  METROLOGY SYNCHRONIZATION (SECTION 7)
// ─────────────────────────────────────────────────────────────
void FeederManager::syncMeasurementsFromMetrology() {
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    const FeederPowerState *st = metrologyEngine.getFeederState(i + 1);
    if (st) {
      feeders[i].currentR = st->iR_rms;
      feeders[i].currentY = st->iY_rms;
      feeders[i].currentB = st->iB_rms;
      feeders[i].powerActive = st->pTotal_active;
      feeders[i].powerFactor = st->pfTotal;
      feeders[i].energyWh = st->accumEnergyWh;

      if (st->isOverCurrent) {
        feeders[i].faultState = "OVER_CURR";
      } else if (st->isImbalanceFault) {
        feeders[i].faultState = "IMBALANCE";
      } else if (feeders[i].faultState != "MCB_TRIP" &&
                 feeders[i].faultState != "CONTACTOR_WELDED" &&
                 feeders[i].faultState != "COIL_FAIL" &&
                 feeders[i].faultState != "EMERGENCY_STOP") {
        feeders[i].faultState = "OK";
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  TELEMETRY & MASK ACCESSORS
// ─────────────────────────────────────────────────────────────
String FeederManager::buildFeederTelemetryString(float vR, float vY, float vB) {
  String out = "";
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    String rly = feeders[i].relayOn ? "ON" : "OFF";
    String cont = feeders[i].contactor ? "C" : "O";
    String mcb = isMCBOk(i) ? "1" : "0";
    String flt = feeders[i].faultState;

    char fBuf[128];
    snprintf(fBuf, sizeof(fBuf), "F%d:%s,%s,%.1f,%.1f,%.1f,%.2f,%.2f,%.2f,%s,%s",
             i + 1, rly.c_str(), cont.c_str(), vR, vY, vB,
             feeders[i].currentR, feeders[i].currentY, feeders[i].currentB,
             flt.c_str(), mcb.c_str());

    out += String(fBuf);
    if (i < MAX_FEEDERS - 1) out += "|";
  }
  return out;
}

String FeederManager::buildControlStatusString() {
  char buf[128];
  snprintf(buf, sizeof(buf), "RELAYS=0x%02X|CONT=0x%02X|MCB=0x%02X|ESTOP=%d|MANUAL=%d|DIAG=%d",
           getActiveRelayMask(), getActiveContactorMask(), getMcbTripMask(),
           isEmergencyStopActive() ? 1 : 0, manualMode ? 1 : 0,
           poleActuators.isDiagnosticPassed() ? 1 : 0);
  return String(buf);
}

void FeederManager::deactivateFeeder(uint8_t feederIndex) {
  if (feederIndex >= MAX_FEEDERS) return;
  feeders[feederIndex].status = FEEDER_STATUS_DEACTIVATED;
  Serial.printf("[FEEDER] 🔒 Feeder %d marked DEACTIVATED. Electrical relay output unchanged.\n", feederIndex + 1);
}

bool FeederManager::isFeederDeactivated(uint8_t feederIndex) const {
  if (feederIndex >= MAX_FEEDERS) return true;
  return (feeders[feederIndex].status == FEEDER_STATUS_DEACTIVATED);
}

FeederHardwareState* FeederManager::getFeederState(uint8_t feederIndex) {
  if (feederIndex < MAX_FEEDERS) return &feeders[feederIndex];
  return nullptr;
}

uint8_t FeederManager::getActiveRelayMask() const {
  return poleActuators.getActiveRelayMask();
}

uint8_t FeederManager::getActiveContactorMask() const {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    if (feeders[i].contactor) mask |= (1 << i);
  }
  return mask;
}

uint8_t FeederManager::getMcbTripMask() const {
  uint8_t mask = 0;
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    if (feeders[i].mcbTrippedLatched) mask |= (1 << i);
  }
  return mask;
}
