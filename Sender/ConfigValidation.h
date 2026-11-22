/*
 * =====================================================================
 *  CONFIGVALIDATION.H — MSCB CENTRAL CONFIGURATION VALIDATION ENGINE
 *  MSCB Smart Pole Automation System — Prompt 20
 *  Enforces Electrical Limits, Radio Duty-Cycle Rules & Range Bounds
 * =====================================================================
 */

#ifndef CONFIG_VALIDATION_H
#define CONFIG_VALIDATION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
  #include <Arduino.h>
#else
  #include "Arduino.h"
#endif

// ─────────────────────────────────────────────────────────────
//  VALIDATION ERROR CODES
// ─────────────────────────────────────────────────────────────
enum ConfigValidationResult {
  CFG_VAL_OK = 0,
  CFG_VAL_ERR_NET_ID_INVALID,
  CFG_VAL_ERR_HOP_LIMIT_OUT_OF_RANGE,
  CFG_VAL_ERR_DISCOVERY_INTERVAL_INVALID,
  CFG_VAL_ERR_HEARTBEAT_INTERVAL_INVALID,
  CFG_VAL_ERR_RETRY_COUNT_INVALID,
  CFG_VAL_ERR_ACK_TIMEOUT_INVALID,
  CFG_VAL_ERR_DUTY_CYCLE_VIOLATION,
  CFG_VAL_ERR_POLE_NAME_EMPTY,
  CFG_VAL_ERR_FEEDER_COUNT_INVALID,
  CFG_VAL_ERR_FEEDER_ID_OUT_OF_RANGE,
  CFG_VAL_ERR_CURRENT_THRESHOLD_OUT_OF_RANGE,
  CFG_VAL_ERR_SCHEDULE_TIME_INVALID,
  CFG_VAL_ERR_VOLTAGE_THRESHOLD_INVALID,
  CFG_VAL_ERR_VOLTAGE_INVERTED,
  CFG_VAL_ERR_CONFIRM_TIME_INVALID,
  CFG_VAL_ERR_HYSTERESIS_OUT_OF_RANGE,
  CFG_VAL_ERR_CALIB_GAIN_OUT_OF_RANGE,
  CFG_VAL_ERR_CALIB_OFFSET_OUT_OF_RANGE,
  CFG_VAL_ERR_CALIB_PHASE_OUT_OF_RANGE,
  CFG_VAL_ERR_CALIB_SENSITIVITY_INVALID,
  CFG_VAL_ERR_UNAUTHORIZED_CALIB,
  CFG_VAL_ERR_VERSION_DOWNGRADE_FORBIDDEN,
  CFG_VAL_ERR_GENERIC
};

const char* configValidationResultToString(ConfigValidationResult res);

// ─────────────────────────────────────────────────────────────
//  VALIDATION RULE THRESHOLDS (ELECTRICAL & RADIO STANDARDS)
// ─────────────────────────────────────────────────────────────
#define MIN_HOP_LIMIT                1
#define MAX_HOP_LIMIT                10
#define MIN_DISCOVERY_INTERVAL_SEC   10
#define MAX_DISCOVERY_INTERVAL_SEC   300
#define MIN_HEARTBEAT_INTERVAL_SEC   15
#define MAX_HEARTBEAT_INTERVAL_SEC   300
#define MIN_RETRY_COUNT              1
#define MAX_RETRY_COUNT              5
#define MIN_ACK_TIMEOUT_MS           500
#define MAX_ACK_TIMEOUT_MS           10000

#define MIN_FEEDER_CURRENT_A         0.5f
#define MAX_FEEDER_CURRENT_A         100.0f  // Industrial MCB breaker physical limit

#define MIN_UNDER_VOLTAGE_V          150.0f
#define MAX_UNDER_VOLTAGE_V          210.0f
#define MIN_OVER_VOLTAGE_V           240.0f
#define MAX_OVER_VOLTAGE_V           300.0f
#define MIN_VOLTAGE_SPREAD_V         20.0f   // overV must exceed underV by >= 20V

#define MIN_FAULT_CONFIRM_MS         20
#define MAX_FAULT_CONFIRM_MS         5000
#define MIN_HYSTERESIS_V             1.0f
#define MAX_HYSTERESIS_V             20.0f
#define MIN_HYSTERESIS_A             0.1f
#define MAX_HYSTERESIS_A             5.0f

#define MIN_CALIB_GAIN               0.800f  // ±20% maximum hardware calibration gain
#define MAX_CALIB_GAIN               1.200f
#define MIN_CALIB_VOLT_OFFSET        -15.0f  // ±15V offset
#define MAX_CALIB_VOLT_OFFSET        15.0f
#define MIN_CALIB_CURR_OFFSET        -5.0f   // ±5A offset
#define MAX_CALIB_CURR_OFFSET        5.0f
#define MIN_CALIB_PHASE_DEG          -15.0f  // ±15 deg phase correction
#define MAX_CALIB_PHASE_DEG          15.0f

#define MAX_RADIO_DUTY_CYCLE_PCT     1.0f    // 1% ETSI / Regulatory airtime limit

// ─────────────────────────────────────────────────────────────
//  VALIDATION CLASS DECLARATION
// ─────────────────────────────────────────────────────────────
class ConfigValidation {
public:
  static ConfigValidationResult validateNetworkConfig(uint16_t netId, uint8_t hopLimit,
                                                     uint16_t discSec, uint16_t hbSec,
                                                     uint8_t retries, uint16_t ackTimeoutMs);

  static ConfigValidationResult validateRadioDutyCycle(uint16_t hbSec, uint16_t telemetrySec,
                                                      uint8_t feederCount);

  static ConfigValidationResult validatePoleConfig(const char* name, const char* area,
                                                  uint8_t feederCount, uint16_t offlineTimeoutSec);

  static ConfigValidationResult validateFeederConfig(uint8_t feederId, const char* name,
                                                    uint8_t relayChannel, float currentThresholdA,
                                                    bool schedEn, int16_t onMin, int16_t offMin);

  static ConfigValidationResult validateFaultConfig(float iMaxA, float vMaxV, float vMinV,
                                                   uint16_t confirmMs, float hystV, float hystA);

  static ConfigValidationResult validateVoltageCalibration(char phase, float offset, float gain,
                                                          float sensitivity, float phaseDeg);

  static ConfigValidationResult validateCurrentCalibration(uint8_t feederId, char phase,
                                                          float offset, float gain,
                                                          float sensitivity, float phaseDeg);

  static float calculatePacketAirtimeMs(size_t payloadBytes);
};

#endif // CONFIG_VALIDATION_H
