/**
 * @file PoleMetrology.h
 * @brief MSCB Smart Pole Node - 21-Channel Metrology & 3x ADS131M08 Driver
 * @version 1.1.0
 * 
 * AUTHORITATIVE METROLOGY & ADC SAFETY ARCHITECTURE:
 * 1. 21 ACTIVE CHANNELS + 3 BALANCED SPARES (24 TOTAL RAW CHANNELS):
 *    - 3x Texas Instruments ADS131M08 (24-bit simultaneous sampling delta-sigma)
 *    - Device 0 (ADC #1, CS /1Y0): Feeders 1..3 Currents (Ch 04..11)
 *    - Device 1 (ADC #2, CS /1Y1): Feeders 3..5 Currents (Ch 12..18) + Spare 1
 *    - Device 2 (ADC #3, CS /1Y2): Feeder 6 Currents (Ch 19..21) + Bus Voltages R,Y,B (Ch 01..03) + Spares 2,3
 * 
 * 2. ZERO-SIMULATION & HARDWARE DETECTION MANDATE:
 *    - Never switch to simulated readings when ADC hardware is missing.
 *    - Hardware presence verified via SPI register readback on all 3 chips.
 *    - If ANY ADC chip is missing, begin() fails and channels report ADC_NOT_CONNECTED.
 * 
 * 3. ZERO-FRAME & STALE FRAME REJECTION:
 *    - All-zero (dead bus / disconnected MISO) or all-one frames are rejected.
 *    - Sampling timeouts enforce CHANNEL_DATA_STALE freshness transitions.
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
#elif __has_include(<StorageTypes.h>)
  #include <StorageTypes.h>
#endif

#define METROLOGY_TOTAL_RAW_CH       24
#define METROLOGY_TOTAL_ACTIVE_CH    21
#define METROLOGY_NUM_ADC_CHIPS      3
#define METROLOGY_CHANNELS_PER_ADC   8
#define METROLOGY_NUM_FEEDERS        6
#define METROLOGY_NUM_PHASES         3

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

// Authoritative mapping table (24 raw channels)
extern const RawAdcChannelMap AUTHORITATIVE_ADC_MAP[METROLOGY_TOTAL_RAW_CH];

class PoleMetrology {
public:
  PoleMetrology();

  // Initialization & Hardware Verification
  bool begin();
  bool detectHardware();
  bool areAllAdcsDetected() const { return allAdcsDetected; }
  bool isAdcChipDetected(uint8_t chipIdx) const;

  // Raw Sample Processing & Frame Validation
  bool processRawAdcFrame(const int32_t rawSamples[METROLOGY_TOTAL_RAW_CH], uint32_t timestampMs);
  void service(); // Timeouts & Freshness watchdog

  // Channel Status & Measurements
  ChannelDataState getChannelState(uint8_t logicalChId1Based) const;
  MeasurementFreshness getChannelFreshness(uint8_t logicalChId1Based) const;
  float getChannelValue(uint8_t logicalChId1Based) const;

  // Voltage Readings (Logical Ch 1..3)
  float getBusVoltageR() const;
  float getBusVoltageY() const;
  float getBusVoltageB() const;
  float getGridFrequency() const { return nominalFrequency; }

  // Feeder Current & Power (Logical Ch 4..21)
  float getFeederCurrent(uint8_t feederIdx0Based, uint8_t phaseIdx0Based) const;
  float getFeederActivePower(uint8_t feederIdx0Based) const;
  float getFeederPowerFactor(uint8_t feederIdx0Based) const;

  // Calibration API (Per-channel independent coefficients)
  bool setChannelCalibration(uint8_t logicalChId1Based, float zeroOffset, float gainMultiplier, float sensitivity, float phaseCorrectionDeg);
  const ChannelCalibRecord& getChannelCalibration(uint8_t logicalChId1Based) const;
  void resetCalibrationToDefaults();

  // Accuracy Certification Status
  bool isClass1_0Certified() const { return false; } // Strictly FALSE without Fluke 6100A laboratory bench report

  // Diagnostic Report
  void printMetrologyAuditReport() const;

private:
  bool checkZeroFrame(const int32_t rawSamples[METROLOGY_TOTAL_RAW_CH]) const;
  int findRawIndexForLogicalCh(uint8_t logicalChId1Based) const;

  bool               allAdcsDetected;
  bool               chipDetected[METROLOGY_NUM_ADC_CHIPS];
  uint32_t           lastFrameTimestampMs;
  float              nominalFrequency;

  // 21 Active Logical Channel States
  ChannelDataState     channelStates[METROLOGY_TOTAL_ACTIVE_CH];
  MeasurementFreshness channelFreshness[METROLOGY_TOTAL_ACTIVE_CH];
  float                channelRmsValues[METROLOGY_TOTAL_ACTIVE_CH];
  ChannelCalibRecord   calibrationTable[METROLOGY_TOTAL_ACTIVE_CH];

  // Derived Metrology
  float feederActivePower[METROLOGY_NUM_FEEDERS];
  float feederPowerFactor[METROLOGY_NUM_FEEDERS];
};

extern PoleMetrology poleMetrology;

#endif // POLE_METROLOGY_H
