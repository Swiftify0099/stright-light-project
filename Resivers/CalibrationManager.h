/*
 * =====================================================================
 *  CALIBRATIONMANAGER.H — 21-CHANNEL VOLTAGE & CURRENT CALIBRATION
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 *  Per-Channel Zero Offset, Gain, Sensitivity, Phase Correction & NVS
 * =====================================================================
 */

#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>

#define NUM_TOTAL_CHANNELS           21
#define NUM_VOLTAGE_CHANNELS         3
#define NUM_CURRENT_CHANNELS         18
#define CALIBRATION_MAGIC            0x43414C49UL // "CALI"
#define CALIBRATION_VERSION          0x0100       // V1.0

// ─────────────────────────────────────────────────────────────
//  INDIVIDUAL CHANNEL CALIBRATION DATA
// ─────────────────────────────────────────────────────────────
struct ChannelCalibData {
  char     channelId[16];        // e.g. "POLE-R-Voltage", "F01-R-Current"
  float    zeroOffset;           // Zero-input ADC offset (Volts or counts)
  float    gainMultiplier;       // Gain calibration factor (nominal 1.000)
  float    sensorSensitivity;    // Transfer multiplier (PT ratio for V, mV/A for I)
  float    phaseCorrectionDeg;   // Phase shift compensation (degrees)
  uint16_t calVersion;           // Calibration revision
  uint32_t calTimestamp;         // Calibration Unix timestamp or uptime
  bool     isValid;              // Verified calibration flag
};

// ─────────────────────────────────────────────────────────────
//  PERSISTENT CALIBRATION TABLE
// ─────────────────────────────────────────────────────────────
struct CalibrationTable {
  uint32_t magic;
  uint16_t tableVersion;
  uint16_t channelCount;
  ChannelCalibData channels[NUM_TOTAL_CHANNELS];
  uint32_t crc32;
};

// ─────────────────────────────────────────────────────────────
//  CALIBRATION MANAGER CLASS
// ─────────────────────────────────────────────────────────────
class CalibrationManager {
public:
  CalibrationManager();

  void init();
  bool loadFromNVS();
  bool saveToNVS();
  bool resetToFactoryDefaults(const String &adminAuthPin);

  // Calibration procedures
  bool performZeroCalibration(uint8_t channelIdx, float rawZeroMeasured);
  bool performSpanCalibration(uint8_t channelIdx, float referenceKnownValue, float rawMeasuredValue);
  bool verifyCalibrationIntegrity() const;

  // Accessors
  const ChannelCalibData* getChannelCalib(uint8_t channelIdx) const;
  float applyCalibration(uint8_t channelIdx, float rawValue) const;

  // Metadata & Helpers
  static const char* getChannelId(uint8_t channelIdx);
  static bool isVoltageChannel(uint8_t channelIdx);
  static bool getFeederAndPhase(uint8_t channelIdx, uint8_t &outFeeder, char &outPhase);
  static uint8_t findChannelIndex(const char* id);

  uint32_t calculateTableCrc(const CalibrationTable &tbl) const;

private:
  CalibrationTable table;
  Preferences      prefs;
  bool             isVerified;

  void loadFactoryCoefficients();
};

extern CalibrationManager calibMgr;

#endif // CALIBRATION_MANAGER_H
