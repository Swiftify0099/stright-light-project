/*
 * =====================================================================
 *  LORAMANAGER.CPP — FIELD POLE LORA RADIO & PROTOCOL MANAGER
 * =====================================================================
 */

#include "LoRaManager.h"
#include "MeshManager.h"
#include "CommandManager.h"
#include "StorageManager.h"

LoRaManager loraMgr;

LoRaManager::LoRaManager() : seqHead(0), nodeMasterSeq(200) {
  for (int i = 0; i < MAX_SEQ_CACHE; i++) {
    seqCache[i].src = "";
    seqCache[i].seq = 0;
  }
}

void LoRaManager::init() {
  Serial.println(F("[LoRa] Initializing Field Pole SX1278..."));
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
  Serial.println(F("[LoRa] Field Pole Radio Ready in Continuous RX"));
}

uint16_t LoRaManager::nextSeq() {
  nodeMasterSeq++;
  if (nodeMasterSeq == 0 || nodeMasterSeq < 200) nodeMasterSeq = 200;
  return nodeMasterSeq;
}

String LoRaManager::buildPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String mySrc = storageMgr.getNodeId().length() > 0 ? storageMgr.getNodeId() : storageMgr.getHwNodeId();
  String nh = (nextHop.length() > 0) ? nextHop : BROADCAST_ID;
  return type + "|" + mySrc + "|" + dst + "|" + nh + "|" + String(ttl) + "|" + String(seq) + "|" + data;
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

void LoRaManager::txMesh(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String packet = buildPacket(type, dst, nextHop, ttl, seq, data);

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  LoRa.receive();

  Serial.printf("[POLE][LORA] TX: %s\n", packet.c_str());
}

void LoRaManager::processIncomingLoRa() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;

  String raw = "";
  while (LoRa.available()) raw += (char)LoRa.read();
  raw.trim();
  if (raw.length() == 0) {
    LoRa.receive();
    return;
  }

  int rssi = LoRa.packetRssi();
  int snr  = (int)LoRa.packetSnr();

  ParsedPacket pkt;
  if (!parsePacket(raw, rssi, snr, pkt)) {
    LoRa.receive();
    return;
  }

  Serial.printf("[POLE] RX Type: %s | Src: %s | Dst: %s | NextHop: %s | Seq: %u | RSSI: %d dBm | Data: %s\n",
                pkt.type.c_str(), pkt.src.c_str(), pkt.dst.c_str(), pkt.nextHop.c_str(), pkt.seq, rssi, pkt.data.c_str());

  meshMgr.addOrUpdateNeighbor(pkt.src, rssi, snr);
  meshMgr.addOrUpdateRoute(pkt.src, pkt.src, 1, rssi);

  String myHw  = storageMgr.getHwNodeId();
  String myLog = storageMgr.getNodeId();

  // Determine if this packet is intended for this node
  bool isForMe = false;
  if (pkt.dst == BROADCAST_ID || pkt.dst == "ALL") {
    isForMe = true;
  } else if (myLog.length() > 0 && (pkt.dst == myLog || pkt.dst.equalsIgnoreCase(myLog))) {
    isForMe = true;
  } else if (myHw.length() > 0 && (pkt.dst == myHw || pkt.dst.equalsIgnoreCase(myHw))) {
    isForMe = true;
  } else if (pkt.type == "CMD" && (myLog.length() == 0 || !storageMgr.isRegistered())) {
    // If not yet registered or logical ID empty, accept commands targeted by HW ID or registration commands
    if (pkt.dst == myHw || pkt.data.startsWith("REGISTERED") || pkt.dst.startsWith("SL-") || pkt.dst.startsWith("FR-") || pkt.dst.startsWith("P")) {
      isForMe = true;
    }
  }

  // Deduplication check
  bool isDuplicate = false;
  for (int i = 0; i < MAX_SEQ_CACHE; i++) {
    if (seqCache[i].src == pkt.src && seqCache[i].seq == pkt.seq && pkt.seq != 0) {
      isDuplicate = true;
      break;
    }
  }

  if (!isDuplicate) {
    seqCache[seqHead].src = pkt.src;
    seqCache[seqHead].seq = pkt.seq;
    seqHead = (seqHead + 1) % MAX_SEQ_CACHE;
  } else if (!isForMe) {
    LoRa.receive();
    return;
  }

  // Process commands intended for this node
  if (isForMe) {
    if (pkt.type == "CMD") {
      cmdMgr.processCommand(pkt.data, pkt.src, pkt.seq, rssi, snr);
    }
    else if (pkt.type == "CHK") {
      meshMgr.startNeighborHealthCheck(pkt.src, pkt.data);
    }
    else if (pkt.type == "NB_PING") {
      txMesh("NB_PONG", pkt.src, pkt.src, 1, nextSeq(), pkt.data);
    }
    else if (pkt.type == "NB_PONG") {
      meshMgr.onNeighborPong(pkt.src);
    }
  }
  // Controlled Mesh Forwarding: ONLY forward if explicitly addressed as the intermediate router
  else if (pkt.ttl > 1 && pkt.src != myLog && pkt.src != myHw) {
    if ((myLog.length() > 0 && pkt.nextHop == myLog) || (myHw.length() > 0 && pkt.nextHop == myHw)) {
      String forwardNextHop = meshMgr.findNextHop(pkt.dst);
      Serial.printf("[POLE][MESH] Forwarding for %s via %s (TTL: %d)\n",
                    pkt.dst.c_str(), forwardNextHop.c_str(), pkt.ttl - 1);
      txMesh(pkt.type, pkt.dst, forwardNextHop, pkt.ttl - 1, pkt.seq, pkt.data);
    }
  }

  LoRa.receive();
}
