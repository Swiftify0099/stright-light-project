/**
 * @file test_task1_sx1278_airtime_correction.cpp
 * @brief Fourth-Pass Independent Verification: SX1278 LoRa Time-on-Air Mathematical Engine
 * 
 * Verifies the exact Semtech SX1278 airtime formulation from datasheet DS.SX1276/7/8/9.W.REV.7:
 * - Proves the payload-symbol denominator is exactly 4*(SF - 2*DE) = 28 for SF7, DE=0.
 * - Reconciles application FIFO payload length (PL=121 bytes for telemetry, CRC=1 hardware).
 * - Tests all 8 operational packet types against hand-calculated expected values.
 * - Compares SF7 (low latency) against SF10 (long range).
 */

#include "ArduinoMock.h"
#include <cmath>
#include <cassert>
#include <iomanip>

struct PacketToaResult {
  const char* name;
  int plBytes;
  int sf;
  double bwKhz;
  int cr;
  int preamble;
  bool hwCrc;
  bool explicitHeader;
  int denominator;
  int nPayloadSymbols;
  double toaPreambleMs;
  double toaPayloadMs;
  double toaTotalMs;
};

PacketToaResult calculateSemtechLoRaToa(const char* name, int plBytes, int sf, double bwKhz, int cr, int preamble, bool hwCrc, bool explicitHeader) {
  PacketToaResult res;
  res.name = name;
  res.plBytes = plBytes;
  res.sf = sf;
  res.bwKhz = bwKhz;
  res.cr = cr;
  res.preamble = preamble;
  res.hwCrc = hwCrc;
  res.explicitHeader = explicitHeader;

  double bwHz = bwKhz * 1000.0;
  double tSymMs = (pow(2.0, sf) / bwHz) * 1000.0;
  res.toaPreambleMs = (preamble + 4.25) * tSymMs;

  int de = (tSymMs > 16.0) ? 1 : 0; // Low Data Rate Optimization
  int ih = explicitHeader ? 0 : 1;
  int crc = hwCrc ? 1 : 0;

  res.denominator = 4 * (sf - 2 * de);

  double num = 8.0 * plBytes - 4.0 * sf + 28.0 + 16.0 * crc - 20.0 * ih;
  double ceilVal = ceil(num / (double)res.denominator);
  if (ceilVal < 0.0) ceilVal = 0.0;

  res.nPayloadSymbols = 8 + (int)(ceilVal * (cr + 4));
  res.toaPayloadMs = res.nPayloadSymbols * tSymMs;
  res.toaTotalMs = res.toaPreambleMs + res.toaPayloadMs;

  return res;
}

int main() {
  std::cout << "======================================================================" << std::endl;
  std::cout << " TASK 1: SX1278 LORA AIRTIME ENGINE INDEPENDENT MATHEMATICAL AUDIT" << std::endl;
  std::cout << "======================================================================" << std::endl;

  // 1. Audit Denominator for SF7, DE=0
  const int sf7 = 7;
  const int de0 = 0;
  const int expectedDenomSF7 = 4 * (sf7 - 2 * de0);
  std::cout << "\n--- 1. PAYLOAD SYMBOL DENOMINATOR AUDIT ---" << std::endl;
  std::cout << "Formula: Denominator = 4 * (SF - 2 * DE)" << std::endl;
  std::cout << "For SF=7, DE=0: 4 * (7 - 2 * 0) = " << expectedDenomSF7 << std::endl;
  std::cout << "Third-Pass Erroneous Denominator : 20 (REFUTED)" << std::endl;
  std::cout << "Fourth-Pass Correct Denominator  : 28 (PROVEN MATHEMATICALLY)" << std::endl;
  assert(expectedDenomSF7 == 28);

  // 2. FIFO Length Reconcilation
  // Application Frame: Header(22B) + TelemetryPayload(83B) + AuthTag(16B) = 121 Bytes
  // When SX1278 hardware CRC is enabled (RegModemConfig2 bit 2), the 2-byte CRC is appended
  // automatically by silicon during RF modulation and verified by receiver hardware.
  // It is NOT placed in the transceiver FIFO. Therefore PL = 121, CRC = 1.
  std::cout << "\n--- 2. PACKET FIFO LENGTH & HARDWARE CRC RECONCILIATION ---" << std::endl;
  std::cout << "Application Header      : 22 bytes" << std::endl;
  std::cout << "Telemetry Payload       : 83 bytes" << std::endl;
  std::cout << "HMAC / GCM Auth Tag     : 16 bytes" << std::endl;
  std::cout << "Total Bytes to FIFO (PL): 121 bytes" << std::endl;
  std::cout << "Hardware CRC Setting    : CRC = 1 (2 bytes appended by SX1278 silicon)" << std::endl;
  std::cout << "Third-Pass double count : 123 bytes with CRC=1 (CORRECTED to PL=121, CRC=1)" << std::endl;

  // 3. Recalculate all 8 operational packet types under SF=7, BW=125kHz, CR=4/5 (CR=1), Preamble=8
  std::cout << "\n--- 3. SX1278 TIME-ON-AIR FOR ALL 8 PACKET TYPES (SF7, BW125kHz, CR4/5) ---" << std::endl;
  std::cout << std::left << std::setw(22) << "Packet Type" 
            << std::setw(10) << "PL (B)" 
            << std::setw(10) << "Denom" 
            << std::setw(12) << "Symbols" 
            << std::setw(16) << "Preamble(ms)" 
            << std::setw(16) << "Payload(ms)" 
            << std::setw(16) << "Total ToA(ms)" << std::endl;
  std::cout << std::string(102, '-') << std::endl;

  // Packet specifications
  struct PktDef { const char* name; int pl; double expectedToa; };
  PktDef packets[] = {
    {"Telemetry (Full 21ch)", 121, 205.056},
    {"Feeder Command (CMD)",    52, 102.656},
    {"Command ACK (CMD_ACK)",   46,  92.416},
    {"Fault Alarm (ALERT)",     54, 102.656},
    {"Sync Beacon (BEACON)",    30,  71.936},
    {"Registration (JOIN_REQ)", 54, 102.656},
    {"Registration (JOIN_ACC)", 62, 118.016},
    {"Routing Update (RREQ)",   47,  92.416},
    {"Discovery Probe (HELLO)", 30,  71.936}
  };

  for (const auto& p : packets) {
    PacketToaResult r = calculateSemtechLoRaToa(p.name, p.pl, 7, 125.0, 1, 8, true, true);
    std::cout << std::left << std::setw(22) << r.name 
              << std::setw(10) << r.plBytes 
              << std::setw(10) << r.denominator 
              << std::setw(12) << r.nPayloadSymbols 
              << std::setw(16) << std::fixed << std::setprecision(3) << r.toaPreambleMs 
              << std::setw(16) << r.toaPayloadMs 
              << std::setw(16) << r.toaTotalMs << std::endl;
    
    assert(r.denominator == 28);
    assert(std::fabs(r.toaTotalMs - p.expectedToa) < 0.001);
  }

  // 4. Comparison with SF=10 (BuildConfig.h long-range profile)
  std::cout << "\n--- 4. COMPARISON: SF7 (Low Airtime) vs SF10 (Long Range Profile) ---" << std::endl;
  PacketToaResult telSF7  = calculateSemtechLoRaToa("Telemetry SF7",  121,  7, 125.0, 1, 8, true, true);
  PacketToaResult telSF10 = calculateSemtechLoRaToa("Telemetry SF10", 121, 10, 125.0, 1, 8, true, true);

  std::cout << "SF7  Telemetry (PL=121B): Tsym=1.024ms, Denom=" << telSF7.denominator  << ", Symbols=" << telSF7.nPayloadSymbols  << " -> ToA = " << telSF7.toaTotalMs  << " ms (" << telSF7.toaTotalMs/1000.0 << " s)" << std::endl;
  std::cout << "SF10 Telemetry (PL=121B): Tsym=8.192ms, Denom=" << telSF10.denominator << ", Symbols=" << telSF10.nPayloadSymbols << " -> ToA = " << telSF10.toaTotalMs << " ms (" << telSF10.toaTotalMs/1000.0 << " s)" << std::endl;

  assert(std::fabs(telSF7.toaTotalMs - 205.056) < 0.001);
  assert(std::fabs(telSF10.toaTotalMs - 1189.888) < 0.001);

  std::cout << "\n>>> [PASS] TASK 1: Semtech SX1278 Airtime Engine Verified with 100% Mathematical Precision." << std::endl;
  return 0;
}
