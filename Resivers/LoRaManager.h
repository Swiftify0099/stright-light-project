/*
 * =====================================================================
 *  LORAMANAGER.H — FIELD POLE LORA RADIO & PROTOCOL MANAGER
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

  // Packet building & parsing
  String buildPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);
  bool   parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out);

  // Mesh transmission
  void   txMesh(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);

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
