/*
 * =====================================================================
 *  VOLTAGESENSOR.H — MULTI-PHASE ZMPT101B VOLTAGE SENSING MODULE
 * =====================================================================
 */

#ifndef VOLTAGE_SENSOR_H
#define VOLTAGE_SENSOR_H

#include <Arduino.h>
#include "config.h"

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

private:
  float  voltageR;
  float  voltageY;
  float  voltageB;

  String voltageStateR;
  String voltageStateY;
  String voltageStateB;

  void sampleChannel(uint8_t pin, float &outVoltage, String &outState);
};

extern VoltageSensor voltageSensor;

#endif // VOLTAGE_SENSOR_H
