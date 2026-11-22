/*
 * =====================================================================
 *  CALIBRATIONMANAGER.CPP — 21-CHANNEL VOLTAGE & CURRENT CALIBRATION
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#include "CalibrationManager.h"
#include <string.h>

CalibrationManager calibMgr;

static const char* const CHANNEL_NAMES[NUM_TOTAL_CHANNELS] = {
  "POLE-R-Voltage", // 0
  "POLE-Y-Voltage", // 1
  "POLE-B-Voltage", // 2
  "F01-R-Current",  // 3
  "F01-Y-Current",  // 4
  "F01-B-Current",  // 5
  "F02-R-Current",  // 6
  "F02-Y-Current",  // 7
  "F02-B-Current",  // 8
  "F03-R-Current",  // 9
  "F03-Y-Current",  // 10
  "F03-B-Current",  // 11
  "F04-R-Current",  // 12
  "F04-Y-Current",  // 13
  "F04-B-Current",  // 14
  "F05-R-Current",  // 15
  "F05-Y-Current",  // 16
  "F05-B-Current",  // 17
  "F06-R-Current",  // 18
  "F06-Y-Current",  // 19
  "F06-B-Current"   // 20
};

CalibrationManager::CalibrationManager() : isVerified(false) {
  loadFactoryCoefficients();
}

const char* CalibrationManager::getChannelId(uint8_t channelIdx) {
  if (channelIdx < NUM_TOTAL_CHANNELS) {
    return CHANNEL_NAMES[channelIdx];
  }
  return "UNKNOWN";
}

bool CalibrationManager::isVoltageChannel(uint8_t channelIdx) {
  return (channelIdx < NUM_VOLTAGE_CHANNELS);
}

bool CalibrationManager::getFeederAndPhase(uint8_t channelIdx, uint8_t &outFeeder, char &outPhase) {
  if (channelIdx < NUM_VOLTAGE_CHANNELS) {
    outFeeder = 0; // Pole common
    if (channelIdx == 0) outPhase = 'R';
    else if (channelIdx == 1) outPhase = 'Y';
    else outPhase = 'B';
    return true;
  }
  if (channelIdx >= NUM_TOTAL_CHANNELS) return false;

  uint8_t currIdx = channelIdx - NUM_VOLTAGE_CHANNELS; // 0..17
  outFeeder = (currIdx / 3) + 1; // 1..6
  uint8_t phaseIdx = currIdx % 3;
  if (phaseIdx == 0) outPhase = 'R';
  else if (phaseIdx == 1) outPhase = 'Y';
  else outPhase = 'B';
  return true;
}

uint8_t CalibrationManager::findChannelIndex(const char* id) {
  if (!id) return 255;
  for (uint8_t i = 0; i < NUM_TOTAL_CHANNELS; i++) {
    if (strcmp(CHANNEL_NAMES[i], id) == 0) return i;
  }
  return 255;
}

uint32_t CalibrationManager::calculateTableCrc(const CalibrationTable &tbl) const {
  // IEEE 802.3 32-bit CRC over table payload excluding the crc32 field itself
  uint32_t crc = 0xFFFFFFFF;
  const uint8_t* data = (const uint8_t*)&tbl;
  size_t length = sizeof(CalibrationTable) - sizeof(uint32_t);

  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(crc & 1)));
    }
  }
  return ~crc;
}

void CalibrationManager::loadFactoryCoefficients() {
  table.magic = CALIBRATION_MAGIC;
  table.tableVersion = CALIBRATION_VERSION;
  table.channelCount = NUM_TOTAL_CHANNELS;

  for (uint8_t i = 0; i < NUM_TOTAL_CHANNELS; i++) {
    strncpy(table.channels[i].channelId, CHANNEL_NAMES[i], sizeof(table.channels[i].channelId) - 1);
    table.channels[i].channelId[sizeof(table.channels[i].channelId) - 1] = '\0';
    table.channels[i].zeroOffset = 0.0f;
    table.channels[i].gainMultiplier = 1.0f;
    table.channels[i].phaseCorrectionDeg = 0.0f;
    table.channels[i].calVersion = CALIBRATION_VERSION;
    table.channels[i].calTimestamp = 1700000000UL;
    table.channels[i].isValid = true;

    if (isVoltageChannel(i)) {
      // Nominal PT ratio: 230.0V RMS produces 0.707V RMS at ADC differential input
      // Sensitivity = 230.0V / 0.7071V = ~325.27
      table.channels[i].sensorSensitivity = 325.27f;
    } else {
      // Nominal CT ratio: 50A produces 1.0V peak into burden resistor (50.0 A/V)
      table.channels[i].sensorSensitivity = 50.0f;
    }
  }

  table.crc32 = calculateTableCrc(table);
  isVerified = true;
}

void CalibrationManager::init() {
  if (!loadFromNVS()) {
    Serial.println(F("[CALIB] No valid NVS calibration table found. Generating Factory Defaults..."));
    loadFactoryCoefficients();
    saveToNVS();
  } else {
    Serial.println(F("[CALIB] Verified 21-channel calibration parameters loaded from NVS"));
  }
}

bool CalibrationManager::loadFromNVS() {
  if (!prefs.begin("calib", true)) {
    return false;
  }

  size_t len = prefs.getBytes("calTable", &table, sizeof(CalibrationTable));
  prefs.end();

  if (len != sizeof(CalibrationTable)) {
    return false;
  }

  if (table.magic != CALIBRATION_MAGIC || table.channelCount != NUM_TOTAL_CHANNELS) {
    return false;
  }

  uint32_t expectedCrc = calculateTableCrc(table);
  if (table.crc32 != expectedCrc) {
    Serial.printf("[CALIB] CRC mismatch! Stored: 0x%08X, Computed: 0x%08X\n", table.crc32, expectedCrc);
    return false;
  }

  isVerified = verifyCalibrationIntegrity();
  return isVerified;
}

bool CalibrationManager::saveToNVS() {
  table.crc32 = calculateTableCrc(table);

  if (!prefs.begin("calib", false)) {
    return false;
  }

  size_t written = prefs.putBytes("calTable", &table, sizeof(CalibrationTable));
  prefs.end();

  bool success = (written == sizeof(CalibrationTable));
  if (success) {
    Serial.println(F("[CALIB] Calibration table successfully written to NVS"));
  } else {
    Serial.println(F("[CALIB] ERROR writing calibration table to NVS"));
  }
  return success;
}

bool CalibrationManager::resetToFactoryDefaults(const String &adminAuthPin) {
  // Administrative authorization requirement
  if (adminAuthPin != "7550") {
    Serial.println(F("[CALIB] REJECTED: Invalid Admin Authorization PIN for Calibration Reset"));
    return false;
  }

  Serial.println(F("[CALIB] Admin authorized: Resetting calibration to factory defaults"));
  loadFactoryCoefficients();
  return saveToNVS();
}

bool CalibrationManager::performZeroCalibration(uint8_t channelIdx, float rawZeroMeasured) {
  if (channelIdx >= NUM_TOTAL_CHANNELS) return false;

  table.channels[channelIdx].zeroOffset = rawZeroMeasured;
  table.channels[channelIdx].calTimestamp = (uint32_t)(millis() / 1000);
  table.channels[channelIdx].calVersion++;
  table.channels[channelIdx].isValid = true;

  Serial.printf("[CALIB] Zero calibrated for %s: Offset = %.5f\n",
                table.channels[channelIdx].channelId, rawZeroMeasured);

  return saveToNVS();
}

bool CalibrationManager::performSpanCalibration(uint8_t channelIdx, float referenceKnownValue, float rawMeasuredValue) {
  if (channelIdx >= NUM_TOTAL_CHANNELS) return false;
  if (fabs(rawMeasuredValue) < 1e-4f) return false;

  // Compute gain factor: ref / measured
  float newGain = referenceKnownValue / rawMeasuredValue;
  // Plausibility check: Gain must not deviate by more than +/- 50% from nominal 1.0
  if (newGain < 0.5f || newGain > 2.0f) {
    Serial.printf("[CALIB] REJECTED: Span gain %.3f out of safety range [0.5, 2.0]\n", newGain);
    return false;
  }

  table.channels[channelIdx].gainMultiplier = newGain;
  table.channels[channelIdx].calTimestamp = (uint32_t)(millis() / 1000);
  table.channels[channelIdx].calVersion++;
  table.channels[channelIdx].isValid = true;

  Serial.printf("[CALIB] Span calibrated for %s: Ref = %.2f, Meas = %.2f, Gain = %.4f\n",
                table.channels[channelIdx].channelId, referenceKnownValue, rawMeasuredValue, newGain);

  return saveToNVS();
}

bool CalibrationManager::verifyCalibrationIntegrity() const {
  if (table.magic != CALIBRATION_MAGIC) return false;
  for (uint8_t i = 0; i < NUM_TOTAL_CHANNELS; i++) {
    if (table.channels[i].gainMultiplier < 0.2f || table.channels[i].gainMultiplier > 5.0f) {
      return false;
    }
    if (!table.channels[i].isValid) {
      return false;
    }
  }
  return true;
}

const ChannelCalibData* CalibrationManager::getChannelCalib(uint8_t channelIdx) const {
  if (channelIdx < NUM_TOTAL_CHANNELS) {
    return &table.channels[channelIdx];
  }
  return nullptr;
}

float CalibrationManager::applyCalibration(uint8_t channelIdx, float rawValue) const {
  if (channelIdx >= NUM_TOTAL_CHANNELS) return 0.0f;
  const ChannelCalibData &c = table.channels[channelIdx];
  float corrected = (rawValue - c.zeroOffset) * c.gainMultiplier * c.sensorSensitivity;
  return corrected;
}
