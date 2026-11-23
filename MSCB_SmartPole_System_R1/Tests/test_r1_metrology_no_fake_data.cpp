/**
 * @file test_r1_metrology_no_fake_data.cpp
 * @brief MSCB Smart Pole System R1 - Zero-Fake-Data, True-RMS & Dynamic Power Factor Metrology Suite
 */

#include "ArduinoMock.h"
#include <cmath>
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../FieldPole_ESP32/PoleMetrology.h"

MockSerial Serial;

static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_REPORT(cond, tag, msg) \
  do { \
    if (cond) { \
      std::cout << "  [" tag "] " << msg << std::endl; \
      g_passCount++; \
    } else { \
      std::cout << "  [FAIL] " << msg << " (Line " << __LINE__ << ")" << std::endl; \
      g_failCount++; \
    } \
  } while (0)

int main() {
  std::cout << "\n========================================================" << std::endl;
  std::cout << "  TEST SUITE 4: ZERO-SIMULATION & REAL METROLOGY ENGINE  " << std::endl;
  std::cout << "========================================================" << std::endl;

  PoleMetrology met;
  met.begin();

  // -----------------------------------------------------------------
  // 1. Zero-Simulation Mandate (Missing ADC Hardware)
  // -----------------------------------------------------------------
  std::cout << "\n[1. ZERO-SIMULATION & HARDWARE DISCONNECTION CHECKS]" << std::endl;
  {
    // Hardware detection fails when chips are absent
    bool detected = met.detectHardware();
    TEST_REPORT(detected == false, "UNIT_PASS", "detectHardware() returns false when SPI chips are missing");
    TEST_REPORT(met.areAllAdcsDetected() == false, "UNIT_PASS", "areAllAdcsDetected() is strictly false");

    // Channels must report CHANNEL_ADC_NOT_CONNECTED and NAN (no synthetic 230V or 50Hz!)
    for (uint8_t ch = 1; ch <= 21; ch++) {
      TEST_REPORT(met.getChannelState(ch) == CHANNEL_ADC_NOT_CONNECTED, "INTEGRATION_PASS",
                  ("Channel " + std::to_string(ch) + " reports CHANNEL_ADC_NOT_CONNECTED").c_str());
      TEST_REPORT(std::isnan(met.getChannelValue(ch)), "INTEGRATION_PASS",
                  ("Channel " + std::to_string(ch) + " reading is strictly NAN (zero synthetic data)").c_str());
      if (ch == 3) break; // Check first 3 bus voltages to avoid console spam
    }

    // Bus voltages must be NAN
    TEST_REPORT(std::isnan(met.getBusVoltageR()), "UNIT_PASS", "getBusVoltageR() returns NAN");
    TEST_REPORT(std::isnan(met.getBusVoltageY()), "UNIT_PASS", "getBusVoltageY() returns NAN");
    TEST_REPORT(std::isnan(met.getBusVoltageB()), "UNIT_PASS", "getBusVoltageB() returns NAN");

    // Power factor must be NAN, NEVER hardcoded to 0.95!
    TEST_REPORT(std::isnan(met.getFeederPowerFactor(0)), "INTEGRATION_PASS",
                "Feeder 1 Power Factor is NAN without hardware (NEVER hardcoded to 0.95)");
  }

  // -----------------------------------------------------------------
  // 2. Flat Zero-Frame / Dead-Bus Rejection
  // -----------------------------------------------------------------
  std::cout << "\n[2. DEAD BUS & ALL-ZERO FRAME REJECTION]" << std::endl;
  {
    int32_t zeroFrame[METROLOGY_TOTAL_RAW_CH] = {0};
    bool processed = met.processRawAdcFrame(zeroFrame, 1000);
    TEST_REPORT(processed == false, "INTEGRATION_PASS",
                "processRawAdcFrame() rejects all-zero frames (Dead bus / SPI disconnected)");
    TEST_REPORT(met.getChannelState(1) == CHANNEL_ZERO_FRAME_REJECTED, "UNIT_PASS",
                "Channel state flags CHANNEL_ZERO_FRAME_REJECTED");
  }

  // -----------------------------------------------------------------
  // 3. True-RMS Calculation with Pure Sine Wave Injection
  // -----------------------------------------------------------------
  std::cout << "\n[3. TRUE-RMS SINE WAVE INTEGRATION (230V / 10A)]" << std::endl;
  {
    // Inject hardware presence for controlled bench metrology evaluation
    met.setAdcChipDetected(0, true); // ADC 1
    met.setAdcChipDetected(1, true); // ADC 2
    met.setAdcChipDetected(2, true); // ADC 3

    // Configure calibration for all 21 active channels
    for (uint8_t c = 1; c <= 3; c++) {
      met.setChannelCalibration(c, 0.0f, 1.0f, 400.0f, 0.0f); // 400.0 V/V ZMPT101B
    }
    for (uint8_t c = 4; c <= 21; c++) {
      met.setChannelCalibration(c, 0.0f, 1.0f, 83.333f, 0.0f); // 83.333 A/V SCT-013-000
    }

    // Target: 230.0 V RMS on Bus V, 10.0 A RMS on Feeder 1 Phase R
    const double V_PER_COUNT = 1.20 / 8388607.0;
    const double v_pin_pk = (230.0 * sqrt(2.0)) / 400.0;
    const double i_pin_pk = (10.0 * sqrt(2.0)) / 83.333;

    // Simulate 1 full cycle of 50 Hz at 4 kSPS (80 samples)
    int32_t frame[METROLOGY_TOTAL_RAW_CH];
    for (int n = 0; n < 80; n++) {
      memset(frame, 0, sizeof(frame));
      double theta = 2.0 * M_PI * ((double)n / 80.0);
      double vR = v_pin_pk * sin(theta);
      double vY = v_pin_pk * sin(theta - 2.0 * M_PI / 3.0);
      double vB = v_pin_pk * sin(theta + 2.0 * M_PI / 3.0);
      double iR = i_pin_pk * sin(theta); // in phase: PF = 1.0

      // Map to raw ADC channels:
      // Bus V Ph R -> raw index 19 (ADC 3 Ch 3)
      frame[19] = (int32_t)(vR / V_PER_COUNT);
      // Bus V Ph Y -> raw index 20 (ADC 3 Ch 4)
      frame[20] = (int32_t)(vY / V_PER_COUNT);
      // Bus V Ph B -> raw index 21 (ADC 3 Ch 5)
      frame[21] = (int32_t)(vB / V_PER_COUNT);

      // Feeder 1 Ph R -> raw index 0 (ADC 1 Ch 0)
      frame[0] = (int32_t)(iR / V_PER_COUNT);

      // Baseline hardware noise on spare channel (ADC 3 Ch 7) prevents artificial all-zero frame
      frame[23] = 300;

      met.processRawAdcFrame(frame, 1000 + (n * 12));
    }

    float measuredVrms = met.getBusVoltageR();
    float measuredIrms = met.getFeederCurrent(0, 0); // Feeder 1, Ph R

    TEST_REPORT(fabs(measuredVrms - 230.0f) < 1.0f, "INTEGRATION_PASS",
                ("True-RMS Bus Voltage verified: " + std::to_string(measuredVrms) + " V (Target: 230.0V)").c_str());
    TEST_REPORT(fabs(measuredIrms - 10.0f) < 0.15f, "INTEGRATION_PASS",
                ("True-RMS Feeder Current verified: " + std::to_string(measuredIrms) + " A (Target: 10.0A)").c_str());

    // -----------------------------------------------------------------
    // 4. Real Instantaneous Active Power & Dynamic Power Factor
    // -----------------------------------------------------------------
    std::cout << "\n[4. ACTIVE POWER & DYNAMIC POWER FACTOR (PF = 1.00)]" << std::endl;
    float activeP = met.getFeederActivePower(0);
    float pf = met.getFeederPowerFactor(0);

    // Expected P = 230.0V * 10.0A = 2300 W (or 2.30 kW)
    TEST_REPORT(fabs(activeP - 2.300f) < 0.05f, "INTEGRATION_PASS",
                ("Dynamic Active Power: " + std::to_string(activeP) + " kW (Expected: 2.30 kW)").c_str());
    TEST_REPORT(fabs(pf - 1.00f) < 0.02f, "INTEGRATION_PASS",
                ("Dynamic Power Factor: " + std::to_string(pf) + " (Target: 1.000, NOT hardcoded!)").c_str());

    // -----------------------------------------------------------------
    // 5. Dynamic Power Factor at 60-degree Phase Shift (PF = 0.50)
    // -----------------------------------------------------------------
    std::cout << "\n[5. DYNAMIC POWER FACTOR TEST AT PF = 0.50 (60-DEGREE LAG)]" << std::endl;
    // Inject current lagging voltage by 60 degrees (cos 60 = 0.50)
    for (int n = 0; n < 80; n++) {
      memset(frame, 0, sizeof(frame));
      double theta = 2.0 * M_PI * ((double)n / 80.0);
      double vR = v_pin_pk * sin(theta);
      double vY = v_pin_pk * sin(theta - 2.0 * M_PI / 3.0);
      double vB = v_pin_pk * sin(theta + 2.0 * M_PI / 3.0);
      double iR = i_pin_pk * sin(theta - (M_PI / 3.0)); // 60 deg lag

      frame[19] = (int32_t)(vR / V_PER_COUNT);
      frame[20] = (int32_t)(vY / V_PER_COUNT);
      frame[21] = (int32_t)(vB / V_PER_COUNT);

      frame[0] = (int32_t)(iR / V_PER_COUNT);

      frame[23] = 300;

      met.processRawAdcFrame(frame, 2000 + (n * 12));
    }

    float activeP_half = met.getFeederActivePower(0);
    float pf_half = met.getFeederPowerFactor(0);

    TEST_REPORT(fabs(activeP_half - 1.150f) < 0.05f, "INTEGRATION_PASS",
                ("Active Power at 60 deg lag: " + std::to_string(activeP_half) + " kW (Expected: 1.15 kW)").c_str());
    TEST_REPORT(fabs(pf_half - 0.50f) < 0.03f, "INTEGRATION_PASS",
                ("Dynamic Power Factor at 60 deg lag: " + std::to_string(pf_half) + " (Target: 0.500)").c_str());
  }

  // -----------------------------------------------------------------
  // 6. Saturation / Over-Range Clipping Protection
  // -----------------------------------------------------------------
  std::cout << "\n[6. SATURATION / OVER-RANGE CLIPPING PROTECTION]" << std::endl;
  {
    int32_t clipFrame[METROLOGY_TOTAL_RAW_CH] = {0};
    clipFrame[19] = 8388000; // Saturating count (> 1.199V)
    clipFrame[0] = 50000;
    met.processRawAdcFrame(clipFrame, 3000);

    TEST_REPORT(met.getChannelState(1) == CHANNEL_OVER_RANGE_CLIPPING, "INTEGRATION_PASS",
                "Channel 1 correctly transitions to CHANNEL_OVER_RANGE_CLIPPING on analog saturation");
  }

  // -----------------------------------------------------------------
  // 7. Authoritative 24-Raw / 21-Active Physical Channel Map Verification
  // -----------------------------------------------------------------
  std::cout << "\n[7. AUTHORITATIVE PHYSICAL ADC CHANNEL MAPPING]" << std::endl;
  {
    // Total raw channels must be 24
    TEST_REPORT(METROLOGY_TOTAL_RAW_CH == 24, "UNIT_PASS", "Authoritative raw channels: exactly 24");
    // Total active channels must be 21
    TEST_REPORT(METROLOGY_TOTAL_ACTIVE_CH == 21, "UNIT_PASS", "Authoritative active channels: exactly 21");
    // Spares must be 3
    int spareCount = 0;
    for (int i = 0; i < 24; i++) {
      if (AUTHORITATIVE_ADC_MAP[i].isSpare) spareCount++;
    }
    TEST_REPORT(spareCount == 3, "UNIT_PASS", "Balanced spare channels: exactly 3");
    // Class 1.0 accuracy withheld pending physical bench certification
    TEST_REPORT(met.isClass1_0Certified() == false, "UNIT_PASS",
                "Class 1.0 certification withheld pending physical laboratory test");
  }

  std::cout << "\n========================================================" << std::endl;
  std::cout << "TEST SUITE 4 SUMMARY: " << g_passCount << " PASSED, " << g_failCount << " FAILED" << std::endl;
  std::cout << "========================================================\n" << std::endl;

  return (g_failCount == 0) ? 0 : 1;
}
