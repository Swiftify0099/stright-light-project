/*
 * =====================================================================
 *  LORAMANAGER.CPP — FIELD POLE LORA RADIO & PROTOCOL MANAGER (V10)
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

String LoRaManager::buildPacket(const String &type, const String &src, const String &dst, const String &nextHop, const String &sender, int ttl, uint16_t seq, const String &data) {
  String nh = (nextHop.length() > 0) ? nextHop : BROADCAST_ID;
  String snd = (sender.length() > 0) ? sender : src;
  return type + "|" + src + "|" + dst + "|" + nh + "|" + snd + "|" + String(ttl) + "|" + String(seq) + "|" + data;
}

bool LoRaManager::parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out) {
  out.valid = false;
  out.rssi = rssi;
  out.snr  = snr;

  String fields[8];
  int fieldCount = 0;
  int start = 0;
  for (int i = 0; i <= (int)raw.length(); i++) {
    bool isEnd = (i == (int)raw.length());
    bool isPipe = (!isEnd && raw[i] == '|' && fieldCount < 7);
    if (isEnd || isPipe) {
      fields[fieldCount++] = raw.substring(start, i);
      start = i + 1;
    }
  }

  if (fieldCount < 6) return false;

  // Check 8-field mesh frame vs 7-field legacy frame
  if (fieldCount >= 8) {
    out.type    = fields[0];
    out.src     = fields[1];
    out.dst     = fields[2];
    out.nextHop = fields[3];
    out.sender  = fields[4];
    out.ttl     = fields[5].toInt();
    out.seq     = (uint16_t)fields[6].toInt();
    out.data    = fields[7];
  } else if (fieldCount == 7) {
    out.type    = fields[0];
    out.src     = fields[1];
    out.dst     = fields[2];
    out.nextHop = fields[3];
    out.sender  = fields[1]; // In 7-field, sender defaults to src
    out.ttl     = fields[4].toInt();
    out.seq     = (uint16_t)fields[5].toInt();
    out.data    = fields[6];
  } else {
    out.type    = fields[0];
    out.src     = fields[1];
    out.dst     = fields[2];
    out.nextHop = fields[3];
    out.sender  = fields[1];
    out.ttl     = fields[4].toInt();
    out.seq     = (uint16_t)fields[5].toInt();
    out.data    = "";
  }

  out.valid = true;
  return true;
}

// Locally originated transmission
void LoRaManager::txMesh(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String mySrc = storageMgr.getNodeId().length() > 0 ? storageMgr.getNodeId() : storageMgr.getHwNodeId();
  txMesh(type, mySrc, dst, nextHop, ttl, seq, data);
}

// Forwarded or explicit source transmission
void LoRaManager::txMesh(const String &type, const String &src, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String mySender = storageMgr.getNodeId().length() > 0 ? storageMgr.getNodeId() : storageMgr.getHwNodeId();
  String packet = buildPacket(type, src, dst, nextHop, mySender, ttl, seq, data);

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

  Serial.printf("[POLE] RX Type: %s | Src: %s | Sender: %s | Dst: %s | NextHop: %s | TTL: %d | Seq: %u | RSSI: %d dBm\n",
                pkt.type.c_str(), pkt.src.c_str(), pkt.sender.c_str(), pkt.dst.c_str(), pkt.nextHop.c_str(), pkt.ttl, pkt.seq, rssi);

  // Update physical link neighbor table using the immediate transmitter
  meshMgr.addOrUpdateNeighbor(pkt.sender, rssi, snr);

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
  }

  // ── ROUTING CONTROL PACKETS (Handled before destination filtering) ──
  if (pkt.type == "BEACON") {
    meshMgr.onBeaconReceived(pkt.sender, pkt.data, rssi, snr);
    LoRa.receive();
    return;
  }
  else if (pkt.type == "RREQ") {
    // If not orphaned, reply with our route parameters
    meshMgr.sendRouteReply(pkt.src);
    LoRa.receive();
    return;
  }
  else if (pkt.type == "RREP") {
    String myHw = storageMgr.getHwNodeId();
    String myLog = storageMgr.getNodeId();
    if (pkt.dst == myLog || pkt.dst == myHw) {
      uint8_t r = INFINITY_RANK;
      uint16_t c = 9999;
      String p = "";
      int ir = pkt.data.indexOf("RANK=");
      if (ir >= 0) {
        int er = pkt.data.indexOf('|', ir);
        r = (uint8_t)pkt.data.substring(ir + 5, er > 0 ? er : pkt.data.length()).toInt();
      }
      int ic = pkt.data.indexOf("COST=");
      if (ic >= 0) {
        int ec = pkt.data.indexOf('|', ic);
        c = (uint16_t)pkt.data.substring(ic + 5, ec > 0 ? ec : pkt.data.length()).toInt();
      }
      int ip = pkt.data.indexOf("PARENT=");
      if (ip >= 0) {
        int ep = pkt.data.indexOf('|', ip);
        p = pkt.data.substring(ip + 7, ep > 0 ? ep : pkt.data.length());
      }
      meshMgr.onRouteReply(pkt.src, r, c, p, rssi, snr);
    }
    LoRa.receive();
    return;
  }

  // If duplicate packet and not for route control, drop it now (prevents forwarding flood)
  if (isDuplicate) {
    LoRa.receive();
    return;
  }

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
    if (pkt.dst == myHw || pkt.data.startsWith("REGISTERED") || pkt.dst.startsWith("SL-") || pkt.dst.startsWith("FR-")) {
      isForMe = true;
    }
  }

  // ── 1. DESTINATION PROCESSING (PACKET FOR THIS NODE) ──
  if (isForMe) {
    if (pkt.type == "CMD") {
      cmdMgr.processCommand(pkt.data, pkt.src, pkt.seq, rssi, snr);
    }
    else if (pkt.type == "CHK") {
      meshMgr.startNeighborHealthCheck(pkt.src, pkt.data);
    }
    else if (pkt.type == "NB_PING") {
      txMesh("NB_PONG", pkt.src, pkt.sender, 1, nextSeq(), pkt.data);
    }
    else if (pkt.type == "NB_PONG") {
      meshMgr.onNeighborPong(pkt.src);
    }
    else if (pkt.type == "ACK") {
      meshMgr.recordTxResult(pkt.sender, true);
    }
  }

  // ── 2. MULTI-HOP FORWARDING ENGINE ──
  String forwardNextHop;
  if (!isForMe && meshMgr.shouldForward(pkt, forwardNextHop)) {
    // Reverse route learning: intermediate node learns downstream path to pkt.src
    int hopsTravelled = DEFAULT_TTL - pkt.ttl + 1;
    meshMgr.addOrUpdateRoute(pkt.src, pkt.sender, hopsTravelled, 0, rssi, snr);

    meshMgr.recordForwardedPacket();
    Serial.printf("[POLE][MESH] ⏩ Forwarding %s for %s via %s (TTL: %d -> %d)\n",
                  pkt.type.c_str(), pkt.dst.c_str(), forwardNextHop.c_str(), pkt.ttl, pkt.ttl - 1);

    txMesh(pkt.type, pkt.src, pkt.dst, forwardNextHop, pkt.ttl - 1, pkt.seq, pkt.data);
  }

  LoRa.receive();
}
