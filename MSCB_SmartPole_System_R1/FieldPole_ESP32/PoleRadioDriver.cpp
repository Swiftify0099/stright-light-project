/**
 * @file PoleRadioDriver.cpp
 * @brief MSCB Smart Pole Node - SX1278 RA-02 LoRa Radio Driver Implementation
 * @version 1.0.0
 */

#include "PoleRadioDriver.h"

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

#define MODE_LONG_RANGE_MODE        0x80
#define MODE_SLEEP                  0x00
#define MODE_STDBY                  0x01
#define MODE_TX                     0x03
#define MODE_RX_CONTINUOUS          0x05

#define IRQ_TX_DONE_MASK            0x08
#define IRQ_PAYLOAD_CRC_ERROR_MASK  0x20
#define IRQ_RX_DONE_MASK            0x40

PoleRadioDriver poleRadio;

PoleRadioDriver::PoleRadioDriver() :
  pinNss(POLE_LORA_NSS_PIN), pinRst(POLE_LORA_RST_PIN), pinDio0(POLE_LORA_DIO0_PIN),
  radioDetected(false), packetsTx(0), packetsRx(0), crcErrors(0),
  lastRssi(-120), lastSnr(0)
#if !defined(ARDUINO)
  , forceTxFail(false)
#endif
{
}

void PoleRadioDriver::configurePins(uint8_t nss, uint8_t rst, uint8_t dio0) {
  pinNss = nss;
  pinRst = rst;
  pinDio0 = dio0;
}

uint8_t PoleRadioDriver::readRegister(uint8_t address) {
#if defined(ARDUINO)
  digitalWrite(pinNss, LOW);
  SPI.transfer(address & 0x7F);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(pinNss, HIGH);
  return val;
#else
  return 0x12; // Mock version on host
#endif
}

void PoleRadioDriver::writeRegister(uint8_t address, uint8_t value) {
#if defined(ARDUINO)
  digitalWrite(pinNss, LOW);
  SPI.transfer(address | 0x80);
  SPI.transfer(value);
  digitalWrite(pinNss, HIGH);
#endif
}

void PoleRadioDriver::resetRadio() {
#if defined(ARDUINO)
  digitalWrite(pinRst, LOW);
  delay(10);
  digitalWrite(pinRst, HIGH);
  delay(10);
#endif
}

void PoleRadioDriver::setModeSleep() {
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_SLEEP);
}

void PoleRadioDriver::setModeStandby() {
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_STDBY);
}

void PoleRadioDriver::setModeRxContinuous() {
  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_RX_CONTINUOUS);
}

bool PoleRadioDriver::begin() {
#if defined(ARDUINO)
  pinMode(pinNss, OUTPUT);
  digitalWrite(pinNss, HIGH);

  pinMode(pinRst, OUTPUT);
  digitalWrite(pinRst, HIGH);

  pinMode(pinDio0, INPUT);
#endif

  resetRadio();

  uint8_t version = readRegister(REG_VERSION);
  if (version != 0x12) {
    radioDetected = false;
    return false;
  }
  radioDetected = true;

  setModeSleep();

  // Frequency: 433.920 MHz
  writeRegister(REG_FRF_MSB, 0x6C);
  writeRegister(REG_FRF_MID, 0x7A);
  writeRegister(REG_FRF_LSB, 0xE1);

  // Conducted Power: +8 dBm (PA_BOOST = 0x80 | 0x06 -> 8 dBm conducted, WPC compliant)
  writeRegister(REG_PA_CONFIG, 0x86);
  writeRegister(REG_OCP, 0x2B);
  writeRegister(REG_LNA, 0x23);

  // Modem configuration: 125 kHz BW, CR 4/5, SF10
  writeRegister(REG_MODEM_CONFIG_1, 0x72);
  writeRegister(REG_MODEM_CONFIG_2, 0xA4);
  writeRegister(REG_MODEM_CONFIG_3, 0x0C);

  // Preamble length (8 symbols)
  writeRegister(REG_PREAMBLE_MSB, 0x00);
  writeRegister(REG_PREAMBLE_LSB, MSCB_LORA_PREAMBLE_LEN);

  // Sync Word: 0x12
  writeRegister(REG_SYNC_WORD, MSCB_LORA_SYNC_WORD);

  writeRegister(REG_FIFO_TX_BASE_ADDR, 0x00);
  writeRegister(REG_FIFO_RX_BASE_ADDR, 0x00);

  setModeStandby();
  setModeRxContinuous();

  return true;
}

bool PoleRadioDriver::hasPacket() {
#if defined(ARDUINO)
  if (!radioDetected) return false;
  if (digitalRead(pinDio0) == HIGH) {
    uint8_t irq = readRegister(REG_IRQ_FLAGS);
    return (irq & IRQ_RX_DONE_MASK) != 0;
  }
  return false;
#else
  return false;
#endif
}

bool PoleRadioDriver::receivePacket(MeshWireFrame &outFrame, int16_t &outRssi, int8_t &outSnr) {
#if defined(ARDUINO)
  if (!radioDetected) return false;

  uint8_t irq = readRegister(REG_IRQ_FLAGS);
  if (!(irq & IRQ_RX_DONE_MASK)) {
    return false;
  }

  writeRegister(REG_IRQ_FLAGS, irq);

  if (irq & IRQ_PAYLOAD_CRC_ERROR_MASK) {
    crcErrors++;
    return false;
  }

  uint8_t numBytes = readRegister(REG_RX_NB_BYTES);
  if (numBytes < sizeof(MeshPacketHeader) + sizeof(uint16_t)) {
    return false;
  }

  uint8_t currentAddr = readRegister(REG_FIFO_RX_CURRENT_ADDR);
  writeRegister(REG_FIFO_ADDR_PTR, currentAddr);

  uint8_t buffer[sizeof(MeshWireFrame)];
  size_t bytesToRead = (numBytes > sizeof(buffer)) ? sizeof(buffer) : numBytes;

  for (size_t i = 0; i < bytesToRead; i++) {
    buffer[i] = readRegister(REG_FIFO);
  }

  int16_t packetRssi = -157 + readRegister(REG_PKT_RSSI_VALUE);
  int8_t packetSnr = ((int8_t)readRegister(REG_PKT_SNR_VALUE)) / 4;
  lastRssi = packetRssi;
  lastSnr = packetSnr;
  outRssi = packetRssi;
  outSnr = packetSnr;

  size_t dataLen = bytesToRead - 2;
  uint16_t expectedCrc = (buffer[bytesToRead - 2] << 8) | buffer[bytesToRead - 1];
  if (!ProtocolCrc::verify(buffer, dataLen, expectedCrc)) {
    crcErrors++;
    return false;
  }

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

bool PoleRadioDriver::sendPacket(const MeshWireFrame &frame) {
#if defined(ARDUINO)
  if (!radioDetected) return false;

  setModeStandby();

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

  writeRegister(REG_FIFO_ADDR_PTR, 0x00);
  for (size_t i = 0; i < totalBytes; i++) {
    writeRegister(REG_FIFO, buffer[i]);
  }
  writeRegister(REG_PAYLOAD_LENGTH, totalBytes);

  writeRegister(REG_OP_MODE, MODE_LONG_RANGE_MODE | MODE_TX);

  uint32_t startMs = millis();
  while ((readRegister(REG_IRQ_FLAGS) & IRQ_TX_DONE_MASK) == 0) {
    if (millis() - startMs > 2000) {
      setModeStandby();
      setModeRxContinuous();
      return false;
    }
    yield();
  }

  writeRegister(REG_IRQ_FLAGS, IRQ_TX_DONE_MASK);
  packetsTx++;
  setModeRxContinuous();
  return true;
#else
  if (!radioDetected) return false;
  if (forceTxFail) return false;
  packetsTx++;
  return true;
#endif
}
