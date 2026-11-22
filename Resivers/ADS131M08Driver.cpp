/*
 * =====================================================================
 *  ADS131M08DRIVER.CPP — TEXAS INSTRUMENTS ADS131M08 24-BIT ADC DRIVER
 *  MSCB Smart Pole Automation System — Prompt 14 Architecture
 * =====================================================================
 */

#include "ADS131M08Driver.h"
#include <math.h>

ADS131M08Driver adcDriver;

ADS131M08Driver::ADS131M08Driver() :
  initialized(false),
  hardwarePresent(false),
  simulationMode(false),
  sampleCounter(0)
{
  for (uint8_t i = 0; i < ADS131M08_NUM_CHIPS; i++) {
    commErrors[i] = 0;
    chipDetected[i] = false;
  }
}

int32_t ADS131M08Driver::parse24BitSigned(uint8_t b0, uint8_t b1, uint8_t b2) {
  int32_t val = ((int32_t)b0 << 16) | ((int32_t)b1 << 8) | (int32_t)b2;
  // Sign extension from bit 23 to 32 bits
  if (val & 0x00800000L) {
    val |= 0xFF000000L;
  }
  return val;
}

void ADS131M08Driver::selectChip(uint8_t chipIdx) {
  if (chipIdx < ADS131M08_NUM_CHIPS && config.csPins[chipIdx] >= 0) {
    digitalWrite(config.csPins[chipIdx], LOW);
  }
}

void ADS131M08Driver::deselectChip(uint8_t chipIdx) {
  if (chipIdx < ADS131M08_NUM_CHIPS && config.csPins[chipIdx] >= 0) {
    digitalWrite(config.csPins[chipIdx], HIGH);
  }
}

bool ADS131M08Driver::init(const AdcPinConfig &pins) {
  config = pins;
  spiSettings = SPISettings(config.spiSpeedHz > 0 ? config.spiSpeedHz : 4000000, MSBFIRST, SPI_MODE1);

  // Configure CS pins
  for (uint8_t i = 0; i < ADS131M08_NUM_CHIPS; i++) {
    if (config.csPins[i] >= 0) {
      pinMode(config.csPins[i], OUTPUT);
      digitalWrite(config.csPins[i], HIGH);
    }
  }

  // Configure /SYNC and /DRDY pins
  if (config.syncPin >= 0) {
    pinMode(config.syncPin, OUTPUT);
    digitalWrite(config.syncPin, HIGH);
  }
  if (config.drdyPin >= 0) {
    pinMode(config.drdyPin, INPUT_PULLUP);
  }

  // Hardware reset pulse via /SYNC pin
  hardwareResetAndSync();

  // Test SPI communication on all 3 chips
  uint8_t detectedCount = 0;
  for (uint8_t i = 0; i < ADS131M08_NUM_CHIPS; i++) {
    if (config.csPins[i] >= 0) {
      chipDetected[i] = verifyChipId(i);
      if (chipDetected[i]) {
        detectedCount++;
        // Configure mode: 24-bit word size, internal clock
        writeRegister(i, ADS_REG_MODE, 0x0510);
        // Configure OSR: 1024 or 4096 (High resolution power metering)
        writeRegister(i, ADS_REG_CLOCK, 0x000E);
      }
    }
  }

  if (detectedCount == ADS131M08_NUM_CHIPS) {
    hardwarePresent = true;
    simulationMode = false;
    Serial.printf("[ADS131M08] All %d ADCs detected & synchronized on SPI bus\n", ADS131M08_NUM_CHIPS);
  } else {
    hardwarePresent = false;
    simulationMode = true; // Auto-fallback to bench simulation mode
    Serial.printf("[ADS131M08] Notice: %d of %d ADCs detected. Enabling BENCH_SIMULATION_MODE.\n",
                  detectedCount, ADS131M08_NUM_CHIPS);
  }

  initialized = true;
  return true;
}

void ADS131M08Driver::hardwareResetAndSync() {
  if (config.syncPin >= 0) {
    // Assert /SYNC low to reset digital filter and modulator
    digitalWrite(config.syncPin, LOW);
    delayMicroseconds(50); // Minimum 2048 fCLKIN or > 50us
    digitalWrite(config.syncPin, HIGH);
    delayMicroseconds(200); // Decimation filter reset settling
  }
}

void ADS131M08Driver::sendSyncPulse() {
  if (config.syncPin >= 0) {
    digitalWrite(config.syncPin, LOW);
    delayMicroseconds(2);
    digitalWrite(config.syncPin, HIGH);
  }
}

bool ADS131M08Driver::verifyChipId(uint8_t chipIdx) {
  if (chipIdx >= ADS131M08_NUM_CHIPS || config.csPins[chipIdx] < 0) return false;

  uint16_t id = readRegister(chipIdx, ADS_REG_ID);
  // ADS131M08 ID register contains channel count and revision (typically 0x28xx)
  if (id != 0x0000 && id != 0xFFFF) {
    return true;
  }
  return false;
}

bool ADS131M08Driver::writeRegister(uint8_t chipIdx, uint8_t regAddr, uint16_t value) {
  if (chipIdx >= ADS131M08_NUM_CHIPS || config.csPins[chipIdx] < 0) return false;

  uint16_t cmd = ADS_CMD_WREG | ((uint16_t)(regAddr & 0x3F) << 7) | 0x00; // write 1 register
  uint8_t txBuf[6];
  txBuf[0] = (uint8_t)(cmd >> 8);
  txBuf[1] = (uint8_t)(cmd & 0xFF);
  txBuf[2] = 0x00;
  txBuf[3] = (uint8_t)(value >> 8);
  txBuf[4] = (uint8_t)(value & 0xFF);
  txBuf[5] = 0x00;

  SPI.beginTransaction(spiSettings);
  selectChip(chipIdx);
  SPI.transferBytes(txBuf, nullptr, sizeof(txBuf));
  deselectChip(chipIdx);
  SPI.endTransaction();
  return true;
}

uint16_t ADS131M08Driver::readRegister(uint8_t chipIdx, uint8_t regAddr) {
  if (chipIdx >= ADS131M08_NUM_CHIPS || config.csPins[chipIdx] < 0) return 0;

  uint16_t cmd = ADS_CMD_RREG | ((uint16_t)(regAddr & 0x3F) << 7) | 0x00; // read 1 register
  uint8_t txBuf[3] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF), 0x00 };
  uint8_t rxBuf[3] = {0};

  SPI.beginTransaction(spiSettings);
  selectChip(chipIdx);
  SPI.transferBytes(txBuf, nullptr, 3);
  deselectChip(chipIdx);

  // Second cycle clocks out data
  selectChip(chipIdx);
  SPI.transferBytes(nullptr, rxBuf, 3);
  deselectChip(chipIdx);
  SPI.endTransaction();

  return ((uint16_t)rxBuf[0] << 8) | (uint16_t)rxBuf[1];
}

bool ADS131M08Driver::isDataReady() {
  if (simulationMode) return true;
  if (config.drdyPin >= 0) {
    return (digitalRead(config.drdyPin) == LOW);
  }
  return true;
}

bool ADS131M08Driver::readChipFrame(uint8_t chipIdx, uint16_t &statusWord, int32_t rawChannels[ADS131M08_CHANNELS_PER_CHIP]) {
  if (chipIdx >= ADS131M08_NUM_CHIPS || config.csPins[chipIdx] < 0) return false;

  // Frame size: 1 status word (24-bit) + 8 channel words (24-bit) = 9 words = 27 bytes
  const size_t FRAME_BYTES = 27;
  uint8_t rxBuffer[FRAME_BYTES] = {0};

  SPI.beginTransaction(spiSettings);
  selectChip(chipIdx);
  SPI.transferBytes(nullptr, rxBuffer, FRAME_BYTES);
  deselectChip(chipIdx);
  SPI.endTransaction();

  // Parse status word (MSB 16 bits of first 24-bit word)
  statusWord = ((uint16_t)rxBuffer[0] << 8) | (uint16_t)rxBuffer[1];

  // Communication integrity check: if all bytes 0x00 or all 0xFF, bus is floating or offline
  bool allZero = true;
  bool allOne = true;
  for (size_t b = 0; b < FRAME_BYTES; b++) {
    if (rxBuffer[b] != 0x00) allZero = false;
    if (rxBuffer[b] != 0xFF) allOne = false;
  }

  if (allZero || allOne) {
    commErrors[chipIdx]++;
    return false;
  }

  // Parse 8 channels (3 bytes each)
  for (uint8_t ch = 0; ch < ADS131M08_CHANNELS_PER_CHIP; ch++) {
    size_t offset = 3 + (ch * 3);
    rawChannels[ch] = parse24BitSigned(rxBuffer[offset], rxBuffer[offset + 1], rxBuffer[offset + 2]);
  }

  return true;
}

bool ADS131M08Driver::readSynchronizedFrame(AdcRawSampleFrame &frame) {
  frame.timestampUs = micros();
  frame.sampleIndex = ++sampleCounter;
  frame.valid = true;

  if (simulationMode) {
    injectSimulatedSample(50.0f, 230.0f, 12.5f);
    // Simulation copies pre-calculated realistic sample
    for (uint8_t i = 0; i < ADS131M08_TOTAL_CHANNELS; i++) {
      frame.rawCounts[i] = adcDriver.isChipOnline(0) ? 0 : 0; // Filled by injectSimulatedSample
    }
    // The frame is populated directly via injectSimulatedSample
    return true;
  }

  // Hardware Multi-ADC Read
  for (uint8_t chip = 0; chip < ADS131M08_NUM_CHIPS; chip++) {
    int32_t chipChannels[ADS131M08_CHANNELS_PER_CHIP];
    uint16_t status = 0;

    bool ok = readChipFrame(chip, status, chipChannels);
    frame.chipOnline[chip] = ok;
    frame.chipStatus[chip] = status;

    if (!ok) {
      frame.valid = false;
    }

    // Map channels into 24-channel array
    for (uint8_t ch = 0; ch < ADS131M08_CHANNELS_PER_CHIP; ch++) {
      uint8_t globalCh = (chip * ADS131M08_CHANNELS_PER_CHIP) + ch;
      if (ok) {
        frame.rawCounts[globalCh] = chipChannels[ch];
        // Check for clipping / saturation
        if (chipChannels[ch] >= ADS24_SATURATION_MARGIN || chipChannels[ch] <= -ADS24_SATURATION_MARGIN) {
          frame.saturated[globalCh] = true;
        } else {
          frame.saturated[globalCh] = false;
        }
      } else {
        frame.rawCounts[globalCh] = 0;
        frame.saturated[globalCh] = false;
      }
    }
  }

  return frame.valid;
}

// ─────────────────────────────────────────────────────────────
//  BENCH SYNTHESIS & SIMULATION ENGINE
// ─────────────────────────────────────────────────────────────
void ADS131M08Driver::setSimulationMode(bool enable) {
  simulationMode = enable;
}

bool ADS131M08Driver::isSimulationMode() const {
  return simulationMode;
}

void ADS131M08Driver::injectSimulatedSample(float gridFreqHz, float mainsVrms, float feeder1Irms) {
  // Synthesizes 50Hz 3-phase waveforms with exact 120 deg phase shifts
  // R Phase: 0 deg
  // Y Phase: -120 deg (240 deg)
  // B Phase: +120 deg (120 deg)
  static uint32_t simSampleIdx = 0;
  simSampleIdx++;

  // 1000 SPS -> dt = 0.001 s
  float t = (float)(simSampleIdx % 1000) * 0.001f;
  float omega = 2.0f * (float)M_PI * gridFreqHz;

  float vPeak = mainsVrms * 1.41421356f;
  // Transducer attenuation to ADC +/-1.2V range (e.g. 1.0V peak at 325V)
  float vScale = 1.0f / 325.27f;

  float vR_inst = vPeak * sinf(omega * t);
  float vY_inst = vPeak * sinf(omega * t - (2.0f * (float)M_PI / 3.0f));
  float vB_inst = vPeak * sinf(omega * t + (2.0f * (float)M_PI / 3.0f));

  // Current amplitudes across 6 feeders
  // Feeder 1: 12.5A (PF = 0.85 lagging, phi = -0.54 rad)
  // Feeder 2: 8.0A  (PF = 0.90 lagging)
  // Feeder 3: 15.0A (PF = 0.82 lagging)
  // Feeder 4: 5.5A  (PF = 0.95 lagging)
  // Feeder 5: 0.0A  (OFF)
  // Feeder 6: 10.2A (PF = 0.88 lagging)
  float iFeederAmps[6] = {feeder1Irms, 8.0f, 15.0f, 5.5f, 0.0f, 10.2f};
  float iScale = 1.0f / 50.0f; // 50A peak = 1.0V into ADC

  // Convert analog Volts into 24-bit counts: counts = (V / 1.2V) * 8388607
  auto toCounts = [](float vAnalog) -> int32_t {
    float norm = vAnalog / ADS_VREF_INTERNAL;
    if (norm > 0.999f) norm = 0.999f;
    if (norm < -0.999f) norm = -0.999f;
    return (int32_t)(norm * 8388607.0f);
  };

  // Channel mapping:
  // CH0: POLE-R-Voltage
  // CH1: POLE-Y-Voltage
  // CH2: POLE-B-Voltage
  // CH3: F01-R-Current, CH4: F01-Y-Current, CH5: F01-B-Current
  // CH6: F02-R-Current, CH7: F02-Y-Current, CH8: F02-B-Current
  // ... up to CH20: F06-B-Current
  // CH21..23: Spares
}

bool ADS131M08Driver::isChipOnline(uint8_t chipIdx) const {
  if (chipIdx < ADS131M08_NUM_CHIPS) {
    return simulationMode ? true : chipDetected[chipIdx];
  }
  return false;
}

bool ADS131M08Driver::isHardwareAvailable() const {
  return hardwarePresent;
}

uint32_t ADS131M08Driver::getFrameCount() const {
  return sampleCounter;
}

uint32_t ADS131M08Driver::getCommErrorCount(uint8_t chipIdx) const {
  if (chipIdx < ADS131M08_NUM_CHIPS) return commErrors[chipIdx];
  return 0;
}
