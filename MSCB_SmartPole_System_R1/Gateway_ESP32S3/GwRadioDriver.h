/**
 * @file GwRadioDriver.h
 * @brief MSCB Smart Pole Gateway - SX1278 RA-02 LoRa Radio Driver
 * @version 1.0.0
 * 
 * Hardware: ESP32-S3 FSPI Native IOMUX Pins
 *   SCK: GPIO 12, MISO: GPIO 13, MOSI: GPIO 11, NSS: GPIO 10, RST: GPIO 14, DIO0: GPIO 2
 * RF Configuration:
 *   Frequency: 433.920 MHz
 *   Bandwidth: 125 kHz
 *   Spreading Factor: SF10
 *   Coding Rate: CR 4/5
 *   Sync Word: 0x12
 *   TX Power: +8 dBm conducted (WPC G.S.R. 347(E) compliant)
 */

#ifndef GW_RADIO_DRIVER_H
#define GW_RADIO_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <SPI.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "GwBoardPins.h"
#include "GwConfig.h"
#include "../MSCBShared/Protocol.h"

class GwRadioDriver {
public:
  GwRadioDriver();

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

  // Set standby or receive mode
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

#if defined(ARDUINO)
  SPIClass loraSpi;
#endif
};

extern GwRadioDriver gwRadio;

#endif // GW_RADIO_DRIVER_H
