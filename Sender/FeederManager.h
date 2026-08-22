/*
 * =====================================================================
 *  FEEDERMANAGER.H — GATEWAY FEEDER TELEMETRY & STATUS PROCESSOR
 * =====================================================================
 */

#ifndef FEEDER_MANAGER_H
#define FEEDER_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

class FeederManager {
public:
  FeederManager();
  void init();

  // Parse incoming feeder telemetry
  void processFeederTelemetry(const String &src, const String &data, int rssi);
  void updateFeederFromStatus(const String &src, float vR, float vY, float current, const String &light, const String &fault, int rssi);

  // Status calculation
  String getFeederStatusIcon(const FeederState &fdr, const NodeRecord *node);
  String getFeederStatusText(const FeederState &fdr, const NodeRecord *node);
  String getLoRaStatusIcon(const FeederState &fdr, const NodeRecord *node);

  // Telegram table generation
  String buildFeederStatusTable(const String &selectedNodeId = "");
  String buildCompactFeederTable(const String &selectedNodeId = "");

  // Feeder retrieval
  FeederState* getFeeder(const String &nodeId, uint8_t feederIndex);
};

extern FeederManager feederMgr;

#endif // FEEDER_MANAGER_H
