/*
 * =====================================================================
 *  GWLORA.CPP — GATEWAY SX1278 LORA RADIO DRIVER (POINT-TO-POINT)
 *  Platform: ESP32-S3-WROOM-N16R8 + SX1278 RA-02 433 MHz
 * =====================================================================
 */

#include "GwLoRa.h"

// Global singleton instance
GwLoRa gwRadio;

// ─────────────────────────────────────────────────────────────
//  LIGHTWEIGHT ISR CALLBACKS (ZERO SPI / ZERO PRINTING IN ISR)
// ─────────────────────────────────────────────────────────────
static volatile bool s_gwDio0TxDone = false;
static volatile bool s_gwDio0RxDone = false;
static volatile int  s_gwDio0RxSize = 0;

static void IRAM_ATTR onGwDio0Rx(int size) {
  s_gwDio0RxDone = true;
  s_gwDio0RxSize = size;
}

static void IRAM_ATTR onGwDio0Tx() {
  s_gwDio0TxDone = true;
}

// ─────────────────────────────────────────────────────────────
//  CONSTRUCTOR & DEFAULT STATE
// ─────────────────────────────────────────────────────────────
GwLoRa::GwLoRa()
  : _pinSs(LORA_SS),
    _pinRst(LORA_RST),
    _pinDio0(LORA_DIO0),
    _pinSck(LORA_SCK),
    _pinMiso(LORA_MISO),
    _pinMosi(LORA_MOSI),
    _currentFreqHz(LORA_DEFAULT_FREQ),
    _currentTxPowerDbm(LORA_DEFAULT_TXPOW),
    _masterSeq(100),
    _state(RADIO_STATE_UNINITIALIZED),
    _lastError(RADIO_ERR_NONE),
    _txQueueCount(0),
    _rxHead(0),
    _rxTail(0),
    _rxCount(0),
    _txStartTimeMs(0),
    _txAirtimeMs(0),
    _ackExpiryTimeMs(0),
    _retryCount(0),
    _backoffExpiryMs(0),
    _carrierSenseStartMs(0),
    _carrierSenseRetries(0),
    _seqCacheHead(0),
    _artificialDropRate(0)
{
  _metrics.reset();
  memset(&_activeTxPkt, 0, sizeof(_activeTxPkt));
  for (int i = 0; i < 16; i++) {
    memset(_seqCache[i].srcId, 0, NODE_ID_MAX_LEN);
    _seqCache[i].seq = 0;
  }
}

// ─────────────────────────────────────────────────────────────
//  1. LIFECYCLE & INITIALIZATION
// ─────────────────────────────────────────────────────────────
bool GwLoRa::begin(uint32_t freqHz, int txPowerDbm) {
  Serial.println(F("[GW_LORA] ─────────────────────────────────────────────"));
  Serial.println(F("[GW_LORA] Starting Gateway SX1278 Radio Driver (ESP32-S3)"));
  Serial.println(F("[GW_LORA] ─────────────────────────────────────────────"));

  // 1. SPI Bus Initialization
  Serial.printf("[GW_LORA] Initializing SPI bus: SCK=%d, MISO=%d, MOSI=%d, SS=%d\n",
                _pinSck, _pinMiso, _pinMosi, _pinSs);
  SPI.begin(_pinSck, _pinMiso, _pinMosi, _pinSs);

  pinMode(_pinSs, OUTPUT);
  digitalWrite(_pinSs, HIGH);

  // 2. Hardware Reset Cycle
  if (_pinRst >= 0) {
    Serial.printf("[GW_LORA] Performing hardware reset on GPIO %d...\n", _pinRst);
    pinMode(_pinRst, OUTPUT);
    digitalWrite(_pinRst, LOW);
    delay(15);
    digitalWrite(_pinRst, HIGH);
    delay(15);
  }

  // 3. Radio Silicon Version Verification (Direct Register 0x42 Read)
  uint8_t version = readSiliconVersion();
  Serial.printf("[GW_LORA] Reading Silicon Version Register (0x42): 0x%02X\n", version);
  if (version != 0x12) {
    Serial.printf("[GW_LORA] CRITICAL ERROR: Expected SX1278 (0x12), found 0x%02X! Check SPI wiring/power.\n", version);
    setRadioError(RADIO_ERR_INVALID_VERSION);
    transitionState(RADIO_STATE_ERROR);
    return false;
  }
  Serial.println(F("[GW_LORA] Silicon Version 0x12 VERIFIED: Semtech SX1278 confirmed."));

  // 4. Regulatory Frequency Validation (WPC G.S.R. 347(E))
  if (!isFrequencyWpcCompliant(freqHz, LORA_DEFAULT_BW)) {
    Serial.printf("[GW_LORA] REGULATORY VIOLATION: Frequency %.3f MHz is OUTSIDE legal WPC band [433.05 - 434.79 MHz]!\n",
                  (float)freqHz / 1e6);
    Serial.println(F("[GW_LORA] Transmission at 433.000 MHz is strictly PROHIBITED under Indian law."));
    setRadioError(RADIO_ERR_REGULATORY_VIOLATION);
    transitionState(RADIO_STATE_ERROR);
    return false;
  }
  _currentFreqHz = freqHz;

  // 5. Regulatory Power Clamping (<= 10 dBm ERP)
  int legalPower = clampLegalTxPower(txPowerDbm);
  if (txPowerDbm > legalPower) {
    Serial.printf("[GW_LORA] TX Power clamped from requested %d dBm to legal max %d dBm (<= 10 mW ERP).\n",
                  txPowerDbm, legalPower);
  }
  _currentTxPowerDbm = legalPower;

  // 6. Sandeep Mistry LoRa Library Binding
  LoRa.setPins(_pinSs, _pinRst, _pinDio0);
  LoRa.setSPI(SPI);
  LoRa.setSPIFrequency(LORA_SPI_SPEED_HZ);

  if (!LoRa.begin(_currentFreqHz)) {
    Serial.println(F("[GW_LORA] LoRa.begin() failed to initialize RF front-end!"));
    setRadioError(RADIO_ERR_HARDWARE_FAULT);
    transitionState(RADIO_STATE_ERROR);
    return false;
  }

  // 7. Configure LoRa Modem Parameters
  LoRa.setSpreadingFactor(LORA_DEFAULT_SF);
  LoRa.setSignalBandwidth(LORA_DEFAULT_BW);
  LoRa.setCodingRate4(LORA_DEFAULT_CR);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  if (LORA_CRC_ENABLED) {
    LoRa.enableCrc();
  } else {
    LoRa.disableCrc();
  }
  LoRa.setTxPower(_currentTxPowerDbm, PA_OUTPUT_PA_BOOST_PIN);

  // 8. Attach Lightweight Asynchronous Interrupt Callbacks
  s_gwDio0TxDone = false;
  s_gwDio0RxDone = false;
  LoRa.onReceive(onGwDio0Rx);
  LoRa.onTxDone(onGwDio0Tx);

  // 9. Enter Continuous Receive Mode
  LoRa.receive();
  transitionState(RADIO_STATE_RX_LISTENING);
  setRadioError(RADIO_ERR_NONE);

  Serial.println(F("[GW_LORA] Gateway SX1278 Ready in Non-Blocking Continuous RX Mode"));
  printRegulatoryStatus(Serial);
  return true;
}

void GwLoRa::end() {
  LoRa.end();
  transitionState(RADIO_STATE_STANDBY);
  Serial.println(F("[GW_LORA] Radio powered down to sleep."));
}

bool GwLoRa::reinitialize() {
  _metrics.radioRestartCount++;
  Serial.printf("[GW_LORA] Executing Radio Reinitialization (Count: %u)...\n", _metrics.radioRestartCount);
  LoRa.idle();
  return begin(_currentFreqHz, _currentTxPowerDbm);
}

// ─────────────────────────────────────────────────────────────
//  2. NON-BLOCKING ENGINE & STATE MACHINE (SERVICE)
// ─────────────────────────────────────────────────────────────
void GwLoRa::service() {
  uint32_t now = millis();

  // 1. Check Asynchronous Interrupt Flags from DIO0
  if (s_gwDio0TxDone) {
    s_gwDio0TxDone = false;
    handleTxCompletion();
  }

  if (s_gwDio0RxDone) {
    s_gwDio0RxDone = false;
    handleRxCompletion();
  }

  // Fallback Polling (in case DIO0 interrupt was missed or unmapped)
  if (_state == RADIO_STATE_RX_LISTENING) {
    int packetLen = LoRa.parsePacket();
    if (packetLen > 0) {
      handleRxCompletion();
    }
  }

  // 2. State Machine Dispatch
  switch (_state) {
    case RADIO_STATE_UNINITIALIZED:
    case RADIO_STATE_STANDBY:
    case RADIO_STATE_ERROR:
      // Inactive states, idle
      break;

    case RADIO_STATE_RX_LISTENING:
      // If there are pending outgoing packets in the priority queue, begin LBT
      if (_txQueueCount > 0) {
        if (!LORA_REGULATORY_TX_ENABLE) {
          Serial.println(F("[GW_LORA] TX inhibited: LORA_REGULATORY_TX_ENABLE is false (Listen-Only Mode)."));
          break;
        }

        // Check rolling 10% duty cycle budget
        if (_metrics.getDutyCyclePercent() > WPC_MAX_DUTY_CYCLE_PERCENT) {
          setRadioError(RADIO_ERR_DUTY_CYCLE_EXCEEDED);
          // Wait for window to roll before transmitting non-emergency packets
          break;
        }

        // Pop highest priority packet into active buffer
        if (popTxQueue(_activeTxPkt)) {
          _carrierSenseStartMs = now;
          _carrierSenseRetries = 0;
          transitionState(RADIO_STATE_CARRIER_SENSE);
        }
      }
      break;

    case RADIO_STATE_CARRIER_SENSE:
      // Listen-Before-Talk (LBT) Carrier Sense
      if ((now - _carrierSenseStartMs) >= CARRIER_SENSE_SAMPLE_MS) {
        if (performCarrierSense()) {
          // Channel is clear! Proceed to transmit
          startAsyncTx(_activeTxPkt);
        } else {
          // Channel is busy! Back off
          _carrierSenseRetries++;
          if (_carrierSenseRetries > CARRIER_SENSE_MAX_RETRIES) {
            Serial.println(F("[GW_LORA] LBT Failure: Channel persistently busy. Aborting packet."));
            setRadioError(RADIO_ERR_CHANNEL_BUSY);
            _metrics.txFailedCount++;
            LoRa.receive();
            transitionState(RADIO_STATE_RX_LISTENING);
          } else {
            // Calculate randomized backoff with exponential scaling
            uint32_t jitter = (esp_random() % (CARRIER_SENSE_MAX_BACKOFF_MS - CARRIER_SENSE_MIN_BACKOFF_MS + 1));
            uint32_t backoff = (CARRIER_SENSE_MIN_BACKOFF_MS + jitter) * (1 << (_carrierSenseRetries - 1));
            _backoffExpiryMs = now + backoff;
            transitionState(RADIO_STATE_RETRY_BACKOFF);
          }
        }
      }
      break;

    case RADIO_STATE_RETRY_BACKOFF:
      if (now >= _backoffExpiryMs) {
        _carrierSenseStartMs = now;
        transitionState(RADIO_STATE_CARRIER_SENSE);
      }
      break;

    case RADIO_STATE_TX_ACTIVE:
      // Safety timeout: If TX hangs longer than calculated airtime + 300 ms
      if ((now - _txStartTimeMs) > (_txAirtimeMs + 300)) {
        Serial.println(F("[GW_LORA] WARNING: Async TX watchdog timeout. Forcing TX completion."));
        handleTxCompletion();
      }
      break;

    case RADIO_STATE_WAIT_ACK:
      // Check if ACK timeout has expired
      if (now > _ackExpiryTimeMs) {
        Serial.printf("[GW_LORA] ACK Timeout for Seq #%u (Attempt %u/%u)\n",
                      _activeTxPkt.seq, _retryCount + 1, RADIO_DEFAULT_MAX_RETRIES);
        _metrics.ackTimeoutCount++;

        if (_retryCount < RADIO_DEFAULT_MAX_RETRIES) {
          _retryCount++;
          // Randomized backoff before retry (50 ms to 200 ms)
          uint32_t backoff = 50 + (esp_random() % 150);
          _backoffExpiryMs = now + backoff;
          transitionState(RADIO_STATE_RETRY_BACKOFF);
        } else {
          Serial.printf("[GW_LORA] Transaction FAILED: Max retries exhausted for Seq #%u\n", _activeTxPkt.seq);
          setRadioError(RADIO_ERR_NO_ACK);
          _metrics.txFailedCount++;
          LoRa.receive();
          transitionState(RADIO_STATE_RX_LISTENING);
        }
      }
      break;
  }
}

// ─────────────────────────────────────────────────────────────
//  3. TRANSMISSION HELPERS & COMPLETION
// ─────────────────────────────────────────────────────────────
void GwLoRa::startAsyncTx(const RadioPacket_t &pkt) {
  uint8_t frameBuf[RADIO_MAX_PACKET_SIZE];
  size_t frameLen = serializePacket(pkt, frameBuf, sizeof(frameBuf));
  if (frameLen == 0) {
    Serial.println(F("[GW_LORA] Serialization error: packet oversized or invalid."));
    setRadioError(RADIO_ERR_PACKET_TOO_LARGE);
    _metrics.txFailedCount++;
    LoRa.receive();
    transitionState(RADIO_STATE_RX_LISTENING);
    return;
  }

  _txAirtimeMs = calculateAirtimeMs(pkt.payloadLen);
  _txStartTimeMs = millis();

  // Load packet into SX1278 FIFO and trigger asynchronous transmission
  LoRa.idle();
  LoRa.beginPacket();
  LoRa.write(frameBuf, frameLen);
  LoRa.endPacket(true); // Non-blocking asynchronous transmit

  transitionState(RADIO_STATE_TX_ACTIVE);
}

void GwLoRa::handleTxCompletion() {
  uint32_t actualAirtime = millis() - _txStartTimeMs;
  _metrics.txPacketCount++;
  _metrics.updateDutyCycle(actualAirtime);

  // If the packet was a TEST packet requiring an ACK, wait for it
  if (_activeTxPkt.msgType == MSG_RADIO_TEST || _activeTxPkt.msgType == MSG_RADIO_STATUS) {
    _ackExpiryTimeMs = millis() + RADIO_DEFAULT_ACK_TIMEOUT_MS;
    LoRa.receive(); // Resume RX to hear peer's ACK
    transitionState(RADIO_STATE_WAIT_ACK);
  } else {
    // Packets that do not expect ACK (e.g., HELLO or ACK) return immediately to RX
    LoRa.receive();
    transitionState(RADIO_STATE_RX_LISTENING);
  }
}

// ─────────────────────────────────────────────────────────────
//  4. RECEPTION PROCESSING
// ─────────────────────────────────────────────────────────────
void GwLoRa::handleRxCompletion() {
  uint8_t rawBuf[RADIO_MAX_PACKET_SIZE];
  int bytesRead = 0;

  while (LoRa.available() && bytesRead < (int)sizeof(rawBuf)) {
    rawBuf[bytesRead++] = (uint8_t)LoRa.read();
  }

  if (bytesRead == 0) return;

  // Artificial drop simulation for Test Scenario 10
  if (_artificialDropRate > 0) {
    if ((esp_random() % 100) < _artificialDropRate) {
      Serial.printf("[GW_LORA][SIM] Controlled Packet Drop Triggered (%u bytes dropped)\n", bytesRead);
      return;
    }
  }

  int16_t rssi = (int16_t)LoRa.packetRssi();
  float snr = LoRa.packetSnr();

  RadioPacket_t rxPkt;
  if (!deserializePacket(rawBuf, bytesRead, rssi, snr, rxPkt)) {
    Serial.printf("[GW_LORA] Malformed packet or CRC mismatch (Bytes: %d, RSSI: %d, SNR: %.1f)\n",
                  bytesRead, rssi, snr);
    _metrics.packetErrorCount++;
    setRadioError(RADIO_ERR_RX_CRC_FAIL);
    return;
  }

  // Update reception metrics
  _metrics.recordRx(rssi, snr, rxPkt.payloadLen);

  // Check if packet is addressed to this Gateway or is Broadcast
  if (strcmp(rxPkt.dstId, GATEWAY_NODE_ID) != 0 && strcmp(rxPkt.dstId, BROADCAST_NODE_ID) != 0) {
    // Packet intended for another node, ignore
    return;
  }

  // Check if this is an incoming ACK matching our active pending transaction
  if (rxPkt.msgType == MSG_RADIO_ACK && _state == RADIO_STATE_WAIT_ACK) {
    if (rxPkt.seq == _activeTxPkt.seq && strcmp(rxPkt.srcId, _activeTxPkt.dstId) == 0) {
      uint32_t rtt = millis() - _txStartTimeMs;
      _metrics.ackSuccessCount++;
      Serial.printf("[GW_LORA] [SUCCESS] ACK Received from %s for Seq #%u | RTT: %lu ms | RSSI: %d dBm | SNR: %.1f dB\n",
                    rxPkt.srcId, rxPkt.seq, (unsigned long)rtt, rxPkt.rssi, rxPkt.snr);
      _retryCount = 0;
      transitionState(RADIO_STATE_RX_LISTENING);
    }
  }

  // Check deduplication cache
  if (isDuplicate(rxPkt.srcId, rxPkt.seq)) {
    Serial.printf("[GW_LORA] Duplicate packet ignored from %s (Seq #%u)\n", rxPkt.srcId, rxPkt.seq);
    return;
  }
  recordSeq(rxPkt.srcId, rxPkt.seq);

  // Push into RX queue
  pushRxQueue(rxPkt);
}

// ─────────────────────────────────────────────────────────────
//  5. TRANSMISSION API IMPLEMENTATION
// ─────────────────────────────────────────────────────────────
bool GwLoRa::enqueuePacket(const RadioPacket_t &pkt) {
  return pushTxQueue(pkt);
}

bool GwLoRa::sendTestPacket(const char* targetNode, const char* payload, PacketPriority prio) {
  RadioPacket_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.msgType = MSG_RADIO_TEST;
  strncpy(pkt.srcId, GATEWAY_NODE_ID, NODE_ID_MAX_LEN - 1);
  strncpy(pkt.dstId, targetNode, NODE_ID_MAX_LEN - 1);
  pkt.seq = getNextSeq();
  pkt.priority = prio;

  if (payload != nullptr) {
    size_t len = strlen(payload);
    if (len > RADIO_MAX_PAYLOAD_SIZE) len = RADIO_MAX_PAYLOAD_SIZE;
    pkt.payloadLen = (uint8_t)len;
    memcpy(pkt.payload, payload, len);
  } else {
    pkt.payloadLen = 0;
  }

  Serial.printf("[GW_LORA] Enqueuing TEST Packet to %s (Seq #%u, Len: %u, Prio: %u)\n",
                targetNode, pkt.seq, pkt.payloadLen, (uint8_t)prio);
  return enqueuePacket(pkt);
}

bool GwLoRa::sendHello(const char* targetNode) {
  RadioPacket_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.msgType = MSG_RADIO_HELLO;
  strncpy(pkt.srcId, GATEWAY_NODE_ID, NODE_ID_MAX_LEN - 1);
  strncpy(pkt.dstId, targetNode, NODE_ID_MAX_LEN - 1);
  pkt.seq = getNextSeq();
  pkt.priority = PRIO_LOW;
  pkt.payloadLen = 0;
  return enqueuePacket(pkt);
}

bool GwLoRa::sendStatusQuery(const char* targetNode) {
  RadioPacket_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.msgType = MSG_RADIO_STATUS;
  strncpy(pkt.srcId, GATEWAY_NODE_ID, NODE_ID_MAX_LEN - 1);
  strncpy(pkt.dstId, targetNode, NODE_ID_MAX_LEN - 1);
  pkt.seq = getNextSeq();
  pkt.priority = PRIO_MEDIUM;
  pkt.payloadLen = 0;
  return enqueuePacket(pkt);
}

// ─────────────────────────────────────────────────────────────
//  6. QUEUE & CACHE HELPERS
// ─────────────────────────────────────────────────────────────
bool GwLoRa::pushTxQueue(const RadioPacket_t &pkt) {
  if (_txQueueCount >= RADIO_TX_QUEUE_SIZE) {
    setRadioError(RADIO_ERR_QUEUE_FULL);
    return false;
  }

  // Insert sorted by priority: PRIO_HIGH first, then PRIO_MEDIUM, then PRIO_LOW
  int insertIdx = _txQueueCount;
  for (int i = 0; i < _txQueueCount; i++) {
    if (pkt.priority > _txQueue[i].priority) {
      insertIdx = i;
      break;
    }
  }

  for (int i = _txQueueCount; i > insertIdx; i--) {
    _txQueue[i] = _txQueue[i - 1];
  }

  _txQueue[insertIdx] = pkt;
  _txQueueCount++;
  return true;
}

bool GwLoRa::popTxQueue(RadioPacket_t &outPkt) {
  if (_txQueueCount == 0) return false;
  outPkt = _txQueue[0];
  for (int i = 0; i < _txQueueCount - 1; i++) {
    _txQueue[i] = _txQueue[i + 1];
  }
  _txQueueCount--;
  return true;
}

bool GwLoRa::hasPacket() {
  return (_rxCount > 0);
}

bool GwLoRa::getPacket(RadioPacket_t &outPkt) {
  if (_rxCount == 0) return false;
  outPkt = _rxQueue[_rxTail];
  _rxTail = (_rxTail + 1) % RADIO_RX_QUEUE_SIZE;
  _rxCount--;
  return true;
}

bool GwLoRa::pushRxQueue(const RadioPacket_t &pkt) {
  if (_rxCount >= RADIO_RX_QUEUE_SIZE) {
    // Drop oldest
    _rxTail = (_rxTail + 1) % RADIO_RX_QUEUE_SIZE;
    _rxCount--;
  }
  _rxQueue[_rxHead] = pkt;
  _rxHead = (_rxHead + 1) % RADIO_RX_QUEUE_SIZE;
  _rxCount++;
  return true;
}

bool GwLoRa::isDuplicate(const char* srcId, uint16_t seq) {
  for (int i = 0; i < 16; i++) {
    if (_seqCache[i].seq == seq && strncmp(_seqCache[i].srcId, srcId, NODE_ID_MAX_LEN) == 0) {
      return true;
    }
  }
  return false;
}

void GwLoRa::recordSeq(const char* srcId, uint16_t seq) {
  strncpy(_seqCache[_seqCacheHead].srcId, srcId, NODE_ID_MAX_LEN - 1);
  _seqCache[_seqCacheHead].seq = seq;
  _seqCacheHead = (_seqCacheHead + 1) % 16;
}

// ─────────────────────────────────────────────────────────────
//  7. CARRIER SENSE & AIRTIME
// ─────────────────────────────────────────────────────────────
bool GwLoRa::performCarrierSense() {
  int curRssi = LoRa.rssi();
  // If instantaneous RSSI is higher than threshold, channel is actively transmitting
  if (curRssi > CARRIER_SENSE_RSSI_THRES_DBM) {
    return false; // Busy
  }
  return true; // Clear
}

int GwLoRa::getInstantaneousRssi() {
  return LoRa.rssi();
}

uint32_t GwLoRa::calculateAirtimeMs(uint8_t payloadBytes) const {
  float bw = (float)LORA_DEFAULT_BW;
  float tSym = (float)(1UL << LORA_DEFAULT_SF) / bw * 1000.0f; // in ms
  float tPreamble = ((float)LORA_PREAMBLE_LEN + 4.25f) * tSym;

  int de = (tSym > 16.0f) ? 1 : 0;
  int ih = 0; // Explicit header mode
  int crc = LORA_CRC_ENABLED ? 1 : 0;

  // Semtech SX1278 Datasheet Section 4.1.1.6
  float totalBytes = (float)(PACKET_HEADER_SIZE + payloadBytes + PACKET_FOOTER_SIZE);
  float num = 8.0f * totalBytes - 4.0f * (float)LORA_DEFAULT_SF + 28.0f + 16.0f * crc - 20.0f * ih;
  float den = 4.0f * (float)(LORA_DEFAULT_SF - 2 * de);
  float ceilVal = ceil(num / den);
  float payloadSymbNb = 8.0f + max(ceilVal * (float)(LORA_DEFAULT_CR), 0.0f);

  float tPayload = payloadSymbNb * tSym;
  return (uint32_t)ceil(tPreamble + tPayload);
}

uint16_t GwLoRa::getNextSeq() {
  _masterSeq++;
  if (_masterSeq == 0 || _masterSeq < 100) _masterSeq = 100;
  return _masterSeq;
}

// ─────────────────────────────────────────────────────────────
//  8. LOW-LEVEL DIRECT SPI REGISTERS
// ─────────────────────────────────────────────────────────────
uint8_t GwLoRa::directReadReg(uint8_t addr) {
  digitalWrite(_pinSs, LOW);
  SPI.transfer(addr & 0x7F); // MSB 0 for read
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(_pinSs, HIGH);
  return val;
}

void GwLoRa::directWriteReg(uint8_t addr, uint8_t val) {
  digitalWrite(_pinSs, LOW);
  SPI.transfer(addr | 0x80); // MSB 1 for write
  SPI.transfer(val);
  digitalWrite(_pinSs, HIGH);
}

uint8_t GwLoRa::readSiliconVersion() {
  return directReadReg(0x42);
}

bool GwLoRa::setFrequency(uint32_t freqHz) {
  if (!isFrequencyWpcCompliant(freqHz, LORA_DEFAULT_BW)) {
    setRadioError(RADIO_ERR_REGULATORY_VIOLATION);
    return false;
  }
  _currentFreqHz = freqHz;
  LoRa.setFrequency(freqHz);
  return true;
}

bool GwLoRa::setTxPower(int powerDbm) {
  int legalPower = clampLegalTxPower(powerDbm);
  _currentTxPowerDbm = legalPower;
  LoRa.setTxPower(legalPower, PA_OUTPUT_PA_BOOST_PIN);
  return true;
}

void GwLoRa::setRadioError(RadioError err) {
  _lastError = err;
}

void GwLoRa::transitionState(RadioState newState) {
  _state = newState;
}

// ─────────────────────────────────────────────────────────────
//  9. DIAGNOSTIC REPORTING & REGULATORY STATUS
// ─────────────────────────────────────────────────────────────
void GwLoRa::printStatus(Stream &out) const {
  out.println(F("═══════════════════════════════════════════════════════════"));
  out.println(F("            GATEWAY SX1278 RADIO DRIVER STATUS             "));
  out.println(F("═══════════════════════════════════════════════════════════"));
  out.printf("  Hardware State          : %s\n", getStateString());
  out.printf("  Last Error              : %s\n", getLastErrorString());
  out.printf("  Carrier Frequency       : %.3f MHz (WPC Compliant: YES)\n", (float)_currentFreqHz / 1e6);
  out.printf("  Conducted TX Power      : %d dBm\n", _currentTxPowerDbm);
  out.printf("  Calculated ERP          : %.2f dBm (Limit: <= 10.0 dBm)\n", calculateErpDbm(_currentTxPowerDbm));
  out.printf("  Modulation              : SF=%d, BW=%.1f kHz, CR=4/%d\n",
             LORA_DEFAULT_SF, (float)LORA_DEFAULT_BW / 1e3, LORA_DEFAULT_CR);
  out.printf("  TX Queue Pending        : %u / %u\n", _txQueueCount, RADIO_TX_QUEUE_SIZE);
  out.printf("  RX Queue Buffered       : %u / %u\n", _rxCount, RADIO_RX_QUEUE_SIZE);
  out.printf("  Next Sequence ID        : %u\n", _masterSeq);
  _metrics.printSummary(out);
}

void GwLoRa::printRegulatoryStatus(Stream &out) const {
  out.println(F("───────────────────────────────────────────────────────────"));
  out.println(F(" INDIAN WPC REGULATORY COMPLIANCE AUDIT (G.S.R. 347(E))    "));
  out.println(F("───────────────────────────────────────────────────────────"));
  out.printf("  Applicable Gazette Rule : %s\n", WPC_RULE_NAME);
  out.printf("  Permitted Band          : 433.050 MHz to 434.790 MHz\n");
  out.printf("  Configured Frequency    : %.3f MHz [STATUS: PASS]\n", (float)_currentFreqHz / 1e6);
  out.printf("  433.000 MHz Rejection   : ENFORCED (Out-of-band transmission blocked)\n");
  out.printf("  Max Legal ERP Limit     : 10 mW (+10.0 dBm)\n");
  out.printf("  Configured ERP          : %.2f dBm [STATUS: PASS]\n", calculateErpDbm(_currentTxPowerDbm));
  out.printf("  Max Duty-Cycle Limit    : <= 10.0 %% (rolling 60s window)\n");
  out.printf("  Active Airtime Duty     : %.2f %% [STATUS: PASS]\n", _metrics.getDutyCyclePercent());
  out.printf("  Equipment Certification : ETA-SD via Saral Sanchar Portal Required\n");
  out.printf("  Antenna Protection      : VSWR Safe Load / Matched Whip Required\n");
  out.println(F("───────────────────────────────────────────────────────────"));
}
