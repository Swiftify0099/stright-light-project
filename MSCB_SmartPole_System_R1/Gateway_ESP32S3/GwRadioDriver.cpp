/**
 * @file GwRadioDriver.cpp
 * @brief MSCB Smart Pole Gateway - SX1278 RA-02 LoRa Radio Driver Implementation
 * @version 1.0.0
 */

#include "GwRadioDriver.h"

// Semtech SX1278 Registers
#define REG_FIFO                    0x00
#define REG_OP_MODE                 0x01
#define REG_FRF_MSB                 0x06
#define REG_FRF_MID                 0x07
#define REG_FRF_LSB                 0x08
#define REG_PA_CONFIG               0x09
#define REG_PA_RAMP                 0x0A
#define REG_OCP                     0x0B
#define REG_LNA                     0x0C
#define REG_FIFO_ADDR_PTR           0x0D
#define REG_FIFO_TX_BASE_ADDR       0x0E
#define REG_FIFO_RX_BASE_ADDR       0x0F
#define REG_FIFO_RX_CURRENT_ADDR    0x10
#define REG_IRQ_FLAGS_MASK          0x11
#define REG_IRQ_FLAGS               0x12
#define REG_RX_NB_BYTES             0x13
#define REG_PKT_SNR_VALUE           0x19
#define REG_PKT_RSSI_VALUE          0x1A
#define REG_MODEM_CONFIG_1          0x1D
#define REG_MODEM_CONFIG_2          0x1E
#define REG_SYMB_TIMEOUT_LSB        0x1F
#define REG_PREAMBLE_MSB            0x20
#define REG_PREAMBLE_LSB            0x21
#define REG_PAYLOAD_LENGTH          0x22
#define REG_MODEM_CONFIG_3          0x26
#define REG_SYNC_WORD               0x39
#define REG_DIO_MAPPING_1           0x40
#define REG_VERSION                 0x42
#define REG_PA_DAC                  0x4D

// SX1278 Modes
#define MODE_LONG_RANGE_MODE        0x80
#define MODE_SLEEP                  0x00
#define MODE_STDBY                  0x01
#define MODE_TX                     0x03
#define MODE_RX_CONTINUOUS          0x05

// IRQ Flags
#define IRQ_TX_DONE_MASK            0x08
#define IRQ_PAYLOAD_CRC_ERROR_MASK  0x20
#define IRQ_RX_DONE_MASK            0x40

GwRadioDriver gwRadio;

GwRadioDriver::GwRadioDriver() :
  radioDetected(false), packetsTx(0), packetsRx(0), crcErrors(0),
  lastRssi(-120), lastSnr(0)
#if defined(ARDUINO)
  , loraSpi(FSPI)
#endif
{
}

uint8_t GwRadioDriver::readRegister(uint8_t address) {
#if defined(ARDUINO)
  digitalWrite(GW_LORA_NSS_PIN, LOW);
  loraSpi.transfer(address & 0x7F);
  uint8_t val = loraSpi.transfer(0x00);
  digitalWrite(GW_LORA_NSS_PIN, HIGH);
  return val;
#else
  return 0x12; // Mock version on host
#endif
}

void GwRadioDriver::writeRegister(uint8_t address, uint8_t value) {
#if defined(ARDUINO)
  digitalWrite(GW_LORA_NSS_PIN, LOW);
  loraSpi.transfer(address | 0x80);
  loraSpi.transfer(value);
  digitalWrite(GW_LORA_NSS_PIN, HIGH);
#endif
}

void GwRadioDriver::resetRadio() {
#if defined(ARDUINO)
  digitalWrite(GW_LORA_RST_PIN, LOW);
  delay(10);
  digitalWrite(GW_LORA_RST_PIN, HIGH);
  delay(10);
#endif
}

void GwRadioDriver::setModeSleep() {
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
}

void GwRadioDriver::setModeStandby() {
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}

void GwRadioDriver::setModeRxContinuous() {
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
}

bool GwRadioDriver::begin() {
#if defined(ARDUINO)
  pinMode(GW_LORA_NSS_PIN, OUTPUT);
  digitalWrite(GW_LORA_NSS_PIN, HIGH);

  pinMode(GW_LORA_RST_PIN, OUTPUT);
  digitalWrite(GW_LORA_RST_PIN, HIGH);

  pinMode(GW_LORA_DIO0_PIN, INPUT);

  // Initialize native FSPI
  loraSpi.begin(GW_LORA_SCK_PIN, GW_LORA_MISO_PIN, GW_LORA_MOSI_PIN, GW_LORA_NSS_PIN);
#endif

  resetRadio();

  // Interrogate Silicon Version (0x12 for SX1276/7/8)
  uint8_t version = readRegister(REG_VERSION);
  if (version != 0x12) {
    radioDetected = false;
    return false;
  }
  radioDetected = true;

  // Configure LoRa Sleep mode to change parameters
  setModeSleep();

  // Center Frequency 433.920 MHz
  // Frf = (433920000 * 2^19) / 32000000 = 7109345.28 = 0x6C7AE1
  writeRegister(REG_FRF_MSB, 0x6C);
  writeRegister(REG_FRF_MID, 0x7A);
  writeRegister(REG_FRF_LSB, 0xE1);

  // Output Power: +8 dBm conducted (PA_BOOST = 0x80, OutputPower = 6 -> Pout = 2 + 6 = 8 dBm)
  // Complies with 10 mW ERP Indian WPC G.S.R. 347(E) with 3 dBi antenna
  writeRegister(REG_PA_CONFIG, 0x86);
  writeRegister(REG_OCP, 0x2B); // 100mA OCP
  writeRegister(REG_LNA, 0x23); // High sensitivity LNA

  // Modem Configuration:
  // BW 125 kHz (0x70) | CR 4/5 (0x02) | Explicit Header (0x00) -> 0x72
  writeRegister(REG_MODEM_CONFIG_1, 0x72);
  // SF10 (0xA0) | TxContinuous (0x00) | RxPayloadCrcOn (0x04) -> 0xA4
  writeRegister(REG_MODEM_CONFIG_2, 0xA4);
  // LowDataRateOptimize (0x08 for SF10 symbol duration > 16ms) | AGC Auto (0x04) -> 0x0C
  writeRegister(REG_MODEM_CONFIG_3, 0x0C);

  // Preamble Length (8 symbols)
  writeRegister(REG_PREAMBLE_MSB, 0x00);
  writeRegister(REG_PREAMBLE_LSB, MSCB_LORA_PREAMBLE_LEN);

  // Sync Word (0x12 private mesh)
  writeRegister(REG_SYNC_WORD, MSCB_LORA_SYNC_WORD);

  // Set base addresses for FIFO
  writeRegister(REG_FIFO_TX_BASE_ADDR, 0x00);
  writeRegister(REG_FIFO_RX_BASE_ADDR, 0x00);

  // Put into Standby then Continuous RX
  setModeStandby();
  setModeRxContinuous();

  return true;
}

bool GwRadioDriver::hasPacket() {
#if defined(ARDUINO)
  if (!radioDetected) return false;
  if (digitalRead(GW_LORA_DIO0_PIN) == HIGH) {
    uint8_t irq = readRegister(REG_IRQ_FLAGS);
    return (irq & IRQ_RX_DONE_MASK) != 0;
  }
  return false;
#else
  return false;
#endif
}

bool GwRadioDriver::receivePacket(MeshWireFrame &outFrame, int16_t &outRssi, int8_t &outSnr) {
#if defined(ARDUINO)
  if (!radioDetected) return false;

  uint8_t irq = readRegister(REG_IRQ_FLAGS);
  if (!(irq & IRQ_RX_DONE_MASK)) {
    return false;
  }

  // Clear IRQ flags
  writeRegister(REG_IRQ_FLAGS, irq);

  // Check CRC error
  if (irq & IRQ_PAYLOAD_CRC_ERROR_MASK) {
    crcErrors++;
    return false;
  }

  uint8_t numBytes = readRegister(REG_RX_NB_BYTES);
  if (numBytes < sizeof(MeshPacketHeader) + sizeof(uint16_t)) {
    // Too short for header + CRC
    return false;
  }

  uint8_t currentAddr = readRegister(REG_FIFO_RX_CURRENT_ADDR);
  writeRegister(REG_FIFO_ADDR_PTR, currentAddr);

  uint8_t buffer[sizeof(MeshWireFrame)];
  size_t bytesToRead = (numBytes > sizeof(buffer)) ? sizeof(buffer) : numBytes;

  for (size_t i = 0; i < bytesToRead; i++) {
    buffer[i] = readRegister(REG_FIFO);
  }

  // Measure link metrics
  // RSSI for 433 MHz: -157 + readRegister(REG_PKT_RSSI_VALUE)
  int16_t packetRssi = -157 + readRegister(REG_PKT_RSSI_VALUE);
  int8_t packetSnr = ((int8_t)readRegister(REG_PKT_SNR_VALUE)) / 4;
  lastRssi = packetRssi;
  lastSnr = packetSnr;
  outRssi = packetRssi;
  outSnr = packetSnr;

  // Validate Wire Frame CRC
  size_t dataLen = bytesToRead - 2;
  uint16_t expectedCrc = (buffer[bytesToRead - 2] << 8) | buffer[bytesToRead - 1];
  if (!ProtocolCrc::verify(buffer, dataLen, expectedCrc)) {
    crcErrors++;
    return false;
  }

  // Check magic bytes
  if (buffer[0] != MSCB_PROTOCOL_MAGIC_0 || buffer[1] != MSCB_PROTOCOL_MAGIC_1) {
    return false;
  }

  memcpy(&outFrame.header, buffer, sizeof(MeshPacketHeader));
  size_t payloadBytes = dataLen - sizeof(MeshPacketHeader);
  if (payloadBytes > MSCB_MAX_PAYLOAD_SIZE) payloadBytes = MSCB_MAX_PAYLOAD_SIZE;
  memcpy(outFrame.payload, buffer + sizeof(MeshPacketHeader), payloadBytes);
  outFrame.crc16 = expectedCrc;

  packetsRx++;
  return true;
#else
  return false;
#endif
}

bool GwRadioDriver::sendPacket(const MeshWireFrame &frame) {
#if defined(ARDUINO)
  if (!radioDetected) return false;

  setModeStandby();

  // Assemble buffer: header + payload + CRC16
  size_t dataLen = sizeof(MeshPacketHeader) + frame.header.payloadLen;
  if (dataLen > sizeof(MeshPacketHeader) + MSCB_MAX_PAYLOAD_SIZE) {
    return false;
  }

  uint8_t buffer[sizeof(MeshWireFrame)];
  memcpy(buffer, &frame.header, sizeof(MeshPacketHeader));
  memcpy(buffer + sizeof(MeshPacketHeader), frame.payload, frame.header.payloadLen);

  uint16_t calculatedCrc = ProtocolCrc::calculate(buffer, dataLen);
  buffer[dataLen] = (calculatedCrc >> 8) & 0xFF;
  buffer[dataLen + 1] = calculatedCrc & 0xFF;
  size_t totalBytes = dataLen + 2;

  // Write to FIFO
  writeRegister(REG_FIFO_ADDR_PTR, 0x00);
  for (size_t i = 0; i < totalBytes; i++) {
    writeRegister(REG_FIFO, buffer[i]);
  }
  writeRegister(REG_PAYLOAD_LENGTH, totalBytes);

  // Switch to TX mode
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

  // Wait for TxDone
  uint32_t startMs = millis();
  while ((readRegister(REG_IRQ_FLAGS) & IRQ_TX_DONE_MASK) == 0) {
    if (millis() - startMs > 2000) {
      // Timeout
      setModeStandby();
      setModeRxContinuous();
      return false;
    }
    yield();
  }

  // Clear IRQ flags
  writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);

  packetsTx++;
  setModeRxContinuous();
  return true;
#else
  packetsTx++;
  return true;
#endif
}
