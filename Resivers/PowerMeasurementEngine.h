/*
 * =====================================================================
 *  POWERMEASUREMENTENGINE.H — 21-CHANNEL INDUSTRIAL METROLOGY ENGINE
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 *  Three-Phase Voltage (R/Y/B) + 18-Channel Current (6 Feeders x 3 Phases)
 *  True RMS, Frequency Tracking, Active Power, Power Factor, Energy & Faults
 * =====================================================================
 */

#ifndef POWER_MEASUREMENT_ENGINE_H
#define POWER_MEASUREMENT_ENGINE_H

#include <Arduino.h>
#include <stdint.h>
#include "ADS131M08Driver.h"
#include "CalibrationManager.h"

// ─────────────────────────────────────────────────────────────
//  METROLOGY SAMPLING & BUFFER CONFIGURATION
// ─────────────────────────────────────────────────────────────
#define METROLOGY_SAMPLE_RATE_HZ     1000UL     // 1000 Samples/sec (1 ms period)
#define METROLOGY_CYCLES_PER_WINDOW  10         // 10 AC cycles at 50Hz = 200ms IEC 61000-4-30 Class S
#define METROLOGY_BUFFER_SIZE        250        // Ring buffer depth (up to 250ms for low frequency 40Hz)
#define MAX_SYSTEM_FEEDERS           6          // 6 Feeders per Pole

// Grid Frequency boundaries (Indian Electricity Grid Code / IEGC)
#define NOMINAL_GRID_FREQ_HZ         50.0f
#define MIN_PLAUSIBLE_FREQ_HZ        45.0f
#define MAX_PLAUSIBLE_FREQ_HZ        55.0f
#define VOLTAGE_NO_SIGNAL_THRESH     20.0f      // Below 20V RMS considered dead mains
#define VOLTAGE_UNDERVOLT_THRESH     180.0f     // Standard Indian grid undervoltage
#define VOLTAGE_OVERVOLT_THRESH      260.0f     // Standard Indian grid overvoltage
#define CURRENT_NOISE_FLOOR_A        0.040f     // 40mA noise floor gating
#define DEFAULT_OVERCURRENT_THRESH_A 25.0f      // Default 25A trip threshold
#define IMBALANCE_ALERT_PERCENT      30.0f      // 30% phase current imbalance threshold

// ─────────────────────────────────────────────────────────────
//  MEASUREMENT VALIDITY ENUMERATION
// ─────────────────────────────────────────────────────────────
enum MeasurementValidity : uint8_t {
  MEAS_VALID               = 0, // Normal verified reading
  MEAS_ZERO_CURRENT        = 1, // Below noise floor / load off
  MEAS_NO_SIGNAL           = 2, // Mains dead / no AC waveform
  MEAS_UNDER_VOLTAGE       = 3, // Voltage below allowable boundary
  MEAS_OVER_VOLTAGE        = 4, // Voltage above allowable boundary
  MEAS_OVER_CURRENT        = 5, // Exceeds safe feeder current threshold
  MEAS_CURRENT_IMBALANCE   = 6, // Unbalanced 3-phase load
  MEAS_PHASE_LOSS          = 7, // Missing phase on 3-phase mains
  MEAS_SENSOR_DISCONNECTED = 8, // Open CT / open input / zero variance
  MEAS_SATURATED           = 9, // ADC hitting +/- FS digital rail
  MEAS_ADC_OFFLINE         = 10,// SPI comm failure / missing DRDY
  MEAS_CAL_INVALID         = 11,// Corrupted or uninitialized calibration
  MEAS_UNAVAILABLE         = 12 // Sync or required waveform missing
};

// ─────────────────────────────────────────────────────────────
//  INDIVIDUAL CHANNEL MEASUREMENT RECORD
// ─────────────────────────────────────────────────────────────
struct ChannelMeasurementRecord {
  char                poleId[16];        // e.g. "MSCB-P001"
  char                channelId[20];     // e.g. "F01-R-Current", "POLE-R-Voltage"
  uint8_t             feederIndex;       // 0=Pole common, 1..6=Feeders
  char                phase;             // 'R', 'Y', 'B'
  float               rmsValue;          // Volts RMS or Amperes RMS
  float               dcOffset;          // Calculated DC bias in engineering units
  float               peakValue;         // Peak amplitude observed
  MeasurementValidity validity;          // Quality enum
  uint16_t            calVersion;        // Calibration version applied
  uint32_t            timestamp;         // Measurement timestamp (uptime seconds)
};

// ─────────────────────────────────────────────────────────────
//  THREE-PHASE COMMON VOLTAGE SYSTEM STATE
// ─────────────────────────────────────────────────────────────
struct CommonVoltageState {
  float               vR_rms;
  float               vY_rms;
  float               vB_rms;
  float               gridFrequencyHz;
  MeasurementValidity stateR;
  MeasurementValidity stateY;
  MeasurementValidity stateB;
  bool                threePhaseSymmetric;
  bool                phaseLossR;
  bool                phaseLossY;
  bool                phaseLossB;
  uint32_t            timestamp;
};

// ─────────────────────────────────────────────────────────────
//  PER-FEEDER THREE-PHASE POWER & ENERGY STATE
// ─────────────────────────────────────────────────────────────
struct FeederPowerState {
  uint8_t             feederId;          // 1 to 6
  // Currents
  float               iR_rms;
  float               iY_rms;
  float               iB_rms;
  MeasurementValidity stateR;
  MeasurementValidity stateY;
  MeasurementValidity stateB;
  // Powers per phase (Watts)
  float               pR_active;
  float               pY_active;
  float               pB_active;
  // Apparent powers (VA)
  float               sR_apparent;
  float               sY_apparent;
  float               sB_apparent;
  // Power factor
  float               pfR;
  float               pfY;
  float               pfB;
  // Feeder Totals
  float               pTotal_active;     // PR + PY + PB
  float               sTotal_apparent;   // SR + SY + SB
  float               pfTotal;           // P_total / S_total
  float               qTotal_reactive;   // VARs
  float               accumEnergyWh;     // Accumulated Watt-hours
  // Faults & Imbalance
  float               currentImbalancePct;
  bool                isOverCurrent;
  bool                isImbalanceFault;
  bool                isSensorFault;
  uint32_t            timestamp;
};

// ─────────────────────────────────────────────────────────────
//  MASTER METROLOGY ENGINE CLASS
// ─────────────────────────────────────────────────────────────
class PowerMeasurementEngine {
public:
  PowerMeasurementEngine();

  void init(const char* poleId = "MSCB-P001");
  void reset();

  // Fast sample insertion (called from timer or DRDY at 1000 Hz)
  void processRawSample(const AdcRawSampleFrame &frame);

  // Background computation (called periodically, e.g. every 200-500ms)
  bool computeMetrologyWindow();

  // Data Getters
  const CommonVoltageState& getVoltageState() const;
  const FeederPowerState* getFeederState(uint8_t feederIndex) const; // 1 to 6
  const ChannelMeasurementRecord* getChannelRecord(uint8_t channelIdx) const; // 0 to 20

  // Quick helper queries
  float getCommonVoltageR() const;
  float getCommonVoltageY() const;
  float getCommonVoltageB() const;
  float getFeederCurrent(uint8_t feederId, char phase) const;
  float getFeederTotalPower(uint8_t feederId) const;
  float getFeederEnergyWh(uint8_t feederId) const;
  float getGridFrequency() const;

  const char* getPoleId() const;
  void setPoleId(const char* id);

  // System Quality & Faults
  bool hasActiveFault() const;
  String getSystemFaultSummary() const;
  static const char* getValidityString(MeasurementValidity val);

private:
  char poleId[16];
  CommonVoltageState voltageState;
  FeederPowerState   feederStates[MAX_SYSTEM_FEEDERS];
  ChannelMeasurementRecord channelRecords[NUM_TOTAL_CHANNELS];

  // Bounded circular sample ring buffer for 21 channels
  float    sampleRingBuffers[NUM_TOTAL_CHANNELS][METROLOGY_BUFFER_SIZE];
  uint32_t sampleTimeBuffer[METROLOGY_BUFFER_SIZE];
  uint16_t ringHead;
  uint16_t ringCount;
  uint32_t totalSamplesProcessed;

  // Zero-crossing & frequency tracking
  float    lastZeroCrossingTimeUs;
  float    measuredGridFreq;
  bool     freqLocked;
  uint16_t windowSampleCount;

  // Timestamps
  uint32_t lastComputeTimestamp;
  uint32_t lastEnergyCalcMs;

  // Internal math primitives
  float calculateZeroCrossingFreq(uint16_t count);
  void computeVoltageChannel(uint8_t chIdx, uint16_t sampleCount, float &outRms, float &outDc, MeasurementValidity &outVal);
  void computeCurrentChannel(uint8_t chIdx, uint16_t sampleCount, float &outRms, float &outDc, MeasurementValidity &outVal);
  void computePowerAndEnergy(uint8_t feederIdx, uint16_t sampleCount);
  void evaluateSystemFaults();
};

extern PowerMeasurementEngine metrologyEngine;

#endif // POWER_MEASUREMENT_ENGINE_H
