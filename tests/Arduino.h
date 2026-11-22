#ifndef ARDUINO_EMU_H
#define ARDUINO_EMU_H

#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>

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
  int lastIndexOf(char c) const {
    size_t pos = rfind(c);
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  int lastIndexOf(const String &s) const {
    size_t pos = rfind(s.c_str());
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  int lastIndexOf(const char* s) const {
    size_t pos = rfind(s ? s : "");
    return (pos == std::string::npos) ? -1 : (int)pos;
  }
  String substring(int start, int end = -1) const {
    if (start < 0) start = 0;
    if (start > (int)length()) return "";
    if (end < 0 || end > (int)length()) return substr(start);
    if (end < start) return "";
    return substr(start, end - start);
  }
  bool startsWith(const String &s) const {
    return rfind(s.c_str(), 0) == 0;
  }
  bool startsWith(const char* s) const {
    return rfind(s ? s : "", 0) == 0;
  }
  void trim() {
    while (!empty() && isspace((unsigned char)front())) erase(begin());
    while (!empty() && isspace((unsigned char)back())) pop_back();
  }
  bool equalsIgnoreCase(const String &s) const {
    if (length() != s.length()) return false;
    for (size_t i = 0; i < length(); i++) {
      if (tolower((unsigned char)(*this)[i]) != tolower((unsigned char)s[i])) return false;
    }
    return true;
  }
};

inline String operator+(const String &lhs, const String &rhs) {
  String s = lhs;
  s.append(rhs);
  return s;
}
inline String operator+(const char* lhs, const String &rhs) {
  String s(lhs);
  s.append(rhs);
  return s;
}
inline String operator+(const String &lhs, const char* rhs) {
  String s = lhs;
  s.append(rhs ? rhs : "");
  return s;
}

extern unsigned long millis();
extern void delay(unsigned long ms);
#define F(s) (s)

class MockSerial {
public:
  template<typename T> void print(T val) {}
  template<typename T> void println(T val) {}
  void println() {}
  template<typename... Args> void printf(const char* fmt, Args... args) {}
};
extern MockSerial Serial;

#endif // ARDUINO_EMU_H
