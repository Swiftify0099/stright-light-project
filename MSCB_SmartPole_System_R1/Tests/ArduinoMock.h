#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#define _USE_MATH_DEFINES
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <cstdarg>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Arduino data types and macros
typedef uint8_t byte;
typedef bool boolean;

#define LOW  0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

#define HEX 16
#define DEC 10

#if defined(ARDUINO_MOCK_IMPLEMENTATION)
uint8_t g_pinStates[40] = {
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
  1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};
unsigned long g_simulatedMillis = 1000UL;
#else
extern uint8_t g_pinStates[40];
extern unsigned long g_simulatedMillis;
#endif

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin < 40) g_pinStates[pin] = val;
}
inline int digitalRead(uint8_t pin) {
  return (pin < 40) ? g_pinStates[pin] : 0;
}
inline void delayMicroseconds(unsigned long) {}

inline unsigned long millis() { return g_simulatedMillis; }
inline void delay(unsigned long ms) { g_simulatedMillis += ms; }

class String : public std::string {
public:
  String() : std::string() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string &s) : std::string(s) {}
  String(char c) : std::string(1, c) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned int v) : std::string(std::to_string(v)) {}
  String(long v) : std::string(std::to_string(v)) {}
  String(unsigned long v) : std::string(std::to_string(v)) {}
  String(uint8_t v) : std::string(std::to_string((unsigned int)v)) {}
  String(float v, int dec = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", dec, v);
    assign(buf);
  }
  String(double v, int dec = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", dec, v);
    assign(buf);
  }
  int toInt() const { return atoi(c_str()); }
  float toFloat() const { return (float)atof(c_str()); }
  int indexOf(char c, int start = 0) const {
    size_t pos = find(c, start);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  int indexOf(const String &s, int start = 0) const {
    size_t pos = find(s.c_str(), start);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  int indexOf(const char* s, int start = 0) const {
    size_t pos = find(s ? s : "", start);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  String substring(unsigned int from, unsigned int to = 0) const {
    if (from >= length()) return String("");
    if (to == 0 || to > length()) to = (unsigned int)length();
    return String(substr(from, to - from));
  }
  bool startsWith(const String &prefix) const {
    return rfind(prefix.c_str(), 0) == 0;
  }
  bool endsWith(const String &suffix) const {
    if (length() < suffix.length()) return false;
    return compare(length() - suffix.length(), suffix.length(), suffix) == 0;
  }
  void toUpperCase() {
    std::transform(begin(), end(), begin(), ::toupper);
  }
  void toLowerCase() {
    std::transform(begin(), end(), begin(), ::tolower);
  }
};

class MockSerial {
public:
  void begin(unsigned long) {}
  void print(const char* s) { if (s) std::cout << s; }
  void print(int v) { std::cout << v; }
  void print(unsigned int v) { std::cout << v; }
  void print(long v) { std::cout << v; }
  void print(unsigned long v) { std::cout << v; }
  void print(double v) { std::cout << v; }
  void print(const String &s) { std::cout << s.c_str(); }
  void println(const char* s = "") { if (s) std::cout << s << std::endl; else std::cout << std::endl; }
  void println(int v) { std::cout << v << std::endl; }
  void println(unsigned int v) { std::cout << v << std::endl; }
  void println(long v) { std::cout << v << std::endl; }
  void println(unsigned long v) { std::cout << v << std::endl; }
  void println(double v) { std::cout << v << std::endl; }
  void println(const String &s) { std::cout << s.c_str() << std::endl; }
  void printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }
};

extern MockSerial Serial;

class MockESP {
public:
  uint32_t getFreeHeap() { return 245000; }
  uint32_t getFreePsram() { return 8192000; }
  uint32_t getPsramSize() { return 8388608; }
};
static MockESP ESP;
inline bool psramFound() { return true; }

#define F(s) (s)

#endif // ARDUINO_MOCK_H
