#ifndef MOCK_SPI_H
#define MOCK_SPI_H

#include <Arduino.h>

class MockSPIClass {
public:
  void begin() {}
  void begin(int sck, int miso, int mosi, int ss) {}
  void end() {}
  void beginTransaction(int settings = 0) {}
  void endTransaction() {}
  uint8_t transfer(uint8_t data) { return 0; }
  void transfer(void *buf, size_t count) {}
};

static MockSPIClass SPI;

#endif // MOCK_SPI_H
