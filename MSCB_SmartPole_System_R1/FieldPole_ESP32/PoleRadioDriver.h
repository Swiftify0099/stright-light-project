/**
 * @file PoleRadioDriver.h
 * @brief MSCB Smart Pole Node - SX1278 RA-02 LoRa Radio Driver
 * @version 1.0.0
 * 
 * Target Board: Classic ESP32-WROOM-32 (VSPI)
 * Pins: SCK 18, MISO 19, MOSI 23, NSS 5, RST 14, DIO0 4
 * RF Configuration:
 *   Frequency: 433.920 MHz
 *   Bandwidth: 125 kHz
 *   Spreading Factor: SF10
 *   Coding Rate: CR 4/5
 *   Sync Word: 0x12
 *   TX Power: +8 dBm conducted (WPC G.S.R. 347(E) compliant)
 */

#ifndef POLE_RADIO_DRIVER_H
#define POLE_RADIO_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <SPI.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "PoleBoardPins.h"
#include "PoleConfig.h"
#include "../MSCBShared/Protocol.h"

class PoleRadioDriver {
public:
  PoleRadioDriver();

  bool begin();
  void resetRadio();
  bool isDetected() const { return radioDetected; }

  // Packet Transmission & Reception
  bool sendPacket(const MeshWireFrame &frame);
  bool receivePacket(MeshWireFrame &outFrame, int16_t &outRssi, int8_t &outSnr);
  bool hasPacket();

  // Statistics
  uint32_t getPacketsTx() const { return packetsTx; }
  uint32_t getPacketsRx() const { return packetsRx; }
  uint32_t getCrcErrors() const { return crcErrors; }
  int16_t  getLastRssi() const { return lastRssi; }
  int8_t   getLastSnr() const { return lastSnr; }

  // Direct Register Access (SPI)
  uint8_t readRegister(uint8_t address);
  void writeRegister(uint8_t address, uint8_t value);

  void setModeStandby();
  void setModeRxContinuous();
  void setModeSleep();

private:
  bool     radioDetected;
  uint32_t packetsTx;
  uint32_t packetsRx;
  uint32_t crcErrors;
  int16_t  lastRssi;
  int8_t   lastSnr;
};

extern PoleRadioDriver poleRadio;

#endif // POLE_RADIO_DRIVER_H
