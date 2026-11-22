/*
 * =====================================================================
 *  CURRENTSENSOR.H — 18-CHANNEL MULTI-FEEDER CURRENT & POWER METROLOGY
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include <Arduino.h>
#include "config.h"
#include "PowerMeasurementEngine.h"

class CurrentSensor {
public:
  CurrentSensor();
  void init();

  void update(float mainsVoltage = 230.0f, bool isLoadActive = true);

  // Backward-compatible Feeder 1 queries
  float  getCurrent() const;
  float  getPower() const;
  float  getEnergyWh() const;
  String getFaultStatus() const;

  // Prompt 14 Multi-Feeder Multi-Phase Extensions
  float  getFeederCurrent(uint8_t feederId, char phase) const;
  float  getFeederPower(uint8_t feederId) const;
  float  getFeederApparentPower(uint8_t feederId) const;
  float  getFeederPowerFactor(uint8_t feederId) const;
  float  getFeederEnergyWh(uint8_t feederId) const;
  float  getFeederImbalance(uint8_t feederId) const;
  bool   isFeederOverCurrent(uint8_t feederId) const;
};

extern CurrentSensor currentSensor;

#endif // CURRENT_SENSOR_H
