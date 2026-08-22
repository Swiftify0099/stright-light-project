/*
 * =====================================================================
 *  CURRENTSENSOR.CPP — ACS712 CURRENT & ENERGY METERING MODULE
 * =====================================================================
 */

#include "CurrentSensor.h"
#include <math.h>

CurrentSensor currentSensor;

CurrentSensor::CurrentSensor() :
  currentA(0.0f), powerW(0.0f), energyWh(0.0f), lastEnergyMs(0), faultStatus("OK")
{}

void CurrentSensor::init() {
  pinMode(CURRENT_PIN, INPUT);
}

void CurrentSensor::update(float mainsVoltage, bool isLoadActive) {
  uint32_t sumIsq = 0;
  const int SAMPLES = 200;

  for (int i = 0; i < SAMPLES; i++) {
    int adc = analogRead(CURRENT_PIN);
    int diff = adc - ACS712_ZERO_ADC;
    sumIsq += (uint32_t)(diff * diff);
    delayMicroseconds(100);
  }

  float meanIsq = (float)sumIsq / SAMPLES;
  float rawIrms = sqrt(meanIsq) * (ADC_VREF / ADC_MAX);
  currentA = rawIrms / ACS712_SENSITIVITY;

  // Noise floor threshold
  if (currentA < 0.08f) currentA = 0.0f;

  powerW = mainsVoltage * currentA;

  unsigned long now = millis();
  if (lastEnergyMs > 0 && now > lastEnergyMs) {
    float hours = (now - lastEnergyMs) / 3600000.0f;
    energyWh += powerW * hours;
  }
  lastEnergyMs = now;

  // Fault determination
  if (currentA > 16.0f) {
    faultStatus = "OVER_CURR";
  } else if (isLoadActive && currentA < 0.05f && mainsVoltage > 200.0f) {
    faultStatus = "LAMP_FAIL";
  } else {
    faultStatus = "OK";
  }
}

float  CurrentSensor::getCurrent() const { return currentA; }
float  CurrentSensor::getPower() const { return powerW; }
float  CurrentSensor::getEnergyWh() const { return energyWh; }
String CurrentSensor::getFaultStatus() const { return faultStatus; }
