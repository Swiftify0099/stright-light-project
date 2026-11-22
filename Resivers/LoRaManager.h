/*
 * =====================================================================
 *  LORAMANAGER.H — FIELD POLE LORA RADIO & PROTOCOL MANAGER (V10)
 * =====================================================================
 */

#ifndef LORA_MANAGER_H
#define LORA_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "config.h"
#include "Types.h"

class LoRaManager {
public:
  LoRaManager();
  void init();

  // Packet building & parsing (8-field mesh frame with 7-field fallback)
  String buildPacket(const String &type, const String &src, const String &dst, const String &nextHop, const String &sender, int ttl, uint16_t seq, const String &data);
  bool   parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out);

  // Mesh transmission
  // For new locally-originated packets (src = myId, sender = myId)
  void   txMesh(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);
  // For forwarded packets (preserves original src, updates sender = myId)
  void   txMesh(const String &type, const String &src, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);

  // Incoming LoRa packet processing
  void   processIncomingLoRa();

  uint16_t nextSeq();

private:
  PoleSeqCacheEntry seqCache[MAX_SEQ_CACHE];
  int seqHead;
  uint16_t nodeMasterSeq;
};

extern LoRaManager loraMgr;

#endif // LORA_MANAGER_H
