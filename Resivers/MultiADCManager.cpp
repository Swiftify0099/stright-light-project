/*
 * =====================================================================
 *  MULTIADCMANAGER.CPP — MULTI-ADC SYNCHRONIZATION & 21-CHANNEL
 *  THREE-PHASE POWER METROLOGY CONTROLLER (ESP32)
 * =====================================================================
 */

#include "MultiADCManager.h"
#include <math.h>

MultiADCManager multiAdcMgr;

// Default calibration scaling multipliers
// Mains Voltage: 230 VAC nominal -> 120k primary / 300 ohm burden -> 400x multiplier
#ifndef ZMPT_VOLTAGE_SCALE_FACTOR
#define ZMPT_VOLTAGE_SCALE_FACTOR 400.0f
#endif

// CT Current: 50A:25mA (1000:1) with 12 ohm burden -> 83.333 A/V
#ifndef CT_CURRENT_SCALE_FACTOR
#define CT_CURRENT_SCALE_FACTOR   83.333f
#endif

MultiADCManager::MultiADCManager() :
  isSynchronized(false), sampleCount(0), lastEnergyCalcMs(0), lastCycleTimeMs(0)
{
  pinCS[0] = ADC_CS1_PIN;
  pinCS[1] = ADC_CS2_PIN;
  pinCS[2] = ADC_CS3_PIN;
  pinSYNC  = ADC_SYNC_PIN;
  pinRESET = ADC_RESET_PIN;
  pinDRDY  = ADC_DRDY_PIN;

  for (int i = 0; i < NUM_ADCS; i++) {
    adcsHealthy[i] = false;
  }

  // Clear feeder states
  for (int f = 0; f < NUM_FEEDERS; f++) {
    feederData[f].totalActivePowerW = 0.0f;
    feederData[f].totalEnergyWh = 0.0f;
    feederData[f].tripped = false;
    feederData[f].compositeFault = "OK";

    for (int p = 0; p < PHASES_PER_FEEDER; p++) {
      feederData[f].phases[p].irms = 0.0f;
      feederData[f].phases[p].ipeak = 0.0f;
      feederData[f].phases[p].activePowerW = 0.0f;
      feederData[f].phases[p].powerFactor = 1.0f;
      feederData[f].phases[p].overCurrent = false;
      feederData[f].phases[p].sensorFault = false;
      feederData[f].phases[p].status = "OK";
      sumIsq[f][p] = 0;
      sumPowerInst[f][p] = 0;
    }
  }

  // Clear bus voltage states
  busVoltage.vrmsR = 0.0f;
  busVoltage.vrmsY = 0.0f;
  busVoltage.vrmsB = 0.0f;
  busVoltage.frequencyHz = 50.0f;
  busVoltage.phaseSequence = "UNKNOWN";
  busVoltage.validR = false;
  busVoltage.validY = false;
  busVoltage.validB = false;
  busVoltage.statusR = "INIT";
  busVoltage.statusY = "INIT";
  busVoltage.statusB = "INIT";

  for (int p = 0; p < PHASES_PER_FEEDER; p++) {
    sumVsq[p] = 0;
  }
}

bool MultiADCManager::init() {
  Serial.println(F("[MultiADC] Initializing 21-Channel 3x ADS131M08 Subsystem..."));

  // 1. Hardware Pin Direction Configuration
  pinMode(pinRESET, OUTPUT);
  pinMode(pinSYNC, OUTPUT);
  pinMode(pinDRDY, INPUT);

  digitalWrite(pinRESET, HIGH);
  digitalWrite(pinSYNC, HIGH);

  // 2. Hardware Reset Sequence for all 3 ADCs
  digitalWrite(pinRESET, LOW);
  delayMicroseconds(600); // Exceeds minimum t_RSTW
  digitalWrite(pinRESET, HIGH);
  delay(5); // Stabilization delay

  // 3. Initialize each ADS131M08 over SPI
  bool allOk = true;
  for (int i = 0; i < NUM_ADCS; i++) {
    adcsHealthy[i] = adcs[i].init(pinCS[i], &SPI, 8000000);
    if (adcsHealthy[i]) {
      // Set High-Resolution mode and OSR 1024 (approx 4 kSPS at 8.192 MHz master clock)
      adcs[i].configureClockAndPower(ADS131_CLOCK_PWR_HR, ADS131_CLOCK_OSR_1024);

      // Default all channels to Gain = 1 for ±1.2V differential input
      for (uint8_t ch = 0; ch < 8; ch++) {
        adcs[i].setChannelGain(ch, ADS131_GAIN_1);
      }
    } else {
      allOk = false;
      Serial.printf("[MultiADC][ERROR] ADC %d (CS Pin %d) failed initialization!\n", i + 1, pinCS[i]);
    }
  }

  // 4. Synchronize all 3 ADCs with master /SYNC pulse
  synchronizeADCs();

  lastEnergyCalcMs = millis();
  lastCycleTimeMs  = millis();

  if (allOk) {
    Serial.println(F("[MultiADC] All 3x ADS131M08 ADCs initialized and phase-synchronized!"));
  } else {
    Serial.println(F("[MultiADC][WARN] Subsystem started in DEGRADED mode. Check wiring/power."));
  }

  return allOk;
}

void MultiADCManager::synchronizeADCs() {
  // A low pulse on the shared /SYNC line simultaneously resets the digital sinc3
  // filters of all 3 devices and aligns conversion intervals to the exact same clock edge.
  digitalWrite(pinSYNC, LOW);
  delayMicroseconds(5); // Minimum SYNC pulse width is 1 clock period (~122 ns)
  digitalWrite(pinSYNC, HIGH);
  delay(2); // Wait for digital filter to acquire first simultaneous sample
  isSynchronized = true;
  Serial.println(F("[MultiADC] Synchronous /SYNC pulse asserted. 24 channels phase-locked."));
}

bool MultiADCManager::checkHealth() {
  bool overall = true;
  for (int i = 0; i < NUM_ADCS; i++) {
    uint16_t id = adcs[i].getDeviceId();
    uint8_t family = (uint8_t)((id >> 8) & 0xFF);
    if (family != 0x28 && id == 0x0000) {
      adcsHealthy[i] = false;
      overall = false;
    } else {
      adcsHealthy[i] = true;
    }
  }
  return overall;
}

void MultiADCManager::update() {
  // Take a block of synchronous samples across AC cycles
  // At 50 Hz, 1 full cycle is 20 ms. We sample 100 points across 1 cycle or burst
  const int SAMPLES_PER_CYCLE = 64;
  
  ADS131_Frame frame1, frame2, frame3;

  for (int s = 0; s < SAMPLES_PER_CYCLE; s++) {
    // Read ADC 1 (Feeders 1, 2, Feeder 3 R/Y)
    bool ok1 = adcs[0].readConversionFrame(frame1);
    // Read ADC 2 (Feeder 3 B, Feeders 4, 5)
    bool ok2 = adcs[1].readConversionFrame(frame2);
    // Read ADC 3 (Feeder 6, Common Bus Voltages R, Y, B)
    bool ok3 = adcs[2].readConversionFrame(frame3);

    if (ok1 && ok2 && ok3) {
      processSampleFrame(frame1.voltage, frame2.voltage, frame3.voltage);
      sampleCount++;
    } else {
      // Hardware communication failure detected
      if (!ok1) adcsHealthy[0] = false;
      if (!ok2) adcsHealthy[1] = false;
      if (!ok3) adcsHealthy[2] = false;
    }
    delayMicroseconds(150); // Inter-sample period
  }

  // Once enough samples have accumulated, compute True-RMS & Power
  if (sampleCount >= SAMPLES_PER_CYCLE) {
    computeCycleRMS();
    sampleCount = 0;
  }
}

void MultiADCManager::processSampleFrame(const float adc1V[8], const float adc2V[8], const float adc3V[8]) {
  // Physical channel assignment based on Candidate Allocation:
  // ADC 1:
  //   Ch 0..2: Feeder 1 (R, Y, B)
  //   Ch 3..5: Feeder 2 (R, Y, B)
  //   Ch 6..7: Feeder 3 (R, Y)
  // ADC 2:
  //   Ch 0:    Feeder 3 (B)
  //   Ch 1..3: Feeder 4 (R, Y, B)
  //   Ch 4..6: Feeder 5 (R, Y, B)
  //   Ch 7:    Spare 1
  // ADC 3:
  //   Ch 0..2: Feeder 6 (R, Y, B)
  //   Ch 3:    Bus Voltage R
  //   Ch 4:    Bus Voltage Y
  //   Ch 5:    Bus Voltage B
  //   Ch 6..7: Spare 2, 3

  // 1. Instantaneous Bus Voltages (VAC)
  float instVR = adc3V[3] * ZMPT_VOLTAGE_SCALE_FACTOR;
  float instVY = adc3V[4] * ZMPT_VOLTAGE_SCALE_FACTOR;
  float instVB = adc3V[5] * ZMPT_VOLTAGE_SCALE_FACTOR;

  sumVsq[0] += (uint64_t)(instVR * instVR);
  sumVsq[1] += (uint64_t)(instVY * instVY);
  sumVsq[2] += (uint64_t)(instVB * instVB);

  // 2. Instantaneous Feeder Currents (A)
  float instI[NUM_FEEDERS][PHASES_PER_FEEDER];

  // Feeder 1 (ADC 1 Ch 0, 1, 2)
  instI[0][0] = adc1V[0] * CT_CURRENT_SCALE_FACTOR;
  instI[0][1] = adc1V[1] * CT_CURRENT_SCALE_FACTOR;
  instI[0][2] = adc1V[2] * CT_CURRENT_SCALE_FACTOR;

  // Feeder 2 (ADC 1 Ch 3, 4, 5)
  instI[1][0] = adc1V[3] * CT_CURRENT_SCALE_FACTOR;
  instI[1][1] = adc1V[4] * CT_CURRENT_SCALE_FACTOR;
  instI[1][2] = adc1V[5] * CT_CURRENT_SCALE_FACTOR;

  // Feeder 3 (ADC 1 Ch 6, 7 & ADC 2 Ch 0)
  instI[2][0] = adc1V[6] * CT_CURRENT_SCALE_FACTOR;
  instI[2][1] = adc1V[7] * CT_CURRENT_SCALE_FACTOR;
  instI[2][2] = adc2V[0] * CT_CURRENT_SCALE_FACTOR;

  // Feeder 4 (ADC 2 Ch 1, 2, 3)
  instI[3][0] = adc2V[1] * CT_CURRENT_SCALE_FACTOR;
  instI[3][1] = adc2V[2] * CT_CURRENT_SCALE_FACTOR;
  instI[3][2] = adc2V[3] * CT_CURRENT_SCALE_FACTOR;

  // Feeder 5 (ADC 2 Ch 4, 5, 6)
  instI[4][0] = adc2V[4] * CT_CURRENT_SCALE_FACTOR;
  instI[4][1] = adc2V[5] * CT_CURRENT_SCALE_FACTOR;
  instI[4][2] = adc2V[6] * CT_CURRENT_SCALE_FACTOR;

  // Feeder 6 (ADC 3 Ch 0, 1, 2)
  instI[5][0] = adc3V[0] * CT_CURRENT_SCALE_FACTOR;
  instI[5][1] = adc3V[1] * CT_CURRENT_SCALE_FACTOR;
  instI[5][2] = adc3V[2] * CT_CURRENT_SCALE_FACTOR;

  // Accumulate squared current & instantaneous power
  for (int f = 0; f < NUM_FEEDERS; f++) {
    sumIsq[f][0] += (uint64_t)(instI[f][0] * instI[f][0]);
    sumIsq[f][1] += (uint64_t)(instI[f][1] * instI[f][1]);
    sumIsq[f][2] += (uint64_t)(instI[f][2] * instI[f][2]);

    sumPowerInst[f][0] += (int64_t)(instVR * instI[f][0]);
    sumPowerInst[f][1] += (int64_t)(instVY * instI[f][1]);
    sumPowerInst[f][2] += (int64_t)(instVB * instI[f][2]);
  }
}

void MultiADCManager::computeCycleRMS() {
  if (sampleCount == 0) return;

  // 1. Compute Bus Voltage RMS
  busVoltage.vrmsR = sqrt((float)sumVsq[0] / sampleCount);
  busVoltage.vrmsY = sqrt((float)sumVsq[1] / sampleCount);
  busVoltage.vrmsB = sqrt((float)sumVsq[2] / sampleCount);

  // Validate Voltages (Standard 230V grid limits: 80V to 280V)
  busVoltage.validR = (busVoltage.vrmsR >= MIN_VALID_MAINS_VOLTAGE && busVoltage.vrmsR <= MAX_VALID_MAINS_VOLTAGE);
  busVoltage.validY = (busVoltage.vrmsY >= MIN_VALID_MAINS_VOLTAGE && busVoltage.vrmsY <= MAX_VALID_MAINS_VOLTAGE);
  busVoltage.validB = (busVoltage.vrmsB >= MIN_VALID_MAINS_VOLTAGE && busVoltage.vrmsB <= MAX_VALID_MAINS_VOLTAGE);

  busVoltage.statusR = busVoltage.validR ? "VALID" : (busVoltage.vrmsR < 25.0f ? "NO_SIGNAL" : "OUT_OF_RANGE");
  busVoltage.statusY = busVoltage.validY ? "VALID" : (busVoltage.vrmsY < 25.0f ? "NO_SIGNAL" : "OUT_OF_RANGE");
  busVoltage.statusB = busVoltage.validB ? "VALID" : (busVoltage.vrmsB < 25.0f ? "NO_SIGNAL" : "OUT_OF_RANGE");

  // Determine phase sequence
  if (busVoltage.validR && busVoltage.validY && busVoltage.validB) {
    busVoltage.phaseSequence = "RYB_NORMAL";
  } else if (!busVoltage.validR || !busVoltage.validY || !busVoltage.validB) {
    busVoltage.phaseSequence = "PHASE_LOST";
  } else {
    busVoltage.phaseSequence = "DEGRADED";
  }

  // 2. Compute Feeder Currents & Power
  unsigned long now = millis();
  float hours = (lastEnergyCalcMs > 0 && now > lastEnergyCalcMs) ? ((now - lastEnergyCalcMs) / 3600000.0f) : 0.0f;
  lastEnergyCalcMs = now;

  for (int f = 0; f < NUM_FEEDERS; f++) {
    float fdrTotalP = 0.0f;
    bool hasFault = false;

    for (int p = 0; p < PHASES_PER_FEEDER; p++) {
      float irms = sqrt((float)sumIsq[f][p] / sampleCount);
      // Suppress sub-noise floor
      if (irms < 0.05f) irms = 0.0f;

      feederData[f].phases[p].irms = irms;

      // Active power = mean(v(t) * i(t))
      float pAct = (float)sumPowerInst[f][p] / sampleCount;
      if (pAct < 0.0f) pAct = 0.0f; // Unidirectional consumption assumption
      feederData[f].phases[p].activePowerW = pAct;
      fdrTotalP += pAct;

      // Apparent power & Power Factor
      float vPhase = (p == 0) ? busVoltage.vrmsR : ((p == 1) ? busVoltage.vrmsY : busVoltage.vrmsB);
      float sApp = vPhase * irms;
      if (sApp > 1.0f) {
        float pf = pAct / sApp;
        if (pf > 1.0f) pf = 1.0f;
        feederData[f].phases[p].powerFactor = pf;
      } else {
        feederData[f].phases[p].powerFactor = 1.0f;
      }

      // Overcurrent evaluation (> 20A threshold)
      if (irms > 20.0f) {
        feederData[f].phases[p].overCurrent = true;
        feederData[f].phases[p].status = "OVER_CURR";
        hasFault = true;
      } else {
        feederData[f].phases[p].overCurrent = false;
        feederData[f].phases[p].status = "OK";
      }

      // Reset accumulators
      sumIsq[f][p] = 0;
      sumPowerInst[f][p] = 0;
    }

    feederData[f].totalActivePowerW = fdrTotalP;
    feederData[f].totalEnergyWh += fdrTotalP * hours;
    feederData[f].compositeFault = hasFault ? "OVER_CURR" : "OK";
  }

  // Reset voltage accumulators
  for (int p = 0; p < PHASES_PER_FEEDER; p++) {
    sumVsq[p] = 0;
  }
}

float MultiADCManager::getFeederCurrent(uint8_t feederIdx, uint8_t phaseIdx) const {
  if (feederIdx < NUM_FEEDERS && phaseIdx < PHASES_PER_FEEDER) {
    return feederData[feederIdx].phases[phaseIdx].irms;
  }
  return 0.0f;
}

float MultiADCManager::getFeederPower(uint8_t feederIdx) const {
  if (feederIdx < NUM_FEEDERS) {
    return feederData[feederIdx].totalActivePowerW;
  }
  return 0.0f;
}

float MultiADCManager::getFeederEnergyWh(uint8_t feederIdx) const {
  if (feederIdx < NUM_FEEDERS) {
    return feederData[feederIdx].totalEnergyWh;
  }
  return 0.0f;
}

String MultiADCManager::getFeederFault(uint8_t feederIdx) const {
  if (feederIdx < NUM_FEEDERS) {
    return feederData[feederIdx].compositeFault;
  }
  return "ERR";
}

float MultiADCManager::getVoltageR() const { return busVoltage.vrmsR; }
float MultiADCManager::getVoltageY() const { return busVoltage.vrmsY; }
float MultiADCManager::getVoltageB() const { return busVoltage.vrmsB; }
float MultiADCManager::getGridFrequency() const { return busVoltage.frequencyHz; }
String MultiADCManager::getPhaseSequence() const { return busVoltage.phaseSequence; }

String MultiADCManager::getVoltageStateR() const { return busVoltage.statusR; }
String MultiADCManager::getVoltageStateY() const { return busVoltage.statusY; }
String MultiADCManager::getVoltageStateB() const { return busVoltage.statusB; }

bool MultiADCManager::isAdcHealthy(uint8_t adcIdx) const {
  if (adcIdx < NUM_ADCS) return adcsHealthy[adcIdx];
  return false;
}
