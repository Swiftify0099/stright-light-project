#ifndef PREFERENCES_H
#define PREFERENCES_H
class Preferences {
public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  bool clear() { return true; }
  size_t putBytes(const char*, const void*, size_t) { return 0; }
  size_t getBytes(const char*, void*, size_t) { return 0; }
  size_t putString(const char*, const char*) { return 0; }
  int getInt(const char*, int = 0) { return 0; }
  bool putInt(const char*, int) { return true; }
  bool isKey(const char*) { return false; }
};
#endif
