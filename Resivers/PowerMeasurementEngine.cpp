/*
 * =====================================================================
 *  POWERMEASUREMENTENGINE.CPP — 21-CHANNEL INDUSTRIAL METROLOGY ENGINE
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#include "PowerMeasurementEngine.h"
#include <math.h>
#include <string.h>

PowerMeasurementEngine metrologyEngine;

PowerMeasurementEngine::PowerMeasurementEngine() :
  ringHead(0),
  ringCount(0),
  totalSamplesProcessed(0),
  lastZeroCrossingTimeUs(0.0f),
  measuredGridFreq(NOMINAL_GRID_FREQ_HZ),
  freqLocked(false),
  windowSampleCount(200),
  lastComputeTimestamp(0),
  lastEnergyCalcMs(0)
{
  strncpy(poleId, "MSCB-P001", sizeof(poleId) - 1);
  poleId[sizeof(poleId) - 1] = '\0';
  reset();
}

void PowerMeasurementEngine::reset() {
  ringHead = 0;
  ringCount = 0;
  totalSamplesProcessed = 0;
  lastZeroCrossingTimeUs = 0.0f;
  measuredGridFreq = NOMINAL_GRID_FREQ_HZ;
  freqLocked = false;
  windowSampleCount = 200;
  lastComputeTimestamp = 0;
  lastEnergyCalcMs = millis();

  memset(sampleRingBuffers, 0, sizeof(sampleRingBuffers));
  memset(sampleTimeBuffer, 0, sizeof(sampleTimeBuffer));

  voltageState.vR_rms = 0.0f;
  voltageState.vY_rms = 0.0f;
  voltageState.vB_rms = 0.0f;
  voltageState.gridFrequencyHz = NOMINAL_GRID_FREQ_HZ;
  voltageState.stateR = MEAS_NO_SIGNAL;
  voltageState.stateY = MEAS_NO_SIGNAL;
  voltageState.stateB = MEAS_NO_SIGNAL;
  voltageState.threePhaseSymmetric = false;
  voltageState.phaseLossR = false;
  voltageState.phaseLossY = false;
  voltageState.phaseLossB = false;
  voltageState.timestamp = 0;

  for (uint8_t f = 0; f < MAX_SYSTEM_FEEDERS; f++) {
    feederStates[f].feederId = f + 1;
    feederStates[f].iR_rms = 0.0f;
    feederStates[f].iY_rms = 0.0f;
    feederStates[f].iB_rms = 0.0f;
    feederStates[f].stateR = MEAS_ZERO_CURRENT;
    feederStates[f].stateY = MEAS_ZERO_CURRENT;
    feederStates[f].stateB = MEAS_ZERO_CURRENT;
    feederStates[f].pR_active = 0.0f;
    feederStates[f].pY_active = 0.0f;
    feederStates[f].pB_active = 0.0f;
    feederStates[f].sR_apparent = 0.0f;
    feederStates[f].sY_apparent = 0.0f;
    feederStates[f].sB_apparent = 0.0f;
    feederStates[f].pfR = 1.0f;
    feederStates[f].pfY = 1.0f;
    feederStates[f].pfB = 1.0f;
    feederStates[f].pTotal_active = 0.0f;
    feederStates[f].sTotal_apparent = 0.0f;
    feederStates[f].pfTotal = 1.0f;
    feederStates[f].qTotal_reactive = 0.0f;
    feederStates[f].accumEnergyWh = 0.0f;
    feederStates[f].currentImbalancePct = 0.0f;
    feederStates[f].isOverCurrent = false;
    feederStates[f].isImbalanceFault = false;
    feederStates[f].isSensorFault = false;
    feederStates[f].timestamp = 0;
  }

  for (uint8_t c = 0; c < NUM_TOTAL_CHANNELS; c++) {
    strncpy(channelRecords[c].poleId, poleId, sizeof(channelRecords[c].poleId) - 1);
    strncpy(channelRecords[c].channelId, CalibrationManager::getChannelId(c), sizeof(channelRecords[c].channelId) - 1);
    uint8_t f = 0;
    char p = 'R';
    CalibrationManager::getFeederAndPhase(c, f, p);
    channelRecords[c].feederIndex = f;
    channelRecords[c].phase = p;
    channelRecords[c].rmsValue = 0.0f;
    channelRecords[c].dcOffset = 0.0f;
    channelRecords[c].peakValue = 0.0f;
    channelRecords[c].validity = CalibrationManager::isVoltageChannel(c) ? MEAS_NO_SIGNAL : MEAS_ZERO_CURRENT;
    channelRecords[c].calVersion = CALIBRATION_VERSION;
    channelRecords[c].timestamp = 0;
  }
}

void PowerMeasurementEngine::init(const char* id) {
  if (id && strlen(id) > 0) {
    setPoleId(id);
  }
  calibMgr.init();
  reset();
  Serial.printf("[METROLOGY] Metrology Engine initialized for Pole: %s (21 Channels, 1000 SPS)\n", poleId);
}

const char* PowerMeasurementEngine::getPoleId() const { return poleId; }

void PowerMeasurementEngine::setPoleId(const char* id) {
  if (id && strlen(id) > 0) {
    strncpy(poleId, id, sizeof(poleId) - 1);
    poleId[sizeof(poleId) - 1] = '\0';
    for (uint8_t c = 0; c < NUM_TOTAL_CHANNELS; c++) {
      strncpy(channelRecords[c].poleId, poleId, sizeof(channelRecords[c].poleId) - 1);
    }
  }
}

void PowerMeasurementEngine::processRawSample(const AdcRawSampleFrame &frame) {
  // Transfer function: counts to normalized Volts (-1.2V to +1.2V)
  const float countsToVolts = ADS_VREF_INTERNAL / 8388607.0f;

  for (uint8_t ch = 0; ch < NUM_TOTAL_CHANNELS; ch++) {
    float vAnalog = (float)frame.rawCounts[ch] * countsToVolts;
    sampleRingBuffers[ch][ringHead] = vAnalog;
  }

  sampleTimeBuffer[ringHead] = frame.timestampUs;

  ringHead = (ringHead + 1) % METROLOGY_BUFFER_SIZE;
  if (ringCount < METROLOGY_BUFFER_SIZE) {
    ringCount++;
  }
  totalSamplesProcessed++;
}

float PowerMeasurementEngine::calculateZeroCrossingFreq(uint16_t count) {
  if (count < 60) return NOMINAL_GRID_FREQ_HZ;

  // Scan channel 0 (R-Voltage) for positive zero crossings with hysteresis
  const float hysteresis = 0.02f; // 20mV hysteresis
  int crossingCount = 0;
  float crossingTimes[16];
  bool armed = false;

  // We scan the most recent 'count' samples from oldest to newest
  int startIdx = (ringHead - count + METROLOGY_BUFFER_SIZE) % METROLOGY_BUFFER_SIZE;

  for (int i = 1; i < count; i++) {
    int prevIdx = (startIdx + i - 1) % METROLOGY_BUFFER_SIZE;
    int currIdx = (startIdx + i) % METROLOGY_BUFFER_SIZE;

    float vPrev = sampleRingBuffers[0][prevIdx];
    float vCurr = sampleRingBuffers[0][currIdx];

    if (vPrev < -hysteresis) {
      armed = true;
    }

    if (armed && vPrev <= 0.0f && vCurr > 0.0f) {
      armed = false;
      // Sub-sample linear interpolation for precise zero-crossing timestamp
      float dt = 0.001f; // 1ms at 1000 SPS
      float frac = (0.0f - vPrev) / (vCurr - vPrev);
      float tCross = (float)i + frac;

      if (crossingCount < 16) {
        crossingTimes[crossingCount++] = tCross;
      }
    }
  }

  if (crossingCount >= 3) {
    // Calculate average period in sample units
    float totalPeriodSamples = crossingTimes[crossingCount - 1] - crossingTimes[0];
    float avgPeriodSamples = totalPeriodSamples / (float)(crossingCount - 1);

    if (avgPeriodSamples > 15.0f && avgPeriodSamples < 25.0f) {
      float freq = (float)METROLOGY_SAMPLE_RATE_HZ / avgPeriodSamples;
      if (freq >= MIN_PLAUSIBLE_FREQ_HZ && freq <= MAX_PLAUSIBLE_FREQ_HZ) {
        freqLocked = true;
        return freq;
      }
    }
  }

  freqLocked = false;
  return NOMINAL_GRID_FREQ_HZ;
}

void PowerMeasurementEngine::computeVoltageChannel(uint8_t chIdx, uint16_t sampleCount, float &outRms, float &outDc, MeasurementValidity &outVal) {
  if (sampleCount == 0) {
    outRms = 0.0f;
    outDc = 0.0f;
    outVal = MEAS_UNAVAILABLE;
    return;
  }

  float sum = 0.0f;
  float sumSq = 0.0f;
  float maxVal = -1e9f;
  float minVal = 1e9f;

  int startIdx = (ringHead - sampleCount + METROLOGY_BUFFER_SIZE) % METROLOGY_BUFFER_SIZE;

  for (uint16_t i = 0; i < sampleCount; i++) {
    int idx = (startIdx + i) % METROLOGY_BUFFER_SIZE;
    float val = sampleRingBuffers[chIdx][idx];
    sum += val;
    sumSq += (val * val);
    if (val > maxVal) maxVal = val;
    if (val < minVal) minVal = val;
  }

  float mean = sum / (float)sampleCount;
  float variance = (sumSq / (float)sampleCount) - (mean * mean);
  if (variance < 0.0f) variance = 0.0f;

  const ChannelCalibData *cal = calibMgr.getChannelCalib(chIdx);
  float rawRms = sqrtf(variance);
  float dcBias = mean;

  // Apply calibration multiplier & offset
  outRms = (rawRms * cal->gainMultiplier * cal->sensorSensitivity) - cal->zeroOffset;
  outDc = dcBias * cal->sensorSensitivity;

  if (outRms < 0.0f) outRms = 0.0f;

  // Peak-to-peak amplitude
  float p2pVolts = (maxVal - minVal) * cal->sensorSensitivity;

  // Quality classification
  if (rawRms > 1.18f) { // Approaching ADS 1.2V rail
    outVal = MEAS_SATURATED;
  } else if (outRms < VOLTAGE_NO_SIGNAL_THRESH || p2pVolts < 25.0f) {
    outVal = MEAS_NO_SIGNAL;
    outRms = 0.0f;
  } else if (outRms < VOLTAGE_UNDERVOLT_THRESH) {
    outVal = MEAS_UNDER_VOLTAGE;
  } else if (outRms > VOLTAGE_OVERVOLT_THRESH) {
    outVal = MEAS_OVER_VOLTAGE;
  } else {
    outVal = MEAS_VALID;
  }
}

void PowerMeasurementEngine::computeCurrentChannel(uint8_t chIdx, uint16_t sampleCount, float &outRms, float &outDc, MeasurementValidity &outVal) {
  if (sampleCount == 0) {
    outRms = 0.0f;
    outDc = 0.0f;
    outVal = MEAS_UNAVAILABLE;
    return;
  }

  float sum = 0.0f;
  float sumSq = 0.0f;
  float maxVal = -1e9f;
  float minVal = 1e9f;

  int startIdx = (ringHead - sampleCount + METROLOGY_BUFFER_SIZE) % METROLOGY_BUFFER_SIZE;

  for (uint16_t i = 0; i < sampleCount; i++) {
    int idx = (startIdx + i) % METROLOGY_BUFFER_SIZE;
    float val = sampleRingBuffers[chIdx][idx];
    sum += val;
    sumSq += (val * val);
    if (val > maxVal) maxVal = val;
    if (val < minVal) minVal = val;
  }

  float mean = sum / (float)sampleCount;
  float variance = (sumSq / (float)sampleCount) - (mean * mean);
  if (variance < 0.0f) variance = 0.0f;

  const ChannelCalibData *cal = calibMgr.getChannelCalib(chIdx);
  float rawRms = sqrtf(variance);
  outDc = mean * cal->sensorSensitivity;

  // Apply calibration
  outRms = (rawRms * cal->gainMultiplier * cal->sensorSensitivity) - cal->zeroOffset;
  if (outRms < 0.0f) outRms = 0.0f;

  // Quality classification
  if (rawRms > 1.18f) {
    outVal = MEAS_SATURATED;
  } else if (outRms < CURRENT_NOISE_FLOOR_A) {
    outVal = MEAS_ZERO_CURRENT;
    outRms = 0.0f;
  } else if (outRms > DEFAULT_OVERCURRENT_THRESH_A) {
    outVal = MEAS_OVER_CURRENT;
  } else {
    outVal = MEAS_VALID;
  }
}

void PowerMeasurementEngine::computePowerAndEnergy(uint8_t feederIdx, uint16_t sampleCount) {
  if (feederIdx >= MAX_SYSTEM_FEEDERS) return;

  FeederPowerState &fdr = feederStates[feederIdx];
  uint8_t chR = 3 + (feederIdx * 3);
  uint8_t chY = 4 + (feederIdx * 3);
  uint8_t chB = 5 + (feederIdx * 3);

  // If voltage is invalid or dead, power is unavailable or 0
  bool vOkR = (voltageState.stateR == MEAS_VALID || voltageState.stateR == MEAS_UNDER_VOLTAGE || voltageState.stateR == MEAS_OVER_VOLTAGE);
  bool vOkY = (voltageState.stateY == MEAS_VALID || voltageState.stateY == MEAS_UNDER_VOLTAGE || voltageState.stateY == MEAS_OVER_VOLTAGE);
  bool vOkB = (voltageState.stateB == MEAS_VALID || voltageState.stateB == MEAS_UNDER_VOLTAGE || voltageState.stateB == MEAS_OVER_VOLTAGE);

  int startIdx = (ringHead - sampleCount + METROLOGY_BUFFER_SIZE) % METROLOGY_BUFFER_SIZE;

  // Calculate Active Power per phase using synchronized instantaneous dot products:
  // P = (1/N) * sum((v[k] - v_dc) * (i[k] - i_dc)) * Kv * Ki
  auto computePhaseP = [&](uint8_t vCh, uint8_t iCh, float &pActive, float &sApparent, float &pf, float vRms, float iRms, bool vValid) {
    if (!vValid || iRms <= 0.001f) {
      pActive = 0.0f;
      sApparent = 0.0f;
      pf = 1.0f;
      return;
    }

    const ChannelCalibData *calV = calibMgr.getChannelCalib(vCh);
    const ChannelCalibData *calI = calibMgr.getChannelCalib(iCh);

    float sumProd = 0.0f;
    float sumV = 0.0f;
    float sumI = 0.0f;

    for (uint16_t i = 0; i < sampleCount; i++) {
      int idx = (startIdx + i) % METROLOGY_BUFFER_SIZE;
      sumV += sampleRingBuffers[vCh][idx];
      sumI += sampleRingBuffers[iCh][idx];
    }
    float dcV = sumV / (float)sampleCount;
    float dcI = sumI / (float)sampleCount;

    for (uint16_t i = 0; i < sampleCount; i++) {
      int idx = (startIdx + i) % METROLOGY_BUFFER_SIZE;
      float vac = sampleRingBuffers[vCh][idx] - dcV;
      float iac = sampleRingBuffers[iCh][idx] - dcI;
      sumProd += (vac * iac);
    }

    float meanProd = sumProd / (float)sampleCount;
    pActive = meanProd * calV->gainMultiplier * calV->sensorSensitivity * calI->gainMultiplier * calI->sensorSensitivity;
    sApparent = vRms * iRms;

    if (sApparent > 1.0f) {
      pf = pActive / sApparent;
      if (pf > 1.0f) pf = 1.0f;
      if (pf < -1.0f) pf = -1.0f;
    } else {
      pf = 1.0f;
    }
  };

  computePhaseP(0, chR, fdr.pR_active, fdr.sR_apparent, fdr.pfR, voltageState.vR_rms, fdr.iR_rms, vOkR);
  computePhaseP(1, chY, fdr.pY_active, fdr.sY_apparent, fdr.pfY, voltageState.vY_rms, fdr.iY_rms, vOkY);
  computePhaseP(2, chB, fdr.pB_active, fdr.sB_apparent, fdr.pfB, voltageState.vB_rms, fdr.iB_rms, vOkB);

  fdr.pTotal_active = fdr.pR_active + fdr.pY_active + fdr.pB_active;
  fdr.sTotal_apparent = fdr.sR_apparent + fdr.sY_apparent + fdr.sB_apparent;

  if (fdr.sTotal_apparent > 2.0f) {
    fdr.pfTotal = fdr.pTotal_active / fdr.sTotal_apparent;
    if (fdr.pfTotal > 1.0f) fdr.pfTotal = 1.0f;
    if (fdr.pfTotal < -1.0f) fdr.pfTotal = -1.0f;
    float diffSq = (fdr.sTotal_apparent * fdr.sTotal_apparent) - (fdr.pTotal_active * fdr.pTotal_active);
    fdr.qTotal_reactive = sqrtf(diffSq > 0.0f ? diffSq : 0.0f);
  } else {
    fdr.pfTotal = 1.0f;
    fdr.qTotal_reactive = 0.0f;
  }

  // Energy integration (Watt-hours)
  unsigned long nowMs = millis();
  if (lastEnergyCalcMs > 0 && nowMs > lastEnergyCalcMs) {
    float hours = (float)(nowMs - lastEnergyCalcMs) / 3600000.0f;
    if (fdr.pTotal_active > 0.0f) {
      fdr.accumEnergyWh += fdr.pTotal_active * hours;
    }
  }

  // 3-Phase Current Imbalance Calculation
  float avgI = (fdr.iR_rms + fdr.iY_rms + fdr.iB_rms) / 3.0f;
  if (avgI > 1.5f) {
    float devR = fabsf(fdr.iR_rms - avgI);
    float devY = fabsf(fdr.iY_rms - avgI);
    float devB = fabsf(fdr.iB_rms - avgI);
    float maxDev = devR;
    if (devY > maxDev) maxDev = devY;
    if (devB > maxDev) maxDev = devB;
    fdr.currentImbalancePct = (maxDev / avgI) * 100.0f;
    fdr.isImbalanceFault = (fdr.currentImbalancePct > IMBALANCE_ALERT_PERCENT);
  } else {
    fdr.currentImbalancePct = 0.0f;
    fdr.isImbalanceFault = false;
  }

  fdr.isOverCurrent = (fdr.iR_rms > DEFAULT_OVERCURRENT_THRESH_A ||
                       fdr.iY_rms > DEFAULT_OVERCURRENT_THRESH_A ||
                       fdr.iB_rms > DEFAULT_OVERCURRENT_THRESH_A);

  fdr.timestamp = (uint32_t)(millis() / 1000);
}

void PowerMeasurementEngine::evaluateSystemFaults() {
  // Phase loss detection: e.g. R phase has healthy voltage but Y or B is missing
  float vRef = voltageState.vR_rms;
  if (vRef > 160.0f) {
    voltageState.phaseLossY = (voltageState.vY_rms < 40.0f);
    voltageState.phaseLossB = (voltageState.vB_rms < 40.0f);
  } else {
    voltageState.phaseLossY = false;
    voltageState.phaseLossB = false;
  }
  if (voltageState.vY_rms > 160.0f && voltageState.vR_rms < 40.0f) {
    voltageState.phaseLossR = true;
  } else {
    voltageState.phaseLossR = false;
  }

  // Voltage balance check
  float avgV = (voltageState.vR_rms + voltageState.vY_rms + voltageState.vB_rms) / 3.0f;
  if (avgV > 150.0f) {
    float maxVDev = fabsf(voltageState.vR_rms - avgV);
    if (fabsf(voltageState.vY_rms - avgV) > maxVDev) maxVDev = fabsf(voltageState.vY_rms - avgV);
    if (fabsf(voltageState.vB_rms - avgV) > maxVDev) maxVDev = fabsf(voltageState.vB_rms - avgV);
    voltageState.threePhaseSymmetric = ((maxVDev / avgV) < 0.10f); // within 10%
  } else {
    voltageState.threePhaseSymmetric = false;
  }
}

bool PowerMeasurementEngine::computeMetrologyWindow() {
  if (ringCount < 50) return false;

  uint16_t sampleCount = (ringCount > METROLOGY_BUFFER_SIZE) ? METROLOGY_BUFFER_SIZE : ringCount;

  // 1. Grid Frequency Tracking
  measuredGridFreq = calculateZeroCrossingFreq(sampleCount);
  voltageState.gridFrequencyHz = measuredGridFreq;

  // Dynamic window sizing to match exact integer number of cycles
  // At 1000 SPS, 1 cycle at 50Hz = 20 samples; 10 cycles = 200 samples.
  float samplesPerCycle = (float)METROLOGY_SAMPLE_RATE_HZ / measuredGridFreq;
  windowSampleCount = (uint16_t)(METROLOGY_CYCLES_PER_WINDOW * samplesPerCycle);
  if (windowSampleCount > sampleCount) windowSampleCount = sampleCount;
  if (windowSampleCount < 40) windowSampleCount = 40;

  // 2. Compute 3 Common Voltages
  float dcBias = 0.0f;
  computeVoltageChannel(0, windowSampleCount, voltageState.vR_rms, dcBias, voltageState.stateR);
  channelRecords[0].rmsValue = voltageState.vR_rms;
  channelRecords[0].dcOffset = dcBias;
  channelRecords[0].validity = voltageState.stateR;
  channelRecords[0].timestamp = (uint32_t)(millis() / 1000);

  computeVoltageChannel(1, windowSampleCount, voltageState.vY_rms, dcBias, voltageState.stateY);
  channelRecords[1].rmsValue = voltageState.vY_rms;
  channelRecords[1].dcOffset = dcBias;
  channelRecords[1].validity = voltageState.stateY;
  channelRecords[1].timestamp = (uint32_t)(millis() / 1000);

  computeVoltageChannel(2, windowSampleCount, voltageState.vB_rms, dcBias, voltageState.stateB);
  channelRecords[2].rmsValue = voltageState.vB_rms;
  channelRecords[2].dcOffset = dcBias;
  channelRecords[2].validity = voltageState.stateB;
  channelRecords[2].timestamp = (uint32_t)(millis() / 1000);

  voltageState.timestamp = (uint32_t)(millis() / 1000);

  // 3. Compute 18 Current Channels
  for (uint8_t f = 0; f < MAX_SYSTEM_FEEDERS; f++) {
    uint8_t chR = 3 + (f * 3);
    uint8_t chY = 4 + (f * 3);
    uint8_t chB = 5 + (f * 3);

    computeCurrentChannel(chR, windowSampleCount, feederStates[f].iR_rms, dcBias, feederStates[f].stateR);
    channelRecords[chR].rmsValue = feederStates[f].iR_rms;
    channelRecords[chR].dcOffset = dcBias;
    channelRecords[chR].validity = feederStates[f].stateR;
    channelRecords[chR].timestamp = voltageState.timestamp;

    computeCurrentChannel(chY, windowSampleCount, feederStates[f].iY_rms, dcBias, feederStates[f].stateY);
    channelRecords[chY].rmsValue = feederStates[f].iY_rms;
    channelRecords[chY].dcOffset = dcBias;
    channelRecords[chY].validity = feederStates[f].stateY;
    channelRecords[chY].timestamp = voltageState.timestamp;

    computeCurrentChannel(chB, windowSampleCount, feederStates[f].iB_rms, dcBias, feederStates[f].stateB);
    channelRecords[chB].rmsValue = feederStates[f].iB_rms;
    channelRecords[chB].dcOffset = dcBias;
    channelRecords[chB].validity = feederStates[f].stateB;
    channelRecords[chB].timestamp = voltageState.timestamp;

    // 4. Compute Real Power, Apparent Power, PF, and Energy
    computePowerAndEnergy(f, windowSampleCount);
  }

  // 5. Evaluate System-Wide Faults
  evaluateSystemFaults();

  lastEnergyCalcMs = millis();
  lastComputeTimestamp = voltageState.timestamp;
  return true;
}

const CommonVoltageState& PowerMeasurementEngine::getVoltageState() const { return voltageState; }
const FeederPowerState* PowerMeasurementEngine::getFeederState(uint8_t feederIndex) const {
  if (feederIndex >= 1 && feederIndex <= MAX_SYSTEM_FEEDERS) {
    return &feederStates[feederIndex - 1];
  }
  return nullptr;
}

const ChannelMeasurementRecord* PowerMeasurementEngine::getChannelRecord(uint8_t channelIdx) const {
  if (channelIdx < NUM_TOTAL_CHANNELS) {
    return &channelRecords[channelIdx];
  }
  return nullptr;
}

float PowerMeasurementEngine::getCommonVoltageR() const { return voltageState.vR_rms; }
float PowerMeasurementEngine::getCommonVoltageY() const { return voltageState.vY_rms; }
float PowerMeasurementEngine::getCommonVoltageB() const { return voltageState.vB_rms; }

float PowerMeasurementEngine::getFeederCurrent(uint8_t feederId, char phase) const {
  if (feederId < 1 || feederId > MAX_SYSTEM_FEEDERS) return 0.0f;
  const FeederPowerState &f = feederStates[feederId - 1];
  if (phase == 'R' || phase == 'r') return f.iR_rms;
  if (phase == 'Y' || phase == 'y') return f.iY_rms;
  if (phase == 'B' || phase == 'b') return f.iB_rms;
  return 0.0f;
}

float PowerMeasurementEngine::getFeederTotalPower(uint8_t feederId) const {
  if (feederId < 1 || feederId > MAX_SYSTEM_FEEDERS) return 0.0f;
  return feederStates[feederId - 1].pTotal_active;
}

float PowerMeasurementEngine::getFeederEnergyWh(uint8_t feederId) const {
  if (feederId < 1 || feederId > MAX_SYSTEM_FEEDERS) return 0.0f;
  return feederStates[feederId - 1].accumEnergyWh;
}

float PowerMeasurementEngine::getGridFrequency() const { return voltageState.gridFrequencyHz; }

bool PowerMeasurementEngine::hasActiveFault() const {
  if (voltageState.phaseLossR || voltageState.phaseLossY || voltageState.phaseLossB) return true;
  for (uint8_t f = 0; f < MAX_SYSTEM_FEEDERS; f++) {
    if (feederStates[f].isOverCurrent || feederStates[f].isImbalanceFault) return true;
  }
  return false;
}

String PowerMeasurementEngine::getSystemFaultSummary() const {
  String out = "";
  if (voltageState.phaseLossR) out += "PHASE_LOSS_R;";
  if (voltageState.phaseLossY) out += "PHASE_LOSS_Y;";
  if (voltageState.phaseLossB) out += "PHASE_LOSS_B;";

  for (uint8_t f = 0; f < MAX_SYSTEM_FEEDERS; f++) {
    if (feederStates[f].isOverCurrent) {
      out += "F" + String(f + 1) + "_OVER_CURR;";
    }
    if (feederStates[f].isImbalanceFault) {
      out += "F" + String(f + 1) + "_IMBALANCE;";
    }
  }
  if (out.length() == 0) return "OK";
  return out;
}

const char* PowerMeasurementEngine::getValidityString(MeasurementValidity val) {
  switch (val) {
    case MEAS_VALID:               return "VALID";
    case MEAS_ZERO_CURRENT:        return "ZERO_CURRENT";
    case MEAS_NO_SIGNAL:           return "NO_SIGNAL";
    case MEAS_UNDER_VOLTAGE:       return "UNDER_VOLT";
    case MEAS_OVER_VOLTAGE:        return "OVER_VOLT";
    case MEAS_OVER_CURRENT:        return "OVER_CURR";
    case MEAS_CURRENT_IMBALANCE:   return "IMBALANCE";
    case MEAS_PHASE_LOSS:          return "PHASE_LOSS";
    case MEAS_SENSOR_DISCONNECTED: return "DISCONNECTED";
    case MEAS_SATURATED:           return "SATURATED";
    case MEAS_ADC_OFFLINE:         return "ADC_OFFLINE";
    case MEAS_CAL_INVALID:         return "CAL_INVALID";
    case MEAS_UNAVAILABLE:         return "UNAVAILABLE";
    default:                       return "UNKNOWN";
  }
}
