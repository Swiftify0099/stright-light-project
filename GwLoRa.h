/*
 * =====================================================================
 *  GWLORA.H — GATEWAY SX1278 LORA RADIO DRIVER (POINT-TO-POINT)
 *  Platform: ESP32-S3-WROOM-N16R8 + SX1278 RA-02 433 MHz
 *
 *  RESPONSIBILITIES:
 *  - Hardware & SPI Bus Management for ESP32-S3
 *  - SX1278 Register Configuration & Silicon Version Verification
 *  - Indian WPC G.S.R. 347(E) Regulatory Compliance Guardrails
 *  - Non-Blocking Asynchronous TX/RX Engine with FreeRTOS Awareness
 *  - Priority TX Queue & Bounded RX Buffer
 *  - Listen-Before-Talk (LBT) Carrier Sense Multiple Access
 *  - Exact Semtech LoRa Airtime & Rolling 10% Duty Cycle Tracking
 *  - P2P Transaction Timeout, Retry Scheduler & Quality Monitoring
 * =====================================================================
 */

#ifndef GW_LORA_H
#define GW_LORA_H

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include "RadioConfig.h"
#include "RadioTypes.h"

class GwLoRa {
public:
  GwLoRa();

  // ─────────────────────────────────────────────────────────────
  //  1. LIFECYCLE & INITIALIZATION
  // ─────────────────────────────────────────────────────────────
  // Initializes SPI, asserts hardware reset, verifies silicon version (0x12),
  // validates frequency against WPC regulations, clamps TX power, and configures modem.
  bool begin(uint32_t freqHz = LORA_DEFAULT_FREQ, int txPowerDbm = LORA_DEFAULT_TXPOW);

  // Shuts down transceiver into ultra-low power sleep
  void end();

  // Hardware reset pulse and full register restoration
  bool reinitialize();

  // ─────────────────────────────────────────────────────────────
  //  2. NON-BLOCKING ENGINE & SERVICE ROUTINE
  // ─────────────────────────────────────────────────────────────
  // Must be called periodically from loop() or a dedicated FreeRTOS task.
  // Drives state machine, manages LBT, async TX, pending ACKs, and timeouts.
  void service();

  // ─────────────────────────────────────────────────────────────
  //  3. TRANSMISSION API (POINT-TO-POINT)
  // ─────────────────────────────────────────────────────────────
  // Enqueues a structured packet into the priority TX queue
  bool enqueuePacket(const RadioPacket_t &pkt);

  // Send a diagnostic TEST packet to a target pole node
  bool sendTestPacket(const char* targetNode, const char* payload, PacketPriority prio = PRIO_MEDIUM);

  // Send a HELLO presence beacon
  bool sendHello(const char* targetNode = BROADCAST_NODE_ID);

  // Send a STATUS telemetry query
  bool sendStatusQuery(const char* targetNode);

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
  // Read Semtech silicon version directly from register 0x42 (Expects 0x12)
  uint8_t readSiliconVersion();

  // Dynamically configure carrier frequency (with strict WPC guardrail)
  bool setFrequency(uint32_t freqHz);

  // Dynamically configure TX power (clamped to <= 10 dBm ERP)
  bool setTxPower(int powerDbm);

  // Get current operating frequency in Hz
  uint32_t getFrequency() const { return _currentFreqHz; }

  // Get current conducted TX power in dBm
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

  // Calculate Semtech LoRa packet airtime in milliseconds
  uint32_t calculateAirtimeMs(uint8_t payloadBytes) const;

  // Print full diagnostic report to Stream
  void printStatus(Stream &out) const;

  // Print regulatory compliance statement
  void printRegulatoryStatus(Stream &out) const;

  // Sequence generator
  uint16_t getNextSeq();

  // Test hook: Simulate artificial packet loss for Test Scenario 10
  void setArtificialDropRate(uint8_t percent) { _artificialDropRate = percent; }

private:
  // Hardware pins
  int8_t _pinSs;
  int8_t _pinRst;
  int8_t _pinDio0;
  int8_t _pinSck;
  int8_t _pinMiso;
  int8_t _pinMosi;

  // Operating parameters
  uint32_t _currentFreqHz;
  int      _currentTxPowerDbm;
  uint16_t _masterSeq;

  // State machine & error tracking
  RadioState _state;
  RadioError _lastError;
  RadioMetrics_t _metrics;

  // Priority TX Queue
  RadioPacket_t _txQueue[RADIO_TX_QUEUE_SIZE];
  uint8_t       _txQueueCount;

  // Bounded RX Queue
  RadioPacket_t _rxQueue[RADIO_RX_QUEUE_SIZE];
  uint8_t       _rxHead;
  uint8_t       _rxTail;
  uint8_t       _rxCount;

  // Active transmission & ACK tracking
  RadioPacket_t _activeTxPkt;
  uint32_t      _txStartTimeMs;
  uint32_t      _txAirtimeMs;
  uint32_t      _ackExpiryTimeMs;
  uint8_t       _retryCount;
  uint32_t      _backoffExpiryMs;

  // Carrier Sense (LBT) tracking
  uint32_t      _carrierSenseStartMs;
  uint8_t       _carrierSenseRetries;

  // Deduplication cache
  struct SeqCacheEntry {
    char     srcId[NODE_ID_MAX_LEN];
    uint16_t seq;
  } _seqCache[16];
  uint8_t _seqCacheHead;

  // Diagnostic flags
  uint8_t _artificialDropRate;

  // Internal helper methods
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

  // Low-level SPI direct register transfer (independent of library)
  uint8_t directReadReg(uint8_t addr);
  void    directWriteReg(uint8_t addr, uint8_t val);
};

// Global instance declaration
extern GwLoRa gwRadio;

#endif // GW_LORA_H
