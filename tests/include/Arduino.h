#ifndef MOCK_ARDUINO_H
#define MOCK_ARDUINO_H

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

#define LOW  0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define HEX 16
#define DEC 10

#define F(x) (x)

extern unsigned long g_simulatedTimeMs;
inline unsigned long millis() { return g_simulatedTimeMs; }
inline void delay(unsigned long ms) { g_simulatedTimeMs += ms; }

extern uint8_t g_pinModes[40];
extern uint8_t g_pinOutputs[40];
extern uint8_t g_pinInputs[40];

inline void pinMode(uint8_t pin, uint8_t mode) {
  if (pin < 40) {
    g_pinModes[pin] = mode;
    if (mode == INPUT_PULLUP) g_pinInputs[pin] = HIGH;
  }
}

inline void digitalWrite(uint8_t pin, uint8_t val) {
  if (pin < 40) {
    g_pinOutputs[pin] = val;
    g_pinInputs[pin] = val;
  }
}

inline uint8_t digitalRead(uint8_t pin) {
  if (pin < 40) return g_pinInputs[pin];
  return LOW;
}

class String {
public:
  std::string s;
  String() : s("") {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string &str) : s(str) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned int v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(float v, int dec = 2) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(dec) << v;
    s = ss.str();
  }
  String(uint8_t v, int base) {
    if (base == 16) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%X", v);
      s = buf;
    } else {
      s = std::to_string(v);
    }
  }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.length(); }
  char operator[](size_t i) const { return s[i]; }
  String& operator+=(const String &rhs) { s += rhs.s; return *this; }
  String& operator+=(const char* rhs) { if(rhs) s += rhs; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  bool operator==(const String &rhs) const { return s == rhs.s; }
  bool operator==(const char* rhs) const { return rhs && s == rhs; }
  bool operator!=(const String &rhs) const { return s != rhs.s; }
  bool operator!=(const char* rhs) const { return !rhs || s != rhs; }
  int indexOf(char c, int from = 0) const {
    size_t pos = s.find(c, from);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  int indexOf(const char* sub, int from = 0) const {
    size_t pos = s.find(sub, from);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  int indexOf(const String &sub, int from = 0) const {
    return indexOf(sub.c_str(), from);
  }
  String substring(int start, int end = -1) const {
    if (start < 0) start = 0;
    if (start >= (int)s.length()) return String("");
    if (end < 0 || end > (int)s.length()) end = (int)s.length();
    if (start >= end) return String("");
    return String(s.substr(start, end - start));
  }
  int toInt() const {
    try { return std::stoi(s); } catch(...) { return 0; }
  }
  float toFloat() const {
    try { return std::stof(s); } catch(...) { return 0.0f; }
  }
  void trim() {
    size_t p = s.find_first_not_of(" \t\r\n");
    size_t q = s.find_last_not_of(" \t\r\n");
    if (p == std::string::npos) s = "";
    else s = s.substr(p, q - p + 1);
  }
  bool startsWith(const String &prefix) const {
    if (prefix.length() > s.length()) return false;
    return s.compare(0, prefix.length(), prefix.s) == 0;
  }
  bool startsWith(const char* prefix) const {
    if (!prefix) return false;
    size_t len = strlen(prefix);
    if (len > s.length()) return false;
    return s.compare(0, len, prefix) == 0;
  }
  bool endsWith(const String &suffix) const {
    if (suffix.length() > s.length()) return false;
    return s.compare(s.length() - suffix.length(), suffix.length(), suffix.s) == 0;
  }
  bool endsWith(const char* suffix) const {
    if (!suffix) return false;
    size_t len = strlen(suffix);
    if (len > s.length()) return false;
    return s.compare(s.length() - len, len, suffix) == 0;
  }
  bool equalsIgnoreCase(const String &other) const {
    if (s.length() != other.s.length()) return false;
    for (size_t i = 0; i < s.length(); i++) {
      if (tolower(s[i]) != tolower(other.s[i])) return false;
    }
    return true;
  }
};

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

inline String operator+(const String &lhs, const String &rhs) { return String(lhs.s + rhs.s); }
inline String operator+(const String &lhs, const char* rhs) { return String(lhs.s + (rhs ? rhs : "")); }
inline String operator+(const char* lhs, const String &rhs) { return String((lhs ? lhs : "") + rhs.s); }

struct MockSerial {
  template<typename... Args>
  void printf(const char* fmt, Args... args) {}
  void println(const char* s) {}
  void println(const String &s) {}
  void print(const char* s) {}
  void print(const String &s) {}
};
extern MockSerial Serial;

struct MockESP {
  void restart() {}
  uint32_t getFreeHeap() { return 248192; }
};
extern MockESP ESP;

#endif // MOCK_ARDUINO_H
