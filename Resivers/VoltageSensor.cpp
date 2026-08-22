/*
 * =====================================================================
 *  VOLTAGESENSOR.CPP — MULTI-PHASE ZMPT101B VOLTAGE SENSING MODULE
 * =====================================================================
 */

#include "VoltageSensor.h"
#include <math.h>

VoltageSensor voltageSensor;

VoltageSensor::VoltageSensor() :
  voltageR(0.0f), voltageY(0.0f), voltageB(0.0f),
  voltageStateR("NO_SIGNAL"), voltageStateY("NO_SIGNAL"), voltageStateB("NO_SIGNAL")
{}

void VoltageSensor::init() {
  pinMode(VOLTAGE_R_PIN, INPUT);
  pinMode(VOLTAGE_Y_PIN, INPUT);
  pinMode(VOLTAGE_B_PIN, INPUT);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db); // 0-3.3V range on ESP32 ADC1

  // Warm-up reading
  update();
}

void VoltageSensor::sampleChannel(uint8_t pin, float &outVoltage, String &outState) {
  if (pin == 0) {
    outVoltage = 0.0f;
    outState = "DISCONNECTED";
    return;
  }

  uint64_t sumVsq = 0;
  uint32_t sumAdc = 0;
  uint32_t minAdc = 4095;
  uint32_t maxAdc = 0;
  const int SAMPLES = 300; // 300 samples across ~30ms (1.5 to 2 AC cycles at 50Hz)
  
  for (int i = 0; i < SAMPLES; i++) {
    uint32_t adc = analogRead(pin);
    sumAdc += adc;
    sumVsq += (uint64_t)(adc * adc);
    if (adc < minAdc) minAdc = adc;
    if (adc > maxAdc) maxAdc = adc;
    delayMicroseconds(90);
  }
  
  float meanAdc = (float)sumAdc / SAMPLES;
  float meanVsq = (float)sumVsq / SAMPLES;
  uint32_t p2p  = (maxAdc >= minAdc) ? (maxAdc - minAdc) : 0;
  
  // Statistical variance cleanly removes DC bias without hardcoded assumptions
  float variance = meanVsq - (meanAdc * meanAdc);
  if (variance < 0.0f) variance = 0.0f;
  
  float rawVrms = sqrt(variance) * (ADC_VREF / ADC_MAX);
  float calculatedMainsV = rawVrms * ZMPT101B_CALIBRATION_FACTOR;
  
  // ── ZMPT101B State Classification ──
  // 1. Physically floating / grounded rail
  if (meanAdc < 80.0f || meanAdc > 4020.0f) {
    outState = "DISCONNECTED";
    outVoltage = 0.0f;
  }
  // 2. DC bias present but AC amplitude flat (Mains is OFF or 0V)
  else if (p2p < 20 || calculatedMainsV < 25.0f) {
    outState = "NO_SIGNAL";
    outVoltage = 0.0f;
  }
  // 3. Valid dynamic measured AC mains voltage
  else {
    outState = "VALID";
    outVoltage = calculatedMainsV;
  }
}

void VoltageSensor::update() {
  sampleChannel(VOLTAGE_R_PIN, voltageR, voltageStateR);
  sampleChannel(VOLTAGE_Y_PIN, voltageY, voltageStateY);
  sampleChannel(VOLTAGE_B_PIN, voltageB, voltageStateB);

#if DEBUG_ZMPT101B
  static unsigned long lastDiagPrint = 0;
  if (millis() - lastDiagPrint >= 5000) {
    lastDiagPrint = millis();
    Serial.printf("[ZMPT101B] R: %.1fV (%s) | Y: %.1fV (%s) | B: %.1fV (%s)\n",
                  voltageR, voltageStateR.c_str(),
                  voltageY, voltageStateY.c_str(),
                  voltageB, voltageStateB.c_str());
  }
#endif
}

float  VoltageSensor::getVoltageR() const { return voltageR; }
float  VoltageSensor::getVoltageY() const { return voltageY; }
float  VoltageSensor::getVoltageB() const { return voltageB; }
String VoltageSensor::getVoltageStateR() const { return voltageStateR; }
String VoltageSensor::getVoltageStateY() const { return voltageStateY; }
String VoltageSensor::getVoltageStateB() const { return voltageStateB; }
