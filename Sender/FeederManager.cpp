/*
 * =====================================================================
 *  FEEDERMANAGER.CPP — GATEWAY FEEDER TELEMETRY & STATUS PROCESSOR
 * =====================================================================
 */

#include "FeederManager.h"
#include "MeshManager.h"
#include "LoRaManager.h"
#include "AlertManager.h"

FeederManager feederMgr;

FeederManager::FeederManager() : nextCommandId(1), cmdHistoryHead(0) {
  for (int i = 0; i < MAX_COMMAND_HISTORY; i++) {
    cmdHistory[i].commandId = 0;
    cmdHistory[i].nodeId = "";
    cmdHistory[i].feederId = 0;
    cmdHistory[i].requestedAction = "";
    cmdHistory[i].status = "IDLE";
    cmdHistory[i].requestTimeMs = 0;
    cmdHistory[i].ackTimeMs = 0;
    cmdHistory[i].stage = "";
    cmdHistory[i].details = "";
  }
}

void FeederManager::init() {}

void FeederManager::updateFeederFromStatus(const String &src, float vR, float vY, float current, const String &light, const String &fault, int rssi) {
  NodeRecord *node = meshMgr.findNode(src);
  if (!node) return;

  unsigned long now = millis();

  for (int f = 0; f < MAX_FEEDERS; f++) {
    if (f == 0) {
      node->feeders[0].relayState = (light == "ON" || light == "1") ? "ON" : "OFF";
    }
    node->feeders[f].voltage1 = vR;
    node->feeders[f].voltage2 = (vY > 10.0f) ? vY : vR;
    node->feeders[f].voltage1Valid = (vR >= MIN_VALID_MAINS_VOLTAGE && vR <= MAX_VALID_MAINS_VOLTAGE);
    node->feeders[f].voltage2Valid = (node->feeders[f].voltage2 >= MIN_VALID_MAINS_VOLTAGE && node->feeders[f].voltage2 <= MAX_VALID_MAINS_VOLTAGE);
    node->feeders[f].lastVoltageUpdate = now;
    node->feeders[f].lastHeartbeat = now;
    node->feeders[f].loraState = "ONLINE";
    node->feeders[f].onlineState = "ONLINE";
    node->feeders[f].faultState = (fault.length() > 0) ? fault : "OK";
  }
}

void FeederManager::processFeederTelemetry(const String &src, const String &data, int rssi) {
  NodeRecord *node = meshMgr.findNode(src);
  if (!node) return;

  unsigned long now = millis();

  // Data format example:
  // F1:ON,230.1,229.5,OK,1|F2:OFF,231.0,230.0,OK,1|...
  int start = 0;

  for (int i = 0; i <= (int)data.length(); i++) {
    if (i == (int)data.length() || data[i] == '|') {
      String segment = data.substring(start, i);
      start = i + 1;

      if (segment.startsWith("F") && segment.length() >= 4 && segment[2] == ':') {
        int fNum = segment.substring(1, 2).toInt(); // 1..6
        if (fNum >= 1 && fNum <= MAX_FEEDERS) {
          int fIdx = fNum - 1;
          String fData = segment.substring(3);

          // Split by comma: relayState, voltage1, voltage2, faultState, mcbState
          int c1 = fData.indexOf(',');
          int c2 = fData.indexOf(',', c1 + 1);
          int c3 = fData.indexOf(',', c2 + 1);
          int c4 = fData.indexOf(',', c3 + 1);

          if (c1 > 0) {
            // If feeder is deactivated, keep status DEACTIVATED and do not alter logical control state
            if (node->feeders[fIdx].status != FEEDER_STATUS_DEACTIVATED) {
              node->feeders[fIdx].relayState = fData.substring(0, c1);
              if (fIdx == 0) {
                node->lightState = node->feeders[0].relayState;
              }
            }
            
            if (c2 > 0) {
              node->feeders[fIdx].voltage1 = fData.substring(c1 + 1, c2).toFloat();
              node->feeders[fIdx].voltage1Valid = (node->feeders[fIdx].voltage1 >= MIN_VALID_MAINS_VOLTAGE && node->feeders[fIdx].voltage1 <= MAX_VALID_MAINS_VOLTAGE);
              node->feeders[fIdx].lastVoltageUpdate = now;
              
              if (c3 > 0) {
                node->feeders[fIdx].voltage2 = fData.substring(c2 + 1, c3).toFloat();
                node->feeders[fIdx].voltage2Valid = (node->feeders[fIdx].voltage2 >= MIN_VALID_MAINS_VOLTAGE && node->feeders[fIdx].voltage2 <= MAX_VALID_MAINS_VOLTAGE);
                
                if (c4 > 0) {
                  node->feeders[fIdx].faultState = fData.substring(c3 + 1, c4);
                  node->feeders[fIdx].mcbR = (fData.substring(c4 + 1) == "1");
                } else {
                  node->feeders[fIdx].faultState = fData.substring(c3 + 1);
                }
              } else {
                node->feeders[fIdx].faultState = fData.substring(c2 + 1);
              }
            } else {
              node->feeders[fIdx].faultState = fData.substring(c1 + 1);
            }

            node->feeders[fIdx].lastHeartbeat = now;
            node->feeders[fIdx].loraState = "ONLINE";
            node->feeders[fIdx].onlineState = "ONLINE";
          }
        }
      }
    }
  }
}

String FeederManager::getFeederStatusIcon(const FeederState &fdr, const NodeRecord *node) {
  if (!node || node->onlineState == "OFFLINE") return "🔴";
  if (fdr.faultState != "OK" && fdr.faultState.length() > 0 && fdr.faultState != "NONE") return "🔴";
  if (node->onlineState == "DEGRADED") return "🟡";
  if (node->onlineState == "ONLINE") return "🟢";
  return "🔴";
}

String FeederManager::getFeederStatusText(const FeederState &fdr, const NodeRecord *node) {
  if (!node || node->onlineState == "OFFLINE") return "🔴 Offline";
  if (fdr.faultState == "MCB_TRIP") return "🔴 MCB Trip";
  if (fdr.faultState == "SENSOR_ERROR") return "🔴 Sensor Err";
  if (fdr.faultState == "OVER_VOLT" || fdr.faultState == "UNDER_VOLT") return "🔴 Volt Fault";
  if (fdr.faultState != "OK" && fdr.faultState.length() > 0 && fdr.faultState != "NONE") return "🔴 Fault";
  if (node->onlineState == "DEGRADED") return "🟡 Degraded";
  if (node->onlineState == "ONLINE") return "🟢 Online";
  return "🔴 Offline";
}

String FeederManager::getLoRaStatusIcon(const FeederState &fdr, const NodeRecord *node) {
  if (!node || node->onlineState == "OFFLINE") return "🔴";
  if (node->onlineState == "DEGRADED") return "🟡";
  if (node->onlineState == "ONLINE") return "🟢";
  return "🔴";
}

String FeederManager::buildFeederStatusTable(const String &selectedNodeId) {
  NodeRecord *targetNode = nullptr;
  if (selectedNodeId.length() > 0) {
    targetNode = meshMgr.findNode(selectedNodeId);
  }
  if (!targetNode && meshMgr.getNodeCount() > 0) {
    targetNode = meshMgr.getNode(0); // Primary default node
  }

  String poleHeader = targetNode ? ("Pole: *" + targetNode->name + "* (`" + targetNode->logicalId + "`)\n") : "System Feeders:\n";
  String msg = "📊 *FEEDER STATUS*\n" + poleHeader + "━━━━━━━━━━━━━━━━━━━━\n\n";

  if (!targetNode) {
    msg += "⚠️ *No registered field nodes available.*\n";
    msg += "Awaiting node registration.\n";
    return msg;
  }

  msg += "```\n";
  msg += "Feeder | Status  | Relay | V-1  | V-2  | LoRa\n";
  msg += "-------+---------+-------+------+------+-----\n";

  unsigned long now = millis();

  for (int i = 0; i < MAX_FEEDERS; i++) {
    const FeederState &f = targetNode->feeders[i];
    
    char fIdBuf[8];
    snprintf(fIdBuf, sizeof(fIdBuf), "F%d", i + 1);

    // Status column
    String stIcon = (targetNode->onlineState == "ONLINE") ? "Online " : (targetNode->onlineState == "DEGRADED" ? "Degrade" : "Offline");
    if (f.faultState != "OK" && f.faultState.length() > 0 && f.faultState != "NONE") {
      stIcon = "Fault  ";
    }

    // Relay column
    String rly = (f.relayState == "ON") ? "ON " : "OFF";

    // Voltage-1 column
    float v1Val = (f.voltage1 > 0) ? f.voltage1 : targetNode->voltage;
    char v1Buf[12];
    if (v1Val > 0) {
      snprintf(v1Buf, sizeof(v1Buf), "%3.0fV", v1Val);
    } else {
      snprintf(v1Buf, sizeof(v1Buf), "  0V");
    }

    // Voltage-2 column
    float v2Val = (f.voltage2 > 0) ? f.voltage2 : ((targetNode->voltage > 0) ? targetNode->voltage : 0.0f);
    char v2Buf[12];
    if (v2Val > 0) {
      snprintf(v2Buf, sizeof(v2Buf), "%3.0fV", v2Val);
    } else {
      snprintf(v2Buf, sizeof(v2Buf), "  0V");
    }

    // LoRa column
    String loraIcon = (targetNode->onlineState == "ONLINE") ? "OK" : (targetNode->onlineState == "DEGRADED" ? "DEG" : "OFF");

    char rowBuf[80];
    snprintf(rowBuf, sizeof(rowBuf), "%-6s | %-7s | %-5s | %-4s | %-4s | %s\n",
             fIdBuf, stIcon.c_str(), rly.c_str(), v1Buf, v2Buf, loraIcon.c_str());
    msg += String(rowBuf);
  }

  msg += "```\n";
  
  if (targetNode->voltageUpdatedAt > 0) {
    unsigned long ageSec = (now - targetNode->voltageUpdatedAt) / 1000;
    if (ageSec > 180) {
      msg += "⚠️ _Telemetry STALE (last update: " + String(ageSec) + "s ago)_\n";
    } else {
      msg += "⏱ _Last Telemetry Update: " + String(ageSec) + "s ago_\n";
    }
  } else {
    msg += "⚠️ _Awaiting live telemetry from field node._\n";
  }

  return msg;
}

String FeederManager::buildCompactFeederTable(const String &selectedNodeId) {
  NodeRecord *targetNode = nullptr;
  if (selectedNodeId.length() > 0) {
    targetNode = meshMgr.findNode(selectedNodeId);
  }
  if (!targetNode && meshMgr.getNodeCount() > 0) {
    targetNode = meshMgr.getNode(0);
  }

  if (!targetNode) return "⚠️ No node telemetry available.";

  String out = "";
  for (int i = 0; i < MAX_FEEDERS; i++) {
    const FeederState &f = targetNode->feeders[i];
    String icon = getFeederStatusIcon(f, targetNode);
    String lora = getLoRaStatusIcon(f, targetNode);
    String rly  = (f.relayState == "ON") ? "ON " : "OFF";
    
    float v1Val = (f.voltage1 > 0) ? f.voltage1 : targetNode->voltage;
    String v1 = (v1Val > 0) ? (String(v1Val, 0) + "V") : "0V";

    float v2Val = (f.voltage2 > 0) ? f.voltage2 : ((targetNode->voltage > 0) ? targetNode->voltage : 0.0f);
    String v2 = (v2Val > 0) ? (String(v2Val, 0) + "V") : "0V";

    out += "F" + String(i + 1) + " | " + icon + " | " + rly + " | " + v1 + " | " + v2 + " | " + lora + "\n";
  }

  return out;
}

FeederState* FeederManager::getFeeder(const String &nodeId, uint8_t feederIndex) {
  NodeRecord *node = meshMgr.findNode(nodeId);
  if (node && feederIndex < MAX_FEEDERS) {
    return &node->feeders[feederIndex];
  }
  return nullptr;
}

void FeederManager::processMetrologyPacket(const String &type, const String &src, const String &data, int rssi) {
  NodeRecord *node = meshMgr.findNode(src);
  if (!node) return;

  unsigned long now = millis();
  node->lastSeen = now;
  node->rssi = rssi;

  // Inform AlertManager of active RF communication
  alertMgr.onNodePacketReceived(node->logicalId.c_str(), rssi, 0, now);

  if (type == "TEL_VOLT") {
    // Format: P=MSCB-P001|TS=17000000|VR=230.2,0|VY=231.5,0|VB=229.8,0|F=50.02|SYM=1
    int pVR = data.indexOf("VR=");
    int pVY = data.indexOf("VY=");
    int pVB = data.indexOf("VB=");
    int pF  = data.indexOf("F=");

    if (pVR >= 0) {
      int comma = data.indexOf(',', pVR);
      int pipe = data.indexOf('|', pVR);
      node->commonVoltageR = (comma > 0) ? data.substring(pVR + 3, comma).toFloat() : data.substring(pVR + 3).toFloat();
      int stCode = (comma > 0 && pipe > comma) ? data.substring(comma + 1, pipe).toInt() : 0;
      node->commonVoltageValidityR = (MeasurementValidity)stCode;
      node->voltage = node->commonVoltageR;
      node->commonVoltageStateR = (node->commonVoltageR >= MIN_VALID_MAINS_VOLTAGE && node->commonVoltageR <= MAX_VALID_MAINS_VOLTAGE) ? "VALID" : "NO_SIGNAL";
      node->voltageState = node->commonVoltageStateR;
    }
    if (pVY >= 0) {
      int comma = data.indexOf(',', pVY);
      int pipe = data.indexOf('|', pVY);
      node->commonVoltageY = (comma > 0) ? data.substring(pVY + 3, comma).toFloat() : data.substring(pVY + 3).toFloat();
      int stCode = (comma > 0 && pipe > comma) ? data.substring(comma + 1, pipe).toInt() : 0;
      node->commonVoltageValidityY = (MeasurementValidity)stCode;
      node->commonVoltageStateY = (node->commonVoltageY >= MIN_VALID_MAINS_VOLTAGE && node->commonVoltageY <= MAX_VALID_MAINS_VOLTAGE) ? "VALID" : "NO_SIGNAL";
    }
    if (pVB >= 0) {
      int comma = data.indexOf(',', pVB);
      int pipe = data.indexOf('|', pVB);
      node->commonVoltageB = (comma > 0) ? data.substring(pVB + 3, comma).toFloat() : data.substring(pVB + 3).toFloat();
      int stCode = (comma > 0 && pipe > comma) ? data.substring(comma + 1, pipe).toInt() : 0;
      node->commonVoltageValidityB = (MeasurementValidity)stCode;
      node->commonVoltageStateB = (node->commonVoltageB >= MIN_VALID_MAINS_VOLTAGE && node->commonVoltageB <= MAX_VALID_MAINS_VOLTAGE) ? "VALID" : "NO_SIGNAL";
    }
    if (pF >= 0) {
      int endF = data.indexOf('|', pF);
      node->gridFrequency = (endF > 0) ? data.substring(pF + 2, endF).toFloat() : data.substring(pF + 2).toFloat();
    }
    node->voltageUpdatedAt = now;

    // Evaluate electrical voltage faults with confirmation & debounce
    alertMgr.evaluateVoltage(node->logicalId.c_str(), 'R', node->commonVoltageR, node->commonVoltageValidityR, now);
    alertMgr.evaluateVoltage(node->logicalId.c_str(), 'Y', node->commonVoltageY, node->commonVoltageValidityY, now);
    alertMgr.evaluateVoltage(node->logicalId.c_str(), 'B', node->commonVoltageB, node->commonVoltageValidityB, now);
    alertMgr.evaluatePhaseLoss(node->logicalId.c_str(), node->commonVoltageR, node->commonVoltageY, node->commonVoltageB, now);

    // Propagate bus voltages to feeders
    for (int f = 0; f < MAX_FEEDERS; f++) {
      node->feeders[f].voltage1 = node->commonVoltageR;
      node->feeders[f].voltage2 = node->commonVoltageY;
      node->feeders[f].voltage3 = node->commonVoltageB;
      node->feeders[f].voltage1Valid = (node->commonVoltageStateR == "VALID");
      node->feeders[f].voltage2Valid = (node->commonVoltageStateY == "VALID");
      node->feeders[f].voltage3Valid = (node->commonVoltageStateB == "VALID");
      node->feeders[f].lastVoltageUpdate = now;
      node->feeders[f].lastHeartbeat = now;
      node->feeders[f].loraState = "ONLINE";
      node->feeders[f].onlineState = "ONLINE";
    }
  }
  else if (type == "TEL_FDR1" || type == "TEL_FDR2") {
    // Format: P=MSCB-P001|B=1|F1:12.4,12.5,12.1,8250,0.86,0|F2:...|F3:...
    int start = 0;
    for (int i = 0; i <= (int)data.length(); i++) {
      if (i == (int)data.length() || data[i] == '|') {
        String segment = data.substring(start, i);
        start = i + 1;

        if (segment.startsWith("F") && segment.length() >= 4 && segment[2] == ':') {
          int fNum = segment.substring(1, 2).toInt(); // 1..6
          if (fNum >= 1 && fNum <= MAX_FEEDERS) {
            int fIdx = fNum - 1;
            String fData = segment.substring(3);

            int c1 = fData.indexOf(',');
            int c2 = fData.indexOf(',', c1 + 1);
            int c3 = fData.indexOf(',', c2 + 1);
            int c4 = fData.indexOf(',', c3 + 1);
            int c5 = fData.indexOf(',', c4 + 1);

            if (c1 > 0 && c2 > 0 && c3 > 0) {
              node->feeders[fIdx].currentR = fData.substring(0, c1).toFloat();
              node->feeders[fIdx].currentY = fData.substring(c1 + 1, c2).toFloat();
              node->feeders[fIdx].currentB = fData.substring(c2 + 1, c3).toFloat();
              node->feeders[fIdx].lastMeasurementTime = now;

              if (c4 > 0) {
                node->feeders[fIdx].totalPowerW = fData.substring(c3 + 1, c4).toFloat();
                if (c5 > 0) {
                  node->feeders[fIdx].powerFactor = fData.substring(c4 + 1, c5).toFloat();
                  int stCode = fData.substring(c5 + 1).toInt();
                  MeasurementValidity val = (MeasurementValidity)stCode;
                  node->feeders[fIdx].currentValidityR = val;
                  node->feeders[fIdx].currentValidityY = val;
                  node->feeders[fIdx].currentValidityB = val;

                  if (stCode == 5) node->feeders[fIdx].faultState = "OVER_CURR";
                  else if (stCode == 6) node->feeders[fIdx].faultState = "IMBALANCE";
                  else node->feeders[fIdx].faultState = "OK";
                }
              }

              // Evaluate overcurrent with confirmation duration and hysteresis
              alertMgr.evaluateOvercurrent(node->logicalId.c_str(), fNum, 'R',
                                           node->feeders[fIdx].currentR, CONF_OVERCURRENT_THRESH_A,
                                           node->feeders[fIdx].currentValidityR, now);
              alertMgr.evaluateOvercurrent(node->logicalId.c_str(), fNum, 'Y',
                                           node->feeders[fIdx].currentY, CONF_OVERCURRENT_THRESH_A,
                                           node->feeders[fIdx].currentValidityY, now);
              alertMgr.evaluateOvercurrent(node->logicalId.c_str(), fNum, 'B',
                                           node->feeders[fIdx].currentB, CONF_OVERCURRENT_THRESH_A,
                                           node->feeders[fIdx].currentValidityB, now);
              alertMgr.evaluateSensorFault(node->logicalId.c_str(), fNum, 'R',
                                           node->feeders[fIdx].currentValidityR, now);

              if (fIdx == 0) {
                node->current = node->feeders[0].currentR;
                node->power   = node->feeders[0].totalPowerW;
              }

              node->feeders[fIdx].lastHeartbeat = now;
              node->feeders[fIdx].loraState = "ONLINE";
              node->feeders[fIdx].onlineState = "ONLINE";
            }
          }
        }
      }
    }
  }
  else if (type == "TEL_ALERT") {
    int pAlert = data.indexOf("ALERT=");
    if (pAlert >= 0) {
      int endA = data.indexOf('|', pAlert);
      String alertType = (endA > 0) ? data.substring(pAlert + 6, endA) : data.substring(pAlert + 6);
      node->fault = alertType;
      node->onlineState = "DEGRADED";

      // If it contains MCB, evaluate MCB trip
      if (alertType.indexOf("MCB") >= 0) {
        alertMgr.evaluateMcbTrip(node->logicalId.c_str(), 1, 'R', true, now);
      } else {
        uint32_t eId = alertMgr.recordEvent(EVT_VOLTAGE_FAULT, ALERT_SEV_P0_CRITICAL,
                                            node->logicalId.c_str(), 0, '-', alertType.c_str());
        alertMgr.enqueueAlert(eId, ALERT_SEV_P0_CRITICAL, node->logicalId.c_str(), node->area,
                              alertType.c_str(), node->primaryParent.c_str(),
                              node->backupParent.c_str(), (uint32_t)(now / 1000),
                              (time(nullptr) > 1700000000UL), "Critical Alert", alertType.c_str());
      }
      Serial.printf("[GW][METROLOGY] ⚠️ CRITICAL ALERT received from %s: %s\n", src.c_str(), alertType.c_str());
    }
  }
}

String FeederManager::build21ChannelMeterTable(const String &selectedNodeId) {
  NodeRecord *node = meshMgr.findNode(selectedNodeId);
  if (!node) return "❌ Node `" + selectedNodeId + "` not found in database.";

  String out = "⚡ *MSCB SMART POLE METROLOGY*\n";
  out += "📍 *Pole:* `" + node->name + "` (`" + node->logicalId + "`)\n";
  out += "📶 *RSSI:* `" + String(node->rssi) + " dBm` | ⏱ *Freq:* `" + String(node->gridFrequency, 2) + " Hz`\n\n";

  out += "🔹 *3-Phase Common Bus Voltage:*\n";
  out += "• Phase R: `" + String(node->commonVoltageR, 1) + " V` (" + node->commonVoltageStateR + ")\n";
  out += "• Phase Y: `" + String(node->commonVoltageY, 1) + " V` (" + node->commonVoltageStateY + ")\n";
  out += "• Phase B: `" + String(node->commonVoltageB, 1) + " V` (" + node->commonVoltageStateB + ")\n\n";

  out += "🔹 *Feeder 3-Phase Current & Power (18 Channels):*\n";
  out += "```\n";
  out += "Fdr | R(A)  | Y(A)  | B(A)  | P(kW) | PF \n";
  out += "----+-------+-------+-------+-------+----\n";

  for (uint8_t f = 0; f < MAX_FEEDERS; f++) {
    char row[64];
    snprintf(row, sizeof(row), " F%d | %5.1f | %5.1f | %5.1f | %5.2f | %4.2f\n",
             f + 1,
             node->feeders[f].currentR,
             node->feeders[f].currentY,
             node->feeders[f].currentB,
             node->feeders[f].totalPowerW / 1000.0f,
             node->feeders[f].powerFactor);
    out += String(row);
  }
  out += "```\n";

  if (node->fault != "OK" && node->fault.length() > 0) {
    out += "⚠️ *Active Fault:* `" + node->fault + "`\n";
  }

  return out;
}

// ─────────────────────────────────────────────────────────────
//  PROMPT 15 SECTION 13: TELEGRAM INTEGRATION APIS
// ─────────────────────────────────────────────────────────────
uint8_t FeederManager::allocateCommandId() {
  uint8_t id = nextCommandId++;
  if (nextCommandId == 0) nextCommandId = 1;
  return id;
}

void FeederManager::storeCommandRecord(uint8_t cmdId, const String &nodeId, uint8_t feederId, const String &action) {
  cmdHistory[cmdHistoryHead].commandId = cmdId;
  cmdHistory[cmdHistoryHead].nodeId = nodeId;
  cmdHistory[cmdHistoryHead].feederId = feederId;
  cmdHistory[cmdHistoryHead].requestedAction = action;
  cmdHistory[cmdHistoryHead].status = "PENDING";
  cmdHistory[cmdHistoryHead].requestTimeMs = millis();
  cmdHistory[cmdHistoryHead].ackTimeMs = 0;
  cmdHistory[cmdHistoryHead].stage = "STAGE_A_SENT";
  cmdHistory[cmdHistoryHead].details = "";
  cmdHistoryHead = (cmdHistoryHead + 1) % MAX_COMMAND_HISTORY;
}

void FeederManager::recordCommandAck(uint8_t commandId, const String &nodeId, const String &status, const String &details) {
  for (int i = 0; i < MAX_COMMAND_HISTORY; i++) {
    if (cmdHistory[i].commandId == commandId && (cmdHistory[i].nodeId == nodeId || nodeId.length() == 0)) {
      cmdHistory[i].status = status;
      cmdHistory[i].ackTimeMs = millis();
      cmdHistory[i].details = details;
      int pStage = details.indexOf("STAGE=");
      if (pStage >= 0) {
        int endS = details.indexOf('|', pStage);
        cmdHistory[i].stage = (endS > 0) ? details.substring(pStage + 6, endS) : details.substring(pStage + 6);
      }
      break;
    }
  }
}

CommandExecutionRecord FeederManager::getCommandExecutionStatus(uint8_t commandId) {
  for (int i = 0; i < MAX_COMMAND_HISTORY; i++) {
    if (cmdHistory[i].commandId == commandId) {
      return cmdHistory[i];
    }
  }
  CommandExecutionRecord empty;
  empty.commandId = commandId;
  empty.status = "NOT_FOUND";
  return empty;
}

PoleControlStatus FeederManager::getPoleControlStatus(const String &nodeId) {
  PoleControlStatus st;
  st.nodeId = nodeId;
  st.name = "";
  st.onlineState = "OFFLINE";
  st.activeRelayMask = 0;
  st.activeContactorMask = 0;
  st.mcbTripMask = 0;
  st.emergencyStop = false;
  st.manualMode = false;
  st.voltageR = 0.0f;
  st.voltageY = 0.0f;
  st.voltageB = 0.0f;
  st.frequency = 50.0f;

  NodeRecord *node = meshMgr.findNode(nodeId);
  if (!node) return st;

  st.name = node->name;
  st.onlineState = node->onlineState;
  st.voltageR = node->commonVoltageR > 0 ? node->commonVoltageR : node->voltage;
  st.voltageY = node->commonVoltageY;
  st.voltageB = node->commonVoltageB;
  st.frequency = node->gridFrequency;

  for (uint8_t f = 0; f < MAX_FEEDERS; f++) {
    st.feeders[f] = getFeederStatus(nodeId, f + 1);
    if (st.feeders[f].relayOn) st.activeRelayMask |= (1 << f);
    if (st.feeders[f].contactorClosed) st.activeContactorMask |= (1 << f);
    if (!st.feeders[f].mcbOk) st.mcbTripMask |= (1 << f);
  }

  return st;
}

FeederControlStatus FeederManager::getFeederStatus(const String &nodeId, uint8_t feederIdx) {
  FeederControlStatus fst;
  fst.feederId = feederIdx;
  fst.name = "Feeder " + String(feederIdx);
  fst.relayOn = false;
  fst.contactorClosed = false;
  fst.mcbOk = true;
  fst.faultState = "OFFLINE";
  fst.currentR = 0.0f;
  fst.currentY = 0.0f;
  fst.currentB = 0.0f;
  fst.activePowerW = 0.0f;
  fst.powerFactor = 1.0f;
  fst.energyKwh = 0.0f;

  if (feederIdx < 1 || feederIdx > MAX_FEEDERS) return fst;
  uint8_t f = feederIdx - 1;

  NodeRecord *node = meshMgr.findNode(nodeId);
  if (!node) return fst;

  const FeederState &fdr = node->feeders[f];
  fst.name = fdr.name.length() > 0 ? fdr.name : ("Feeder " + String(feederIdx));
  fst.relayOn = (fdr.relayState == "ON");
  fst.contactorClosed = fdr.contactor;
  fst.mcbOk = fdr.mcbR;
  fst.faultState = fdr.faultState;
  fst.currentR = fdr.currentR;
  fst.currentY = fdr.currentY;
  fst.currentB = fdr.currentB;
  fst.activePowerW = fdr.totalPowerW;
  fst.powerFactor = fdr.powerFactor;
  fst.energyKwh = fdr.energyKwh;
  return fst;
}

bool FeederManager::requestFeederOn(const String &nodeId, uint8_t feederIdx, const String &callerId, uint32_t durationSec) {
  if (feederIdx < 1 || feederIdx > MAX_FEEDERS) return false;
  uint8_t cmdId = allocateCommandId();
  storeCommandRecord(cmdId, nodeId, feederIdx, "ON");

  String cmdStr = "FEEDER_" + String(feederIdx) + "_ON|ID=" + String(cmdId) + "|AUTH=MSCB_AUTH_OK";
  if (durationSec > 0) cmdStr += "|DUR=" + String(durationSec);

  loraMgr.queueCommand(callerId, nodeId, cmdStr, PRIO_MEDIUM);
  Serial.printf("[GW][FEEDER] Queued FEEDER_%d_ON for node %s (CmdId: %u)\n", feederIdx, nodeId.c_str(), cmdId);
  return true;
}

bool FeederManager::requestFeederOff(const String &nodeId, uint8_t feederIdx, const String &callerId) {
  if (feederIdx < 1 || feederIdx > MAX_FEEDERS) return false;
  uint8_t cmdId = allocateCommandId();
  storeCommandRecord(cmdId, nodeId, feederIdx, "OFF");

  String cmdStr = "FEEDER_" + String(feederIdx) + "_OFF|ID=" + String(cmdId) + "|AUTH=MSCB_AUTH_OK";
  loraMgr.queueCommand(callerId, nodeId, cmdStr, PRIO_MEDIUM);
  Serial.printf("[GW][FEEDER] Queued FEEDER_%d_OFF for node %s (CmdId: %u)\n", feederIdx, nodeId.c_str(), cmdId);
  return true;
}

bool FeederManager::requestGroupControl(const String &nodeId, uint8_t feederMask, bool turnOn, uint32_t staggerMs, const String &callerId) {
  uint8_t cmdId = allocateCommandId();
  storeCommandRecord(cmdId, nodeId, 0, turnOn ? "GROUP_ON" : "GROUP_OFF");

  String cmdStr = (turnOn ? "GROUP_ON|" : "GROUP_OFF|") + String(feederMask) + "|ID=" + String(cmdId) + "|STAGGER=" + String(staggerMs);
  loraMgr.queueCommand(callerId, nodeId, cmdStr, PRIO_MEDIUM);
  Serial.printf("[GW][FEEDER] Queued Group Control for node %s (Mask: 0x%02X, Req: %s)\n",
                nodeId.c_str(), feederMask, turnOn ? "ON" : "OFF");
  return true;
}

String FeederManager::getMcbStatus(const String &nodeId, uint8_t feederIdx) {
  if (feederIdx < 1 || feederIdx > MAX_FEEDERS) return "UNKNOWN";
  NodeRecord *node = meshMgr.findNode(nodeId);
  if (!node) return "NODE_OFFLINE";

  const FeederState &fdr = node->feeders[feederIdx - 1];
  if (fdr.faultState == "MCB_TRIP") return "TRIPPED";
  return fdr.mcbR ? "NORMAL" : "TRIPPED";
}

String FeederManager::getContactorFeedback(const String &nodeId, uint8_t feederIdx) {
  if (feederIdx < 1 || feederIdx > MAX_FEEDERS) return "UNKNOWN";
  NodeRecord *node = meshMgr.findNode(nodeId);
  if (!node) return "NODE_OFFLINE";

  const FeederState &fdr = node->feeders[feederIdx - 1];
  if (fdr.faultState == "CONTACTOR_WELDED") return "WELDED";
  if (fdr.faultState == "COIL_FAIL") return "COIL_FAIL";
  return fdr.contactor ? "CLOSED" : "OPEN";
}

PhaseCurrentStatus FeederManager::getPhaseCurrentStatus(const String &nodeId, uint8_t feederIdx) {
  PhaseCurrentStatus st;
  st.feederId = feederIdx;
  st.currentR = 0.0f;
  st.currentY = 0.0f;
  st.currentB = 0.0f;
  st.isImbalanced = false;
  st.imbalancePercent = 0.0f;
  st.isOverCurrent = false;
  st.statusText = "NORMAL";

  if (feederIdx < 1 || feederIdx > MAX_FEEDERS) {
    st.statusText = "INVALID_FEEDER";
    return st;
  }

  NodeRecord *node = meshMgr.findNode(nodeId);
  if (!node) {
    st.statusText = "NODE_OFFLINE";
    return st;
  }

  const FeederState &fdr = node->feeders[feederIdx - 1];
  st.currentR = fdr.currentR;
  st.currentY = fdr.currentY;
  st.currentB = fdr.currentB;

  float maxI = fdr.currentR > fdr.currentY ? (fdr.currentR > fdr.currentB ? fdr.currentR : fdr.currentB) : (fdr.currentY > fdr.currentB ? fdr.currentY : fdr.currentB);
  float minI = fdr.currentR < fdr.currentY ? (fdr.currentR < fdr.currentB ? fdr.currentR : fdr.currentB) : (fdr.currentY < fdr.currentB ? fdr.currentY : fdr.currentB);
  float avgI = (fdr.currentR + fdr.currentY + fdr.currentB) / 3.0f;

  if (avgI > 0.5f) {
    st.imbalancePercent = ((maxI - minI) / avgI) * 100.0f;
    if (st.imbalancePercent > 30.0f) {
      st.isImbalanced = true;
      st.statusText = "CURRENT_IMBALANCE";
    }
  }

  if (maxI > POLE_OVERCURRENT_TRIP_A) {
    st.isOverCurrent = true;
    st.statusText = "OVER_CURRENT";
  }

  return st;
}

PhaseVoltageStatus FeederManager::getPhaseVoltageStatus(const String &nodeId) {
  PhaseVoltageStatus st;
  st.nodeId = nodeId;
  st.voltageR = 0.0f;
  st.voltageY = 0.0f;
  st.voltageB = 0.0f;
  st.frequency = 50.0f;
  st.phaseLossR = false;
  st.phaseLossY = false;
  st.phaseLossB = false;
  st.isUnderVolt = false;
  st.isOverVolt = false;
  st.statusText = "NORMAL";

  NodeRecord *node = meshMgr.findNode(nodeId);
  if (!node) {
    st.statusText = "NODE_OFFLINE";
    return st;
  }

  st.voltageR = node->commonVoltageR > 0 ? node->commonVoltageR : node->voltage;
  st.voltageY = node->commonVoltageY;
  st.voltageB = node->commonVoltageB;
  st.frequency = node->gridFrequency;

  if (st.voltageR < 20.0f) st.phaseLossR = true;
  if (st.voltageY < 20.0f && st.voltageY > 0) st.phaseLossY = true;
  if (st.voltageB < 20.0f && st.voltageB > 0) st.phaseLossB = true;

  if (st.phaseLossR || st.phaseLossY || st.phaseLossB) {
    st.statusText = "PHASE_LOSS";
  } else if (st.voltageR < VOLTAGE_UNDERVOLT_THRESH) {
    st.isUnderVolt = true;
    st.statusText = "UNDER_VOLTAGE";
  } else if (st.voltageR > VOLTAGE_OVERVOLT_THRESH) {
    st.isOverVolt = true;
    st.statusText = "OVER_VOLTAGE";
  }

  return st;
}
