/*
 * =====================================================================
 *  LORAMANAGER.H — GATEWAY LORA RADIO & PROTOCOL MANAGER
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

  // Packet builder & parser
  String buildPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);
  bool   parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out);

  // Transmission & Command Queue
  void   dispatchLoRaPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);
  void   queueCommand(const String &chatId, const String &nodeId, const String &cmd, uint8_t priority = PRIO_MEDIUM);
  void   processCommandQueue();

  // RX processing
  void   processIncomingLoRa();

  // Sequence generator & status
  uint16_t getNextSeq();
  String getLastPacketInfo() const;

private:
  GwSeqCacheEntry seqCache[MAX_SEQ_CACHE];
  int seqHead;
  uint16_t masterSeq;

  PendingCommand cmdQueue[MAX_CMD_QUEUE];
  String lastLoRaPacketInfo;
};

extern LoRaManager loraMgr;

#endif // LORA_MANAGER_H
