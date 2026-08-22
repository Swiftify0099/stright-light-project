/*
 * =====================================================================
 *  FEEDERMANAGER.CPP — GATEWAY FEEDER TELEMETRY & STATUS PROCESSOR
 * =====================================================================
 */

#include "FeederManager.h"
#include "MeshManager.h"

FeederManager feederMgr;

FeederManager::FeederManager() {}

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
            node->feeders[fIdx].relayState = fData.substring(0, c1);
            if (fIdx == 0) {
              node->lightState = node->feeders[0].relayState;
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
