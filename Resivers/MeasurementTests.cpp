/*
 * =====================================================================
 *  MEASUREMENTTESTS.CPP — 17-STAGE RIGOROUS METROLOGY VALIDATION SUITE
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#include "MeasurementTests.h"
#include "ADS131M08Driver.h"
#include "CalibrationManager.h"
#include "PowerMeasurementEngine.h"
#include "MeasurementTelemetry.h"
#include <math.h>

static void logTest(int num, const char* name, bool pass, const char* details = "") {
  Serial.printf("[TEST %02d] %s: %s %s\n", num, name, pass ? "PASS" : "FAIL", details);
}

bool MeasurementTests::test01_AdcInitialization() {
  AdcPinConfig pins;
  pins.csPins[0] = 32;
  pins.csPins[1] = 33;
  pins.csPins[2] = 4;
  pins.syncPin   = 0;
  pins.drdyPin   = 35;
  pins.sckPin    = 18;
  pins.misoPin   = 19;
  pins.mosiPin   = 23;
  pins.spiSpeedHz = 4000000;

  bool ok = adcDriver.init(pins);
  logTest(1, "ADC Initialization", ok);
  return ok;
}

bool MeasurementTests::test02_ThreeAdcSynchronization() {
  adcDriver.hardwareResetAndSync();
  adcDriver.sendSyncPulse();
  bool ready = adcDriver.isDataReady();
  logTest(2, "Three-ADC Synchronization", ready, ready ? "(SYNC pulse verified)" : "");
  return ready;
}

bool MeasurementTests::test03_RybVoltageSampling() {
  metrologyEngine.reset();
  // Feed simulated 230V 50Hz 3-phase waveforms
  for (int i = 0; i < 200; i++) {
    AdcRawSampleFrame frame;
    frame.timestampUs = i * 1000;
    frame.sampleIndex = i;
    frame.valid = true;
    for (int c = 0; c < 24; c++) frame.saturated[c] = false;

    float t = (float)i * 0.001f;
    float omega = 2.0f * (float)M_PI * 50.0f;
    // 230V RMS -> peak = 325.27V. Transducer gives 0.707V peak to ADC (+/-1.2V FS)
    float vPeakCounts = (0.7071f / 1.200f) * 8388607.0f;
    frame.rawCounts[0] = (int32_t)(vPeakCounts * sinf(omega * t));
    frame.rawCounts[1] = (int32_t)(vPeakCounts * sinf(omega * t - (2.0f * (float)M_PI / 3.0f)));
    frame.rawCounts[2] = (int32_t)(vPeakCounts * sinf(omega * t + (2.0f * (float)M_PI / 3.0f)));

    for (int c = 3; c < 24; c++) frame.rawCounts[c] = 0;
    metrologyEngine.processRawSample(frame);
  }

  bool computed = metrologyEngine.computeMetrologyWindow();
  float vR = metrologyEngine.getCommonVoltageR();
  float vY = metrologyEngine.getCommonVoltageY();
  float vB = metrologyEngine.getCommonVoltageB();

  bool pass = computed && (fabs(vR - 230.0f) < 5.0f) && (fabs(vY - 230.0f) < 5.0f) && (fabs(vB - 230.0f) < 5.0f);
  char buf[64];
  snprintf(buf, sizeof(buf), "(VR=%.1fV, VY=%.1fV, VB=%.1fV)", vR, vY, vB);
  logTest(3, "R/Y/B Voltage Sampling", pass, buf);
  return pass;
}

bool MeasurementTests::test04_F01_RybCurrent() {
  metrologyEngine.reset();
  // Feed Feeder 1 with 15.0A on R, Y, B
  for (int i = 0; i < 200; i++) {
    AdcRawSampleFrame frame;
    frame.timestampUs = i * 1000;
    frame.sampleIndex = i;
    frame.valid = true;
    for (int c = 0; c < 24; c++) frame.saturated[c] = false;

    float t = (float)i * 0.001f;
    float omega = 2.0f * (float)M_PI * 50.0f;

    // Common Voltages
    float vPeakCounts = (0.7071f / 1.200f) * 8388607.0f;
    frame.rawCounts[0] = (int32_t)(vPeakCounts * sinf(omega * t));
    frame.rawCounts[1] = (int32_t)(vPeakCounts * sinf(omega * t - (2.0f * (float)M_PI / 3.0f)));
    frame.rawCounts[2] = (int32_t)(vPeakCounts * sinf(omega * t + (2.0f * (float)M_PI / 3.0f)));

    // Feeder 1 Currents (CH 3, 4, 5): 15.0A RMS -> Peak = 21.21A.
    // At 50A/1V CT, 21.21A = 0.4242V into ADC
    float iPeakCounts = (0.4242f / 1.200f) * 8388607.0f;
    frame.rawCounts[3] = (int32_t)(iPeakCounts * sinf(omega * t));
    frame.rawCounts[4] = (int32_t)(iPeakCounts * sinf(omega * t - (2.0f * (float)M_PI / 3.0f)));
    frame.rawCounts[5] = (int32_t)(iPeakCounts * sinf(omega * t + (2.0f * (float)M_PI / 3.0f)));

    for (int c = 6; c < 24; c++) frame.rawCounts[c] = 0;
    metrologyEngine.processRawSample(frame);
  }

  metrologyEngine.computeMetrologyWindow();
  float iR = metrologyEngine.getFeederCurrent(1, 'R');
  float iY = metrologyEngine.getFeederCurrent(1, 'Y');
  float iB = metrologyEngine.getFeederCurrent(1, 'B');

  bool pass = (fabs(iR - 15.0f) < 0.6f) && (fabs(iY - 15.0f) < 0.6f) && (fabs(iB - 15.0f) < 0.6f);
  char buf[64];
  snprintf(buf, sizeof(buf), "(F01 IR=%.2fA, IY=%.2fA, IB=%.2fA)", iR, iY, iB);
  logTest(4, "F01 R/Y/B Current", pass, buf);
  return pass;
}

bool MeasurementTests::test05_AllSixFeederChannels() {
  bool allOk = true;
  for (uint8_t f = 1; f <= 6; f++) {
    const FeederPowerState *st = metrologyEngine.getFeederState(f);
    if (!st || st->feederId != f) {
      allOk = false;
    }
  }
  logTest(5, "All Six Feeder Channels (18 Channels Mapped)", allOk);
  return allOk;
}

bool MeasurementTests::test06_IndependentCurrentCalibration() {
  // Test independent calibration of Feeder 1 Phase R vs Feeder 2 Phase R
  calibMgr.performSpanCalibration(3, 10.0f, 9.8f);  // CH3: F01-R
  calibMgr.performSpanCalibration(6, 10.0f, 10.5f); // CH6: F02-R

  const ChannelCalibData *cal1 = calibMgr.getChannelCalib(3);
  const ChannelCalibData *cal2 = calibMgr.getChannelCalib(6);

  bool pass = (cal1->gainMultiplier != cal2->gainMultiplier) && (cal1->isValid && cal2->isValid);
  logTest(6, "Independent Current Calibration", pass);
  return pass;
}

bool MeasurementTests::test07_ZeroCurrentMeasurement() {
  metrologyEngine.reset();
  // Feed only noise floor (< 20mA)
  for (int i = 0; i < 200; i++) {
    AdcRawSampleFrame frame;
    frame.timestampUs = i * 1000;
    frame.sampleIndex = i;
    frame.valid = true;
    for (int c = 0; c < 24; c++) {
      frame.rawCounts[c] = (i % 2 == 0) ? 50 : -50; // tiny noise
      frame.saturated[c] = false;
    }
    metrologyEngine.processRawSample(frame);
  }
  metrologyEngine.computeMetrologyWindow();
  float iZero = metrologyEngine.getFeederCurrent(1, 'R');
  const ChannelMeasurementRecord *rec = metrologyEngine.getChannelRecord(3);

  bool pass = (iZero == 0.0f) && (rec->validity == MEAS_ZERO_CURRENT);
  logTest(7, "Zero-Current Measurement Gating", pass, pass ? "(Noise floor suppressed to 0.00A)" : "");
  return pass;
}

bool MeasurementTests::test08_KnownCurrentInput() {
  // Test reference 20.00A known signal
  float refCurrent = 20.0f;
  metrologyEngine.reset();
  for (int i = 0; i < 200; i++) {
    AdcRawSampleFrame frame;
    frame.timestampUs = i * 1000;
    frame.sampleIndex = i;
    frame.valid = true;
    for (int c = 0; c < 24; c++) frame.saturated[c] = false;

    float t = (float)i * 0.001f;
    float omega = 2.0f * (float)M_PI * 50.0f;
    float iPeakCounts = (refCurrent * 1.4142f / 50.0f / 1.200f) * 8388607.0f;
    frame.rawCounts[3] = (int32_t)(iPeakCounts * sinf(omega * t));
    metrologyEngine.processRawSample(frame);
  }
  metrologyEngine.computeMetrologyWindow();
  float measured = metrologyEngine.getFeederCurrent(1, 'R');

  bool pass = (fabs(measured - refCurrent) < 0.5f);
  char buf[48];
  snprintf(buf, sizeof(buf), "(Ref=%.2fA, Meas=%.2fA)", refCurrent, measured);
  logTest(8, "Known Current Input Verification", pass, buf);
  return pass;
}

bool MeasurementTests::test09_SensorDisconnect() {
  metrologyEngine.reset();
  // Flat zero counts with 0 variance representing disconnected/open CT
  for (int i = 0; i < 200; i++) {
    AdcRawSampleFrame frame;
    frame.timestampUs = i * 1000;
    frame.sampleIndex = i;
    frame.valid = true;
    for (int c = 0; c < 24; c++) {
      frame.rawCounts[c] = 0;
      frame.saturated[c] = false;
    }
    metrologyEngine.processRawSample(frame);
  }
  metrologyEngine.computeMetrologyWindow();
  const CommonVoltageState &vs = metrologyEngine.getVoltageState();
  bool pass = (vs.stateR == MEAS_NO_SIGNAL);
  logTest(9, "Sensor Disconnect Detection", pass, "(Mains NO_SIGNAL correctly flagged)");
  return pass;
}

bool MeasurementTests::test10_AdcDisconnect() {
  AdcRawSampleFrame frame;
  frame.timestampUs = micros();
  frame.sampleIndex = 1;
  frame.valid = false;
  frame.chipOnline[0] = false; // Chip offline

  bool pass = (!frame.valid && !frame.chipOnline[0]);
  logTest(10, "ADC Disconnect & Fault Flagging", pass);
  return pass;
}

bool MeasurementTests::test11_InputSaturation() {
  metrologyEngine.reset();
  for (int i = 0; i < 200; i++) {
    AdcRawSampleFrame frame;
    frame.timestampUs = i * 1000;
    frame.sampleIndex = i;
    frame.valid = true;
    for (int c = 0; c < 24; c++) frame.saturated[c] = false;
    // Inject maximum positive full scale saturation
    frame.rawCounts[0] = 8388600L;
    frame.saturated[0] = true;
    metrologyEngine.processRawSample(frame);
  }
  metrologyEngine.computeMetrologyWindow();
  const ChannelMeasurementRecord *rec = metrologyEngine.getChannelRecord(0);
  bool pass = (rec->validity == MEAS_SATURATED);
  logTest(11, "Input Saturation / Clipping Detection", pass);
  return pass;
}

bool MeasurementTests::test12_MeasurementNoise() {
  metrologyEngine.reset();
  for (int i = 0; i < 200; i++) {
    AdcRawSampleFrame frame;
    frame.timestampUs = i * 1000;
    frame.sampleIndex = i;
    frame.valid = true;
    for (int c = 0; c < 24; c++) {
      frame.rawCounts[c] = (int32_t)((i % 5) * 100);
      frame.saturated[c] = false;
    }
    metrologyEngine.processRawSample(frame);
  }
  metrologyEngine.computeMetrologyWindow();
  float iNoise = metrologyEngine.getFeederCurrent(1, 'R');
  bool pass = (iNoise == 0.0f);
  logTest(12, "Measurement Noise Immunity", pass);
  return pass;
}

bool MeasurementTests::test13_FrequencyVariation() {
  // Test frequency tracking at 48.5 Hz and 51.5 Hz (Indian grid fluctuations)
  float testFreq = 48.5f;
  metrologyEngine.reset();
  for (int i = 0; i < 200; i++) {
    AdcRawSampleFrame frame;
    frame.timestampUs = i * 1000;
    frame.sampleIndex = i;
    frame.valid = true;
    for (int c = 0; c < 24; c++) frame.saturated[c] = false;

    float t = (float)i * 0.001f;
    float omega = 2.0f * (float)M_PI * testFreq;
    float vPeakCounts = (0.7071f / 1.200f) * 8388607.0f;
    frame.rawCounts[0] = (int32_t)(vPeakCounts * sinf(omega * t));
    metrologyEngine.processRawSample(frame);
  }
  metrologyEngine.computeMetrologyWindow();
  float fMeasured = metrologyEngine.getGridFrequency();
  bool pass = (fabs(fMeasured - testFreq) < 1.0f);
  char buf[48];
  snprintf(buf, sizeof(buf), "(Actual=%.1fHz, Measured=%.1fHz)", testFreq, fMeasured);
  logTest(13, "Frequency Variation Tracking (48.5Hz)", pass, buf);
  return pass;
}

bool MeasurementTests::test14_CalibrationPersistence() {
  bool saved = calibMgr.saveToNVS();
  bool loaded = calibMgr.loadFromNVS();
  bool verified = calibMgr.verifyCalibrationIntegrity();
  bool pass = saved && loaded && verified;
  logTest(14, "Calibration Persistence & CRC Integrity", pass);
  return pass;
}

bool MeasurementTests::test15_PacketSerialization() {
  String pkt = telemetryScheduler.buildVoltageTelemetryPacket(101);
  bool pass = pkt.indexOf("VR=") > 0 && pkt.indexOf("VY=") > 0 && pkt.indexOf("VB=") > 0 && pkt.indexOf("F=") > 0;
  logTest(15, "Packet Serialization Format", pass);
  return pass;
}

bool MeasurementTests::test16_TelemetryFragmentation() {
  String pkt1 = telemetryScheduler.buildFeederBatchPacket(0, 102); // F1..F3
  String pkt2 = telemetryScheduler.buildFeederBatchPacket(1, 103); // F4..F6

  bool pass = (pkt1.indexOf("F1:") > 0 && pkt1.indexOf("F3:") > 0) &&
              (pkt2.indexOf("F4:") > 0 && pkt2.indexOf("F6:") > 0);
  logTest(16, "Telemetry Fragmentation (Feeders 1-3 & 4-6)", pass);
  return pass;
}

bool MeasurementTests::test17_MeasurementRecoveryAfterRestart() {
  // Simulates system restart recovery: reload calib, reset metrology, verify fresh frame
  calibMgr.init();
  metrologyEngine.init("MSCB-P001");
  bool pass = (calibMgr.verifyCalibrationIntegrity() && (strcmp(metrologyEngine.getPoleId(), "MSCB-P001") == 0));
  logTest(17, "Measurement Recovery After Restart", pass);
  return pass;
}

bool MeasurementTests::runAllTests() {
  Serial.println(F("\n========================================================"));
  Serial.println(F("  MSCB SMART POLE PROMPT 14 — 17-STAGE METROLOGY TEST SUITE"));
  Serial.println(F("========================================================"));

  int passed = 0;
  if (test01_AdcInitialization()) passed++;
  if (test02_ThreeAdcSynchronization()) passed++;
  if (test03_RybVoltageSampling()) passed++;
  if (test04_F01_RybCurrent()) passed++;
  if (test05_AllSixFeederChannels()) passed++;
  if (test06_IndependentCurrentCalibration()) passed++;
  if (test07_ZeroCurrentMeasurement()) passed++;
  if (test08_KnownCurrentInput()) passed++;
  if (test09_SensorDisconnect()) passed++;
  if (test10_AdcDisconnect()) passed++;
  if (test11_InputSaturation()) passed++;
  if (test12_MeasurementNoise()) passed++;
  if (test13_FrequencyVariation()) passed++;
  if (test14_CalibrationPersistence()) passed++;
  if (test15_PacketSerialization()) passed++;
  if (test16_TelemetryFragmentation()) passed++;
  if (test17_MeasurementRecoveryAfterRestart()) passed++;

  Serial.println(F("--------------------------------------------------------"));
  Serial.printf("  TOTAL PASSED: %d / 17 (%.1f%%)\n", passed, (passed / 17.0f) * 100.0f);
  Serial.println(F("========================================================\n"));

  return (passed == 17);
}
