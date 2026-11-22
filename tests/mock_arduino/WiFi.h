#ifndef WIFI_H
#define WIFI_H

#include <Arduino.h>

#define WL_CONNECTED 3
#define WL_DISCONNECTED 6
#define WIFI_STA 1

struct IPAddress {
  String toString() const { return "192.168.43.100"; }
};

struct WiFiMock {
  int status() { return WL_CONNECTED; }
  IPAddress localIP() { return IPAddress(); }
  IPAddress subnetMask() { return IPAddress(); }
  IPAddress gatewayIP() { return IPAddress(); }
  IPAddress dnsIP() { return IPAddress(); }
  String macAddress() { return "A4:CF:12:88:99:AA"; }
  int RSSI() { return -55; }
  void mode(int) {}
  void begin(const char*, const char*) {}
  void disconnect() {}
  void reconnect() {}
};

extern WiFiMock WiFi;

#endif // WIFI_H
