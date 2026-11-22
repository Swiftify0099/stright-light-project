/*
 * =====================================================================
 *  ADS131M08.H — TEXAS INSTRUMENTS ADS131M08 8-CHANNEL 24-BIT DELTA-SIGMA
 *  SIMULTANEOUS SAMPLING ADC HARDWARE DRIVER (ESP32 / ARDUINO)
 *  
 *  Author: Senior Industrial Measurement & Hardware Engineer
 *  Platform: ESP32 Industrial Measurement System
 * =====================================================================
 */

#ifndef ADS131M08_H
#define ADS131M08_H

#include <Arduino.h>
#include <SPI.h>

// ─────────────────────────────────────────────────────────────
//  ADS131M08 COMMAND OPCODES (16-bit MSB Aligned)
// ─────────────────────────────────────────────────────────────
#define ADS131_CMD_NULL       0x0000
#define ADS131_CMD_RESET      0x0011
#define ADS131_CMD_STANDBY    0x0022
#define ADS131_CMD_WAKEUP     0x0033
#define ADS131_CMD_LOCK       0x0555
#define ADS131_CMD_UNLOCK     0x0655

// RREG: 101a aaaa annn nnnn (a = 7-bit addr, n = count - 1)
#define ADS131_CMD_RREG(addr, count) ((uint16_t)(0xA000 | (((addr) & 0x3F) << 7) | (((count) - 1) & 0x7F)))
// WREG: 011a aaaa annn nnnn (a = 7-bit addr, n = count - 1)
#define ADS131_CMD_WREG(addr, count) ((uint16_t)(0x6000 | (((addr) & 0x3F) << 7) | (((count) - 1) & 0x7F)))

// ─────────────────────────────────────────────────────────────
//  ADS131M08 REGISTER ADDRESS MAP
// ─────────────────────────────────────────────────────────────
#define ADS131_REG_ID          0x00
#define ADS131_REG_STATUS      0x01
#define ADS131_REG_MODE        0x02
#define ADS131_REG_CLOCK       0x03
#define ADS131_REG_GAIN1       0x04
#define ADS131_REG_CFG         0x05
#define ADS131_REG_GAIN2       0x06
#define ADS131_REG_REGMAP_CRC  0x07
#define ADS131_REG_RESERVED    0x08

// Channel Registers (Base 0x09, 5 registers per channel)
#define ADS131_REG_CH_CFG(ch)      (0x09 + 5 * (ch))
#define ADS131_REG_CH_OCAL_MSB(ch) (0x0A + 5 * (ch))
#define ADS131_REG_CH_OCAL_LSB(ch) (0x0B + 5 * (ch))
#define ADS131_REG_CH_GCAL_MSB(ch) (0x0C + 5 * (ch))
#define ADS131_REG_CH_GCAL_LSB(ch) (0x0D + 5 * (ch))

// ─────────────────────────────────────────────────────────────
//  REGISTER BITFIELDS & CONSTANTS
// ─────────────────────────────────────────────────────────────
// MODE Register (0x02)
#define ADS131_MODE_REG_CRC_EN     (1 << 14)
#define ADS131_MODE_RX_CRC_EN      (1 << 13)
#define ADS131_MODE_CRC_CCITT      (1 << 12)
#define ADS131_MODE_RESET          (1 << 8)
#define ADS131_MODE_WLENGTH_16     (0 << 0)
#define ADS131_MODE_WLENGTH_24     (1 << 0) // Default
#define ADS131_MODE_WLENGTH_32_Z   (2 << 0)
#define ADS131_MODE_WLENGTH_32_S   (3 << 0)

// CLOCK Register (0x03)
#define ADS131_CLOCK_PWR_VLP       (0 << 4) // Very Low Power
#define ADS131_CLOCK_PWR_LP        (1 << 4) // Low Power
#define ADS131_CLOCK_PWR_HR        (2 << 4) // High Resolution (Default)

#define ADS131_CLOCK_OSR_128       (0 << 1)
#define ADS131_CLOCK_OSR_256       (1 << 1)
#define ADS131_CLOCK_OSR_512       (2 << 1)
#define ADS131_CLOCK_OSR_1024      (3 << 1)
#define ADS131_CLOCK_OSR_2048      (4 << 1)
#define ADS131_CLOCK_OSR_4096      (5 << 1)
#define ADS131_CLOCK_OSR_8192      (6 << 1)
#define ADS131_CLOCK_OSR_16384     (7 << 1)

// CFG Register (0x05)
#define ADS131_CFG_REF_PWR_INT     (1 << 0) // Enable Internal 1.2V Reference

// PGA Gains
enum ADS131_Gain {
  ADS131_GAIN_1   = 0,
  ADS131_GAIN_2   = 1,
  ADS131_GAIN_4   = 2,
  ADS131_GAIN_8   = 3,
  ADS131_GAIN_16  = 4,
  ADS131_GAIN_32  = 5,
  ADS131_GAIN_64  = 6,
  ADS131_GAIN_128 = 7
};

// Conversion Result Structure
struct ADS131_Frame {
  uint16_t status;
  int32_t  raw[8];       // 24-bit sign-extended differential samples
  float    voltage[8];   // Scaled differential voltage (V)
  uint16_t crc;
  bool     valid;
};

// ─────────────────────────────────────────────────────────────
//  ADS131M08 DRIVER CLASS
// ─────────────────────────────────────────────────────────────
class ADS131M08 {
public:
  ADS131M08();

  // Initialization & Hardware Setup
  bool init(uint8_t csPin, SPIClass *spiBus = &SPI, uint32_t spiSpeed = 8000000);
  void hardwareReset(uint8_t resetPin);
  bool softwareReset();

  // Low-level SPI Register Access
  uint16_t readRegister(uint8_t regAddr);
  bool     writeRegister(uint8_t regAddr, uint16_t value);

  // Configuration Methods
  bool setChannelGain(uint8_t channel, ADS131_Gain gain);
  bool setChannelPhase(uint8_t channel, uint16_t phaseWords);
  bool setChannelOffsetCal(uint8_t channel, int32_t offset24);
  bool setChannelGainCal(uint8_t channel, uint32_t gain24);
  bool configureClockAndPower(uint16_t pwrMode, uint16_t osr);

  // Simultaneous Readout
  bool readConversionFrame(ADS131_Frame &outFrame);

  // Diagnostic & Identification
  uint16_t getDeviceId();
  bool     isHealthy() const;
  uint8_t  getCsPin() const;

private:
  uint8_t   cs;
  SPIClass *spi;
  uint32_t  speed;
  bool      initialized;
  bool      healthy;
  ADS131_Gain channelGains[8];

  // Helper conversions
  static int32_t signExtend24(uint32_t val24);
  static float   codeToVoltage(int32_t code, ADS131_Gain gain);
  static uint16_t computeCRC(const uint8_t *data, size_t len);
};

#endif // ADS131M08_H
