/*
 * =====================================================================
 *  VOLTAGESENSOR.CPP — THREE-PHASE HIGH PRECISION VOLTAGE METROLOGY
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#include "VoltageSensor.h"
#include <math.h>

VoltageSensor voltageSensor;

VoltageSensor::VoltageSensor() : lastUpdateMs(0) {}

void VoltageSensor::init() {
  AdcPinConfig pins;
  pins.csPins[0] = ADC_CS1_PIN;
  pins.csPins[1] = ADC_CS2_PIN;
  pins.csPins[2] = ADC_CS3_PIN;
  pins.syncPin   = ADC_SYNC_PIN;
  pins.drdyPin   = ADC_DRDY_PIN;
  pins.sckPin    = ADC_SCK_PIN;
  pins.misoPin   = ADC_MISO_PIN;
  pins.mosiPin   = ADC_MOSI_PIN;
  pins.spiSpeedHz = ADS131_SPI_SPEED;

  adcDriver.init(pins);
  metrologyEngine.init();

  // Initial update
  update();
  Serial.println(F("[VOLT] High-Precision Three-Phase Voltage Metrology Subsystem Ready"));
}

void VoltageSensor::update() {
  AdcRawSampleFrame frame;
  // Read synchronized frame across all 3 ADS131M08 chips
  if (adcDriver.readSynchronizedFrame(frame)) {
    metrologyEngine.processRawSample(frame);
  }

  // Periodic window computation
  metrologyEngine.computeMetrologyWindow();

#if DEBUG_ZMPT101B
  static unsigned long lastDiagPrint = 0;
  if (millis() - lastDiagPrint >= 5000) {
    lastDiagPrint = millis();
    Serial.printf("[METROLOGY-VOLT] R: %.1fV (%s) | Y: %.1fV (%s) | B: %.1fV (%s) | Freq: %.2f Hz\n",
                  getVoltageR(), getVoltageStateR().c_str(),
                  getVoltageY(), getVoltageStateY().c_str(),
                  getVoltageB(), getVoltageStateB().c_str(),
                  getGridFrequency());
  }
#endif
}

float VoltageSensor::getVoltageR() const {
  return metrologyEngine.getCommonVoltageR();
}

float VoltageSensor::getVoltageY() const {
  return metrologyEngine.getCommonVoltageY();
}

float VoltageSensor::getVoltageB() const {
  return metrologyEngine.getCommonVoltageB();
}

String VoltageSensor::getVoltageStateR() const {
  return String(PowerMeasurementEngine::getValidityString(metrologyEngine.getVoltageState().stateR));
}

String VoltageSensor::getVoltageStateY() const {
  return String(PowerMeasurementEngine::getValidityString(metrologyEngine.getVoltageState().stateY));
}

String VoltageSensor::getVoltageStateB() const {
  return String(PowerMeasurementEngine::getValidityString(metrologyEngine.getVoltageState().stateB));
}

bool VoltageSensor::isVoltageValidR() const {
  return (metrologyEngine.getVoltageState().stateR == MEAS_VALID);
}

bool VoltageSensor::isVoltageValidY() const {
  return (metrologyEngine.getVoltageState().stateY == MEAS_VALID);
}

bool VoltageSensor::isVoltageValidB() const {
  return (metrologyEngine.getVoltageState().stateB == MEAS_VALID);
}

float VoltageSensor::getGridFrequency() const {
  return metrologyEngine.getGridFrequency();
}

bool VoltageSensor::isThreePhaseBalanced() const {
  return metrologyEngine.getVoltageState().threePhaseSymmetric;
}
