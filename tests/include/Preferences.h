#ifndef MOCK_PREFERENCES_H
#define MOCK_PREFERENCES_H

#include <Arduino.h>
#include <string>
#include <map>
#include <cstdint>

class Preferences {
public:
  bool begin(const char* name, bool readOnly = false) { return true; }
  void end() {}
  bool clear() { return true; }
  bool remove(const char* key) { return true; }
  size_t putString(const char* key, const char* value) { return 0; }
  size_t putString(const char* key, const String &value) { return 0; }
  String getString(const char* key, const String &defaultValue = "") { return defaultValue; }
  size_t putUInt(const char* key, uint32_t value) { return 0; }
  uint32_t getUInt(const char* key, uint32_t defaultValue = 0) { return defaultValue; }
  size_t putInt(const char* key, int32_t value) { return 0; }
  int32_t getInt(const char* key, int32_t defaultValue = 0) { return defaultValue; }
  size_t putBool(const char* key, bool value) { return 0; }
  bool getBool(const char* key, bool defaultValue = false) { return defaultValue; }
  size_t putFloat(const char* key, float value) { return 0; }
  float getFloat(const char* key, float defaultValue = 0.0f) { return defaultValue; }
  size_t putBytes(const char* key, const void* value, size_t len) { return len; }
  size_t getBytes(const char* key, void* buf, size_t maxLen) { return 0; }
};

#endif // MOCK_PREFERENCES_H
