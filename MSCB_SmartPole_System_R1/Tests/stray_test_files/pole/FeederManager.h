/*
 * =====================================================================
 *  FEEDERMANAGER.H — FIELD POLE MULTI-FEEDER & MCB CONTROLLER
 *  MSCB Smart Pole Automation System — Prompt 15 Architecture
 *  Three-Phase Contactor Supervision, MCB Feedback & Electrical Safety
 * =====================================================================
 */

#ifndef FEEDER_MANAGER_H
#define FEEDER_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"
#include "PoleActuators.h"

#define CONTACTOR_ACTUATION_TIMEOUT_MS 1000UL // 1.0s window for mechanical contactor closure
#define DEFAULT_GROUP_STAGGER_MS       500UL  // 500ms delay between staggered contactor steps

class FeederManager {
public:
  FeederManager();
  void init(bool useSafePins = false);

  // 1. Individual Feeder State Requests & Command State Machine
  bool requestFeederState(uint8_t feederIndex, bool turnOn, const String &callerId = "LOCAL",
                          uint8_t commandId = 0, uint32_t expirationMs = 0, uint16_t seq = 0);
  void setFeederRelay(uint8_t feederIndex, bool on, const String &reason = "MANUAL");
  void setMainRelay(bool on, const String &reason = "MANUAL");
  bool isMainRelayOn() const;
  bool isFeederRelayOn(uint8_t feederIndex) const;

  // 2. Group Control & Staggered Switching
  bool requestGroupControl(uint8_t feederMask, bool turnOn, uint32_t staggerDelayMs = DEFAULT_GROUP_STAGGER_MS,
                           const String &callerId = "GROUP_CMD");
  void updateGroupStep();
  bool isGroupControlActive() const;
  const StaggeredGroupControl& getGroupControlStatus() const;

  // 3. MCB & Contactor Sensing with Multi-Sample Debouncing
  void updateMCBInputs();
  void updateContactorFeedback();
  bool isMCBOk(uint8_t feederIndex) const;
  bool isMCBTripped(uint8_t feederIndex) const;
  void clearMcbTrip(uint8_t feederIndex); // Local physical reset confirmation
  bool isContactorClosed(uint8_t feederIndex) const;
  ContactorFeedbackQuality getContactorFeedbackQuality(uint8_t feederIndex) const;
  void resetFeederFault(uint8_t feederIndex);

  // Simulation hooks for automated unit & bench testing
  void setSimulatedContactorFeedback(uint8_t feederIndex, bool closed);
  void clearSimulatedContactorRig(uint8_t feederIndex);
  void setSimulatedMcbTrip(uint8_t feederIndex, bool tripped);
  void enableHardwareSimulation(bool enable);

  // 4. Command State Machine Engine
  void updateCommandStateMachine();
  CommandTransaction* getActiveTransaction(uint8_t feederIndex);
  CommandExecutionState getFeederCommandState(uint8_t feederIndex) const;

  // 5. Manual Override & Emergency Interlock
  void setManualMode(bool active);
  bool isManualMode() const;
  void triggerEmergencyStop(const String &reason = "E_STOP");
  void resetEmergencyStop();
  bool isEmergencyStopActive() const;

  // 6. Three-Phase Metrology Synchronization (Prompt 14 Engine)
  void syncMeasurementsFromMetrology();

  // 7. Telemetry & Status Serialization
  String buildFeederTelemetryString(float vR, float vY, float vB = 0.0f);
  String buildControlStatusString();

  // 8. Feeder Deactivation (Prompt 12 Lifecycle)
  void deactivateFeeder(uint8_t feederIndex);
  bool isFeederDeactivated(uint8_t feederIndex) const;

  // 9. Status & Mask Accessors
  FeederHardwareState* getFeederState(uint8_t feederIndex);
  uint8_t getActiveRelayMask() const;
  uint8_t getActiveContactorMask() const;
  uint8_t getMcbTripMask() const;

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

  void executeSafetyCheck(uint8_t feederIndex, CommandTransaction &tx);
  void verifyContactorTransition(uint8_t feederIndex, CommandTransaction &tx);
  void verifyElectricalMeasurements(uint8_t feederIndex, CommandTransaction &tx);
};

extern FeederManager feederMgr;

#endif // FEEDER_MANAGER_H
