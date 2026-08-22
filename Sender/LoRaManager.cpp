/*
 * =====================================================================
 *  LORAMANAGER.CPP — GATEWAY LORA RADIO & PROTOCOL MANAGER
 * =====================================================================
 */

#include "LoRaManager.h"
#include "MeshManager.h"
#include "TelegramManager.h"
#include "TelegramMenus.h"
#include "FeederManager.h"
#include "ResetManager.h"
#include "StorageManager.h"

LoRaManager loraMgr;

LoRaManager::LoRaManager() : seqHead(0), masterSeq(100), lastLoRaPacketInfo("None yet") {
  for (int i = 0; i < MAX_SEQ_CACHE; i++) {
    seqCache[i].src = "";
    seqCache[i].seq = 0;
  }
  for (int i = 0; i < MAX_CMD_QUEUE; i++) {
    cmdQueue[i].active = false;
  }
}

void LoRaManager::init() {
  Serial.println(F("[LoRa] Initializing Gateway SX1278..."));
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println(F("[LoRa] FAILED! Check SPI wiring and SX1278 power."));
    while (1) { delay(1000); }
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TXPOW, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  if (LORA_CRC_ENABLED) LoRa.enableCrc();
  LoRa.receive();

  Serial.printf("[LoRa] Frequency = %.1f MHz, SF=%d, BW=%.1f kHz, TXPow=%d dBm\n",
                LORA_FREQ / 1E6, LORA_SF, LORA_BW / 1E3, LORA_TXPOW);
  Serial.println(F("[LoRa] Gateway Radio Ready"));
}

uint16_t LoRaManager::getNextSeq() {
  masterSeq++;
  if (masterSeq == 0 || masterSeq < 100) masterSeq = 100;
  return masterSeq;
}

String LoRaManager::getLastPacketInfo() const {
  return lastLoRaPacketInfo;
}

String LoRaManager::buildPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String nh = (nextHop.length() > 0) ? nextHop : BROADCAST_ID;
  return type + "|" + String(GATEWAY_ID) + "|" + dst + "|" + nh + "|" + String(ttl) + "|" + String(seq) + "|" + data;
}

bool LoRaManager::parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out) {
  out.valid = false;
  out.rssi = rssi;
  out.snr  = snr;

  String fields[7];
  int fieldCount = 0;
  int start = 0;
  for (int i = 0; i <= (int)raw.length(); i++) {
    bool isEnd = (i == (int)raw.length());
    bool isPipe = (!isEnd && raw[i] == '|' && fieldCount < 6);
    if (isEnd || isPipe) {
      fields[fieldCount++] = raw.substring(start, i);
      start = i + 1;
    }
  }

  if (fieldCount < 6) return false;

  out.type    = fields[0];
  out.src     = fields[1];
  out.dst     = fields[2];
  out.nextHop = fields[3];
  out.ttl     = fields[4].toInt();
  out.seq     = (uint16_t)fields[5].toInt();
  out.data    = (fieldCount == 7) ? fields[6] : "";
  out.valid   = true;
  return true;
}

void LoRaManager::dispatchLoRaPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String packet = buildPacket(type, dst, nextHop, ttl, seq, data);
  
  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  LoRa.receive();
  
  Serial.printf("[GW][LORA] TX: %s\n", packet.c_str());
  lastLoRaPacketInfo = "TX " + type + " to " + dst + " (Seq " + String(seq) + ")";
}

void LoRaManager::queueCommand(const String &chatId, const String &nodeId, const String &cmd, uint8_t priority) {
  uint16_t seq = getNextSeq();
  Serial.printf("[GW] Queueing command seq=%u for '%s' -> %s (Prio: %d)\n", seq, cmd.c_str(), nodeId.c_str(), priority);

  for (int i = 0; i < MAX_CMD_QUEUE; i++) {
    if (!cmdQueue[i].active) {
      cmdQueue[i].active = true;
      cmdQueue[i].seq = seq;
      cmdQueue[i].chatId = chatId;
      cmdQueue[i].messageId = 0;
      cmdQueue[i].targetNodeId = nodeId;
      cmdQueue[i].cmdString = cmd;
      cmdQueue[i].priority = priority;
      cmdQueue[i].generation = 0;
      cmdQueue[i].sentAt = 0;
      cmdQueue[i].retries = 0;
      cmdQueue[i].nextHop = meshMgr.findRouteNextHop(nodeId);
      cmdQueue[i].pingSentMs = 0;
      return;
    }
  }
  Serial.println(F("[GW][CMD] ⚠️ Command queue full!"));
  if (chatId.length() > 0 && tgMgr.isTelegramOnline()) {
    tgMgr.sendMessage(chatId, "⚠️ *Command Queue Full!* Please wait a moment.", "Markdown");
  }
}

void LoRaManager::processCommandQueue() {
  unsigned long now = millis();
  int bestIdx = -1;
  int highestPrio = -1;

  for (int i = 0; i < MAX_CMD_QUEUE; i++) {
    if (cmdQueue[i].active) {
      if (cmdQueue[i].sentAt == 0) {
        if ((int)cmdQueue[i].priority > highestPrio) {
          highestPrio = (int)cmdQueue[i].priority;
          bestIdx = i;
        }
      }
      else if (now - cmdQueue[i].sentAt >= ACK_TIMEOUT_MS) {
        if (cmdQueue[i].retries < MAX_CMD_RETRIES) {
          if ((int)cmdQueue[i].priority > highestPrio) {
            highestPrio = (int)cmdQueue[i].priority;
            bestIdx = i;
          }
        } else {
          Serial.printf("[GW][CMD] ❌ Command '%s' to %s timed out after %d retries! (Seq: %u)\n",
                        cmdQueue[i].cmdString.c_str(), cmdQueue[i].targetNodeId.c_str(),
                        MAX_CMD_RETRIES, cmdQueue[i].seq);
          
          storageMgr.addGwLog(cmdQueue[i].cmdString.c_str(), cmdQueue[i].targetNodeId.c_str(), 1);
          
          if (cmdQueue[i].chatId.length() > 0 && tgMgr.isTelegramOnline() && cmdQueue[i].targetNodeId != "ALL") {
            String failMsg = "⚠️ *Command Timeout*\n\n"
                             "📍 *Target:* `" + cmdQueue[i].targetNodeId + "`\n"
                             "⚡ *Command:* `" + cmdQueue[i].cmdString + "`\n"
                             "⚠️ *Status:* No ACK response after " + String(MAX_CMD_RETRIES) + " retries.\n"
                             "💡 *Note:* Device may be powered off or out of RF range.";
            tgMgr.sendMessage(cmdQueue[i].chatId, failMsg, "Markdown");
          }

          cmdQueue[i].active = false;
        }
      }
    }
  }

  if (bestIdx >= 0) {
    cmdQueue[bestIdx].sentAt = millis();
    cmdQueue[bestIdx].retries++;

    if (cmdQueue[bestIdx].retries > 1) {
      Serial.printf("[GW][CMD] Retrying (%d/%d) '%s' -> %s (Seq: %u)...\n",
                    cmdQueue[bestIdx].retries, MAX_CMD_RETRIES,
                    cmdQueue[bestIdx].cmdString.c_str(), cmdQueue[bestIdx].targetNodeId.c_str(),
                    cmdQueue[bestIdx].seq);
    } else {
      Serial.printf("[GW] LoRa TX = %s -> %s (Seq: %u)\n",
                    cmdQueue[bestIdx].cmdString.c_str(), cmdQueue[bestIdx].targetNodeId.c_str(), cmdQueue[bestIdx].seq);
    }

    dispatchLoRaPacket("CMD", cmdQueue[bestIdx].targetNodeId, cmdQueue[bestIdx].nextHop,
                       DEFAULT_TTL, cmdQueue[bestIdx].seq, cmdQueue[bestIdx].cmdString);
  }
}

void LoRaManager::processIncomingLoRa() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;

  String raw = "";
  while (LoRa.available()) raw += (char)LoRa.read();
  raw.trim();
  if (raw.length() == 0) return;

  int rssi = LoRa.packetRssi();
  int snr  = (int)LoRa.packetSnr();

  ParsedPacket pkt;
  if (!parsePacket(raw, rssi, snr, pkt)) return;

  // Deduplication cache
  for (int i = 0; i < MAX_SEQ_CACHE; i++) {
    if (seqCache[i].src == pkt.src && seqCache[i].seq == pkt.seq && pkt.seq != 0) {
      return;
    }
  }
  seqCache[seqHead].src = pkt.src;
  seqCache[seqHead].seq = pkt.seq;
  seqHead = (seqHead + 1) % MAX_SEQ_CACHE;

  Serial.printf("[GW][LORA] RX Type: %s | Src: %s | Dst: %s | RSSI: %d dBm | Data: %s\n",
                pkt.type.c_str(), pkt.src.c_str(), pkt.dst.c_str(), rssi, pkt.data.c_str());

  lastLoRaPacketInfo = "RX " + pkt.type + " from " + pkt.src + " (" + String(rssi) + "dBm)";

  // Update routing table from sender
  meshMgr.updateRouteTable(pkt.src, pkt.src, 1, rssi);

  // CRITICAL REQUIREMENT 23: Gateway lastSeen must update from ANY valid packet from a node
  int updateIdx = meshMgr.findNodeIndex(pkt.src);
  if (updateIdx >= 0) {
    NodeRecord *n = meshMgr.getNode(updateIdx);
    if (n) {
      n->lastSeen = millis();
      n->rssi = rssi;
      n->snr = snr;

      // CRITICAL REQUIREMENT 24 & Farmer Recovery: Recovery alert when transitioning from OFFLINE -> ONLINE
      if (n->onlineState == "OFFLINE") {
        n->onlineState = "ONLINE";
        if (tgMgr.isTelegramOnline() && n->registered) {
          String typeTitle = (n->type == "FARMER") ? "🌾 *FARMER NODE RECOVERED*" : "🟢 *POLE RECOVERED*";
          String alert = typeTitle + "\n\n"
                         "📍 *Name:* *" + n->name + "* (`" + n->logicalId + "`)\n"
                         "📶 *RSSI:* `" + String(rssi) + " dBm`\n"
                         "🗺️ *Route:* `Gateway ← " + pkt.src + "`\n"
                         "⏱ *Status:* Re-established mesh communication.";
          tgMgr.sendMessage(OWNER_CHAT, alert, "Markdown");
        }
      } else {
        n->onlineState = "ONLINE";
      }
    }
  }

  // ── 1. NEW_NODE PACKET ──
  if (pkt.type == "NEW_NODE") {
    String hwId = pkt.src;
    int idxHw = pkt.data.indexOf("HW_ID=");
    if (idxHw >= 0) {
      int endHw = pkt.data.indexOf('|', idxHw);
      hwId = (endHw > 0) ? pkt.data.substring(idxHw + 6, endHw) : pkt.data.substring(idxHw + 6);
    }

    int nodeIdx = meshMgr.findNodeIndex(hwId);
    if (nodeIdx >= 0 && meshMgr.getNode(nodeIdx)->registered) {
      NodeRecord *node = meshMgr.getNode(nodeIdx);
      String regConfirm = node->logicalId + "|" + node->name + "|" + node->type;
      dispatchLoRaPacket("CMD", hwId, hwId, DEFAULT_TTL, getNextSeq(), "REGISTERED|" + regConfirm);
      Serial.printf("[GW][REG] Node %s already registered. Re-sent confirm.\n", hwId.c_str());
    } else {
      int pIdx = meshMgr.addPendingReg(hwId, rssi);
      if (pIdx >= 0 && tgMgr.isTelegramOnline()) {
        String msg = "🆕 *NEW NODE DETECTED*\n\n"
                     "⚙️ *Hardware ID:* `" + hwId + "`\n"
                     "📶 *RSSI:* `" + String(rssi) + " dBm`\n\n"
                     "Do you want to register this field node?";
        String keyboard = "{\"inline_keyboard\":[["
                          "{\"text\":\"✅ Register\",\"callback_data\":\"REG:" + hwId + "\"},"
                          "{\"text\":\"❌ Reject\",\"callback_data\":\"RREJ:" + hwId + "\"}"
                          "]]}";
        tgMgr.sendMessageWithInlineKeyboard(OWNER_CHAT, msg, "Markdown", keyboard);
      }
    }
  }
  // ── 2. NBHEALTHREPORT PACKET ──
  else if (pkt.type == "NBHEALTHREPORT") {
    meshMgr.onNeighborHealthReport(pkt.src, pkt.data, rssi);
  }
  // ── 3. ACK PACKET ──
  else if (pkt.type == "ACK") {
    Serial.printf("[GW] ACK received seq=%u from %s: %s\n", pkt.seq, pkt.src.c_str(), pkt.data.c_str());

    int s1 = pkt.data.indexOf('|');
    int s2 = pkt.data.indexOf('|', s1 + 1);
    String ackCmd    = (s1 > 0) ? pkt.data.substring(0, s1) : pkt.data;
    String ackStatus = (s1 > 0 && s2 > 0) ? pkt.data.substring(s1 + 1, s2) : (s1 > 0 ? pkt.data.substring(s1 + 1) : "OK");
    String ackState  = (s2 > 0) ? pkt.data.substring(s2 + 1) : "";

    int srcIdx = meshMgr.findNodeIndex(pkt.src);
    if (srcIdx < 0) srcIdx = meshMgr.addOrUpdateNode(pkt.src, pkt.src, "STREET", rssi, snr);
    NodeRecord *node = meshMgr.getNode(srcIdx);

    if (node) {
      if (ackState.indexOf("STATE=ON") >= 0 || ackState == "ON" || ackState.startsWith("ON")) {
        node->lightState = "ON";
        node->feeders[0].relayState = "ON";
      }
      if (ackState.indexOf("STATE=OFF") >= 0 || ackState == "OFF" || ackState.startsWith("OFF")) {
        node->lightState = "OFF";
        node->feeders[0].relayState = "OFF";
      }

      int pV = ackState.indexOf("V=");
      if (pV >= 0) {
        int endV = ackState.indexOf('|', pV);
        node->voltage = ackState.substring(pV + 2, endV > 0 ? endV : ackState.length()).toFloat();
        node->voltageUpdatedAt = millis();
        node->feeders[0].voltage1 = node->voltage;
        node->feeders[0].lastVoltageUpdate = millis();
      }
      int pVs = ackState.indexOf("VS=");
      if (pVs >= 0) {
        int endVs = ackState.indexOf('|', pVs);
        node->voltageState = ackState.substring(pVs + 3, endVs > 0 ? endVs : ackState.length());
        node->feeders[0].voltage1State = node->voltageState;
      }
      int pA = ackState.indexOf("A=");
      if (pA >= 0) {
        int endA = ackState.indexOf('|', pA);
        node->current = ackState.substring(pA + 2, endA > 0 ? endA : ackState.length()).toFloat();
      }
    }

    if (ackCmd == "RESET") {
      resetMgr.onResetAckReceived(pkt.src, pkt.seq);
    }

    tgMgr.onBatchAckReceived(pkt.src, ackCmd, (node ? node->lightState : ""));

    for (int i = 0; i < MAX_CMD_QUEUE; i++) {
      if (cmdQueue[i].active) {
        bool targetMatch = (cmdQueue[i].targetNodeId == pkt.src || 
                            (node && cmdQueue[i].targetNodeId == node->logicalId) || 
                            (node && cmdQueue[i].targetNodeId == node->hwId) ||
                            cmdQueue[i].targetNodeId == "ALL");

        if (targetMatch && (cmdQueue[i].seq == pkt.seq || cmdQueue[i].cmdString.startsWith(ackCmd))) {
          cmdQueue[i].active = false;
          storageMgr.addGwLog(cmdQueue[i].cmdString.c_str(), pkt.src.c_str(), 0);

          if (cmdQueue[i].chatId.length() > 0 && tgMgr.isTelegramOnline() && 
              cmdQueue[i].targetNodeId != "ALL" && !resetMgr.isResetAllInProgress()) {
            String pName   = node ? node->name : pkt.src;
            String stateSt = node ? node->lightState : "UPDATED";
            bool isFarmer  = (node && node->type == "FARMER");
            
            String confMsg = "━━━━━━━━━━━━━━━━━━━━\n";
            if (ackCmd == "TIMER_ON") {
              confMsg += "⏱ *" + String(isFarmer ? "PUMP TIMER STARTED" : "LIGHT TIMER STARTED") + "*\n";
            } else if (isFarmer) {
              confMsg += "✅ *" + String(stateSt == "ON" ? "PUMP ON CONFIRMED" : "PUMP OFF CONFIRMED") + "*\n";
            } else {
              confMsg += "✅ *" + String(stateSt == "ON" ? "LIGHT ON" : "LIGHT OFF") + "*\n";
            }
            confMsg += "━━━━━━━━━━━━━━━━━━━━\n\n";

            confMsg += String(isFarmer ? "🌾 Farmer Station:\n*" : "💡 Pole:\n*") + pName + "*\n\n";
            confMsg += "Status:\n🟢 Online\n\n";
            confMsg += String(isFarmer ? "Pump State:\n" : "Light State:\n") + String(stateSt == "ON" ? "🟢 RUNNING" : "🔴 OFF") + "\n\n";

            if (node && node->voltage > 0) {
              confMsg += "Voltage:\n⚡ " + String(node->voltage, 1) + " V\n\n";
            } else if (node && node->voltageState == "NO_SIGNAL") {
              confMsg += "Voltage:\n⚡ 0.0 V (Mains OFF)\n\n";
            } else {
              confMsg += "Voltage:\n⚡ 0.0 V\n\n";
            }

            if (node && (node->current > 0 || stateSt == "ON")) {
              confMsg += "Current:\n🔌 " + String(node->current, 2) + " A\n\n";
            }

            confMsg += "LoRa:\n🟢 Online (" + String(rssi) + " dBm)\n\n"
                       "ACK:\n✅ Confirmed\n"
                       "━━━━━━━━━━━━━━━━━━━━";
            tgMgr.sendMessage(cmdQueue[i].chatId, confMsg, "Markdown");
          }
          break;
        }
      }
    }
  }
  // ── 4. LINK_ACK PACKET ──
  else if (pkt.type == "LINK_ACK") {
    for (int i = 0; i < MAX_CMD_QUEUE; i++) {
      if (cmdQueue[i].active && (cmdQueue[i].seq == pkt.seq || cmdQueue[i].cmdString.startsWith("PING"))) {
        unsigned long rtt = (cmdQueue[i].pingSentMs > 0) ? (millis() - cmdQueue[i].pingSentMs) : 0;
        cmdQueue[i].active = false;
        
        if (cmdQueue[i].chatId.length() > 0 && tgMgr.isTelegramOnline()) {
          String linkMsg = "📡 *LoRa Link Test Result*\n\n"
                           "📍 *Node:* `" + pkt.src + "`\n"
                           "⏱ *Round-Trip Time:* `" + String(rtt) + " ms`\n"
                           "📶 *Gateway RX RSSI:* `" + String(rssi) + " dBm`\n"
                           "📶 *Remote Node RSSI:* `" + pkt.data + "`\n"
                           "🟢 *Status:* LINK ACTIVE & HEALTHY";
          tgMgr.sendMessage(cmdQueue[i].chatId, linkMsg, "Markdown");
        }
        break;
      }
    }
  }
  // ── 5. HEARTBEAT PACKET ──
  else if (pkt.type == "HEARTBEAT") {
    int idx = meshMgr.findNodeIndex(pkt.src);
    if (idx < 0) idx = meshMgr.addOrUpdateNode(pkt.src, pkt.src, "STREET", rssi, snr);
    NodeRecord *node = meshMgr.getNode(idx);

    if (node) {
      node->lastHeartbeat = millis();
      
      int pV = pkt.data.indexOf("V=");
      if (pV >= 0) {
        int endV = pkt.data.indexOf('|', pV);
        node->voltage = pkt.data.substring(pV + 2, endV > 0 ? endV : pkt.data.length()).toFloat();
        node->voltageUpdatedAt = millis();
      }
      int pVs = pkt.data.indexOf("VS=");
      if (pVs >= 0) {
        int endVs = pkt.data.indexOf('|', pVs);
        node->voltageState = pkt.data.substring(pVs + 3, endVs > 0 ? endVs : pkt.data.length());
      }
      int pA = pkt.data.indexOf("A=");
      if (pA >= 0) {
        int endA = pkt.data.indexOf('|', pA);
        node->current = pkt.data.substring(pA + 2, endA > 0 ? endA : pkt.data.length()).toFloat();
      }
      int pU = pkt.data.indexOf("UPTIME=");
      if (pU >= 0) {
        int endU = pkt.data.indexOf('|', pU);
        node->uptimeSec = pkt.data.substring(pU + 7, endU > 0 ? endU : pkt.data.length()).toInt();
      }
      int pL = pkt.data.indexOf("LIGHT=");
      if (pL >= 0) {
        int endL = pkt.data.indexOf('|', pL);
        node->lightState = pkt.data.substring(pL + 6, endL > 0 ? endL : pkt.data.length());
        node->feeders[0].relayState = node->lightState;
      }

      if (pkt.data.indexOf("F1:") >= 0) {
        feederMgr.processFeederTelemetry(pkt.src, pkt.data, rssi);
      } else {
        feederMgr.updateFeederFromStatus(pkt.src, node->voltage, 0.0f, node->current, node->lightState, node->fault, rssi);
      }
    }
  }
  // ── 6. STATUS PACKET ──
  else if (pkt.type == "STATUS") {
    int idx = meshMgr.findNodeIndex(pkt.src);
    if (idx < 0) idx = meshMgr.addOrUpdateNode(pkt.src, pkt.src, "STREET", rssi, snr);
    NodeRecord *node = meshMgr.getNode(idx);

    if (node) {
      int pV = pkt.data.indexOf("V=");
      if (pV >= 0) {
        int endV = pkt.data.indexOf('|', pV);
        node->voltage = pkt.data.substring(pV + 2, endV > 0 ? endV : pkt.data.length()).toFloat();
        node->voltageUpdatedAt = millis();
      }
      int pVs = pkt.data.indexOf("VS=");
      if (pVs >= 0) {
        int endVs = pkt.data.indexOf('|', pVs);
        node->voltageState = pkt.data.substring(pVs + 3, endVs > 0 ? endVs : pkt.data.length());
      }
      int pA = pkt.data.indexOf("A=");
      if (pA >= 0) {
        int endA = pkt.data.indexOf('|', pA);
        node->current = pkt.data.substring(pA + 2, endA > 0 ? endA : pkt.data.length()).toFloat();
      }
      int pP = pkt.data.indexOf("P=");
      if (pP >= 0) {
        int endP = pkt.data.indexOf('|', pP);
        node->power = pkt.data.substring(pP + 2, endP > 0 ? endP : pkt.data.length()).toFloat();
      }
      int pK = pkt.data.indexOf("KWH=");
      if (pK >= 0) {
        int endK = pkt.data.indexOf('|', pK);
        node->totalKwh = pkt.data.substring(pK + 4, endK > 0 ? endK : pkt.data.length()).toFloat();
      }
      int pL = pkt.data.indexOf("LGT=");
      if (pL >= 0) {
        int endL = pkt.data.indexOf('|', pL);
        node->lightState = pkt.data.substring(pL + 4, endL > 0 ? endL : pkt.data.length());
        node->feeders[0].relayState = node->lightState;
      }
      int pF = pkt.data.indexOf("FLT=");
      if (pF >= 0) {
        int endF = pkt.data.indexOf('|', pF);
        node->fault = pkt.data.substring(pF + 4, endF > 0 ? endF : pkt.data.length());
      }
      int pU = pkt.data.indexOf("UP=");
      if (pU >= 0) {
        int endU = pkt.data.indexOf('|', pU);
        node->uptimeSec = pkt.data.substring(pU + 3, endU > 0 ? endU : pkt.data.length()).toInt();
      }

      if (pkt.data.indexOf("F1:") >= 0) {
        feederMgr.processFeederTelemetry(pkt.src, pkt.data, rssi);
      } else {
        feederMgr.updateFeederFromStatus(pkt.src, node->voltage, 0.0f, node->current, node->lightState, node->fault, rssi);
      }

      // Check if this status was requested via Telegram
      for (int i = 0; i < MAX_CMD_QUEUE; i++) {
        if (cmdQueue[i].active && (cmdQueue[i].cmdString.startsWith("GET_STATUS") || cmdQueue[i].cmdString.startsWith("STATUS"))) {
          bool match = (cmdQueue[i].targetNodeId == pkt.src || 
                        cmdQueue[i].targetNodeId == node->logicalId || 
                        cmdQueue[i].targetNodeId == node->hwId ||
                        cmdQueue[i].targetNodeId == "ALL");
          if (match) {
            cmdQueue[i].active = false;

            if (cmdQueue[i].chatId.length() > 0 && tgMgr.isTelegramOnline()) {
              bool isFarmer = (node->type == "FARMER");
              String statMsg = "━━━━━━━━━━━━━━━━━━━━\n"
                               "📊 *" + String(isFarmer ? "FARMER STATION STATUS" : "POLE STATUS") + "*\n"
                               "━━━━━━━━━━━━━━━━━━━━\n\n"
                               "📍 *Name:* *" + node->name + "* (`" + node->logicalId + "`)\n"
                               "🟢 *Status:* ONLINE\n"
                               "⚡ *" + String(isFarmer ? "Pump State:*" : "Light State:*") + " " + String(node->lightState == "ON" ? "🟢 ON" : "🔴 OFF") + "\n"
                               "⚡ *Voltage (ZMPT101B):* `" + String(node->voltage, 1) + " V`\n"
                               "🔌 *Current (ACS712):* `" + String(node->current, 2) + " A`\n"
                               "💡 *Active Power:* `" + String(node->power, 1) + " W`\n"
                               "📶 *LoRa RSSI:* `" + String(rssi) + " dBm`\n"
                               "⏱ *Uptime:* `" + tgMenus.formatUptime((unsigned long)node->uptimeSec * 1000) + "`\n\n"
                               "📊 *Feeders (F1–F6):*\n" +
                               feederMgr.buildCompactFeederTable(node->logicalId) + "\n"
                               "━━━━━━━━━━━━━━━━━━━━";
              tgMgr.sendMessage(cmdQueue[i].chatId, statMsg, "Markdown");
            }
            break;
          }
        }
      }
    }
  }
  // ── 7. FDR PACKET ──
  else if (pkt.type == "FDR") {
    feederMgr.processFeederTelemetry(pkt.src, pkt.data, rssi);
  }

  LoRa.receive();
}
