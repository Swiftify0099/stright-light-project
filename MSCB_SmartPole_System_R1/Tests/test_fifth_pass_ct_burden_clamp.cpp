/**
 * @file test_fifth_pass_ct_burden_clamp.cpp
 * @brief Fifth-Pass Independent Verification: 18-Channel CT Sensor & AFE Clamp Conflict Audit
 * 
 * Evaluates all 18 Feeder Current channels against Stage-1 antiparallel clamp (+/- 0.70V):
 * - Exact CT turns ratio (1000:1 for ZMCT103C, 2000:1 for SCT-013-000)
 * - Maximum specified primary current (8A/10A for Feeder 1, 28A for Feeders 2/3, 45A for Feeders 4/5, 30A for Feeder 6)
 * - Burden resistance tolerance (+/- 0.1% to +/- 1.0%)
 * - Peak secondary current (Ipk = Irms * sqrt(2))
 * - Peak burden voltage (Vpk = Ipk * Rb)
 * - Silicon diode clamp conduction threshold (+/- 0.70V nominal, 0.50V knee at 25C, 0.44V knee at 55C)
 * - Maximum ADC linear input (+/- 1.20V FSR)
 * - Identifies every channel clipped or distorted during normal full-load operation
 * - Specifies safe replacement protection design (Dual-series antiparallel pairs or bidirectional TVS).
 */

#include "ArduinoMock.h"
#include <cmath>
#include <cassert>
#include <iomanip>
#include <vector>
#include <string>

struct ChannelSpec {
  int chStart;
  int chEnd;
  std::string feederName;
  std::string application;
  std::string ctModel;
  double turnsRatio;       // Primary / Secondary
  double primaryCurrentRms;// Amperes RMS
  double burdenResistance; // Ohms
  double burdenTolerance;  // Fraction (e.g. 0.01 for 1%)
};

int main() {
  std::cout << "======================================================================" << std::endl;
  std::cout << " TASK 2 (FIFTH-PASS): 18-CHANNEL CT SENSOR & AFE CLAMP CONFLICT AUDIT" << std::endl;
  std::cout << "======================================================================" << std::endl;

  std::vector<ChannelSpec> matrix = {
    {4,  6,  "Feeder 1", "Municipal Street Lights", "ZMCT103C",   1000.0,  8.0, 50.0, 0.01},
    {4,  6,  "Feeder 1*", "Municipal Street (Max)", "ZMCT103C",   1000.0, 10.0, 50.0, 0.01},
    {7,  9,  "Feeder 2", "Agricultural Pump 1",    "SCT-013-000", 2000.0, 28.0, 30.0, 0.01},
    {10, 12, "Feeder 3", "Agricultural Pump 2",    "SCT-013-000", 2000.0, 28.0, 30.0, 0.01},
    {13, 15, "Feeder 4", "Heavy Distribution Feeder", "SCT-013-000", 2000.0, 45.0, 27.0, 0.01},
    {16, 18, "Feeder 5", "Commercial Feeder",      "SCT-013-000", 2000.0, 45.0, 27.0, 0.01},
    {19, 21, "Feeder 6", "Auxiliary / Spare Feeder", "SCT-013-000", 2000.0, 30.0, 30.0, 0.01}
  };

  const double CLAMP_THRESHOLD_NOM_V = 0.700; // Nominally cited +/- 0.70V antiparallel diode
  const double DIODE_KNEE_25C_V      = 0.550; // Silicon diode partial conduction begins
  const double DIODE_KNEE_55C_V      = 0.490; // Diode knee at +55C (outdoor pole enclosure, -2mV/C)
  const double ADC_DIFF_FSR_V        = 1.200; // TI ADS131M08 full scale range (+/- 1.20V)

  std::cout << "\n--- 1. PROTECTION CLAMP VS MEASUREMENT RANGE CONSTRAINTS ---" << std::endl;
  std::cout << "Canonical Channel Mapping : Ch 01-03 = Common Bus Voltage (R, Y, B)" << std::endl;
  std::cout << "                          : Ch 04-21 = Feeder Currents (Feeders 1 to 6, R, Y, B)" << std::endl;
  std::cout << "Stage-1 Antiparallel Diode Clamp Threshold : +/- " << CLAMP_THRESHOLD_NOM_V << " V" << std::endl;
  std::cout << "Silicon Diode Conduction Knee (+25 C)      : " << DIODE_KNEE_25C_V << " V (Leakage > 10 uA)" << std::endl;
  std::cout << "Silicon Diode Conduction Knee (+55 C)      : " << DIODE_KNEE_55C_V << " V (Enclosure temperature)" << std::endl;
  std::cout << "ADS131M08 True Differential Linear Span    : +/- " << ADC_DIFF_FSR_V << " V" << std::endl;

  std::cout << "\n--- 2. DETAILED 18-CHANNEL SENSOR & BURDEN VOLTAGE MATRIX ---" << std::endl;
  std::cout << std::left << std::setw(8)  << "Ch #"
            << std::setw(12) << "Feeder"
            << std::setw(13) << "CT Model"
            << std::setw(9)  << "Ipri(A)"
            << std::setw(10) << "Isec(mA)"
            << std::setw(8)  << "Rb(Ohm)"
            << std::setw(11) << "Vpk(Nom)"
            << std::setw(11) << "Vpk(Tol+)"
            << std::setw(18) << "Clamp Status"
            << std::setw(10) << "Distortion" << std::endl;
  std::cout << std::string(100, '-') << std::endl;

  int totalClippedChannels = 0;
  int totalDistortedChannels = 0;

  for (const auto& item : matrix) {
    double iSecRmsMa = (item.primaryCurrentRms / item.turnsRatio) * 1000.0;
    double iSecPkMa  = iSecRmsMa * std::sqrt(2.0);

    double vBurdenNomV = (iSecPkMa / 1000.0) * item.burdenResistance;
    double vBurdenTolV = (iSecPkMa / 1000.0) * (item.burdenResistance * (1.0 + item.burdenTolerance));

    std::string clampStatus;
    std::string distortion;

    if (vBurdenNomV >= CLAMP_THRESHOLD_NOM_V) {
      clampStatus = "SEVERE CLIPPING";
      distortion  = "YES (CRITICAL)";
      totalClippedChannels += (item.chEnd - item.chStart + 1);
    } else if (vBurdenNomV >= DIODE_KNEE_55C_V) {
      clampStatus = "PARTIAL CONDUCTION";
      distortion  = "YES (TEMP DRIFT)";
      totalDistortedChannels += (item.chEnd - item.chStart + 1);
    } else {
      clampStatus = "SAFE (NO CONDUCTION)";
      distortion  = "NO";
    }

    std::string chRange = (item.chStart == item.chEnd) ? 
                          std::to_string(item.chStart) : 
                          (std::to_string(item.chStart) + "-" + std::to_string(item.chEnd));

    std::cout << std::left << std::setw(8)  << chRange
              << std::setw(12) << item.feederName
              << std::setw(13) << item.ctModel
              << std::setw(9)  << std::fixed << std::setprecision(1) << item.primaryCurrentRms
              << std::setw(10) << std::setprecision(2) << iSecRmsMa
              << std::setw(8)  << std::setprecision(1) << item.burdenResistance
              << std::setw(11) << std::setprecision(4) << vBurdenNomV
              << std::setw(11) << std::setprecision(4) << vBurdenTolV
              << std::setw(18) << clampStatus
              << std::setw(10) << distortion << std::endl;
  }

  std::cout << "\n--- 3. AUDIT FINDINGS ACROSS ALL 18 CURRENT CHANNELS ---" << std::endl;
  std::cout << "[DEFECT IDENTIFIED]: Feeder 4 (Ch 13-15) & Feeder 5 (Ch 16-18) produce 0.8591V peak across 27-ohm burden." << std::endl;
  std::cout << "  * This EXCEEDS the stated +/- 0.70V antiparallel clamp threshold by +0.1591V (+22.7%)!" << std::endl;
  std::cout << "  * At +1% burden tolerance, peak voltage reaches 0.8677V." << std::endl;
  std::cout << "  * The clamp conducts heavily on EVERY AC peak, flattening sinusoidal waveforms." << std::endl;
  std::cout << "  * Destroys True-RMS accuracy, introduces 3rd/5th harmonic distortion, corrupts power factor." << std::endl;
  std::cout << "[DEFECT IDENTIFIED]: Feeder 1 at 10A max primary (Ch 4-6) produces 0.7071V peak, also exceeding 0.70V clamp." << std::endl;
  std::cout << "[DEFECT IDENTIFIED]: Feeders 2, 3, 6 (Ch 7-12, 19-21) produce 0.594V to 0.636V peak, entering diode conduction at +55 C ambient." << std::endl;

  std::cout << "\n--- 4. NEXPERIA BAV199 TOPOLOGY, PINOUT & PACKAGE COUNT ---" << std::endl;
  std::cout << "1. Internal Two-Series-Diode Topology (SOT-23 / TO-236AB):" << std::endl;
  std::cout << "   - Pin 1: Anode Diode 1 (A1)" << std::endl;
  std::cout << "   - Pin 2: Cathode Diode 2 (K2)" << std::endl;
  std::cout << "   - Pin 3: Common Node: Cathode Diode 1 connected to Anode Diode 2 (K1/A2)" << std::endl;
  std::cout << "   - Internal Structure: Exactly TWO DIODES IN SERIES between Pin 1 and Pin 2." << std::endl;
  std::cout << "2. Symmetrical Bidirectional Clamping Implementation:" << std::endl;
  std::cout << "   - Since a single BAV199 package conducts only from Pin 1 to Pin 2, bidirectional" << std::endl;
  std::cout << "     clamping across differential burden terminals (AINP, AINM) REQUIRES TWO PACKAGES:" << std::endl;
  std::cout << "     * Package 1 (Forward clamp): Pin 1 -> AINP, Pin 2 -> AINM (Pin 3 NC)" << std::endl;
  std::cout << "     * Package 2 (Reverse clamp): Pin 1 -> AINM, Pin 2 -> AINP (Pin 3 NC)" << std::endl;
  std::cout << "3. Exact Package Count on Board:" << std::endl;
  std::cout << "   - 18 Current Channels x 2 packages/channel = 36 BAV199 SOT-23 Packages (72 diodes)." << std::endl;
  std::cout << "   - 3 Voltage Channels (if VT secondary clamped) x 2 packages = 6 BAV199 Packages." << std::endl;
  std::cout << "   - Total Active Package Count (21 channels): 42 BAV199 Packages (84 diodes)." << std::endl;

  std::cout << "\n--- 5. GUARANTEED DATASHEET CLAMP VOLTAGE & THERMAL DERATING ---" << std::endl;
  std::cout << "Guaranteed Nexperia Datasheet VF (Rev 8 per single diode):" << std::endl;
  std::cout << "  * VF <= 0.90V at IF = 1.0 mA" << std::endl;
  std::cout << "  * VF <= 1.00V at IF = 10.0 mA" << std::endl;
  std::cout << "  * VF <= 1.10V at IF = 50.0 mA" << std::endl;
  std::cout << "Guaranteed Clamp Voltage for Two Diodes in Series:" << std::endl;
  std::cout << "  * At IF = 10 mA (Fault current): Vclamp_max = 2 * 1.00V = 2.00 V (Guaranteed Max!)" << std::endl;
  std::cout << "  * At IF = 1 mA: Vclamp_max = 2 * 0.90V = 1.80 V" << std::endl;
  std::cout << "  * Nominal Typical Clamp (+25 C): 2 * 0.70V = +/- 1.40 V" << std::endl;
  std::cout << "Temperature Sensitivity (-2.0 mV/C per junction -> -4.0 mV/C for series pair):" << std::endl;
  std::cout << "  * Conduction knee at +25 C : ~1.10 V (Leakage < 5 pA)" << std::endl;
  std::cout << "  * Conduction knee at +55 C : 1.10V - (30 * 0.004V) = 0.98 V" << std::endl;
  std::cout << "  * Conduction knee at +70 C : 1.10V - (45 * 0.004V) = 0.92 V" << std::endl;

  std::cout << "\n--- 6. RESOLUTION OF CONTRADICTION: +/-1.40V CLAMP VS ADS131M08 +/-1.20V FSR ---" << std::endl;
  std::cout << "[CONTRADICTION IDENTIFIED & RESOLVED]:" << std::endl;
  std::cout << "  * Prior report stated: 'Dual-series clamp (+/-1.40V) fits inside ADS131M08 +/-1.20V FSR'." << std::endl;
  std::cout << "  * Contradiction: 1.40V is GREATER than 1.20V! A 1.40V clamp cannot be inside 1.20V FSR." << std::endl;
  std::cout << "  * Resolution:" << std::endl;
  std::cout << "    1. Linear Metrology Span = ADS131M08 differential Full Scale Range = +/- 1.20 V (Gain = 1)." << std::endl;
  std::cout << "       All nominal AC peak voltages must be <= 1.20 V to avoid digital code clipping." << std::endl;
  std::cout << "    2. Overvoltage Protection Boundary = Dual-Series Clamp (+/- 1.40 V nom, <= 2.00 V max at 10mA)." << std::endl;
  std::cout << "       The clamp is designed to protect silicon from destructive open-secondary CT spikes." << std::endl;
  std::cout << "       It sits ABOVE linear FSR (+/-1.20V), but SAFELY BELOW silicon damage limits" << std::endl;
  std::cout << "       (ADS131M08 single-ended absolute max -1.60V to +3.60V, diff abs max ~5.2V)." << std::endl;
  std::cout << "    3. Thermal Margin Defect on Feeders 4 & 5:" << std::endl;
  std::cout << "       At +70 C ambient, the 2-series knee drops to 0.92V." << std::endl;
  std::cout << "       Feeders 4 & 5 produce 0.8677V peak (+1% tolerance)." << std::endl;
  std::cout << "       Margin to knee is ONLY: 0.92V - 0.868V = 52 mV!" << std::endl;
  std::cout << "       This 52 mV margin risks sub-threshold non-linear leakage across the burden resistor." << std::endl;
  std::cout << "    4. Mandatory Remediation:" << std::endl;
  std::cout << "       Burden resistors on Feeders 4 & 5 must be reduced from 27 ohm to 20 ohm" << std::endl;
  std::cout << "       (reducing peak voltage from 0.859V to 0.636V, restoring 284 mV margin at +70 C)." << std::endl;

  std::cout << "\n>>> DISPOSITION: GATE B REMAINS STRICTLY BLOCKED!" << std::endl;
  std::cout << "    AFE design and CT BOM remain unapproved until a complete, fully tested AFE daughterboard" << std::endl;
  std::cout << "    PCB with safe burden sizing and verified thermal margins is fabricated and bench-tested." << std::endl;

  return 0;
}
