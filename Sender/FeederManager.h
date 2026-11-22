/*
 * =====================================================================
 *  FEEDERMANAGER.H — GATEWAY FEEDER TELEMETRY & STATUS PROCESSOR
 *  MSCB Smart Pole Automation System — Prompt 15 Architecture
 * =====================================================================
 */

#ifndef FEEDER_MANAGER_H
#define FEEDER_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

#define MAX_COMMAND_HISTORY 16

class FeederManager {
public:
  FeederManager();
  void init();

  // Parse incoming feeder telemetry
  void processFeederTelemetry(const String &src, const String &data, int rssi);
  void processMetrologyPacket(const String &type, const String &src, const String &data, int rssi);
  void updateFeederFromStatus(const String &src, float vR, float vY, float current, const String &light, const String &fault, int rssi);

  // Status calculation
  String getFeederStatusIcon(const FeederState &fdr, const NodeRecord *node);
  String getFeederStatusText(const FeederState &fdr, const NodeRecord *node);
  String getLoRaStatusIcon(const FeederState &fdr, const NodeRecord *node);

  // Telegram table generation
  String buildFeederStatusTable(const String &selectedNodeId = "");
  String buildCompactFeederTable(const String &selectedNodeId = "");
  String build21ChannelMeterTable(const String &selectedNodeId = "");

  // Feeder retrieval
  FeederState* getFeeder(const String &nodeId, uint8_t feederIndex);

  // ─────────────────────────────────────────────────────────────
  //  PROMPT 15 SECTION 13: TELEGRAM INTEGRATION APIS
  // ─────────────────────────────────────────────────────────────
  PoleControlStatus      getPoleControlStatus(const String &nodeId);
  FeederControlStatus    getFeederStatus(const String &nodeId, uint8_t feederIdx);
  bool                   requestFeederOn(const String &nodeId, uint8_t feederIdx, const String &callerId = "TELEGRAM", uint32_t durationSec = 0);
  bool                   requestFeederOff(const String &nodeId, uint8_t feederIdx, const String &callerId = "TELEGRAM");
  bool                   requestGroupControl(const String &nodeId, uint8_t feederMask, bool turnOn, uint32_t staggerMs = 500, const String &callerId = "TELEGRAM");
  CommandExecutionRecord getCommandExecutionStatus(uint8_t commandId);
  String                 getMcbStatus(const String &nodeId, uint8_t feederIdx);
  String                 getContactorFeedback(const String &nodeId, uint8_t feederIdx);
  PhaseCurrentStatus     getPhaseCurrentStatus(const String &nodeId, uint8_t feederIdx);
  PhaseVoltageStatus     getPhaseVoltageStatus(const String &nodeId);

  // ACK Processor
  void recordCommandAck(uint8_t commandId, const String &nodeId, const String &status, const String &details);

private:
  uint8_t                nextCommandId;
  CommandExecutionRecord cmdHistory[MAX_COMMAND_HISTORY];
  uint8_t                cmdHistoryHead;

  uint8_t allocateCommandId();
  void storeCommandRecord(uint8_t cmdId, const String &nodeId, uint8_t feederId, const String &action);
};

extern FeederManager feederMgr;

#endif // FEEDER_MANAGER_H
