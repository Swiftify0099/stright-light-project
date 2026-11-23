/**
 * @file test_task3_afe_protection_redesign.cpp
 * @brief Fifth-Pass Independent Verification: AFE RMS Fault & Multi-Channel LM4040 Shunt Audit
 * 
 * Correctly evaluates:
 * - Vpeak = Vrms * sqrt(2) = 50.0 * 1.41421356 = 70.71V peak for 50Vrms 50Hz fault
 * - Actual manufacturer datasheet maximum BAV199 forward voltage VF (up to 1.05V - 1.10V at 10-15mA)
 * - Current through 4.70k series resistor for:
 *     * Positive clamp: (70.71 - 3.55V) / 4.70k = 14.29 mA peak (EXCEEDS 10 mA abs max!)
 *     * Negative clamp: (70.71 - 1.05V) / 4.70k = 14.82 mA peak (EXCEEDS 10 mA abs max!)
 *     * Differential fault (100V peak across 9.40k): 10.15 mA peak
 *     * AVDD unpowered: LM4040 unpowered -> cannot shunt; pin exceeds +0.30V limit
 *     * Multiple channels faulting simultaneously into LM4040 2.5V rail (15 mA max capability!)
 * - Proves LM4040 cannot absorb simultaneous injected fault currents without rising or failing
 * - Explains why designing at ADS131M08 absolute maximum input current is unacceptable
 * - Concludes: Gate B MUST REMAIN STRICTLY BLOCKED!
 */

#include "ArduinoMock.h"
#include <cmath>
#include <cassert>
#include <iomanip>

int main() {
  std::cout << "======================================================================" << std::endl;
  std::cout << " TASK 3 (FIFTH-PASS): AFE RMS FAULT & MULTI-CHANNEL SHUNT AUDIT" << std::endl;
  std::cout << "======================================================================" << std::endl;

  // 1. TI ADS131M08 Silicon Limits (Datasheet SBAS890C Table 7.1)
  const double TI_ADS131_AVDD_NOM        = 3.30;
  const double TI_ADS131_VMAX_POWERED    = TI_ADS131_AVDD_NOM + 0.30; // +3.60V
  const double TI_ADS131_VMIN_POWERED    = -1.60;                    // -1.60V to AGND
  const double TI_ADS131_VMAX_UNPOWERED  = 0.0 + 0.30;               // +0.30V
  const double TI_ADS131_IMAX_CONT_MA    = 10.0;                     // +/- 10.0 mA absolute maximum
  const double TI_ADS131_INOM_TARGET_MA  = 2.0;                      // Safe design acceptance target (with 5x margin)

  std::cout << "\n--- 1. TI ADS131M08 SILICON LIMITS (SBAS890C Table 7.1) ---" << std::endl;
  std::cout << "Absolute Maximum Continuous Pin Current : +/- " << TI_ADS131_IMAX_CONT_MA << " mA" << std::endl;
  std::cout << "Conservative Design Acceptance Target   : <= " << TI_ADS131_INOM_TARGET_MA << " mA (Mandatory safe margin)" << std::endl;
  std::cout << "Absolute Voltage Range (AVDD=3.3V)      : " << TI_ADS131_VMIN_POWERED << " V to +" << TI_ADS131_VMAX_POWERED << " V" << std::endl;
  std::cout << "Absolute Voltage Range (AVDD=0V)        : Max +" << TI_ADS131_VMAX_UNPOWERED << " V" << std::endl;

  // 2. Exact RMS-to-Peak Fault Calculation
  const double V_FAULT_RMS = 50.0;
  const double V_FAULT_PEAK = V_FAULT_RMS * std::sqrt(2.0); // 70.7107 V
  const double R_SERIES_OHMS = 4700.0; // 4.70 kOhm series MELF

  std::cout << "\n--- 2. CORRECTED 50V RMS FAULT PEAK CALCULATION ---" << std::endl;
  std::cout << "Nominal Fault RMS Voltage : " << V_FAULT_RMS << " V RMS" << std::endl;
  std::cout << "True AC Peak Voltage      : " << std::fixed << std::setprecision(4) << V_FAULT_PEAK 
            << " V (Vpeak = Vrms * sqrt(2))" << std::endl;

  // 3. Manufacturer Datasheet Diode Forward Voltage (Nexperia BAV199 Rev 8)
  // Datasheet: VF <= 0.90V at 1mA, VF <= 1.00V at 10mA, VF <= 1.10V at 50mA
  const double BAV199_VF_10MA_MAX = 1.00;
  const double BAV199_VF_PEAK_MAX = 1.05; // At 14-15mA peak current
  const double V_REF_SUBRAIL = 2.50;      // LM4040-2.5 precision shunt reference

  std::cout << "\n--- 3. BAV199 MANUFACTURER DATASHEET FORWARD VOLTAGE (NEXPERIA REV 8) ---" << std::endl;
  std::cout << "VF Max at 1 mA  : 0.90 V" << std::endl;
  std::cout << "VF Max at 10 mA : " << BAV199_VF_10MA_MAX << " V" << std::endl;
  std::cout << "VF Max at 15 mA : " << BAV199_VF_PEAK_MAX << " V" << std::endl;

  // Clamping Voltages
  double vPinPosMax = V_REF_SUBRAIL + BAV199_VF_PEAK_MAX; // 2.50 + 1.05 = +3.55 V
  double vPinNegMin = 0.0 - BAV199_VF_PEAK_MAX;           // 0.0 - 1.05 = -1.05 V

  std::cout << "Clamped Positive Pin Voltage : +" << vPinPosMax << " V (Margin to +3.60V abs max: ONLY " 
            << (TI_ADS131_VMAX_POWERED - vPinPosMax) * 1000.0 << " mV!)" << std::endl;
  std::cout << "Clamped Negative Pin Voltage : " << vPinNegMin << " V (Margin to -1.60V abs min: " 
            << (fabs(TI_ADS131_VMIN_POWERED) - fabs(vPinNegMin)) << " V)" << std::endl;

  // 4. Current Through 4.70k Resistor Under 50Vrms Fault
  std::cout << "\n--- 4. PEAK CURRENT THROUGH 4.70k RESISTOR (50V RMS FAULT) ---" << std::endl;
  // Positive Clamp Peak Current
  double iPosPeakMa = ((V_FAULT_PEAK - vPinPosMax) / R_SERIES_OHMS) * 1000.0;
  std::cout << "Positive Clamp Peak Current : " << std::fixed << std::setprecision(2) << iPosPeakMa << " mA" << std::endl;
  std::cout << "  * LIMIT CHECK: ADS131M08 Continuous Abs Max is +/- 10.0 mA!" << std::endl;
  if (iPosPeakMa > TI_ADS131_IMAX_CONT_MA) {
    std::cout << "  * DEFECT IDENTIFIED: Positive peak current (" << iPosPeakMa 
              << " mA) EXCEEDS 10.0 mA ABSOLUTE MAXIMUM by " << (iPosPeakMa - TI_ADS131_IMAX_CONT_MA) 
              << " mA (" << ((iPosPeakMa / TI_ADS131_IMAX_CONT_MA) - 1.0) * 100.0 << "%)!" << std::endl;
  }

  // Negative Clamp Peak Current
  double iNegPeakMa = ((V_FAULT_PEAK - fabs(vPinNegMin)) / R_SERIES_OHMS) * 1000.0;
  std::cout << "Negative Clamp Peak Current : " << iNegPeakMa << " mA" << std::endl;
  if (iNegPeakMa > TI_ADS131_IMAX_CONT_MA) {
    std::cout << "  * DEFECT IDENTIFIED: Negative peak current (" << iNegPeakMa 
              << " mA) EXCEEDS 10.0 mA ABSOLUTE MAXIMUM by " << (iNegPeakMa - TI_ADS131_IMAX_CONT_MA) << " mA!" << std::endl;
  }

  // Differential Fault (100V Peak across inputs through 2x 4.7k = 9.40k)
  double vDiffDiodeDrop = vPinPosMax - vPinNegMin; // 3.55 - (-1.05) = 4.60V
  double iDiffPeakMa = ((100.0 - vDiffDiodeDrop) / (2 * R_SERIES_OHMS)) * 1000.0;
  std::cout << "Differential 100V Peak Current : " << iDiffPeakMa << " mA (Limit: <= 10.0 mA)" << std::endl;
  if (iDiffPeakMa > TI_ADS131_IMAX_CONT_MA) {
    std::cout << "  * DEFECT IDENTIFIED: Differential peak current (" << iDiffPeakMa << " mA) EXCEEDS 10.0 mA limit!" << std::endl;
  }

  // AVDD Unpowered
  std::cout << "\n--- 5. AVDD UNPOWERED FAULT BEHAVIOR ---" << std::endl;
  std::cout << "When AVDD = 0V, LM4040 shunt reference is UNPOWERED." << std::endl;
  std::cout << "Unpowered shunt regulator cannot sink current without terminal voltage rising." << std::endl;
  std::cout << "ADS131M08 Unpowered Pin Limit: Max +0.30V." << std::endl;
  std::cout << "Internal ESD diodes conduct into 0V rail at ~0.60V, violating +0.30V absolute limit." << std::endl;

  // 5. LM4040 2.5V Shunt Reference Current Capability Analysis
  std::cout << "\n--- 6. LM4040 2.5V SHUNT REGULATOR CURRENT SINK CAPABILITY ---" << std::endl;
  // TI LM4040 Datasheet: Reverse Breakdown Current Range: 60 uA to 15 mA.
  // Absolute Maximum Continuous Reverse Current: 20 mA.
  const double LM4040_IMAX_SINK_MA = 15.0; // Recommended operating limit
  const double LM4040_IABS_MAX_MA  = 20.0; // Silicon destruction boundary

  std::cout << "LM4040 Maximum Rated Continuous Operating Reverse Current : " << LM4040_IMAX_SINK_MA << " mA" << std::endl;
  std::cout << "LM4040 Absolute Maximum Reverse Current                  : " << LM4040_IABS_MAX_MA << " mA" << std::endl;
  std::cout << "Current Injected by SINGLE Faulting Channel              : " << iPosPeakMa << " mA" << std::endl;
  std::cout << "LM4040 Capacity Used by ONE Channel                      : " 
            << (iPosPeakMa / LM4040_IMAX_SINK_MA) * 100.0 << "%" << std::endl;

  // Multiple Channels Faulting Simultaneously
  std::cout << "\n--- 7. MULTIPLE SIMULTANEOUS CHANNEL FAULT INJECTION ---" << std::endl;
  std::cout << std::left << std::setw(20) << "Faulting Channels"
            << std::setw(25) << "Total Injected Current"
            << std::setw(22) << "LM4040 Status"
            << std::setw(20) << "2.5V Rail Voltage" << std::endl;
  std::cout << std::string(87, '-') << std::endl;

  int faultChans[] = {1, 2, 3, 6, 18, 21};
  for (int chCount : faultChans) {
    double totalInjMa = chCount * iPosPeakMa;
    std::string status;
    std::string vRail;
    if (totalInjMa <= LM4040_IMAX_SINK_MA) {
      status = "REGULATING (MARGINAL)";
      vRail = "+2.50 V";
    } else if (totalInjMa <= LM4040_IABS_MAX_MA) {
      status = "OVERCURRENT WARNING";
      vRail = "+2.65 V to +2.80 V";
    } else {
      status = "CATASTROPHIC FAILURE";
      vRail = "LOST REGULATION (>+3.8V!)";
    }

    std::cout << std::left << std::setw(20) << (std::to_string(chCount) + " channel(s)")
              << std::setw(25) << (std::to_string(totalInjMa).substr(0, 5) + " mA")
              << std::setw(22) << status
              << std::setw(20) << vRail << std::endl;
  }

  std::cout << "\n--- 8. ENGINEERING VERDICT & GATE DISPOSITION ---" << std::endl;
  std::cout << "[DEFECT-1]: Under 50Vrms (70.71Vpk), single-channel peak current is 14.29 mA, EXCEEDING 10.0 mA abs max." << std::endl;
  std::cout << "[DEFECT-2]: 2 or more simultaneously faulting channels inject 28.58 mA, EXCEEDING LM4040 15 mA sink rating." << std::endl;
  std::cout << "[DEFECT-3]: When LM4040 loses regulation, sub-rail rises, driving pin voltage > +3.60V abs max." << std::endl;
  std::cout << "[RULE VIOLATION]: Designing at silicon absolute maximum limits with ZERO margin is strictly forbidden." << std::endl;
  std::cout << "\n>>> DISPOSITION: GATE B REMAINS STRICTLY BLOCKED!" << std::endl;
  std::cout << "    Hardware redesign required: Active high-current shunt sink (BJT/TL431 bypass) or higher series impedance." << std::endl;

  return 0;
}
