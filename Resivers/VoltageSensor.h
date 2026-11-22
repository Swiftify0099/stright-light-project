/*
 * =====================================================================
 *  VOLTAGESENSOR.H — THREE-PHASE HIGH PRECISION VOLTAGE METROLOGY
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#ifndef VOLTAGE_SENSOR_H
#define VOLTAGE_SENSOR_H

#include <Arduino.h>
#include "config.h"
#include "ADS131M08Driver.h"
#include "PowerMeasurementEngine.h"

class VoltageSensor {
public:
  VoltageSensor();
  void init();

  void update();

  float  getVoltageR() const;
  float  getVoltageY() const;
  float  getVoltageB() const;

  String getVoltageStateR() const;
  String getVoltageStateY() const;
  String getVoltageStateB() const;

  bool   isVoltageValidR() const;
  bool   isVoltageValidY() const;
  bool   isVoltageValidB() const;

  float  getGridFrequency() const;
  bool   isThreePhaseBalanced() const;

private:
  unsigned long lastUpdateMs;
};

extern VoltageSensor voltageSensor;

#endif // VOLTAGE_SENSOR_H
