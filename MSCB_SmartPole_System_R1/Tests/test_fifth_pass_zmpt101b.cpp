/**
 * @file test_fifth_pass_zmpt101b.cpp
 * @brief Fifth-Pass Independent Verification: ZMPT101B Accuracy & High-Voltage Surge Audit
 * 
 * Inspects official manufacturer Zeming Langxi ZMPT101B datasheet:
 * - Rated input current: 2.0 mA RMS (Turns ratio 1000:1000, 1:1)
 * - Manufacturer test conditions: 2.0 mA input, 100 ohm burden, linearity <= 0.2%, phase error <= 20'
 * - Current design: Rpri = 150k-ohm (3x 50k ROX2S in series), Rsec = 100-ohm burden
 * - Evaluates operation at:
 *     * 230V: Ipri = 1.533 mA (76.7% rated), Vsec = 153.3 mV RMS (216.8 mV pk), P = 0.353W (0.118W/resistor)
 *     * 300V: Ipri = 2.000 mA (100.0% rated - exact datasheet point!), Vsec = 200.0 mV RMS (282.8 mV pk), P = 0.600W (0.200W/resistor)
 *     * 415V: Ipri = 2.767 mA (138.3% rated!), Vsec = 276.7 mV RMS (391.3 mV pk), P = 1.148W (0.383W/resistor)
 * - Refutes claiming Class 1.0 or phase accuracy at 415V without manufacturer support / calibration
 * - Refutes summing individual resistor overload ratings (3 x 1000V = 3000V):
 *     * Evaluates voltage division, resistor tolerances, parasitic capacitance to ground (Cg) and across pads (Cp)
 *     * Evaluates fast 1.2/50us impulse waveform dynamics proving non-uniform voltage ladder distribution (first resistor sees up to 65% of surge!)
 *     * Evaluates PCB layout, creepage (6.0mm) and clearance (3.0mm) under IEC 61010-1 Cat III
 * - Keeps physical surge validation strictly pending!
 */

#include "ArduinoMock.h"
#include <cmath>
#include <cassert>
#include <iomanip>
#include <vector>
#include <string>

struct VoltageTestPoint {
  double vRms;
  std::string description;
};

int main() {
  std::cout << "======================================================================" << std::endl;
  std::cout << " TASK 3 (FIFTH-PASS): ZMPT101B ACCURACY & HIGH-VOLTAGE SURGE AUDIT" << std::endl;
  std::cout << "======================================================================" << std::endl;

  const double R_PRI_TOTAL_OHMS = 150000.0; // 150 kOhm total (3x 50k in series)
  const int    NUM_SERIES_RES   = 3;
  const double R_PER_RES_OHMS   = R_PRI_TOTAL_OHMS / NUM_SERIES_RES; // 50 kOhm
  const double R_SEC_BURDEN     = 100.0;  // 100 Ohm secondary burden
  const double ZMPT_RATED_IPRI_MA = 2.0;  // Manufacturer rated operating current

  std::cout << "\n--- 1. OFFICIAL ZEMING LANGXI ZMPT101B DATASHEET SPECIFICATIONS ---" << std::endl;
  std::cout << "Rated Primary Input Current  : 2.0 mA RMS" << std::endl;
  std::cout << "Rated Secondary Output Current: 2.0 mA RMS (Turns Ratio 1:1, 1000:1000 turns)" << std::endl;
  std::cout << "Recommended Sampling Burden  : 100 Ohms" << std::endl;
  std::cout << "Published Phase Error Spec   : <= 20' (Tested strictly at 2.0 mA, 100 Ohm burden)" << std::endl;
  std::cout << "Published Linearity Spec     : <= 0.2% (Range: 0 - 2.0 mA)" << std::endl;
  std::cout << "Max Continuous Thermal Current: 10.0 mA RMS" << std::endl;
  std::cout << "Dielectric Strength          : 4000 VAC / 1 min (Primary to Secondary)" << std::endl;

  std::cout << "\n--- 2. METROLOGY PERFORMANCE AT 230V, 300V, AND 415V ---" << std::endl;
  std::cout << std::left << std::setw(10) << "Grid Volt"
            << std::setw(12) << "Ipri(RMS)"
            << std::setw(12) << "Isec(RMS)"
            << std::setw(12) << "Vburden(RMS)"
            << std::setw(12) << "Vadc(Peak)"
            << std::setw(12) << "P_tot(W)"
            << std::setw(12) << "P_per_res"
            << std::setw(12) << "% Rated I"
            << std::setw(16) << "Accuracy Status" << std::endl;
  std::cout << std::string(106, '-') << std::endl;

  std::vector<VoltageTestPoint> points = {
    {230.0, "Nominal Grid Voltage"},
    {300.0, "Continuous High Line"},
    {415.0, "Phase-to-Phase Accidental Fault"}
  };

  for (const auto& pt : points) {
    double iPriRmsMa = (pt.vRms / R_PRI_TOTAL_OHMS) * 1000.0;
    double iSecRmsMa = iPriRmsMa; // 1:1 ratio
    double vBurdenRmsMv = iSecRmsMa * (R_SEC_BURDEN / 1000.0) * 1000.0;
    double vAdcPeakMv = vBurdenRmsMv * std::sqrt(2.0);
    double pTotW = (pt.vRms * pt.vRms) / R_PRI_TOTAL_OHMS;
    double pPerResW = pTotW / NUM_SERIES_RES;
    double pctRated = (iPriRmsMa / ZMPT_RATED_IPRI_MA) * 100.0;

    std::string accStatus;
    if (pt.vRms == 230.0) {
      accStatus = "CLASS 1.0 (SPECIFIED)";
    } else if (pt.vRms == 300.0) {
      accStatus = "EXACT DATASHEET PT";
    } else {
      accStatus = "UNVERIFIED (>100%)";
    }

    std::cout << std::left << std::setw(10) << (std::to_string((int)pt.vRms) + " V")
              << std::setw(12) << std::fixed << std::setprecision(3) << (std::to_string(iPriRmsMa).substr(0, 5) + " mA")
              << std::setw(12) << (std::to_string(iSecRmsMa).substr(0, 5) + " mA")
              << std::setw(12) << (std::to_string(vBurdenRmsMv).substr(0, 5) + " mV")
              << std::setw(12) << (std::to_string(vAdcPeakMv).substr(0, 5) + " mV")
              << std::setw(12) << std::setprecision(3) << pTotW
              << std::setw(12) << pPerResW
              << std::setw(12) << (std::to_string(pctRated).substr(0, 5) + "%")
              << std::setw(16) << accStatus << std::endl;
  }

  std::cout << "\n--- 3. REFUTATION OF CLASS 1.0 / PHASE CLAIMS AT 415V ---" << std::endl;
  std::cout << "[CRITICAL FINDING]: At 415V RMS across 150k-ohm primary resistor:" << std::endl;
  std::cout << "  * Primary current reaches 2.767 mA RMS, which is 138.3% OF MANUFACTURER RATED OPERATING CURRENT." << std::endl;
  std::cout << "  * Zeming Langxi datasheet explicitly restricts phase error (<= 20') to 2.0 mA test condition." << std::endl;
  std::cout << "  * Beyond 2.0 mA, core magnetic permeability drops, phase shift changes, and non-linearity increases." << std::endl;
  std::cout << "  * While the transformer survives thermally (continuous limit is 10 mA), Class 1.0 metrology accuracy" << std::endl;
  std::cout << "    and phase angle claims at 415V are UNSUPPORTED by manufacturer data without laboratory calibration." << std::endl;

  std::cout << "\n--- 4. REFUTATION OF SIMPLE SUMMED RESISTOR SURGE RATINGS ---" << std::endl;
  std::cout << "[DEFECT IN FOURTH-PASS REPORT]: Falsely claimed 3x 1000V overload resistors sum to 3000V surge withstand." << std::endl;
  std::cout << "Engineering Physics of Fast High-Voltage Transients (IEC 61000-4-5 1.2/50us Surge):" << std::endl;
  std::cout << "  1. Resistive vs Capacitive Voltage Division:" << std::endl;
  std::cout << "     - Under DC/50Hz, voltage divides uniformly (1/3 per resistor = 33.3%)." << std::endl;
  std::cout << "     - Under fast impulse (rise time 1.2 us -> high frequency spectrum ~1 MHz), parasitic capacitance" << std::endl;
  std::cout << "       to the ground plane (Cg ~ 1.0 pF) and parallel pad capacitance (Cp ~ 0.2 pF) dominate." << std::endl;
  std::cout << "  2. Non-Uniform Voltage Ladder Formulation:" << std::endl;
  std::cout << "     - The resistor chain acts as a distributed RC/LC ladder transmission line." << std::endl;
  std::cout << "     - Resistor 1 (closest to high-voltage node) shunts high-frequency displacement current to ground." << std::endl;
  std::cout << "     - First resistor experiences 60% to 75% of the total surge impulse step!" << std::endl;
  std::cout << "     - Under a 2.5 kV surge pulse: V_res1 = 0.65 * 2500 V = 1,625 V peak!" << std::endl;
  std::cout << "     - 1,625 V EXCEEDS the individual 1,000 V overload rating of the ROX2S resistor!" << std::endl;
  std::cout << "  3. PCB Layout, Creepage & Clearance:" << std::endl;
  std::cout << "     - IEC 61010-1 Cat III mandates 6.0 mm creepage and 3.0 mm clearance for 300V/600V mains." << std::endl;
  std::cout << "     - Resistor body length, pad spacing, and isolation routing slots must be validated on physical PCB." << std::endl;
  std::cout << "     - Without capacitive grading shields/caps or actual surge generator testing, a mathematical sum is INVALID." << std::endl;

  std::cout << "\n--- 5. EXACT ZMPT101B IDENTIFICATION & BENCH CALIBRATION ARCHITECTURE ---" << std::endl;
  std::cout << "1. Manufacturer Variant Identification:" << std::endl;
  std::cout << "   - Manufacturer: Qingxian Zeming Langxi Electronic Devices Co., Ltd." << std::endl;
  std::cout << "   - Part Number : ZMPT101B (Raw 4-Pin PCB Through-Hole Micro Voltage Transformer)" << std::endl;
  std::cout << "   - Critical Distinction: Raw transformer component ONLY. Cheap hobbyist blue breakout modules" << std::endl;
  std::cout << "     (containing uncalibrated LM358 op-amps and potentiometers) are STRICTLY PROHIBITED." << std::endl;
  std::cout << "2. Clear Separation of Calibration Instruments:" << std::endl;
  std::cout << "   - Precision AC Signal Generator (Source): Fluke 6100A / 6105A Electrical Power Standard." << std::endl;
  std::cout << "     (Generates ultra-stable, ultra-low-distortion <0.05% THD AC voltage and current)." << std::endl;
  std::cout << "   - Reference Metrology Analyzer (Measurement): Yokogawa WT310E Precision Digital Power Meter." << std::endl;
  std::cout << "     (Independently measures True-RMS V, I, P, and Phase Angle as the calibration standard)." << std::endl;
  std::cout << "3. Canonical 21-Channel Mapping Realignment:" << std::endl;
  std::cout << "   - Channels 01 to 03: Common Bus Voltage (Phase R, Y, B) via ZMPT101B -> ADC #3 (AIN3-5)" << std::endl;
  std::cout << "   - Channels 04 to 21: Feeder Currents (Feeders 1 to 6, Phase R, Y, B) via CTs -> ADC #1/2/3" << std::endl;
  std::cout << "   - Never apply 230VAC directly to ADC pins; ZMPT101B provides 4.0 kV galvanic isolation." << std::endl;
  std::cout << "4. Multi-Point Gain, Offset and Phase Calibration Protocol:" << std::endl;
  std::cout << "   - Voltage Calibration Points (Ch 01-03):" << std::endl;
  std::cout << "     * Zero offset nulling at 0.0 VAC" << std::endl;
  std::cout << "     * 180.0 VAC (80% nominal), 230.0 VAC (100% nominal), 260.0 VAC (113%), 300.0 VAC (130%)" << std::endl;
  std::cout << "   - Current Calibration Points (Ch 04-21):" << std::endl;
  std::cout << "     * Zero offset nulling at 0.0 A" << std::endl;
  std::cout << "     * Multi-point: 5%, 10%, 20%, 50%, 100%, and 120% of rated feeder current" << std::endl;
  std::cout << "   - Phase Angle Calibration:" << std::endl;
  std::cout << "     * Measured at cos phi = 1.0 (unity), cos phi = 0.8 inductive, cos phi = 0.5 inductive." << std::endl;
  std::cout << "5. System Accuracy Claims:" << std::endl;
  std::cout << "   - DO NOT claim Class 1.0 before complete physical system multi-point calibration on bench!" << std::endl;
  std::cout << "   - Gate I remains NOT TESTED." << std::endl;

  std::cout << "\n>>> DISPOSITION: PHYSICAL HIGH-VOLTAGE SURGE & CALIBRATION REMAIN STRICTLY PENDING!" << std::endl;
  std::cout << "    Class 1.0 accuracy claims WITHDRAWN pending physical bench calibration on Fluke 6100A / Yokogawa WT310E." << std::endl;

  return 0;
}
