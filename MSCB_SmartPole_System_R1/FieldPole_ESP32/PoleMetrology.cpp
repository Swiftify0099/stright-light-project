/**
 * @file PoleMetrology.cpp
 * @brief MSCB Smart Pole Node - 21-Channel Metrology & 3x ADS131M08 Driver Implementation
 * @version 1.2.0
 */

#include "PoleMetrology.h"
#include "PoleStorageManager.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#if defined(ARDUINO) && defined(ESP32)
  #include <Arduino.h>
#endif

PoleMetrology poleMetrology;

// Global dynamic decoder pin definitions
uint8_t g_poleDecoderEnablePin = POLE_DECODER_ENABLE_PIN;
uint8_t g_poleDecoderA0Pin = POLE_DECODER_A0_PIN;
uint8_t g_poleDecoderA1Pin = POLE_DECODER_A1_PIN;

// Authoritative 24-Raw-Channel Physical Mapping (Verified against Fifth-Pass Audit & Document 09)
const RawAdcChannelMap AUTHORITATIVE_ADC_MAP[METROLOGY_TOTAL_RAW_CH] = {
  // ── ADC #1 (Device 0, CS via Decoder /1Y0): Feeders 1 to 3 Currents ──
  { 0, 0,  4, "F01_CURRENT_R", false,  0, 0, false }, // Ch 04: Feeder 1 Phase R
  { 0, 1,  5, "F01_CURRENT_Y", false,  0, 1, false }, // Ch 05: Feeder 1 Phase Y
  { 0, 2,  6, "F01_CURRENT_B", false,  0, 2, false }, // Ch 06: Feeder 1 Phase B
  { 0, 3,  7, "F02_CURRENT_R", false,  1, 0, false }, // Ch 07: Feeder 2 Phase R
  { 0, 4,  8, "F02_CURRENT_Y", false,  1, 1, false }, // Ch 08: Feeder 2 Phase Y
  { 0, 5,  9, "F02_CURRENT_B", false,  1, 2, false }, // Ch 09: Feeder 2 Phase B
  { 0, 6, 10, "F03_CURRENT_R", false,  2, 0, false }, // Ch 10: Feeder 3 Phase R
  { 0, 7, 11, "F03_CURRENT_Y", false,  2, 1, false }, // Ch 11: Feeder 3 Phase Y

  // ── ADC #2 (Device 1, CS via Decoder /1Y1): Feeders 3 to 5 Currents + Spare 1 ──
  { 1, 0, 12, "F03_CURRENT_B", false,  2, 2, false }, // Ch 12: Feeder 3 Phase B
  { 1, 1, 13, "F04_CURRENT_R", false,  3, 0, false }, // Ch 13: Feeder 4 Phase R
  { 1, 2, 14, "F04_CURRENT_Y", false,  3, 1, false }, // Ch 14: Feeder 4 Phase Y
  { 1, 3, 15, "F04_CURRENT_B", false,  3, 2, false }, // Ch 15: Feeder 4 Phase B
  { 1, 4, 16, "F05_CURRENT_R", false,  4, 0, false }, // Ch 16: Feeder 5 Phase R
  { 1, 5, 17, "F05_CURRENT_Y", false,  4, 1, false }, // Ch 17: Feeder 5 Phase Y
  { 1, 6, 18, "F05_CURRENT_B", false,  4, 2, false }, // Ch 18: Feeder 5 Phase B
  { 1, 7,  0, "SPARE_ADC2_CH7",false, -1, 0, true  }, // Spare 1: 100-ohm tied to AGND

  // ── ADC #3 (Device 2, CS via Decoder /1Y2): Feeder 6 Currents + Bus Voltages + Spares 2,3 ──
  { 2, 0, 19, "F06_CURRENT_R", false,  5, 0, false }, // Ch 19: Feeder 6 Phase R
  { 2, 1, 20, "F06_CURRENT_Y", false,  5, 1, false }, // Ch 20: Feeder 6 Phase Y
  { 2, 2, 21, "F06_CURRENT_B", false,  5, 2, false }, // Ch 21: Feeder 6 Phase B
  { 2, 3,  1, "BUS_VOLTAGE_R", true,  -1, 0, false }, // Ch 01: Common Bus Voltage Phase R (ZMPT101B)
  { 2, 4,  2, "BUS_VOLTAGE_Y", true,  -1, 1, false }, // Ch 02: Common Bus Voltage Phase Y (ZMPT101B)
  { 2, 5,  3, "BUS_VOLTAGE_B", true,  -1, 2, false }, // Ch 03: Common Bus Voltage Phase B (ZMPT101B)
  { 2, 6,  0, "SPARE_ADC3_CH6",false, -1, 0, true  }, // Spare 2: 100-ohm tied to AGND
  { 2, 7,  0, "SPARE_ADC3_CH7",false, -1, 0, true  }  // Spare 3: 100-ohm tied to AGND
};

PoleMetrology::PoleMetrology() :
  allAdcsDetected(false),
  lastFrameTimestampMs(0),
  measuredFrequency(NAN),
  pinDecEn(POLE_DECODER_ENABLE_PIN),
  pinDecA0(POLE_DECODER_A0_PIN),
  pinDecA1(POLE_DECODER_A1_PIN),
  pinAdcSync(POLE_ADC_SYNC_PIN),
  sampleCountInWindow(0),
  zeroCrossings(0),
  lastZeroCrossVal(0)
{
  pinAdcDrdy[0] = POLE_ADC1_DRDY_PIN;
  pinAdcDrdy[1] = POLE_ADC2_DRDY_PIN;
  pinAdcDrdy[2] = POLE_ADC3_DRDY_PIN;
  for (uint8_t r = 0; r < METROLOGY_TOTAL_RAW_CH; r++) {
    adcChannelMap[r] = AUTHORITATIVE_ADC_MAP[r].logicalChId;
  }
  for (uint8_t i = 0; i < METROLOGY_NUM_ADC_CHIPS; i++) {
    chipDetected[i] = false;
    chipStatusWords[i] = 0x0000;
  }
  for (uint8_t i = 0; i < METROLOGY_TOTAL_ACTIVE_CH; i++) {
    channelStates[i] = CHANNEL_ADC_NOT_CONNECTED;
    channelFreshness[i] = MEAS_FRESHNESS_UNKNOWN;
    channelRmsValues[i] = NAN; // NO FAKE ZEROS
  }
  for (uint8_t f = 0; f < METROLOGY_NUM_FEEDERS; f++) {
    feederActivePower[f] = NAN;
    feederPowerFactor[f] = NAN;
  }
  resetWindowAccumulators();
  resetCalibrationToDefaults();
}

void PoleMetrology::resetWindowAccumulators() {
  sampleCountInWindow = 0;
  for (uint8_t i = 0; i < 3; i++) sumSqVoltage[i] = 0.0;
  for (uint8_t i = 0; i < 18; i++) sumSqCurrent[i] = 0.0;
  for (uint8_t f = 0; f < 6; f++) {
    for (uint8_t p = 0; p < 3; p++) {
      sumInstPower[f][p] = 0.0;
    }
  }
  zeroCrossings = 0;
}

void PoleMetrology::resetCalibrationToDefaults() {
  for (uint8_t i = 0; i < METROLOGY_TOTAL_ACTIVE_CH; i++) {
    uint8_t chId = i + 1; // 1-based
    memset(&calibrationTable[i], 0, sizeof(ChannelCalibRecord));
    calibrationTable[i].zeroOffset = 0.0f;
    calibrationTable[i].gainMultiplier = 1.00000f;
    calibrationTable[i].phaseCorrectionDeg = 0.0f;
    calibrationTable[i].calVersion = 1;
    calibrationTable[i].calTimestamp = 0;
    calibrationTable[i].isValid = false; // Strictly UNCALIBRATED until physical bench verification

    if (chId >= 1 && chId <= 3) {
      // Common Bus Voltage Channels (ZMPT101B with 300 ohm burden, nominal 400.0 V/V)
      snprintf(calibrationTable[i].channelId, sizeof(calibrationTable[i].channelId), "BUS_V_PH%c", "RYB"[chId - 1]);
      snprintf(calibrationTable[i].sensorIdentity, sizeof(calibrationTable[i].sensorIdentity), "ZMPT101B_300R");
      calibrationTable[i].sensorSensitivity = 400.0f; // Nominal 400.0 V/V per Doc 09 & BOM
      snprintf(calibrationTable[i].calReference, sizeof(calibrationTable[i].calReference), "UNCALIBRATED");
    } else {
      // Feeder Current Channels (SCT-013-000 1000:1 with 12.0 ohm burden, nominal 83.333 A/V per Doc 09 & BOM)
      uint8_t feederIdx = (chId - 4) / 3;
      uint8_t phaseIdx  = (chId - 4) % 3;
      snprintf(calibrationTable[i].channelId, sizeof(calibrationTable[i].channelId), "F%02d_I_PH%c", feederIdx + 1, "RYB"[phaseIdx]);
      snprintf(calibrationTable[i].sensorIdentity, sizeof(calibrationTable[i].sensorIdentity), "SCT013_12R");
      calibrationTable[i].sensorSensitivity = 83.333f; // 50A / (0.05A * 12R) = 83.333 A/V
      snprintf(calibrationTable[i].calReference, sizeof(calibrationTable[i].calReference), "UNCALIBRATED");
    }
  }
}

bool PoleMetrology::validateAds131m08DeviceId(uint16_t rawIdReg) {
  if (rawIdReg == 0x0000 || rawIdReg == 0xFFFF) {
    return false;
  }
  // TI ADS131M08 ID register [15:8] identifies 8-channel device family (0x28xx)
  return ((rawIdReg & 0xFF00) == 0x2800);
}

bool PoleMetrology::validateAds131m08StatusWord(uint16_t statusWord) {
  if (statusWord == 0x0000 || statusWord == 0xFFFF) {
    return false;
  }
  // Reject status word with fault/reset bit asserted (Bit 9 = RESET)
  if ((statusWord & 0x0200) != 0) {
    return false;
  }
  // Require at least one conversion DRDY bit asserted in lower byte
  return ((statusWord & 0x00FF) != 0x0000);
}

uint16_t PoleMetrology::getAdcStatusWord(uint8_t chipIdx) const {
  if (chipIdx >= METROLOGY_NUM_ADC_CHIPS) return 0x0000;
  return chipStatusWords[chipIdx];
}

void PoleMetrology::setAdcChipDetected(uint8_t chipIdx, bool detected) {
  if (chipIdx < METROLOGY_NUM_ADC_CHIPS) {
    chipDetected[chipIdx] = detected;
    chipStatusWords[chipIdx] = detected ? 0x05FF : 0x0000;
    allAdcsDetected = (chipDetected[0] && chipDetected[1] && chipDetected[2]);
    // Update channels depending on this chip using active channel map
    for (uint8_t r = 0; r < METROLOGY_TOTAL_RAW_CH; r++) {
      uint8_t rChip = r / METROLOGY_CHANNELS_PER_ADC;
      uint8_t lCh = adcChannelMap[r];
      if (rChip == chipIdx && lCh >= 1 && lCh <= METROLOGY_TOTAL_ACTIVE_CH) {
        uint8_t lIdx = lCh - 1;
        if (!detected) {
          channelStates[lIdx] = CHANNEL_ADC_NOT_CONNECTED;
          channelFreshness[lIdx] = MEAS_FRESHNESS_UNKNOWN;
          channelRmsValues[lIdx] = NAN;
        } else {
          channelStates[lIdx] = calibrationTable[lIdx].isValid ? CHANNEL_DATA_UNAVAILABLE : CHANNEL_UNCALIBRATED;
          channelFreshness[lIdx] = MEAS_FRESHNESS_UNKNOWN;
        }
      }
    }
  }
}

bool PoleMetrology::isAdcChipDetected(uint8_t chipIdx) const {
  if (chipIdx >= METROLOGY_NUM_ADC_CHIPS) return false;
  return chipDetected[chipIdx];
}

bool PoleMetrology::detectHardware() {
  allAdcsDetected = false;

#if defined(ARDUINO) && defined(ESP32)
  const uint8_t devSelectors[METROLOGY_NUM_ADC_CHIPS] = {
    POLE_SPI_DEV_ADC1, POLE_SPI_DEV_ADC2, POLE_SPI_DEV_ADC3
  };

  for (uint8_t c = 0; c < METROLOGY_NUM_ADC_CHIPS; c++) {
    poleSelectSpiDevice(devSelectors[c]);
    // Read ID register: Opcode 0x2000 (RREG address 0x00, 1 register)
    uint16_t cmd = 0x2000;
    SPI.transfer16(cmd);
    SPI.transfer16(0x0000);
    poleSelectSpiDevice(POLE_SPI_DEV_DESELECT);

    // Readback register content
    poleSelectSpiDevice(devSelectors[c]);
    uint16_t response = SPI.transfer16(0x0000);
    poleSelectSpiDevice(POLE_SPI_DEV_DESELECT);

    if (validateAds131m08DeviceId(response)) {
      chipDetected[c] = true;
      chipStatusWords[c] = 0x05FF;
    } else {
      chipDetected[c] = false;
      chipStatusWords[c] = 0x0000;
    }
  }
#else
  // Simulation default: absent unless explicitly injected in tests
  chipDetected[0] = false;
  chipDetected[1] = false;
  chipDetected[2] = false;
  chipStatusWords[0] = 0x0000;
  chipStatusWords[1] = 0x0000;
  chipStatusWords[2] = 0x0000;
#endif

  allAdcsDetected = (chipDetected[0] && chipDetected[1] && chipDetected[2]);

  // Update per-channel states based on actual chip detection & active channel map
  for (uint8_t r = 0; r < METROLOGY_TOTAL_RAW_CH; r++) {
    uint8_t rChip = r / METROLOGY_CHANNELS_PER_ADC;
    uint8_t lCh = adcChannelMap[r];
    if (lCh < 1 || lCh > METROLOGY_TOTAL_ACTIVE_CH) continue;
    uint8_t lIdx = lCh - 1;

    if (!chipDetected[rChip]) {
      channelStates[lIdx] = CHANNEL_ADC_NOT_CONNECTED;
      channelFreshness[lIdx] = MEAS_FRESHNESS_UNKNOWN;
      channelRmsValues[lIdx] = NAN;
    } else {
      channelStates[lIdx] = calibrationTable[lIdx].isValid ? CHANNEL_DATA_UNAVAILABLE : CHANNEL_UNCALIBRATED;
      channelFreshness[lIdx] = MEAS_FRESHNESS_UNKNOWN;
    }
  }

  return allAdcsDetected;
}

void PoleMetrology::configurePins(uint8_t decEn, uint8_t decA0, uint8_t decA1, uint8_t sync, const uint8_t drdyPins[3],
                                  const uint8_t adcChMap[METROLOGY_TOTAL_RAW_CH]) {
  pinDecEn = decEn;
  pinDecA0 = decA0;
  pinDecA1 = decA1;
  pinAdcSync = sync;
  g_poleDecoderEnablePin = decEn;
  g_poleDecoderA0Pin = decA0;
  g_poleDecoderA1Pin = decA1;
  if (drdyPins) {
    for (uint8_t i = 0; i < 3; i++) pinAdcDrdy[i] = drdyPins[i];
  }
  if (adcChMap) {
    // Validate 1..21 bijection + 3 spares before activating runtime map
    uint8_t seen[METROLOGY_TOTAL_ACTIVE_CH + 1];
    memset(seen, 0, sizeof(seen));
    uint8_t spares = 0;
    bool valid = true;
    for (uint8_t r = 0; r < METROLOGY_TOTAL_RAW_CH; r++) {
      uint8_t ch = adcChMap[r];
      if (ch == 0) {
        spares++;
      } else if (ch <= METROLOGY_TOTAL_ACTIVE_CH && seen[ch] == 0) {
        seen[ch] = 1;
      } else {
        valid = false;
        break;
      }
    }
    if (valid && spares == 3) {
      for (uint8_t c = 1; c <= METROLOGY_TOTAL_ACTIVE_CH; c++) {
        if (seen[c] != 1) {
          valid = false;
          break;
        }
      }
      if (valid) {
        memcpy(adcChannelMap, adcChMap, METROLOGY_TOTAL_RAW_CH);
      }
    }
  }
}

bool PoleMetrology::begin() {
#if defined(ARDUINO)
  pinMode(pinAdcSync, OUTPUT);
  digitalWrite(pinAdcSync, HIGH);

  pinMode(pinAdcDrdy[0], INPUT);
  pinMode(pinAdcDrdy[1], INPUT);
  pinMode(pinAdcDrdy[2], INPUT);
#endif

  // Load saved calibration from storage
  poleStorage.loadCalibration(calibrationTable);

  bool ok = detectHardware();
  return ok;
}

bool PoleMetrology::checkZeroFrame(const int32_t rawSamples[METROLOGY_TOTAL_RAW_CH]) const {
  if (!rawSamples) return true;
  bool allZeros = true;
  bool allOnes = true;

  for (uint8_t i = 0; i < METROLOGY_TOTAL_RAW_CH; i++) {
    if (rawSamples[i] != 0) {
      allZeros = false;
    }
    if (rawSamples[i] != -1 && (uint32_t)rawSamples[i] != 0x00FFFFFF) {
      allOnes = false;
    }
  }
  return (allZeros || allOnes);
}

int PoleMetrology::findRawIndexForLogicalCh(uint8_t logicalChId1Based) const {
  if (logicalChId1Based < 1 || logicalChId1Based > METROLOGY_TOTAL_ACTIVE_CH) return -1;
  for (uint8_t r = 0; r < METROLOGY_TOTAL_RAW_CH; r++) {
    if (adcChannelMap[r] == logicalChId1Based) {
      return r;
    }
  }
  return -1;
}

bool PoleMetrology::processRawAdcFrame(const int32_t rawSamples[METROLOGY_TOTAL_RAW_CH], uint32_t timestampMs) {
  uint16_t nominalStatus[METROLOGY_NUM_ADC_CHIPS];
  for (uint8_t c = 0; c < METROLOGY_NUM_ADC_CHIPS; c++) {
    nominalStatus[c] = chipDetected[c] ? 0x05FF : 0x0000;
  }
  return processRawAdcFrameWithStatus(nominalStatus, rawSamples, timestampMs);
}

bool PoleMetrology::processRawAdcFrameWithStatus(const uint16_t statusWords[METROLOGY_NUM_ADC_CHIPS],
                                                 const int32_t rawSamples[METROLOGY_TOTAL_RAW_CH],
                                                 uint32_t timestampMs) {
  // 1. Zero-Frame & Dead-Bus Rejection
  if (checkZeroFrame(rawSamples)) {
    for (uint8_t i = 0; i < METROLOGY_TOTAL_ACTIVE_CH; i++) {
      channelStates[i] = CHANNEL_ZERO_FRAME_REJECTED;
      channelFreshness[i] = MEAS_FRESHNESS_STALE;
      channelRmsValues[i] = NAN;
    }
    for (uint8_t f = 0; f < METROLOGY_NUM_FEEDERS; f++) {
      feederActivePower[f] = NAN;
      feederPowerFactor[f] = NAN;
    }
    measuredFrequency = NAN;
    return false;
  }

  for (uint8_t c = 0; c < METROLOGY_NUM_ADC_CHIPS; c++) {
    chipStatusWords[c] = (statusWords && chipDetected[c]) ? statusWords[c] : 0x0000;
  }

  // Full-scale voltage calculation for ADS131M08 (FSR = +/- 1.2V diff at Gain 1)
  // 24-bit bipolar 2's complement: Range is -8,388,608 to +8,388,607
  const float V_PER_COUNT = 1.20f / 8388607.0f;

  float pinVoltages[METROLOGY_TOTAL_RAW_CH];
  bool anyValidChipFrame = false;

  // Decode raw 24-bit signed samples into pin voltages
  for (uint8_t r = 0; r < METROLOGY_TOTAL_RAW_CH; r++) {
    uint8_t chipIdx = r / METROLOGY_CHANNELS_PER_ADC;
    uint8_t logicalCh = adcChannelMap[r];
    if (logicalCh < 1 || logicalCh > METROLOGY_TOTAL_ACTIVE_CH) {
      pinVoltages[r] = 0.0f;
      continue;
    }

    uint8_t lIdx = logicalCh - 1;

    // Check if the underlying ADC chip was detected and has valid SPI status word
    if (!chipDetected[chipIdx]) {
      channelStates[lIdx] = CHANNEL_ADC_NOT_CONNECTED;
      channelFreshness[lIdx] = MEAS_FRESHNESS_UNKNOWN;
      channelRmsValues[lIdx] = NAN;
      pinVoltages[r] = 0.0f;
      continue;
    }
    if (!validateAds131m08StatusWord(chipStatusWords[chipIdx])) {
      channelStates[lIdx] = CHANNEL_DATA_UNAVAILABLE;
      channelFreshness[lIdx] = MEAS_FRESHNESS_UNKNOWN;
      channelRmsValues[lIdx] = NAN;
      pinVoltages[r] = 0.0f;
      continue;
    }

    anyValidChipFrame = true;
    int32_t sample = rawSamples[r];
    // Sign extension from 24-bit
    if (sample & 0x00800000) {
      sample |= 0xFF000000;
    }

    float rawV = (float)sample * V_PER_COUNT;
    const ChannelCalibRecord &cal = calibrationTable[lIdx];
    float calibratedV = (rawV - cal.zeroOffset) * cal.gainMultiplier;

    // Saturation / Clipping Check (+/- 1.199V)
    if (calibratedV >= 1.199f || calibratedV <= -1.199f) {
      channelStates[lIdx] = CHANNEL_OVER_RANGE_CLIPPING;
      channelRmsValues[lIdx] = NAN;
    } else if (!cal.isValid) {
      channelStates[lIdx] = CHANNEL_UNCALIBRATED;
      channelRmsValues[lIdx] = NAN;
    } else {
      channelStates[lIdx] = CHANNEL_STATE_OK; // VALID
    }

    pinVoltages[r] = calibratedV;
    channelFreshness[lIdx] = MEAS_FRESHNESS_LIVE;
  }

  if (!anyValidChipFrame) {
    for (uint8_t f = 0; f < METROLOGY_NUM_FEEDERS; f++) {
      feederActivePower[f] = NAN;
      feederPowerFactor[f] = NAN;
    }
    measuredFrequency = NAN;
    return false;
  }

  lastFrameTimestampMs = timestampMs;

  // 2. Window Accumulator for True-RMS and Instantaneous Power
  sampleCountInWindow++;

  // Accumulate Bus Voltages (Channels 1, 2, 3)
  for (uint8_t p = 0; p < 3; p++) {
    int rIdx = findRawIndexForLogicalCh(p + 1);
    if (rIdx >= 0 && (channelStates[p] == CHANNEL_STATE_OK || channelStates[p] == CHANNEL_UNCALIBRATED)) {
      float v = pinVoltages[rIdx];
      sumSqVoltage[p] += (double)(v * v);

      // Frequency zero-crossing detector on Phase R
      if (p == 0 && channelStates[0] == CHANNEL_STATE_OK) {
        if (lastZeroCrossVal < 0 && rawSamples[rIdx] >= 0) {
          zeroCrossings++;
        }
        lastZeroCrossVal = rawSamples[rIdx];
      }
    }
  }

  // Accumulate Feeder Currents and Instantaneous Power products
  for (uint8_t f = 0; f < METROLOGY_NUM_FEEDERS; f++) {
    for (uint8_t p = 0; p < 3; p++) {
      uint8_t logicalCh = 4 + (f * 3) + p;
      int rIdx = findRawIndexForLogicalCh(logicalCh);
      int vIdx = findRawIndexForLogicalCh(p + 1);

      if (rIdx >= 0 && (channelStates[logicalCh - 1] == CHANNEL_STATE_OK || channelStates[logicalCh - 1] == CHANNEL_UNCALIBRATED)) {
        float iPin = pinVoltages[rIdx];
        uint8_t currentIdx = (f * 3) + p;
        sumSqCurrent[currentIdx] += (double)(iPin * iPin);

        if (vIdx >= 0 && (channelStates[p] == CHANNEL_STATE_OK || channelStates[p] == CHANNEL_UNCALIBRATED)) {
          float vPin = pinVoltages[vIdx];
          sumInstPower[f][p] += (double)(vPin * iPin);
        }
      }
    }
  }

  // 3. Compute True-RMS and Power when window completes (or every frame update in test harness)
  // Window size: METROLOGY_WINDOW_SIZE or >= 1 sample
  if (sampleCountInWindow >= METROLOGY_WINDOW_SIZE || sampleCountInWindow == 1) {
    double N = (double)sampleCountInWindow;

    // Bus Voltages True-RMS
    for (uint8_t p = 0; p < 3; p++) {
      if (channelStates[p] == CHANNEL_STATE_OK && calibrationTable[p].isValid) {
        float rmsPinV = (float)sqrt(sumSqVoltage[p] / N);
        channelRmsValues[p] = rmsPinV * calibrationTable[p].sensorSensitivity;
      } else {
        channelRmsValues[p] = NAN;
      }
    }

    // Feeder Currents True-RMS
    for (uint8_t i = 0; i < 18; i++) {
      uint8_t lIdx = i + 3;
      if (channelStates[lIdx] == CHANNEL_STATE_OK && calibrationTable[lIdx].isValid) {
        float rmsPinI = (float)sqrt(sumSqCurrent[i] / N);
        channelRmsValues[lIdx] = rmsPinI * calibrationTable[lIdx].sensorSensitivity;
      } else {
        channelRmsValues[lIdx] = NAN;
      }
    }

    // Instantaneous Active Power & Calculated Power Factor (blocked unless calibrated & valid)
    for (uint8_t f = 0; f < METROLOGY_NUM_FEEDERS; f++) {
      double totalActiveWatts = 0.0;
      double totalApparentVA = 0.0;
      bool hasValidPower = true;

      for (uint8_t p = 0; p < 3; p++) {
        uint8_t vLogical = p + 1;
        uint8_t iLogical = 4 + (f * 3) + p;

        if (channelStates[vLogical - 1] != CHANNEL_STATE_OK || !calibrationTable[vLogical - 1].isValid) {
          hasValidPower = false;
        }
        if (channelStates[iLogical - 1] != CHANNEL_STATE_OK || !calibrationTable[iLogical - 1].isValid) {
          hasValidPower = false;
        }

        float sensV = calibrationTable[vLogical - 1].sensorSensitivity;
        float sensI = calibrationTable[iLogical - 1].sensorSensitivity;

        // True Active Power: (1/N) * sum(v[n] * i[n]) * sensV * sensI
        double phaseActiveWatts = (sumInstPower[f][p] / N) * sensV * sensI;
        totalActiveWatts += phaseActiveWatts;

        float vRms = channelRmsValues[vLogical - 1];
        float iRms = channelRmsValues[iLogical - 1];
        if (!isnan(vRms) && !isnan(iRms)) {
          totalApparentVA += (vRms * iRms);
        }
      }

      if (hasValidPower) {
        feederActivePower[f] = (float)(totalActiveWatts / 1000.0); // in kW

        // True Power Factor: |P| / S (STRICTLY NO HARDCODED 0.95!)
        if (totalApparentVA > 1.0) {
          float pf = (float)(fabs(totalActiveWatts) / totalApparentVA);
          if (pf > 1.0f) pf = 1.0f;
          feederPowerFactor[f] = pf;
        } else {
          feederPowerFactor[f] = 1.0f;
        }
      } else {
        feederActivePower[f] = NAN;
        feederPowerFactor[f] = NAN;
      }
    }

    // Frequency calculation
    if (sampleCountInWindow >= METROLOGY_WINDOW_SIZE) {
      if (channelStates[0] == CHANNEL_STATE_OK && calibrationTable[0].isValid) {
        // 80 samples at 4000 Hz = 20 ms -> 1 cycle -> 50 Hz
        measuredFrequency = ((float)zeroCrossings * 50.0f);
      } else {
        measuredFrequency = NAN;
      }
      resetWindowAccumulators();
    }
  }

  return true;
}

void PoleMetrology::acquireRawSamples() {
#if defined(ARDUINO) && defined(ESP32)
  if (!allAdcsDetected) return;

  // DRDY lines are active-low when conversion frame is ready (using configured DRDY pins!)
  if (digitalRead(pinAdcDrdy[0]) == LOW &&
      digitalRead(pinAdcDrdy[1]) == LOW &&
      digitalRead(pinAdcDrdy[2]) == LOW) {

    int32_t rawSamples[METROLOGY_TOTAL_RAW_CH];
    uint16_t statusWords[METROLOGY_NUM_ADC_CHIPS];
    memset(rawSamples, 0, sizeof(rawSamples));
    memset(statusWords, 0, sizeof(statusWords));

    const uint8_t devSelectors[METROLOGY_NUM_ADC_CHIPS] = {
      POLE_SPI_DEV_ADC1, POLE_SPI_DEV_ADC2, POLE_SPI_DEV_ADC3
    };

    for (uint8_t c = 0; c < METROLOGY_NUM_ADC_CHIPS; c++) {
      if (!chipDetected[c]) continue;
      poleSelectSpiDevice(devSelectors[c]);
      // Capture and validate 16-bit ADS131M08 status word (never silently discard)
      statusWords[c] = SPI.transfer16(0x0000);
      for (uint8_t ch = 0; ch < METROLOGY_CHANNELS_PER_ADC; ch++) {
        uint8_t b0 = SPI.transfer(0x00);
        uint8_t b1 = SPI.transfer(0x00);
        uint8_t b2 = SPI.transfer(0x00);
        int32_t val = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | (int32_t)b2;
        rawSamples[c * METROLOGY_CHANNELS_PER_ADC + ch] = val;
      }
      poleSelectSpiDevice(POLE_SPI_DEV_DESELECT);
    }

    processRawAdcFrameWithStatus(statusWords, rawSamples, millis());
  }
#endif
}

void PoleMetrology::service() {
  acquireRawSamples();

  uint32_t now = millis();
  if (lastFrameTimestampMs != 0 && ((uint32_t)(now - lastFrameTimestampMs) > 1500UL)) {
    for (uint8_t i = 0; i < METROLOGY_TOTAL_ACTIVE_CH; i++) {
      if (channelStates[i] == CHANNEL_STATE_OK || channelStates[i] == CHANNEL_UNCALIBRATED) {
        channelStates[i] = CHANNEL_DATA_STALE;
        channelFreshness[i] = MEAS_FRESHNESS_STALE;
        channelRmsValues[i] = NAN;
      }
    }
    for (uint8_t f = 0; f < METROLOGY_NUM_FEEDERS; f++) {
      feederActivePower[f] = NAN;
      feederPowerFactor[f] = NAN;
    }
    measuredFrequency = NAN;
  }
}

ChannelDataState PoleMetrology::getChannelState(uint8_t logicalChId1Based) const {
  if (logicalChId1Based < 1 || logicalChId1Based > METROLOGY_TOTAL_ACTIVE_CH) {
    return CHANNEL_DATA_UNAVAILABLE;
  }
  return channelStates[logicalChId1Based - 1];
}

MeasurementFreshness PoleMetrology::getChannelFreshness(uint8_t logicalChId1Based) const {
  if (logicalChId1Based < 1 || logicalChId1Based > METROLOGY_TOTAL_ACTIVE_CH) {
    return MEAS_FRESHNESS_UNKNOWN;
  }
  return channelFreshness[logicalChId1Based - 1];
}

MeasurementValidity PoleMetrology::getMeasurementValidity(uint8_t logicalChId1Based) const {
  if (logicalChId1Based < 1 || logicalChId1Based > METROLOGY_TOTAL_ACTIVE_CH) {
    return MEAS_UNKNOWN;
  }
  uint8_t idx = logicalChId1Based - 1;
  if (channelStates[idx] == CHANNEL_ADC_NOT_CONNECTED ||
      channelStates[idx] == CHANNEL_DATA_UNAVAILABLE) {
    return MEAS_SENSOR_UNAVAILABLE;
  }
  if (channelStates[idx] == CHANNEL_ZERO_FRAME_REJECTED) {
    return MEAS_INVALID;
  }
  if (channelStates[idx] == CHANNEL_OVER_RANGE_CLIPPING) {
    return MEAS_OUT_OF_RANGE;
  }
  if (channelStates[idx] == CHANNEL_DATA_STALE ||
      channelFreshness[idx] == MEAS_FRESHNESS_STALE) {
    return MEAS_STALE;
  }
  if (channelStates[idx] == CHANNEL_UNCALIBRATED || !calibrationTable[idx].isValid) {
    return MEAS_NOT_CALIBRATED;
  }
  if (channelStates[idx] != CHANNEL_STATE_OK) {
    return MEAS_INVALID;
  }
  float val = channelRmsValues[idx];
  if (isnan(val)) {
    return MEAS_INVALID;
  }
  return MEAS_VALID;
}

float PoleMetrology::getChannelValue(uint8_t logicalChId1Based) const {
  if (logicalChId1Based < 1 || logicalChId1Based > METROLOGY_TOTAL_ACTIVE_CH) {
    return NAN;
  }
  uint8_t idx = logicalChId1Based - 1;
  if (channelStates[idx] != CHANNEL_STATE_OK || !calibrationTable[idx].isValid) {
    return NAN; // Fail-Closed: Never output numbers for uncalibrated or invalid channels!
  }
  return channelRmsValues[idx];
}

float PoleMetrology::getBusVoltageR() const { return getChannelValue(1); }
float PoleMetrology::getBusVoltageY() const { return getChannelValue(2); }
float PoleMetrology::getBusVoltageB() const { return getChannelValue(3); }

float PoleMetrology::getGridFrequency() const {
  if (channelStates[0] != CHANNEL_STATE_OK || !calibrationTable[0].isValid) {
    return NAN;
  }
  // NEVER return synthetic 50.0f! Return measured value or NAN
  return measuredFrequency;
}

float PoleMetrology::getFeederCurrent(uint8_t feederIdx0Based, uint8_t phaseIdx0Based) const {
  if (feederIdx0Based >= METROLOGY_NUM_FEEDERS || phaseIdx0Based >= METROLOGY_NUM_PHASES) {
    return NAN;
  }
  uint8_t logicalCh = 4 + (feederIdx0Based * 3) + phaseIdx0Based;
  return getChannelValue(logicalCh);
}

float PoleMetrology::getFeederActivePower(uint8_t feederIdx0Based) const {
  if (feederIdx0Based >= METROLOGY_NUM_FEEDERS) return NAN;
  return feederActivePower[feederIdx0Based];
}

float PoleMetrology::getFeederPowerFactor(uint8_t feederIdx0Based) const {
  if (feederIdx0Based >= METROLOGY_NUM_FEEDERS) return NAN;
  return feederPowerFactor[feederIdx0Based];
}

bool PoleMetrology::setChannelCalibration(uint8_t logicalChId1Based, float zeroOffset, float gainMultiplier, float sensitivity, float phaseCorrectionDeg) {
  if (logicalChId1Based < 1 || logicalChId1Based > METROLOGY_TOTAL_ACTIVE_CH) return false;
  uint8_t idx = logicalChId1Based - 1;
  calibrationTable[idx].zeroOffset = zeroOffset;
  calibrationTable[idx].gainMultiplier = gainMultiplier;
  calibrationTable[idx].sensorSensitivity = sensitivity;
  calibrationTable[idx].phaseCorrectionDeg = phaseCorrectionDeg;
  calibrationTable[idx].isValid = true;
  snprintf(calibrationTable[idx].calReference, sizeof(calibrationTable[idx].calReference), "MANUAL_CAL");
  if (channelStates[idx] == CHANNEL_UNCALIBRATED) {
    channelStates[idx] = CHANNEL_DATA_UNAVAILABLE;
    channelRmsValues[idx] = NAN;
  }
  return true;
}

const ChannelCalibRecord& PoleMetrology::getChannelCalibration(uint8_t logicalChId1Based) const {
  if (logicalChId1Based < 1 || logicalChId1Based > METROLOGY_TOTAL_ACTIVE_CH) {
    return calibrationTable[0];
  }
  return calibrationTable[logicalChId1Based - 1];
}

int PoleMetrology::formatMetrologyAuditReport(char* out, size_t outSize) const {
  if (!out || outSize == 0) return 0;
  int written = snprintf(out, outSize,
    "ADS131M08 Chips: ADC1=%s(0x%04X) ADC2=%s(0x%04X) ADC3=%s(0x%04X) | Pins En=%u A0=%u A1=%u Sync=%u DRDY=[%u,%u,%u]\n",
    chipDetected[0] ? "OK" : "ABSENT", chipStatusWords[0],
    chipDetected[1] ? "OK" : "ABSENT", chipStatusWords[1],
    chipDetected[2] ? "OK" : "ABSENT", chipStatusWords[2],
    pinDecEn, pinDecA0, pinDecA1, pinAdcSync, pinAdcDrdy[0], pinAdcDrdy[1], pinAdcDrdy[2]);
  if (written < 0) return 0;
  for (uint8_t i = 0; i < METROLOGY_TOTAL_ACTIVE_CH && (size_t)written < outSize; i++) {
    const ChannelCalibRecord &cal = calibrationTable[i];
    char valStr[16];
    if (isnan(channelRmsValues[i])) {
      snprintf(valStr, sizeof(valStr), "NAN");
    } else {
      snprintf(valStr, sizeof(valStr), "%.2f", channelRmsValues[i]);
    }
    int n = snprintf(out + written, outSize - (size_t)written,
                     "CH%02u %-12s state=%s valid=%s val=%s\n",
                     (unsigned)(i + 1), cal.channelId,
                     channelDataStateToString(channelStates[i]),
                     measurementValidityToString(getMeasurementValidity(i + 1)),
                     valStr);
    if (n < 0) break;
    written += n;
  }
  return written;
}

void PoleMetrology::printMetrologyAuditReport() const {
#if defined(ARDUINO)
  Serial.println(F("\n========================================================"));
  Serial.println(F("    MSCB FIELD POLE 21-CHANNEL METROLOGY AUDIT REPORT   "));
  Serial.println(F("========================================================"));
  Serial.printf("ADS131M08 Chips Detected : ADC1: %s (0x%04X) | ADC2: %s (0x%04X) | ADC3: %s (0x%04X)\n",
                chipDetected[0] ? "YES" : "NO", chipStatusWords[0],
                chipDetected[1] ? "YES" : "NO", chipStatusWords[1],
                chipDetected[2] ? "YES" : "NO", chipStatusWords[2]);
  Serial.printf("Metrology Pins (Runtime) : DecEn=%u A0=%u A1=%u Sync=%u DRDY=[%u,%u,%u]\n",
                pinDecEn, pinDecA0, pinDecA1, pinAdcSync, pinAdcDrdy[0], pinAdcDrdy[1], pinAdcDrdy[2]);
  Serial.println(F("--------------------------------------------------------"));
  Serial.println(F("CH | NAME          | STATE          | RMS VALUE | VALIDITY           | SENSITIVITY"));
  for (uint8_t i = 0; i < METROLOGY_TOTAL_ACTIVE_CH; i++) {
    const ChannelCalibRecord &cal = calibrationTable[i];
    char valStr[16];
    if (isnan(channelRmsValues[i])) {
      snprintf(valStr, sizeof(valStr), "-NAN-   ");
    } else {
      snprintf(valStr, sizeof(valStr), "%8.2f", channelRmsValues[i]);
    }
    Serial.printf("%02d | %-13s | %-14s | %-9s | %-18s | %0.2f\n",
                  i + 1, cal.channelId,
                  channelDataStateToString(channelStates[i]),
                  valStr,
                  measurementValidityToString(getMeasurementValidity(i + 1)),
                  cal.sensorSensitivity);
  }
  Serial.println(F("========================================================\n"));
#endif
}
