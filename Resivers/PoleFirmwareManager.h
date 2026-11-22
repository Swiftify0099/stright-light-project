/*
 * =====================================================================
 *  POLEFIRMWAREMANAGER.H — FIELD POLE FIRMWARE MAINTENANCE & OTA
 *  MSCB Smart Pole Automation System — Prompt 20 Architecture
 *  Wi-Fi AP Maintenance Mode, Wired Serial Recovery & LoRa Math Evaluator
 * =====================================================================
 */

#ifndef POLE_FIRMWARE_MANAGER_H
#define POLE_FIRMWARE_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <WiFi.h>
  #include <WebServer.h>
  #include <esp_ota_ops.h>
  #include <esp_partition.h>
#else
  #include "Arduino.h"
#endif

enum PoleMaintenanceModeState {
  POLE_MAINT_IDLE = 0,
  POLE_MAINT_AP_ACTIVE,
  POLE_MAINT_CLIENT_CONNECTED,
  POLE_MAINT_UPLOADING,
  POLE_MAINT_VERIFYING,
  POLE_MAINT_FLASHING,
  POLE_MAINT_AWAITING_REBOOT,
  POLE_MAINT_REBOOTING,
  POLE_MAINT_FAILED
};

struct LoRaFeasibilityReport {
  uint32_t firmwareSizeBytes;
  uint16_t packetChunkSize;
  uint32_t totalPackets;
  float    timeOnAirPerPacketMs;
  float    totalRawAirtimeSec;
  float    minRegulatoryTimeSec;  // Under 1% duty-cycle
  float    minRegulatoryTimeHours;
  float    minRegulatoryTimeDays;
  float    packetSuccessProb3Hops;
  bool     isFeasibleForProduction;
  char     verdict[64];
};

class PoleFirmwareManager {
public:
  PoleFirmwareManager();

  void init();

  // Option 1: Local Wi-Fi Access Point Maintenance Mode
  bool startMaintenanceAP(const char* adminPin, String &outSsid, String &outIp);
  void stopMaintenanceAP();
  bool isMaintenanceAPActive() const;
  void update(); // Handles AP timeout & web server client processing

  // Web Upload & OTA Processing
  bool handleFirmwareUpload(const uint8_t *data, size_t length, const char *expectedSha256,
                            const char *signatureHex, String &outError);

  // Option 2: Wired Serial Service Recovery
  bool processSerialRecoveryChunk(uint16_t chunkIndex, const uint8_t *data, size_t len,
                                 bool isLastChunk, String &outError);

  // Option 3: Experimental LoRa Firmware Feasibility Evaluation
  static LoRaFeasibilityReport evaluateLoRaFeasibility(uint32_t binarySizeBytes, uint8_t meshHops);
  bool processExperimentalLoRaChunk(uint16_t chunkIdx, uint16_t totalChunks,
                                    const uint8_t *data, size_t len, String &outAck);

  // Rollback & Partition Verification
  void markAppValid();
  bool rollbackApp();
  String getPartitionSchemeReport() const;

  PoleMaintenanceModeState getState() const;

private:
  PoleMaintenanceModeState state;
  bool                     apActive;
  uint32_t                 apStartedMs;
  char                     maintenancePin[16];

  uint8_t                  publicKey[32];

  bool verifyImageSignature(const char *sha256Hex, const char *sigHex);
};

extern PoleFirmwareManager poleFwMgr;

#endif // POLE_FIRMWARE_MANAGER_H
