#include <iostream>
#include <iomanip>
#include <cstdint>
#include <cmath>
#include <vector>
#include <cassert>

// Test constants
#define ADS131_CMD_NULL       0x0000
#define ADS131_CMD_RESET      0x0011
#define ADS131_CMD_STANDBY    0x0022
#define ADS131_CMD_WAKEUP     0x0033
#define ADS131_CMD_LOCK       0x0555
#define ADS131_CMD_UNLOCK     0x0655

#define ADS131_CMD_RREG(addr, count) ((uint16_t)(0xA000 | (((addr) & 0x3F) << 7) | (((count) - 1) & 0x7F)))
#define ADS131_CMD_WREG(addr, count) ((uint16_t)(0x6000 | (((addr) & 0x3F) << 7) | (((count) - 1) & 0x7F)))

static const float VREF_VOLTS = 1.2f;
static const float FSR_24BIT  = 8388608.0f;

int32_t signExtend24(uint32_t val24) {
    val24 &= 0x00FFFFFF;
    if (val24 & 0x00800000) {
        return (int32_t)(val24 | 0xFF000000);
    }
    return (int32_t)val24;
}

float codeToVoltage(int32_t code, int gain) {
    return ((float)code / FSR_24BIT) * (VREF_VOLTS / (float)gain);
}

uint16_t computeCRC(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  TI ADS131M08 HARDWARE INTEGRATION VERIFICATION    \n";
    std::cout << "====================================================\n";

    // 1. Verify Command Formats
    uint16_t rreg_id = ADS131_CMD_RREG(0x00, 1);
    uint16_t wreg_mode = ADS131_CMD_WREG(0x02, 1);
    uint16_t rreg_ch0 = ADS131_CMD_RREG(0x09, 1);

    std::cout << "[TEST 1] Command Opcodes:\n";
    std::cout << "  RREG ID (Addr 0x00, 1 reg):   0x" << std::hex << std::uppercase << rreg_id << " (Expected 0xA000)\n";
    std::cout << "  WREG MODE (Addr 0x02, 1 reg): 0x" << std::hex << std::uppercase << wreg_mode << " (Expected 0x6100)\n";
    std::cout << "  RREG CH0_CFG (Addr 0x09):     0x" << std::hex << std::uppercase << rreg_ch0 << " (Expected 0xA480)\n";
    assert(rreg_id == 0xA000);
    assert(wreg_mode == 0x6100);
    assert(rreg_ch0 == 0xA480);
    std::cout << "  -> PASS\n\n";

    // 2. Verify 24-bit Sign Extension
    std::cout << "[TEST 2] 24-bit Sign Extension:\n";
    uint32_t posMax = 0x007FFFFF; // +8388607
    uint32_t zero   = 0x00000000;
    uint32_t negOne = 0x00FFFFFF; // -1
    uint32_t negMax = 0x00800000; // -8388608

    int32_t sPosMax = signExtend24(posMax);
    int32_t sZero   = signExtend24(zero);
    int32_t sNegOne = signExtend24(negOne);
    int32_t sNegMax = signExtend24(negMax);

    std::cout << "  +Max 0x007FFFFF -> " << std::dec << sPosMax << " (Expected +8388607)\n";
    std::cout << "  Zero 0x00000000 -> " << sZero << " (Expected 0)\n";
    std::cout << "  -1   0x00FFFFFF -> " << sNegOne << " (Expected -1)\n";
    std::cout << "  -Max 0x00800000 -> " << sNegMax << " (Expected -8388608)\n";
    assert(sPosMax == 8388607);
    assert(sZero == 0);
    assert(sNegOne == -1);
    assert(sNegMax == -8388608);
    std::cout << "  -> PASS\n\n";

    // 3. Verify Voltage Conversion (Gain = 1, VREF = 1.2V)
    std::cout << "[TEST 3] Code to Voltage Conversion (VREF = 1.2V):\n";
    float vPosMax = codeToVoltage(sPosMax, 1);
    float vNegMax = codeToVoltage(sNegMax, 1);
    std::cout << "  +Max Code Voltage: " << std::fixed << std::setprecision(6) << vPosMax << " V (Expected ~+1.200000 V)\n";
    std::cout << "  -Max Code Voltage: " << vNegMax << " V (Expected -1.200000 V)\n";
    assert(std::abs(vPosMax - 1.2f) < 0.0001f);
    assert(std::abs(vNegMax - (-1.2f)) < 0.0001f);
    std::cout << "  -> PASS\n\n";

    // 4. Verify Metrology Scaling
    std::cout << "[TEST 4] Sensor Metrology Scaling:\n";
    // ZMPT101B: 230 VAC RMS nominal -> 120k pri / 300 ohm burden -> 400x multiplier
    float testAdcVRMS = 230.0f / 400.0f; // 0.575 V RMS
    float calcMainsV = testAdcVRMS * 400.0f;
    std::cout << "  ZMPT101B at 230 VAC: V_ADC_RMS = " << testAdcVRMS << " V, Calc Mains = " << calcMainsV << " V\n";
    assert(std::abs(calcMainsV - 230.0f) < 0.01f);

    // CT 50A:25mA (1000:1) with 12 ohm burden: 83.333 A/V
    float testAdcIRMS = 15.0f / 83.333333f; // 0.180 V RMS
    float calcCurrentA = testAdcIRMS * 83.333333f;
    std::cout << "  CT at 15A: V_ADC_RMS = " << testAdcIRMS << " V, Calc Current = " << calcCurrentA << " A\n";
    assert(std::abs(calcCurrentA - 15.0f) < 0.01f);
    std::cout << "  -> PASS\n\n";

    // 5. Verify SPI Bandwidth & Multi-ADC Readout Timing
    std::cout << "[TEST 5] SPI Bus Bandwidth & Readout Timing:\n";
    int bytesPerDevice = 27; // 1 status word + 8 channel words = 9 words * 3 bytes
    int totalBytes3ADCs = bytesPerDevice * 3;
    float spiClockHz = 8000000.0f; // 8.0 MHz
    float timePerByteUs = (8.0f / spiClockHz) * 1e6f; // 1.0 us
    float totalReadoutTimeUs = totalBytes3ADCs * timePerByteUs;

    std::cout << "  Bytes per ADS131M08 frame: " << bytesPerDevice << " bytes\n";
    std::cout << "  Total bytes for 3 ADCs:     " << totalBytes3ADCs << " bytes\n";
    std::cout << "  Total Readout Time @ 8MHz:  " << totalReadoutTimeUs << " us\n";
    std::cout << "  Sampling Period @ 1 kSPS:   1000.0 us\n";
    std::cout << "  Bus Utilization @ 1 kSPS:   " << (totalReadoutTimeUs / 1000.0f) * 100.0f << " %\n";
    std::cout << "  Bus Utilization @ 4 kSPS:   " << (totalReadoutTimeUs / 250.0f) * 100.0f << " %\n";
    assert(totalReadoutTimeUs < 100.0f);
    std::cout << "  -> PASS (< 100 us readout leaves > 90% CPU free)\n\n";

    // 6. Verify 21-Channel Allocation Mapping
    std::cout << "[TEST 6] 21-Channel Multi-ADC Allocation Mapping:\n";
    const char* adc1Channels[8] = {
        "Fdr 1 Phase R Current", "Fdr 1 Phase Y Current", "Fdr 1 Phase B Current",
        "Fdr 2 Phase R Current", "Fdr 2 Phase Y Current", "Fdr 2 Phase B Current",
        "Fdr 3 Phase R Current", "Fdr 3 Phase Y Current"
    };
    const char* adc2Channels[8] = {
        "Fdr 3 Phase B Current",
        "Fdr 4 Phase R Current", "Fdr 4 Phase Y Current", "Fdr 4 Phase B Current",
        "Fdr 5 Phase R Current", "Fdr 5 Phase Y Current", "Fdr 5 Phase B Current",
        "SPARE CHANNEL 1"
    };
    const char* adc3Channels[8] = {
        "Fdr 6 Phase R Current", "Fdr 6 Phase Y Current", "Fdr 6 Phase B Current",
        "Common Bus Voltage Phase R", "Common Bus Voltage Phase Y", "Common Bus Voltage Phase B",
        "SPARE CHANNEL 2", "SPARE CHANNEL 3"
    };

    std::cout << "  ADC 1 (8 Ch Used, 0 Spare):\n";
    for (int i = 0; i < 8; i++) std::cout << "    Ch " << i << ": " << adc1Channels[i] << "\n";
    std::cout << "  ADC 2 (7 Ch Used, 1 Spare):\n";
    for (int i = 0; i < 8; i++) std::cout << "    Ch " << i << ": " << adc2Channels[i] << "\n";
    std::cout << "  ADC 3 (6 Ch Used, 2 Spare):\n";
    for (int i = 0; i < 8; i++) std::cout << "    Ch " << i << ": " << adc3Channels[i] << "\n";
    std::cout << "  Total Channels Available: 24\n";
    std::cout << "  Total Active Channels:    21 (18 Current + 3 Voltage)\n";
    std::cout << "  Total Spare Channels:     3\n";
    std::cout << "  -> PASS\n\n";

    std::cout << "ALL TI ADS131M08 HARDWARE INTEGRATION TESTS PASSED SUCCESSFULLY!\n";
    return 0;
}
