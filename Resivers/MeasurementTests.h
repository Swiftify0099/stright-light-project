/*
 * =====================================================================
 *  MEASUREMENTTESTS.H — 17-STAGE RIGOROUS METROLOGY VALIDATION SUITE
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#ifndef MEASUREMENT_TESTS_H
#define MEASUREMENT_TESTS_H

#include <Arduino.h>

class MeasurementTests {
public:
  static bool runAllTests();

  // 17 Individual Test Cases
  static bool test01_AdcInitialization();
  static bool test02_ThreeAdcSynchronization();
  static bool test03_RybVoltageSampling();
  static bool test04_F01_RybCurrent();
  static bool test05_AllSixFeederChannels();
  static bool test06_IndependentCurrentCalibration();
  static bool test07_ZeroCurrentMeasurement();
  static bool test08_KnownCurrentInput();
  static bool test09_SensorDisconnect();
  static bool test10_AdcDisconnect();
  static bool test11_InputSaturation();
  static bool test12_MeasurementNoise();
  static bool test13_FrequencyVariation();
  static bool test14_CalibrationPersistence();
  static bool test15_PacketSerialization();
  static bool test16_TelemetryFragmentation();
  static bool test17_MeasurementRecoveryAfterRestart();
};

#endif // MEASUREMENT_TESTS_H
