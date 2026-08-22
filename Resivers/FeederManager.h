/*
 * =====================================================================
 *  FEEDERMANAGER.H — FIELD POLE MULTI-FEEDER & MCB CONTROLLER
 * =====================================================================
 */

#ifndef FEEDER_MANAGER_H
#define FEEDER_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

class FeederManager {
public:
  FeederManager();
  void init();

  // Relay control
  void setFeederRelay(uint8_t feederIndex, bool on, const String &reason = "MANUAL");
  void setMainRelay(bool on, const String &reason = "MANUAL");
  bool isMainRelayOn() const;
  bool isFeederRelayOn(uint8_t feederIndex) const;

  // MCB & Contactor sensing
  void updateMCBInputs();
  bool isMCBOk(uint8_t feederIndex) const;

  // Telemetry serialization
  String buildFeederTelemetryString(float vR, float vY);

  FeederHardwareState* getFeederState(uint8_t feederIndex);

private:
  FeederHardwareState feeders[MAX_FEEDERS];
  unsigned long lastDebounceMs;
};

extern FeederManager feederMgr;

#endif // FEEDER_MANAGER_H
