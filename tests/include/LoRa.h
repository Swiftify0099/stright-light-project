#ifndef MOCK_LORA_H
#define MOCK_LORA_H

#include <Arduino.h>

class MockLoRaClass {
public:
  void setPins(int ss, int reset, int dio0) {}
  bool begin(long frequency) { return true; }
  void end() {}
  void setSpreadingFactor(int sf) {}
  void setSignalBandwidth(long sbw) {}
  void setCodingRate4(int denominator) {}
  void setTxPower(int level, int outputPin = 0) {}
  void setSyncWord(int sw) {}
  void enableCrc() {}
  void disableCrc() {}
  void receive(int size = 0) {}
  void idle() {}
  int parsePacket(int size = 0) { return 0; }
  int packetRssi() { return -70; }
  float packetSnr() { return 8.0f; }
  int available() { return 0; }
  int read() { return -1; }
  int peek() { return -1; }
  size_t beginPacket(int implicitHeader = false) { return 1; }
  size_t endPacket(bool async = false) { return 1; }
  size_t write(uint8_t byte) { return 1; }
  size_t write(const uint8_t *buffer, size_t size) { return size; }
  size_t print(const String &s) { return s.length(); }
  size_t print(const char* s) { return strlen(s); }
};

static MockLoRaClass LoRa;

#define PA_OUTPUT_PA_BOOST_PIN 1

#endif // MOCK_LORA_H
