/*
 * =====================================================================
 *  ADS131M08DRIVER.H — TEXAS INSTRUMENTS ADS131M08 24-BIT ADC DRIVER
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 *  Three-ADC Synchronized Metrology Subsystem (24 Channels Total, 21 Active)
 *  Platform: ESP32 / ESP32-S3 (SPI Mode 1, Simultaneous Sampling)
 * =====================================================================
 */

#ifndef ADS131M08_DRIVER_H
#define ADS131M08_DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include <stdint.h>

// ─────────────────────────────────────────────────────────────
//  ADS131M08 HARDWARE CONSTANTS & REGISTER MAP
// ─────────────────────────────────────────────────────────────
#define ADS131M08_CHANNELS_PER_CHIP  8
#define ADS131M08_NUM_CHIPS          3
#define ADS131M08_TOTAL_CHANNELS     (ADS131M08_CHANNELS_PER_CHIP * ADS131M08_NUM_CHIPS) // 24
#define ADS131M08_ACTIVE_CHANNELS    21  // 3 Voltage + 18 Current

// ADS131M08 Commands (16-bit commands)
#define ADS_CMD_NULL                 0x0000
#define ADS_CMD_RESET                0x0011
#define ADS_CMD_STANDBY              0x0022
#define ADS_CMD_WAKEUP               0x0033
#define ADS_CMD_LOCK                 0x0555
#define ADS_CMD_UNLOCK               0x0655
#define ADS_CMD_RREG                 0xA000
#define ADS_CMD_WREG                 0x6000

// ADS131M08 Register Addresses
#define ADS_REG_ID                   0x00
#define ADS_REG_STATUS               0x01
#define ADS_REG_MODE                 0x02
#define ADS_REG_CLOCK                0x03
#define ADS_REG_GAIN1                0x04
#define ADS_REG_GAIN2                0x05
#define ADS_REG_CFG                  0x06
#define ADS_REG_THRSHLD_MSB          0x07
#define ADS_REG_THRSHLD_LSB          0x08
#define ADS_REG_CH0_CFG              0x09
#define ADS_REG_CH1_CFG              0x0A
#define ADS_REG_CH2_CFG              0x0B
#define ADS_REG_CH3_CFG              0x0C
#define ADS_REG_CH4_CFG              0x0D
#define ADS_REG_CH5_CFG              0x0E
#define ADS_REG_CH6_CFG              0x0F
#define ADS_REG_CH7_CFG              0x10

// STATUS Register Bitmasks
#define ADS_STATUS_LOCK              (1 << 15)
#define ADS_STATUS_F_RESYNC          (1 << 14)
#define ADS_STATUS_F_WDT             (1 << 10)
#define ADS_STATUS_FAULT             (1 << 8)
#define ADS_STATUS_RESET             (1 << 0)

// 24-bit Clipping / Saturation Thresholds
#define ADS24_MAX_POSITIVE           8388607L    // 0x7FFFFF
#define ADS24_MAX_NEGATIVE          -8388608L    // 0x800000
#define ADS24_SATURATION_MARGIN      8380000L    // Saturation threshold (> 99.9% FS)

// Reference voltage & full scale
#define ADS_VREF_INTERNAL            1.200f      // 1.2V nominal internal reference
#define ADS_FULL_SCALE_VOLTAGE       1.200f

// ─────────────────────────────────────────────────────────────
//  RAW SYNCHRONIZED ADC FRAME STRUCTURE
// ─────────────────────────────────────────────────────────────
struct AdcRawSampleFrame {
  uint32_t timestampUs;                          // Microsecond timestamp of DRDY edge
  uint32_t sampleIndex;                          // Monotonically increasing sample sequence
  int32_t  rawCounts[ADS131M08_TOTAL_CHANNELS];  // 24-bit sign-extended counts
  uint16_t chipStatus[ADS131M08_NUM_CHIPS];      // Status word per chip
  bool     chipOnline[ADS131M08_NUM_CHIPS];      // SPI response validity
  bool     saturated[ADS131M08_TOTAL_CHANNELS];  // Clipping detected flag
  bool     valid;                                // Entire frame validity
};

// ─────────────────────────────────────────────────────────────
//  HARDWARE PIN CONFIGURATION STRUCTURE
// ─────────────────────────────────────────────────────────────
struct AdcPinConfig {
  int8_t csPins[ADS131M08_NUM_CHIPS];  // Chip Select pins for ADC1, ADC2, ADC3
  int8_t syncPin;                       // Common /SYNC pulse pin
  int8_t drdyPin;                       // Common /DRDY interrupt/poll pin
  int8_t sckPin;                        // SPI SCK
  int8_t misoPin;                       // SPI MISO
  int8_t mosiPin;                       // SPI MOSI
  uint32_t spiSpeedHz;                  // SPI clock speed (typical 4-8 MHz)
};

// ─────────────────────────────────────────────────────────────
//  ADS131M08 MULTI-CHIP DRIVER CLASS
// ─────────────────────────────────────────────────────────────
class ADS131M08Driver {
public:
  ADS131M08Driver();

  // Lifecycle
  bool init(const AdcPinConfig &pins);
  void hardwareResetAndSync();
  void sendSyncPulse();

  // Configuration
  bool writeRegister(uint8_t chipIdx, uint8_t regAddr, uint16_t value);
  uint16_t readRegister(uint8_t chipIdx, uint8_t regAddr);
  bool verifyChipId(uint8_t chipIdx);

  // Sampling
  bool isDataReady();
  bool readSynchronizedFrame(AdcRawSampleFrame &frame);

  // Status & Health
  bool isChipOnline(uint8_t chipIdx) const;
  bool isHardwareAvailable() const;
  uint32_t getFrameCount() const;
  uint32_t getCommErrorCount(uint8_t chipIdx) const;

  // Bench Simulation & Injection
  void setSimulationMode(bool enable);
  bool isSimulationMode() const;
  void injectSimulatedSample(float gridFreqHz = 50.0f, float mainsVrms = 230.0f, float feeder1Irms = 12.5f);

private:
  AdcPinConfig config;
  SPISettings  spiSettings;
  bool         initialized;
  bool         hardwarePresent;
  bool         simulationMode;
  uint32_t     sampleCounter;
  uint32_t     commErrors[ADS131M08_NUM_CHIPS];
  bool         chipDetected[ADS131M08_NUM_CHIPS];

  // Internal SPI primitives
  void selectChip(uint8_t chipIdx);
  void deselectChip(uint8_t chipIdx);
  bool readChipFrame(uint8_t chipIdx, uint16_t &statusWord, int32_t rawChannels[ADS131M08_CHANNELS_PER_CHIP]);
  static int32_t parse24BitSigned(uint8_t b0, uint8_t b1, uint8_t b2);
};

extern ADS131M08Driver adcDriver;

#endif // ADS131M08_DRIVER_H
