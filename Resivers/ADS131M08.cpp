/*
 * =====================================================================
 *  ADS131M08.CPP — TEXAS INSTRUMENTS ADS131M08 8-CHANNEL 24-BIT DELTA-SIGMA
 *  SIMULTANEOUS SAMPLING ADC HARDWARE DRIVER (ESP32 / ARDUINO)
 * =====================================================================
 */

#include "ADS131M08.h"

// Full Scale differential reference voltage (internal 1.2V precision bandgap)
static const float VREF_VOLTS = 1.2f;
static const float FSR_24BIT  = 8388608.0f; // 2^23

ADS131M08::ADS131M08() :
  cs(255), spi(&SPI), speed(8000000), initialized(false), healthy(false)
{
  for (int i = 0; i < 8; i++) {
    channelGains[i] = ADS131_GAIN_1;
  }
}

int32_t ADS131M08::signExtend24(uint32_t val24) {
  val24 &= 0x00FFFFFF;
  if (val24 & 0x00800000) {
    return (int32_t)(val24 | 0xFF000000);
  }
  return (int32_t)val24;
}

float ADS131M08::codeToVoltage(int32_t code, ADS131_Gain gain) {
  float gainFactor = 1.0f;
  switch (gain) {
    case ADS131_GAIN_1:   gainFactor = 1.0f; break;
    case ADS131_GAIN_2:   gainFactor = 2.0f; break;
    case ADS131_GAIN_4:   gainFactor = 4.0f; break;
    case ADS131_GAIN_8:   gainFactor = 8.0f; break;
    case ADS131_GAIN_16:  gainFactor = 16.0f; break;
    case ADS131_GAIN_32:  gainFactor = 32.0f; break;
    case ADS131_GAIN_64:  gainFactor = 64.0f; break;
    case ADS131_GAIN_128: gainFactor = 128.0f; break;
    default: gainFactor = 1.0f; break;
  }
  return ((float)code / FSR_24BIT) * (VREF_VOLTS / gainFactor);
}

uint16_t ADS131M08::computeCRC(const uint8_t *data, size_t len) {
  // 16-bit CCITT polynomial: x^16 + x^12 + x^5 + 1 (0x1021), Init: 0xFFFF
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

bool ADS131M08::init(uint8_t csPin, SPIClass *spiBus, uint32_t spiSpeed) {
  cs = csPin;
  spi = (spiBus != nullptr) ? spiBus : &SPI;
  speed = spiSpeed;

  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);

  delay(5); // Stabilization delay after power up

  // Verify SPI interface and device presence by reading ID register
  softwareReset();
  delay(2);

  uint16_t devId = getDeviceId();
  // ADS131M08 Device ID field in upper byte should be 0x28 (or non-zero valid silicon ID)
  uint8_t devFamily = (uint8_t)((devId >> 8) & 0xFF);
  
  if (devFamily == 0x28 || (devId != 0x0000 && devId != 0xFFFF)) {
    initialized = true;
    healthy = true;
    Serial.printf("[ADS131] Device on CS %d detected successfully! ID=0x%04X (Family 0x%02X, Rev 0x%02X)\n",
                  cs, devId, devFamily, (uint8_t)(devId & 0xFF));
    return true;
  }

  Serial.printf("[ADS131][WARN] Device on CS %d failed ID verification. Read 0x%04X\n", cs, devId);
  initialized = false;
  healthy = false;
  return false;
}

void ADS131M08::hardwareReset(uint8_t resetPin) {
  pinMode(resetPin, OUTPUT);
  digitalWrite(resetPin, LOW);
  delayMicroseconds(500); // Exceeds minimum t_RSTW (250 µs)
  digitalWrite(resetPin, HIGH);
  delay(1); // Wait for digital filter acquisition
}

bool ADS131M08::softwareReset() {
  if (cs == 255) return false;

  spi->beginTransaction(SPISettings(speed, MSBFIRST, SPI_MODE1));
  digitalWrite(cs, LOW);

  // Send RESET Command: 0x0011 MSB-aligned in 24-bit word (0x00, 0x11, 0x00)
  spi->transfer(0x00);
  spi->transfer(0x11);
  spi->transfer(0x00);

  // Remaining words in frame (8 channel dummy words = 24 bytes)
  for (int i = 0; i < 24; i++) {
    spi->transfer(0x00);
  }

  digitalWrite(cs, HIGH);
  spi->endTransaction();

  delayMicroseconds(500);
  return true;
}

uint16_t ADS131M08::readRegister(uint8_t regAddr) {
  if (cs == 255) return 0;

  uint16_t rregCmd = ADS131_CMD_RREG(regAddr, 1);

  // 1. Send RREG Command in Frame 1
  spi->beginTransaction(SPISettings(speed, MSBFIRST, SPI_MODE1));
  digitalWrite(cs, LOW);

  spi->transfer((uint8_t)(rregCmd >> 8));
  spi->transfer((uint8_t)(rregCmd & 0xFF));
  spi->transfer(0x00); // 24-bit word padding

  // Complete Frame 1 with dummy bytes
  for (int i = 0; i < 24; i++) {
    spi->transfer(0x00);
  }

  digitalWrite(cs, HIGH);
  spi->endTransaction();

  delayMicroseconds(5);

  // 2. Clock out Register response in Frame 2 (sent in response word slot)
  spi->beginTransaction(SPISettings(speed, MSBFIRST, SPI_MODE1));
  digitalWrite(cs, LOW);

  uint8_t respMSB = spi->transfer(0x00);
  uint8_t respLSB = spi->transfer(0x00);
  spi->transfer(0x00); // discard padding

  for (int i = 0; i < 24; i++) {
    spi->transfer(0x00);
  }

  digitalWrite(cs, HIGH);
  spi->endTransaction();

  return ((uint16_t)respMSB << 8) | respLSB;
}

bool ADS131M08::writeRegister(uint8_t regAddr, uint16_t value) {
  if (cs == 255) return false;

  uint16_t wregCmd = ADS131_CMD_WREG(regAddr, 1);

  spi->beginTransaction(SPISettings(speed, MSBFIRST, SPI_MODE1));
  digitalWrite(cs, LOW);

  // Word 0: WREG command
  spi->transfer((uint8_t)(wregCmd >> 8));
  spi->transfer((uint8_t)(wregCmd & 0xFF));
  spi->transfer(0x00);

  // Word 1: Register Value
  spi->transfer((uint8_t)(value >> 8));
  spi->transfer((uint8_t)(value & 0xFF));
  spi->transfer(0x00);

  // Remaining 7 dummy words (21 bytes)
  for (int i = 0; i < 21; i++) {
    spi->transfer(0x00);
  }

  digitalWrite(cs, HIGH);
  spi->endTransaction();

  delayMicroseconds(10);
  return true;
}

bool ADS131M08::setChannelGain(uint8_t channel, ADS131_Gain gain) {
  if (channel >= 8) return false;
  channelGains[channel] = gain;

  uint8_t regAddr = (channel < 4) ? ADS131_REG_GAIN1 : ADS131_REG_GAIN2;
  uint16_t currentReg = readRegister(regAddr);
  uint8_t shift = (channel % 4) * 4;

  currentReg &= ~(0x0007 << shift);
  currentReg |= ((uint16_t)gain << shift);

  return writeRegister(regAddr, currentReg);
}

bool ADS131M08::setChannelPhase(uint8_t channel, uint16_t phaseWords) {
  if (channel >= 8) return false;
  uint8_t regAddr = ADS131_REG_CH_CFG(channel);
  uint16_t currentReg = readRegister(regAddr);

  // Bits 15:6 are PHASE[9:0]
  currentReg &= 0x003F; // preserve lower 6 bits (MUX and reserved)
  currentReg |= ((phaseWords & 0x03FF) << 6);

  return writeRegister(regAddr, currentReg);
}

bool ADS131M08::setChannelOffsetCal(uint8_t channel, int32_t offset24) {
  if (channel >= 8) return false;
  uint16_t msb = (uint16_t)((offset24 >> 8) & 0xFFFF);
  uint16_t lsb = (uint16_t)((offset24 & 0xFF) << 8);

  bool ok1 = writeRegister(ADS131_REG_CH_OCAL_MSB(channel), msb);
  bool ok2 = writeRegister(ADS131_REG_CH_OCAL_LSB(channel), lsb);
  return (ok1 && ok2);
}

bool ADS131M08::setChannelGainCal(uint8_t channel, uint32_t gain24) {
  if (channel >= 8) return false;
  uint16_t msb = (uint16_t)((gain24 >> 8) & 0xFFFF);
  uint16_t lsb = (uint16_t)((gain24 & 0xFF) << 8);

  bool ok1 = writeRegister(ADS131_REG_CH_GCAL_MSB(channel), msb);
  bool ok2 = writeRegister(ADS131_REG_CH_GCAL_LSB(channel), lsb);
  return (ok1 && ok2);
}

bool ADS131M08::configureClockAndPower(uint16_t pwrMode, uint16_t osr) {
  uint16_t clockVal = (pwrMode & 0x0030) | (osr & 0x000E);
  return writeRegister(ADS131_REG_CLOCK, clockVal);
}

bool ADS131M08::readConversionFrame(ADS131_Frame &outFrame) {
  if (cs == 255) {
    outFrame.valid = false;
    return false;
  }

  spi->beginTransaction(SPISettings(speed, MSBFIRST, SPI_MODE1));
  digitalWrite(cs, LOW);

  // Frame structure in 24-bit mode:
  // Word 0: STATUS (3 bytes)
  // Word 1..8: CH0 to CH7 Data (8 * 3 = 24 bytes)
  // Total = 27 bytes (without CRC)
  uint8_t rxBuf[27];
  for (int i = 0; i < 27; i++) {
    rxBuf[i] = spi->transfer(0x00);
  }

  digitalWrite(cs, HIGH);
  spi->endTransaction();

  // Parse STATUS Word
  outFrame.status = ((uint16_t)rxBuf[0] << 8) | rxBuf[1];

  // Parse Channels 0 to 7
  for (int ch = 0; ch < 8; ch++) {
    int baseIdx = 3 + (ch * 3);
    uint32_t raw24 = ((uint32_t)rxBuf[baseIdx] << 16) |
                     ((uint32_t)rxBuf[baseIdx + 1] << 8) |
                      (uint32_t)rxBuf[baseIdx + 2];
    outFrame.raw[ch] = signExtend24(raw24);
    outFrame.voltage[ch] = codeToVoltage(outFrame.raw[ch], channelGains[ch]);
  }

  outFrame.crc = 0;
  outFrame.valid = (outFrame.status != 0x0000 && outFrame.status != 0xFFFF);
  healthy = outFrame.valid;
  return outFrame.valid;
}

uint16_t ADS131M08::getDeviceId() {
  return readRegister(ADS131_REG_ID);
}

bool ADS131M08::isHealthy() const {
  return healthy;
}

uint8_t ADS131M08::getCsPin() const {
  return cs;
}
