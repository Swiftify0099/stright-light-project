/*
 * =====================================================================
 *  CONFIGVALIDATION.CPP — MSCB CENTRAL CONFIGURATION VALIDATION ENGINE
 *  MSCB Smart Pole Automation System — Prompt 20
 * =====================================================================
 */

#include "ConfigValidation.h"
#include <string.h>
#include <math.h>

const char* configValidationResultToString(ConfigValidationResult res) {
  switch (res) {
    case CFG_VAL_OK:                                return "OK";
    case CFG_VAL_ERR_NET_ID_INVALID:                return "ERR_NET_ID_INVALID";
    case CFG_VAL_ERR_HOP_LIMIT_OUT_OF_RANGE:        return "ERR_HOP_LIMIT_OUT_OF_RANGE";
    case CFG_VAL_ERR_DISCOVERY_INTERVAL_INVALID:    return "ERR_DISCOVERY_INTERVAL_INVALID";
    case CFG_VAL_ERR_HEARTBEAT_INTERVAL_INVALID:    return "ERR_HEARTBEAT_INTERVAL_INVALID";
    case CFG_VAL_ERR_RETRY_COUNT_INVALID:           return "ERR_RETRY_COUNT_INVALID";
    case CFG_VAL_ERR_ACK_TIMEOUT_INVALID:           return "ERR_ACK_TIMEOUT_INVALID";
    case CFG_VAL_ERR_DUTY_CYCLE_VIOLATION:          return "ERR_DUTY_CYCLE_VIOLATION";
    case CFG_VAL_ERR_POLE_NAME_EMPTY:               return "ERR_POLE_NAME_EMPTY";
    case CFG_VAL_ERR_FEEDER_COUNT_INVALID:          return "ERR_FEEDER_COUNT_INVALID";
    case CFG_VAL_ERR_FEEDER_ID_OUT_OF_RANGE:        return "ERR_FEEDER_ID_OUT_OF_RANGE";
    case CFG_VAL_ERR_CURRENT_THRESHOLD_OUT_OF_RANGE:return "ERR_CURRENT_THRESHOLD_OUT_OF_RANGE";
    case CFG_VAL_ERR_SCHEDULE_TIME_INVALID:         return "ERR_SCHEDULE_TIME_INVALID";
    case CFG_VAL_ERR_VOLTAGE_THRESHOLD_INVALID:     return "ERR_VOLTAGE_THRESHOLD_INVALID";
    case CFG_VAL_ERR_VOLTAGE_INVERTED:              return "ERR_VOLTAGE_INVERTED";
    case CFG_VAL_ERR_CONFIRM_TIME_INVALID:          return "ERR_CONFIRM_TIME_INVALID";
    case CFG_VAL_ERR_HYSTERESIS_OUT_OF_RANGE:       return "ERR_HYSTERESIS_OUT_OF_RANGE";
    case CFG_VAL_ERR_CALIB_GAIN_OUT_OF_RANGE:       return "ERR_CALIB_GAIN_OUT_OF_RANGE";
    case CFG_VAL_ERR_CALIB_OFFSET_OUT_OF_RANGE:     return "ERR_CALIB_OFFSET_OUT_OF_RANGE";
    case CFG_VAL_ERR_CALIB_PHASE_OUT_OF_RANGE:      return "ERR_CALIB_PHASE_OUT_OF_RANGE";
    case CFG_VAL_ERR_CALIB_SENSITIVITY_INVALID:     return "ERR_CALIB_SENSITIVITY_INVALID";
    case CFG_VAL_ERR_UNAUTHORIZED_CALIB:            return "ERR_UNAUTHORIZED_CALIB";
    case CFG_VAL_ERR_VERSION_DOWNGRADE_FORBIDDEN:   return "ERR_VERSION_DOWNGRADE_FORBIDDEN";
    case CFG_VAL_ERR_GENERIC:                       return "ERR_GENERIC";
    default:                                        return "UNKNOWN_ERROR";
  }
}

float ConfigValidation::calculatePacketAirtimeMs(size_t payloadBytes) {
  // LoRa SX1278 calculation: SF10, BW 125kHz, CR 4/5, Preamble 8 symbols
  // Ts = 2^10 / 125000 = 8.192 ms
  const float tsMs = 8.192f;
  const float tPreambleMs = (8.0f + 4.25f) * tsMs; // 100.352 ms

  // Explicit header, CRC enabled, DE = 0
  int pl = (int)payloadBytes;
  float num = 8.0f * pl - 4.0f * 10.0f + 28.0f + 16.0f;
  float den = 4.0f * 10.0f;
  int ceilBlocks = (int)ceilf(num / den);
  if (ceilBlocks < 0) ceilBlocks = 0;
  int nPayload = 8 + ceilBlocks * 5; // CR = 4/5 means multiplier 5

  return tPreambleMs + (nPayload * tsMs);
}

ConfigValidationResult ConfigValidation::validateNetworkConfig(uint16_t netId, uint8_t hopLimit,
                                                             uint16_t discSec, uint16_t hbSec,
                                                             uint8_t retries, uint16_t ackTimeoutMs) {
  if (netId == 0) return CFG_VAL_ERR_NET_ID_INVALID;
  if (hopLimit < MIN_HOP_LIMIT || hopLimit > MAX_HOP_LIMIT) return CFG_VAL_ERR_HOP_LIMIT_OUT_OF_RANGE;
  if (discSec < MIN_DISCOVERY_INTERVAL_SEC || discSec > MAX_DISCOVERY_INTERVAL_SEC) return CFG_VAL_ERR_DISCOVERY_INTERVAL_INVALID;
  if (hbSec < MIN_HEARTBEAT_INTERVAL_SEC || hbSec > MAX_HEARTBEAT_INTERVAL_SEC) return CFG_VAL_ERR_HEARTBEAT_INTERVAL_INVALID;
  if (retries < MIN_RETRY_COUNT || retries > MAX_RETRY_COUNT) return CFG_VAL_ERR_RETRY_COUNT_INVALID;
  if (ackTimeoutMs < MIN_ACK_TIMEOUT_MS || ackTimeoutMs > MAX_ACK_TIMEOUT_MS) return CFG_VAL_ERR_ACK_TIMEOUT_INVALID;

  return CFG_VAL_OK;
}

ConfigValidationResult ConfigValidation::validateRadioDutyCycle(uint16_t hbSec, uint16_t telemetrySec,
                                                              uint8_t feederCount) {
  // Heartbeat is ~48 bytes; Metrology telemetry is ~180 bytes
  float airtimeHbMs = calculatePacketAirtimeMs(48);
  float airtimeTelemMs = calculatePacketAirtimeMs(180);

  // In 1 hour (3600 seconds = 3,600,000 ms), allowable TX is <= 1% = 36,000 ms
  if (hbSec == 0 || telemetrySec == 0) return CFG_VAL_ERR_DUTY_CYCLE_VIOLATION;

  float hbTransmissionsPerHour = 3600.0f / (float)hbSec;
  float telemTransmissionsPerHour = 3600.0f / (float)telemetrySec;

  float totalAirtimeMsPerHour = (hbTransmissionsPerHour * airtimeHbMs) +
                                (telemTransmissionsPerHour * airtimeTelemMs);

  float dutyCyclePct = (totalAirtimeMsPerHour / 3600000.0f) * 100.0f;

  if (dutyCyclePct > MAX_RADIO_DUTY_CYCLE_PCT) {
    return CFG_VAL_ERR_DUTY_CYCLE_VIOLATION;
  }

  return CFG_VAL_OK;
}

ConfigValidationResult ConfigValidation::validatePoleConfig(const char* name, const char* area,
                                                          uint8_t feederCount, uint16_t offlineTimeoutSec) {
  if (!name || strlen(name) == 0) return CFG_VAL_ERR_POLE_NAME_EMPTY;
  if (feederCount < 1 || feederCount > 6) return CFG_VAL_ERR_FEEDER_COUNT_INVALID;
  if (offlineTimeoutSec < 60 || offlineTimeoutSec > 1800) return CFG_VAL_ERR_HEARTBEAT_INTERVAL_INVALID;
  return CFG_VAL_OK;
}

ConfigValidationResult ConfigValidation::validateFeederConfig(uint8_t feederId, const char* name,
                                                            uint8_t relayChannel, float currentThresholdA,
                                                            bool schedEn, int16_t onMin, int16_t offMin) {
  if (feederId < 1 || feederId > 6) return CFG_VAL_ERR_FEEDER_ID_OUT_OF_RANGE;
  if (relayChannel > 5) return CFG_VAL_ERR_GENERIC;
  if (currentThresholdA < MIN_FEEDER_CURRENT_A || currentThresholdA > MAX_FEEDER_CURRENT_A || isnan(currentThresholdA)) {
    return CFG_VAL_ERR_CURRENT_THRESHOLD_OUT_OF_RANGE;
  }
  if (schedEn) {
    if (onMin < 0 || onMin > 1439 || offMin < 0 || offMin > 1439) {
      return CFG_VAL_ERR_SCHEDULE_TIME_INVALID;
    }
  }
  return CFG_VAL_OK;
}

ConfigValidationResult ConfigValidation::validateFaultConfig(float iMaxA, float vMaxV, float vMinV,
                                                           uint16_t confirmMs, float hystV, float hystA) {
  if (iMaxA < MIN_FEEDER_CURRENT_A || iMaxA > MAX_FEEDER_CURRENT_A || isnan(iMaxA)) {
    return CFG_VAL_ERR_CURRENT_THRESHOLD_OUT_OF_RANGE;
  }
  if (vMaxV - vMinV < MIN_VOLTAGE_SPREAD_V) {
    return CFG_VAL_ERR_VOLTAGE_INVERTED;
  }
  if (vMinV < MIN_UNDER_VOLTAGE_V || vMinV > MAX_UNDER_VOLTAGE_V || isnan(vMinV)) {
    return CFG_VAL_ERR_VOLTAGE_THRESHOLD_INVALID;
  }
  if (vMaxV < MIN_OVER_VOLTAGE_V || vMaxV > MAX_OVER_VOLTAGE_V || isnan(vMaxV)) {
    return CFG_VAL_ERR_VOLTAGE_THRESHOLD_INVALID;
  }
  if (confirmMs < MIN_FAULT_CONFIRM_MS || confirmMs > MAX_FAULT_CONFIRM_MS) {
    return CFG_VAL_ERR_CONFIRM_TIME_INVALID;
  }
  if (hystV < MIN_HYSTERESIS_V || hystV > MAX_HYSTERESIS_V || isnan(hystV)) {
    return CFG_VAL_ERR_HYSTERESIS_OUT_OF_RANGE;
  }
  if (hystA < MIN_HYSTERESIS_A || hystA > MAX_HYSTERESIS_A || isnan(hystA)) {
    return CFG_VAL_ERR_HYSTERESIS_OUT_OF_RANGE;
  }
  return CFG_VAL_OK;
}

ConfigValidationResult ConfigValidation::validateVoltageCalibration(char phase, float offset, float gain,
                                                                  float sensitivity, float phaseDeg) {
  if (phase != 'R' && phase != 'Y' && phase != 'B') return CFG_VAL_ERR_GENERIC;
  if (isnan(offset) || offset < MIN_CALIB_VOLT_OFFSET || offset > MAX_CALIB_VOLT_OFFSET) {
    return CFG_VAL_ERR_CALIB_OFFSET_OUT_OF_RANGE;
  }
  if (isnan(gain) || gain < MIN_CALIB_GAIN || gain > MAX_CALIB_GAIN) {
    return CFG_VAL_ERR_CALIB_GAIN_OUT_OF_RANGE;
  }
  if (isnan(sensitivity) || sensitivity <= 0.001f || sensitivity > 1000.0f) {
    return CFG_VAL_ERR_CALIB_SENSITIVITY_INVALID;
  }
  if (isnan(phaseDeg) || phaseDeg < MIN_CALIB_PHASE_DEG || phaseDeg > MAX_CALIB_PHASE_DEG) {
    return CFG_VAL_ERR_CALIB_PHASE_OUT_OF_RANGE;
  }
  return CFG_VAL_OK;
}

ConfigValidationResult ConfigValidation::validateCurrentCalibration(uint8_t feederId, char phase,
                                                                  float offset, float gain,
                                                                  float sensitivity, float phaseDeg) {
  if (feederId < 1 || feederId > 6) return CFG_VAL_ERR_FEEDER_ID_OUT_OF_RANGE;
  if (phase != 'R' && phase != 'Y' && phase != 'B') return CFG_VAL_ERR_GENERIC;
  if (isnan(offset) || offset < MIN_CALIB_CURR_OFFSET || offset > MAX_CALIB_CURR_OFFSET) {
    return CFG_VAL_ERR_CALIB_OFFSET_OUT_OF_RANGE;
  }
  if (isnan(gain) || gain < MIN_CALIB_GAIN || gain > MAX_CALIB_GAIN) {
    return CFG_VAL_ERR_CALIB_GAIN_OUT_OF_RANGE;
  }
  if (isnan(sensitivity) || sensitivity <= 0.001f || sensitivity > 1000.0f) {
    return CFG_VAL_ERR_CALIB_SENSITIVITY_INVALID;
  }
  if (isnan(phaseDeg) || phaseDeg < MIN_CALIB_PHASE_DEG || phaseDeg > MAX_CALIB_PHASE_DEG) {
    return CFG_VAL_ERR_CALIB_PHASE_OUT_OF_RANGE;
  }
  return CFG_VAL_OK;
}
