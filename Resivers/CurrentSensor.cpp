/*
 * =====================================================================
 *  CURRENTSENSOR.CPP — 18-CHANNEL MULTI-FEEDER CURRENT & POWER METROLOGY
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#include "CurrentSensor.h"

CurrentSensor currentSensor;

CurrentSensor::CurrentSensor() {}

void CurrentSensor::init() {
  Serial.println(F("[CURRENT] 18-Channel Synchronized Current & Power Metrology Subsystem Ready"));
}

void CurrentSensor::update(float mainsVoltage, bool isLoadActive) {
  // Metrology engine updates current and powers synchronously within computeMetrologyWindow()
  // Triggered via VoltageSensor::update() and timer loops.
}

float CurrentSensor::getCurrent() const {
  return metrologyEngine.getFeederCurrent(1, 'R');
}

float CurrentSensor::getPower() const {
  return metrologyEngine.getFeederTotalPower(1);
}

float CurrentSensor::getEnergyWh() const {
  return metrologyEngine.getFeederEnergyWh(1);
}

String CurrentSensor::getFaultStatus() const {
  return metrologyEngine.getSystemFaultSummary();
}

float CurrentSensor::getFeederCurrent(uint8_t feederId, char phase) const {
  return metrologyEngine.getFeederCurrent(feederId, phase);
}

float CurrentSensor::getFeederPower(uint8_t feederId) const {
  return metrologyEngine.getFeederTotalPower(feederId);
}

float CurrentSensor::getFeederApparentPower(uint8_t feederId) const {
  const FeederPowerState *st = metrologyEngine.getFeederState(feederId);
  return st ? st->sTotal_apparent : 0.0f;
}

float CurrentSensor::getFeederPowerFactor(uint8_t feederId) const {
  const FeederPowerState *st = metrologyEngine.getFeederState(feederId);
  return st ? st->pfTotal : 1.0f;
}

float CurrentSensor::getFeederEnergyWh(uint8_t feederId) const {
  return metrologyEngine.getFeederEnergyWh(feederId);
}

float CurrentSensor::getFeederImbalance(uint8_t feederId) const {
  const FeederPowerState *st = metrologyEngine.getFeederState(feederId);
  return st ? st->currentImbalancePct : 0.0f;
}

bool CurrentSensor::isFeederOverCurrent(uint8_t feederId) const {
  const FeederPowerState *st = metrologyEngine.getFeederState(feederId);
  return st ? st->isOverCurrent : false;
}
