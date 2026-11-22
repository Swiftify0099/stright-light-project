/*
 * =====================================================================
 *  MULTIADCMANAGER.H — MULTI-ADC SYNCHRONIZATION & 21-CHANNEL
 *  THREE-PHASE POWER METROLOGY CONTROLLER (ESP32)
 *
 *  Manages 3x TI ADS131M08 (24 channels total, 21 active channels):
 *  - 18 Independent Feeder Current Channels (Feeders 1-6 x Phases R, Y, B)
 *  - 3 Common Bus Voltage Channels (Phases R, Y, B)
 *  - 3 Uncommitted Spare Channels
 * =====================================================================
 */

#ifndef MULTI_ADC_MANAGER_H
#define MULTI_ADC_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "ADS131M08.h"

#define NUM_ADCS             3
#define CHANNELS_PER_ADC     8
#define TOTAL_ADC_CHANNELS   24
#define ACTIVE_MEAS_CHANNELS 21
#define NUM_FEEDERS          6
#define PHASES_PER_FEEDER    3 // 0: R Phase, 1: Y Phase, 2: B Phase

// Metrology Data Structure for a Single Phase Current Channel
struct PhaseCurrentData {
  float   irms;            // True-RMS Current (Amperes)
  float   ipeak;           // Peak Current (Amperes)
  float   activePowerW;    // Active Power (Watts)
  float   powerFactor;     // Power Factor (-1.0 to 1.0)
  bool    overCurrent;     // Overcurrent flag
  bool    sensorFault;     // Disconnected / open-circuit flag
  String  status;          // "OK", "OVER_CURR", "DISCONNECTED"
};

// Metrology Data Structure for a Feeder (All 3 Phases)
struct FeederMeasurement {
  PhaseCurrentData phases[PHASES_PER_FEEDER]; // 0: R, 1: Y, 2: B
  float            totalActivePowerW;
  float            totalEnergyWh;
  bool             tripped;
  String           compositeFault;
};

// Metrology Data Structure for Common 3-Phase Bus Voltage
struct BusVoltageMeasurement {
  float  vrmsR;            // Phase R RMS Voltage (VAC)
  float  vrmsY;            // Phase Y RMS Voltage (VAC)
  float  vrmsB;            // Phase B RMS Voltage (VAC)
  float  frequencyHz;      // Grid Frequency (Hz)
  String phaseSequence;    // "RYB_NORMAL", "RBY_REVERSED", "PHASE_LOST"
  bool   validR;
  bool   validY;
  bool   validB;
  String statusR;
  String statusY;
  String statusB;
};

class MultiADCManager {
public:
  MultiADCManager();

  // Hardware Initialization & Multi-ADC Synchronization
  bool init();
  void synchronizeADCs();
  bool checkHealth();

  // Periodic Measurement Acquisition & DSP Metrology
  void update();

  // Metrology Query API
  float getFeederCurrent(uint8_t feederIdx, uint8_t phaseIdx) const;
  float getFeederPower(uint8_t feederIdx) const;
  float getFeederEnergyWh(uint8_t feederIdx) const;
  String getFeederFault(uint8_t feederIdx) const;

  float getVoltageR() const;
  float getVoltageY() const;
  float getVoltageB() const;
  float getGridFrequency() const;
  String getPhaseSequence() const;

  String getVoltageStateR() const;
  String getVoltageStateY() const;
  String getVoltageStateB() const;

  // Diagnostic & Raw Access
  bool isAdcHealthy(uint8_t adcIdx) const;
  float getRawChannelVoltage(uint8_t channelIdx) const;

private:
  ADS131M08 adcs[NUM_ADCS];
  bool      adcsHealthy[NUM_ADCS];
  bool      isSynchronized;

  // Measurement State
  FeederMeasurement     feederData[NUM_FEEDERS];
  BusVoltageMeasurement busVoltage;

  // Running Accumulators for RMS & Power DSP
  uint32_t sampleCount;
  uint64_t sumIsq[NUM_FEEDERS][PHASES_PER_FEEDER];
  uint64_t sumVsq[PHASES_PER_FEEDER];
  int64_t  sumPowerInst[NUM_FEEDERS][PHASES_PER_FEEDER];
  unsigned long lastEnergyCalcMs;
  unsigned long lastCycleTimeMs;

  // Hardware Pins
  uint8_t pinCS[NUM_ADCS];
  uint8_t pinSYNC;
  uint8_t pinRESET;
  uint8_t pinDRDY;

  // Internal Metrology Helpers
  void processSampleFrame(const float adc1V[8], const float adc2V[8], const float adc3V[8]);
  void computeCycleRMS();
  void mapChannelToPhysical(uint8_t globalCh, float valV);
};

extern MultiADCManager multiAdcMgr;

#endif // MULTI_ADC_MANAGER_H
