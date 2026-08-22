/*
 * =====================================================================
 *  CURRENTSENSOR.H — ACS712 CURRENT & ENERGY METERING MODULE
 * =====================================================================
 */

#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include <Arduino.h>
#include "config.h"

class CurrentSensor {
public:
  CurrentSensor();
  void init();

  void update(float mainsVoltage, bool isLoadActive);

  float  getCurrent() const;
  float  getPower() const;
  float  getEnergyWh() const;
  String getFaultStatus() const;

private:
  float  currentA;
  float  powerW;
  float  energyWh;
  unsigned long lastEnergyMs;
  String faultStatus;
};

extern CurrentSensor currentSensor;

#endif // CURRENT_SENSOR_H
