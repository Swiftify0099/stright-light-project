/**
 * @file PoleMetrology.h
 * @brief MSCB Smart Pole Node - 21-Channel Metrology & 3x ADS131M08 Driver
 * @version 1.2.0
 * 
 * AUTHORITATIVE METROLOGY & ADC SAFETY ARCHITECTURE:
 * 1. 21 ACTIVE CHANNELS + 3 BALANCED SPARES (24 TOTAL RAW CHANNELS):
 *    - 3x Texas Instruments ADS131M08 (24-bit simultaneous sampling delta-sigma)
 *    - Device 0 (ADC #1, CS /1Y0): Feeders 1..3 Currents (Ch 04..11)
 *    - Device 1 (ADC #2, CS /1Y1): Feeders 4..5 Currents (Ch 12..18) + Spare 1
 *    - Device 2 (ADC #3, CS /1Y2): Feeder 6 Currents (Ch 19..21) + Bus Voltages R,Y,B (Ch 01..03) + Spares 2,3
 * 
 * 2. ZERO-SIMULATION & HARDWARE DETECTION MANDATE (NO FAKE DATA):
 *    - Never switch to simulated readings when ADC hardware is missing.
 *    - Hardware presence verified via SPI register readback on all 3 chips.
 *    - If ANY ADC chip is missing, channels depending on it report CHANNEL_ADC_NOT_CONNECTED.
 *    - For unverified / disconnected states, readings return NAN or invalid status.
 * 
 * 3. REAL WINDOW-BASED METROLOGY:
 *    - True-RMS computed over real sample windows: sqrt( (1/N) * sum( (x[n] - offset)^2 ) ).
 *    - Real Active Power computed from synchronized instantaneous v[n] * i[n] integration.
 *    - Real Power Factor computed from |P| / S (strictly NO hardcoded 0.95!).
 *    - Real Grid Frequency computed from zero-crossing intervals.
 * 
 * 4. CALIBRATION & ACCURACY:
 *    - 21 independent ChannelCalibRecord structures.
 *    - Class 1.0 accuracy strictly withheld pending physical bench certification.
 */

#ifndef POLE_METROLOGY_H
#define POLE_METROLOGY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#if defined(ARDUINO) && defined(ESP32)
  #include <Arduino.h>
  #include <SPI.h>
#else
  #include <cmath>
  #include <cstring>
  #include <cstdio>
  #include "../Tests/ArduinoMock.h"
#endif

#include "PoleBoardPins.h"
#include "PoleConfig.h"
#if __has_include("../MSCBShared/StorageTypes.h")
  #include "../MSCBShared/StorageTypes.h"
  #include "../MSCBShared/NetworkTypes.h"
#elif __has_include(<StorageTypes.h>)
  #include <StorageTypes.h>
  #include <NetworkTypes.h>
#endif

#define METROLOGY_TOTAL_RAW_CH       24
#define METROLOGY_TOTAL_ACTIVE_CH    21
#define METROLOGY_NUM_ADC_CHIPS      3
#define METROLOGY_CHANNELS_PER_ADC   8
#define METROLOGY_NUM_FEEDERS        6
#define METROLOGY_NUM_PHASES         3
#define METROLOGY_WINDOW_SIZE        80 // Nominal 1 cycle at 4 kSPS / 50 Hz

#define ADS131M08_REG_ID             0x00
#define ADS131M08_REG_STATUS         0x01
#define ADS131M08_REG_MODE           0x02
#define ADS131M08_REG_CLOCK          0x03
#define ADS131M08_REG_GAIN1          0x04
#define ADS131M08_REG_GAIN2          0x05
#define ADS131M08_REG_CFG            0x06

// Raw ADC physical pin allocation record
struct RawAdcChannelMap {
  uint8_t     chipIndex;      // 0=ADC1, 1=ADC2, 2=ADC3
  uint8_t     chipChannel;    // 0..7
  uint8_t     logicalChId;    // 1..21 (0 for spare)
  const char* signalName;     // Signal description
  bool        isVoltage;      // true for Bus Voltage, false for Feeder Current
  int8_t      feederIndex;    // 0..5 (-1 for bus voltage / spare)
  uint8_t     phaseIndex;     // 0=R, 1=Y, 2=B
  bool        isSpare;        // true for balanced spares
};

extern const RawAdcChannelMap AUTHORITATIVE_ADC_MAP[METROLOGY_TOTAL_RAW_CH];

class PoleMetrology {
public:
  PoleMetrology();

  // Initialization & Hardware Verification
  bool begin();
  bool detectHardware();
  bool areAllAdcsDetected() const { return allAdcsDetected; }
  bool isAdcChipDetected(uint8_t chipIdx) const;
  void setAdcChipDetected(uint8_t chipIdx, bool detected); // Test harness control

  // Raw Sample Processing & Frame Validation
  bool processRawAdcFrame(const int32_t rawSamples[METROLOGY_TOTAL_RAW_CH], uint32_t timestampMs);
  void acquireRawSamples();
  void service(); // Continuous acquisition, timeouts & freshness watchdog

  // Channel Status & Measurements
  ChannelDataState getChannelState(uint8_t logicalChId1Based) const;
  MeasurementFreshness getChannelFreshness(uint8_t logicalChId1Based) const;
  MeasurementValidity getMeasurementValidity(uint8_t logicalChId1Based) const;
  float getChannelValue(uint8_t logicalChId1Based) const;

  // Voltage Readings (Logical Ch 1..3: R, Y, B)
  float getBusVoltageR() const;
  float getBusVoltageY() const;
  float getBusVoltageB() const;
  float getGridFrequency() const;

  // Feeder Current & Power (Logical Ch 4..21)
  float getFeederCurrent(uint8_t feederIdx0Based, uint8_t phaseIdx0Based) const;
  float getFeederActivePower(uint8_t feederIdx0Based) const;
  float getFeederPowerFactor(uint8_t feederIdx0Based) const;

  // Calibration API (Per-channel independent coefficients)
  bool setChannelCalibration(uint8_t logicalChId1Based, float zeroOffset, float gainMultiplier, float sensitivity, float phaseCorrectionDeg);
  const ChannelCalibRecord& getChannelCalibration(uint8_t logicalChId1Based) const;
  void resetCalibrationToDefaults();

  // Accuracy Certification Status
  bool isClass1_0Certified() const { return false; } // Strictly FALSE without physical calibration certificate

  // Diagnostic Report
  void printMetrologyAuditReport() const;

private:
  bool checkZeroFrame(const int32_t rawSamples[METROLOGY_TOTAL_RAW_CH]) const;
  int findRawIndexForLogicalCh(uint8_t logicalChId1Based) const;
  void resetWindowAccumulators();

  bool               allAdcsDetected;
  bool               chipDetected[METROLOGY_NUM_ADC_CHIPS];
  uint32_t           lastFrameTimestampMs;
  float              measuredFrequency;

  // 21 Active Logical Channel States
  ChannelDataState     channelStates[METROLOGY_TOTAL_ACTIVE_CH];
  MeasurementFreshness channelFreshness[METROLOGY_TOTAL_ACTIVE_CH];
  float                channelRmsValues[METROLOGY_TOTAL_ACTIVE_CH];
  ChannelCalibRecord   calibrationTable[METROLOGY_TOTAL_ACTIVE_CH];

  // Derived Metrology per Feeder
  float feederActivePower[METROLOGY_NUM_FEEDERS];
  float feederPowerFactor[METROLOGY_NUM_FEEDERS];

  // True-RMS & Instantaneous Power Window Accumulators
  uint16_t sampleCountInWindow;
  double   sumSqVoltage[3];               // R, Y, B
  double   sumSqCurrent[18];              // 6 feeders * 3 phases
  double   sumInstPower[6][3];            // Instantaneous v[p] * i[f,p] products
  uint16_t zeroCrossings;
  int32_t  lastZeroCrossVal;
};

extern PoleMetrology poleMetrology;

#endif // POLE_METROLOGY_H
