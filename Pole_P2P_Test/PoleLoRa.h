/*
 * =====================================================================
 *  POLELORA.H — SECONDARY POLE NODE SX1278 LORA DRIVER (POINT-TO-POINT)
 *  Platform: ESP32-WROOM-32 + SX1278 RA-02 433 MHz
 *
 *  RESPONSIBILITIES:
 *  - Hardware & VSPI Bus Management for ESP32-WROOM-32
 *  - SX1278 Register Configuration & Silicon Version Verification (0x12)
 *  - Indian WPC G.S.R. 347(E) Regulatory Compliance Guardrails
 *  - Non-Blocking Continuous RX Engine with Automatic Reliable ACK
 *  - Priority TX Queue & Bounded RX Buffer
 *  - Listen-Before-Talk (LBT) Carrier Sense Multiple Access
 *  - Deduplication Cache & Automatic Repeat reQuest (ARQ) Handling
 *  - Exact Semtech LoRa Airtime & Rolling 10% Duty Cycle Tracking
 * =====================================================================
 */

#ifndef POLE_LORA_H
#define POLE_LORA_H

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "RadioConfig.h"
#include "RadioTypes.h"

class PoleLoRa {
public:
  PoleLoRa();

  // ─────────────────────────────────────────────────────────────
  //  1. LIFECYCLE & INITIALIZATION
  // ─────────────────────────────────────────────────────────────
  // Initializes SPI, asserts hardware reset, verifies silicon version (0x12),
  // validates frequency against WPC regulations, clamps TX power, and configures modem.
  bool begin(const char* nodeId = DEFAULT_POLE_NODE_ID,
             uint32_t freqHz = LORA_DEFAULT_FREQ,
             int txPowerDbm = LORA_DEFAULT_TXPOW);

  // Shuts down transceiver into sleep mode
  void end();

  // Hardware reset pulse and full register restoration
  bool reinitialize();

  // ─────────────────────────────────────────────────────────────
  //  2. NON-BLOCKING ENGINE & SERVICE ROUTINE
  // ─────────────────────────────────────────────────────────────
  // Drives state machine, manages LBT, async TX, automatic ACKs, and queue dispatch.
  void service();

  // ─────────────────────────────────────────────────────────────
  //  3. TRANSMISSION & RESPONDER API
  // ─────────────────────────────────────────────────────────────
  // Enqueues a packet into the priority TX queue
  bool enqueuePacket(const RadioPacket_t &pkt);

  // Send a diagnostic TEST packet to Gateway
  bool sendTestToGateway(const char* payload, PacketPriority prio = PRIO_MEDIUM);

  // Send a HELLO presence beacon to Gateway
  bool sendHelloToGateway();

  // Send an immediate high-priority ACK to Gateway
  bool sendAckToGateway(uint16_t seq, const char* diagnosticData = nullptr);

  // ─────────────────────────────────────────────────────────────
  //  4. RECEPTION API
  // ─────────────────────────────────────────────────────────────
  // Checks if a received packet is ready in the RX queue
  bool hasPacket();

  // Dequeues the next received packet
  bool getPacket(RadioPacket_t &outPkt);

  // ─────────────────────────────────────────────────────────────
  //  5. RADIO CONFIGURATION & VERIFICATION
  // ─────────────────────────────────────────────────────────────
  uint8_t readSiliconVersion();
  bool setFrequency(uint32_t freqHz);
  bool setTxPower(int powerDbm);
  void setNodeId(const char* id);

  const char* getNodeId() const { return _nodeId; }
  uint32_t getFrequency() const { return _currentFreqHz; }
  int getTxPower() const { return _currentTxPowerDbm; }

  // ─────────────────────────────────────────────────────────────
  //  6. CHANNEL QUALITY & METRICS MONITORING
  // ─────────────────────────────────────────────────────────────
  const RadioMetrics_t& getMetrics() const { return _metrics; }
  void resetMetrics() { _metrics.reset(); }

  RadioState getState() const { return _state; }
  const char* getStateString() const { return getRadioStateName(_state); }

  RadioError getLastError() const { return _lastError; }
  const char* getLastErrorString() const { return getRadioErrorString(_lastError); }

  int getLastRssi() const { return _metrics.lastRssi; }
  float getLastSnr() const { return _metrics.lastSnr; }
  int getInstantaneousRssi();

  uint32_t calculateAirtimeMs(uint8_t payloadBytes) const;
  void printStatus(Stream &out) const;
  void printRegulatoryStatus(Stream &out) const;

  uint16_t getNextSeq();

  // Test hook: Simulate artificial packet loss for Test Scenario 10
  void setArtificialDropRate(uint8_t percent) { _artificialDropRate = percent; }

  // Test hook: Simulate duplicate packet transmission
  bool sendDuplicateTest(const char* targetNode, uint16_t existingSeq);

private:
  char    _nodeId[NODE_ID_MAX_LEN];
  int8_t  _pinSs;
  int8_t  _pinRst;
  int8_t  _pinDio0;
  int8_t  _pinSck;
  int8_t  _pinMiso;
  int8_t  _pinMosi;

  uint32_t _currentFreqHz;
  int      _currentTxPowerDbm;
  uint16_t _nodeSeq;

  RadioState     _state;
  RadioError     _lastError;
  RadioMetrics_t _metrics;

  RadioPacket_t  _txQueue[RADIO_TX_QUEUE_SIZE];
  uint8_t        _txQueueCount;

  RadioPacket_t  _rxQueue[RADIO_RX_QUEUE_SIZE];
  uint8_t        _rxHead;
  uint8_t        _rxTail;
  uint8_t        _rxCount;

  RadioPacket_t  _activeTxPkt;
  uint32_t       _txStartTimeMs;
  uint32_t       _txAirtimeMs;
  uint32_t       _ackExpiryTimeMs;
  uint8_t        _retryCount;
  uint32_t       _backoffExpiryMs;

  uint32_t       _carrierSenseStartMs;
  uint8_t        _carrierSenseRetries;

  struct SeqCacheEntry {
    char     srcId[NODE_ID_MAX_LEN];
    uint16_t seq;
  } _seqCache[16];
  uint8_t _seqCacheHead;

  uint8_t _artificialDropRate;

  void setRadioError(RadioError err);
  void transitionState(RadioState newState);
  bool pushTxQueue(const RadioPacket_t &pkt);
  bool popTxQueue(RadioPacket_t &outPkt);
  bool pushRxQueue(const RadioPacket_t &pkt);
  bool isDuplicate(const char* srcId, uint16_t seq);
  void recordSeq(const char* srcId, uint16_t seq);

  void handleTxCompletion();
  void handleRxCompletion();
  void startAsyncTx(const RadioPacket_t &pkt);
  bool performCarrierSense();

  uint8_t directReadReg(uint8_t addr);
  void    directWriteReg(uint8_t addr, uint8_t val);
};

// Global instance declaration
extern PoleLoRa poleRadio;

#endif // POLE_LORA_H
