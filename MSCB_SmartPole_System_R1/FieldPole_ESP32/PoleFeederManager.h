/**
 * @file PoleFeederManager.h
 * @brief MSCB Smart Pole Node - Six-Feeder Command & Auxiliary Feedback Manager
 * @version 1.0.0
 * 
 * Enforces:
 * - Individual, group, and all-feeder switching.
 * - Staggered activation to prevent inrush current surges.
 * - Command state progression:
 *   SENT -> RECEIVED -> ACCEPTED -> OUTPUT_CHANGED -> AUXILIARY_FEEDBACK_VERIFIED -> COMPLETED
 * - Contactor auxiliary feedback via MCP23S17 / hardware.
 * - Strict prohibition: GPIO readback is never treated as contactor feedback.
 * - If feedback hardware absent: reports FEEDBACK_NOT_CONNECTED / FEEDBACK_UNAVAILABLE.
 * - Production relay lock enabled by default (isolated DC bench test only).
 */

#ifndef POLE_FEEDER_MANAGER_H
#define POLE_FEEDER_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "PoleConfig.h"
#include "PoleBoardPins.h"
#include "PoleActuators.h"
#include "PoleStorageManager.h"
#include "PoleMeshRouter.h"
#if __has_include("../MSCBShared/CommandSecurity.h")
  #include "../MSCBShared/Protocol.h"
  #include "../MSCBShared/NetworkTypes.h"
  #include "../MSCBShared/CommandSecurity.h"
#elif __has_include(<CommandSecurity.h>)
  #include <Protocol.h>
  #include <NetworkTypes.h>
  #include <CommandSecurity.h>
#else
  #include "Protocol.h"
  #include "NetworkTypes.h"
  #include "CommandSecurity.h"
#endif

#define POLE_FEEDER_STAGGER_INTERVAL_MS 500UL

class PoleFeederManager {
public:
  PoleFeederManager();

  bool begin();
  void service(); // Staggered switching ticker

  // Command Processing
  bool processCommandPayload(const PktFeederCmdPayload &cmd);

  // Isolated Low-Voltage DC Commissioning & Bench Test Mode
  void setBenchTestMode(bool enabled) { 
    benchTestMode = enabled; 
    poleActuators.setBenchTestOverride(enabled);
  }
  bool isBenchTestMode() const { return benchTestMode; }
  void setLocalPhysicalAuthorization(bool authorized) {
    poleActuators.setLocalPhysicalAuthorization(authorized);
  }
  bool isLocalPhysicalAuthorized() const {
    return poleActuators.isLocalPhysicalAuthorized();
  }

  // Hardware Auxiliary Feedback Interrogation
  bool isAuxHardwareDetected() const { return auxHardwareDetected; }
  uint8_t readContactorAuxMask();
  uint8_t readMcbTripMask();

  // Actuation API
  FeederCommandAckState executeFeederSwitch(uint8_t feederId1Based, uint8_t action);
  void executeGroupSwitch(uint8_t groupMask, uint8_t action);
  void executeAllSwitch(uint8_t action);
  void emergencyShutdownAll(const char* reason = "EMERGENCY_SHUTDOWN");

private:
  bool detectMcp23s17();
  void sendAckUplink(uint8_t feederId, uint32_t seqNumber, FeederCommandAckState ackState);

  bool    benchTestMode;
  bool    auxHardwareDetected;

  // Stagger queue
  uint8_t pendingStaggerMask;
  uint8_t pendingStaggerAction;
  uint32_t lastStaggerStepMs;
  uint32_t activeCommandSeq;
};

extern PoleFeederManager poleFeederMgr;

#endif // POLE_FEEDER_MANAGER_H

