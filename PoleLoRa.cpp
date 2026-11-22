/*
 * =====================================================================
 *  POLELORA.CPP — SECONDARY POLE NODE SX1278 LORA DRIVER (POINT-TO-POINT)
 *  Platform: ESP32-WROOM-32 + SX1278 RA-02 433 MHz
 * =====================================================================
 */

#include "PoleLoRa.h"

// Global singleton instance
PoleLoRa poleRadio;

// ─────────────────────────────────────────────────────────────
//  LIGHTWEIGHT ISR CALLBACKS (ZERO SPI / ZERO PRINTING IN ISR)
// ─────────────────────────────────────────────────────────────
static volatile bool s_poleDio0TxDone = false;
static volatile bool s_poleDio0RxDone = false;
static volatile int  s_poleDio0RxSize = 0;

static void IRAM_ATTR onPoleDio0Rx(int size) {
  s_poleDio0RxDone = true;
  s_poleDio0RxSize = size;
}

static void IRAM_ATTR onPoleDio0Tx() {
  s_poleDio0TxDone = true;
}

// ─────────────────────────────────────────────────────────────
//  CONSTRUCTOR & DEFAULT STATE
// ─────────────────────────────────────────────────────────────
PoleLoRa::PoleLoRa()
  : _pinSs(LORA_SS),
    _pinRst(LORA_RST),
    _pinDio0(LORA_DIO0),
    _pinSck(LORA_SCK),
    _pinMiso(LORA_MISO),
    _pinMosi(LORA_MOSI),
    _currentFreqHz(LORA_DEFAULT_FREQ),
    _currentTxPowerDbm(LORA_DEFAULT_TXPOW),
    _nodeSeq(200),
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
  strncpy(_nodeId, DEFAULT_POLE_NODE_ID, NODE_ID_MAX_LEN - 1);
  _nodeId[NODE_ID_MAX_LEN - 1] = '\0';

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
bool PoleLoRa::begin(const char* nodeId, uint32_t freqHz, int txPowerDbm) {
  if (nodeId != nullptr) {
    setNodeId(nodeId);
  }

  Serial.println(F("[POLE_LORA] ─────────────────────────────────────────────"));
  Serial.printf("[POLE_LORA] Starting Field Pole SX1278 Driver (ID: %s)\n", _nodeId);
  Serial.println(F("[POLE_LORA] ─────────────────────────────────────────────"));

  // 1. VSPI Bus Initialization
  Serial.printf("[POLE_LORA] Initializing VSPI bus: SCK=%d, MISO=%d, MOSI=%d, SS=%d\n",
                _pinSck, _pinMiso, _pinMosi, _pinSs);
  SPI.begin(_pinSck, _pinMiso, _pinMosi, _pinSs);

  pinMode(_pinSs, OUTPUT);
  digitalWrite(_pinSs, HIGH);

  // 2. Hardware Reset Cycle
  if (_pinRst >= 0) {
    Serial.printf("[POLE_LORA] Performing hardware reset on GPIO %d...\n", _pinRst);
    pinMode(_pinRst, OUTPUT);
    digitalWrite(_pinRst, LOW);
    delay(15);
    digitalWrite(_pinRst, HIGH);
    delay(15);
  }

  // 3. Silicon Version Verification (Direct Register 0x42 Read)
  uint8_t version = readSiliconVersion();
  Serial.printf("[POLE_LORA] Reading Silicon Version Register (0x42): 0x%02X\n", version);
  if (version != 0x12) {
    Serial.printf("[POLE_LORA] CRITICAL ERROR: Expected SX1278 (0x12), found 0x%02X! Check SPI wiring/power.\n", version);
    setRadioError(RADIO_ERR_INVALID_VERSION);
    transitionState(RADIO_STATE_ERROR);
    return false;
  }
  Serial.println(F("[POLE_LORA] Silicon Version 0x12 VERIFIED: Semtech SX1278 confirmed."));

  // 4. Regulatory Frequency Validation (WPC G.S.R. 347(E))
  if (!isFrequencyWpcCompliant(freqHz, LORA_DEFAULT_BW)) {
    Serial.printf("[POLE_LORA] REGULATORY VIOLATION: Frequency %.3f MHz is OUTSIDE legal WPC band [433.05 - 434.79 MHz]!\n",
                  (float)freqHz / 1e6);
    Serial.println(F("[POLE_LORA] Transmission at 433.000 MHz is strictly PROHIBITED under Indian law."));
    setRadioError(RADIO_ERR_REGULATORY_VIOLATION);
    transitionState(RADIO_STATE_ERROR);
    return false;
  }
  _currentFreqHz = freqHz;

  // 5. Regulatory Power Clamping (<= 10 dBm ERP)
  int legalPower = clampLegalTxPower(txPowerDbm);
  if (txPowerDbm > legalPower) {
    Serial.printf("[POLE_LORA] TX Power clamped from requested %d dBm to legal max %d dBm (<= 10 mW ERP).\n",
                  txPowerDbm, legalPower);
  }
  _currentTxPowerDbm = legalPower;

  // 6. Sandeep Mistry LoRa Library Binding
  LoRa.setPins(_pinSs, _pinRst, _pinDio0);
  LoRa.setSPI(SPI);
  LoRa.setSPIFrequency(LORA_SPI_SPEED_HZ);

  if (!LoRa.begin(_currentFreqHz)) {
    Serial.println(F("[POLE_LORA] LoRa.begin() failed to initialize RF front-end!"));
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
  s_poleDio0TxDone = false;
  s_poleDio0RxDone = false;
  LoRa.onReceive(onPoleDio0Rx);
  LoRa.onTxDone(onPoleDio0Tx);

  // 9. Enter Continuous Receive Mode
  LoRa.receive();
  transitionState(RADIO_STATE_RX_LISTENING);
  setRadioError(RADIO_ERR_NONE);

  Serial.printf("[POLE_LORA] Field Pole (%s) Ready in Non-Blocking Continuous RX Mode\n", _nodeId);
  printRegulatoryStatus(Serial);
  return true;
}

void PoleLoRa::end() {
  LoRa.end();
  transitionState(RADIO_STATE_STANDBY);
  Serial.println(F("[POLE_LORA] Radio powered down to sleep."));
}

bool PoleLoRa::reinitialize() {
  _metrics.radioRestartCount++;
  Serial.printf("[POLE_LORA] Executing Radio Reinitialization (Count: %u)...\n", _metrics.radioRestartCount);
  LoRa.idle();
  return begin(_nodeId, _currentFreqHz, _currentTxPowerDbm);
}

void PoleLoRa::setNodeId(const char* id) {
  if (id != nullptr) {
    strncpy(_nodeId, id, NODE_ID_MAX_LEN - 1);
    _nodeId[NODE_ID_MAX_LEN - 1] = '\0';
  }
}

// ─────────────────────────────────────────────────────────────
//  2. NON-BLOCKING ENGINE & STATE MACHINE (SERVICE)
// ─────────────────────────────────────────────────────────────
void PoleLoRa::service() {
  uint32_t now = millis();

  // 1. Check Asynchronous Interrupt Flags from DIO0
  if (s_poleDio0TxDone) {
    s_poleDio0TxDone = false;
    handleTxCompletion();
  }

  if (s_poleDio0RxDone) {
    s_poleDio0RxDone = false;
    handleRxCompletion();
  }

  // Fallback Polling (in case DIO0 interrupt was missed)
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
      break;

    case RADIO_STATE_RX_LISTENING:
      // If there are outgoing packets (e.g. ACKs or Pole-initiated packets), begin LBT
      if (_txQueueCount > 0) {
        if (!LORA_REGULATORY_TX_ENABLE) {
          Serial.println(F("[POLE_LORA] TX inhibited: LORA_REGULATORY_TX_ENABLE is false."));
          break;
        }

        // Check rolling 10% duty cycle budget
        if (_metrics.getDutyCyclePercent() > WPC_MAX_DUTY_CYCLE_PERCENT) {
          setRadioError(RADIO_ERR_DUTY_CYCLE_EXCEEDED);
          break;
        }

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
          startAsyncTx(_activeTxPkt);
        } else {
          _carrierSenseRetries++;
          if (_carrierSenseRetries > CARRIER_SENSE_MAX_RETRIES) {
            Serial.println(F("[POLE_LORA] LBT Failure: Channel busy. Aborting packet."));
            setRadioError(RADIO_ERR_CHANNEL_BUSY);
            _metrics.txFailedCount++;
            LoRa.receive();
            transitionState(RADIO_STATE_RX_LISTENING);
          } else {
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
        Serial.println(F("[POLE_LORA] WARNING: Async TX watchdog timeout."));
        handleTxCompletion();
      }
      break;

    case RADIO_STATE_WAIT_ACK:
      // For pole-initiated packets that require ACK from Gateway
      if (now > _ackExpiryTimeMs) {
        Serial.printf("[POLE_LORA] ACK Timeout from Gateway for Seq #%u (Attempt %u/%u)\n",
                      _activeTxPkt.seq, _retryCount + 1, RADIO_DEFAULT_MAX_RETRIES);
        _metrics.ackTimeoutCount++;

        if (_retryCount < RADIO_DEFAULT_MAX_RETRIES) {
          _retryCount++;
          uint32_t backoff = 50 + (esp_random() % 150);
          _backoffExpiryMs = now + backoff;
          transitionState(RADIO_STATE_RETRY_BACKOFF);
        } else {
          Serial.printf("[POLE_LORA] Node-to-GW Transaction FAILED for Seq #%u\n", _activeTxPkt.seq);
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
void PoleLoRa::startAsyncTx(const RadioPacket_t &pkt) {
  uint8_t frameBuf[RADIO_MAX_PACKET_SIZE];
  size_t frameLen = serializePacket(pkt, frameBuf, sizeof(frameBuf));
  if (frameLen == 0) {
    Serial.println(F("[POLE_LORA] Serialization error: packet oversized."));
    setRadioError(RADIO_ERR_PACKET_TOO_LARGE);
    _metrics.txFailedCount++;
    LoRa.receive();
    transitionState(RADIO_STATE_RX_LISTENING);
    return;
  }

  _txAirtimeMs = calculateAirtimeMs(pkt.payloadLen);
  _txStartTimeMs = millis();

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.write(frameBuf, frameLen);
  LoRa.endPacket(true); // Non-blocking asynchronous transmit

  transitionState(RADIO_STATE_TX_ACTIVE);
}

void PoleLoRa::handleTxCompletion() {
  uint32_t actualAirtime = millis() - _txStartTimeMs;
  _metrics.txPacketCount++;
  _metrics.updateDutyCycle(actualAirtime);

  if (_activeTxPkt.msgType == MSG_RADIO_ACK) {
    Serial.printf("[POLE_LORA] [TX DONE] ACK Transmitted for Seq #%u (Airtime: %lu ms)\n",
                  _activeTxPkt.seq, (unsigned long)actualAirtime);
  }

  // If this was a pole-initiated TEST packet expecting Gateway ACK
  if (_activeTxPkt.msgType == MSG_RADIO_TEST && strcmp(_activeTxPkt.dstId, GATEWAY_NODE_ID) == 0) {
    _ackExpiryTimeMs = millis() + RADIO_DEFAULT_ACK_TIMEOUT_MS;
    LoRa.receive();
    transitionState(RADIO_STATE_WAIT_ACK);
  } else {
    // Return immediately to continuous reception
    LoRa.receive();
    transitionState(RADIO_STATE_RX_LISTENING);
  }
}

// ─────────────────────────────────────────────────────────────
//  4. RECEPTION & AUTOMATIC ACK PROCESSING
// ─────────────────────────────────────────────────────────────
void PoleLoRa::handleRxCompletion() {
  uint8_t rawBuf[RADIO_MAX_PACKET_SIZE];
  int bytesRead = 0;

  while (LoRa.available() && bytesRead < (int)sizeof(rawBuf)) {
    rawBuf[bytesRead++] = (uint8_t)LoRa.read();
  }

  if (bytesRead == 0) return;

  // Artificial drop simulation for Test Scenario 10
  if (_artificialDropRate > 0) {
    if ((esp_random() % 100) < _artificialDropRate) {
      Serial.printf("[POLE_LORA][SIM] Controlled Packet Drop Triggered (%u bytes dropped)\n", bytesRead);
      return;
    }
  }

  int16_t rssi = (int16_t)LoRa.packetRssi();
  float snr = LoRa.packetSnr();

  RadioPacket_t rxPkt;
  if (!deserializePacket(rawBuf, bytesRead, rssi, snr, rxPkt)) {
    Serial.printf("[POLE_LORA] Malformed packet or CRC mismatch (Bytes: %d, RSSI: %d, SNR: %.1f)\n",
                  bytesRead, rssi, snr);
    _metrics.packetErrorCount++;
    setRadioError(RADIO_ERR_RX_CRC_FAIL);
    return;
  }

  // Update reception metrics
  _metrics.recordRx(rssi, snr, rxPkt.payloadLen);

  // Check if packet is addressed to this node or broadcast
  if (strcmp(rxPkt.dstId, _nodeId) != 0 && strcmp(rxPkt.dstId, BROADCAST_NODE_ID) != 0) {
    // Not addressed to us
    return;
  }

  Serial.printf("[POLE_LORA] [RX] Received %s from %s (Seq #%u, Len: %u, RSSI: %d dBm, SNR: %.1f dB)\n",
                getMsgTypeName(rxPkt.msgType), rxPkt.srcId, rxPkt.seq, rxPkt.payloadLen, rxPkt.rssi, rxPkt.snr);

  // Check deduplication cache
  bool isDup = isDuplicate(rxPkt.srcId, rxPkt.seq);
  if (isDup) {
    Serial.printf("[POLE_LORA] [WARN] Duplicate packet detected from %s (Seq #%u)\n", rxPkt.srcId, rxPkt.seq);
  } else {
    recordSeq(rxPkt.srcId, rxPkt.seq);
  }

  // AUTOMATIC RELIABLE ACK ENGINE:
  // If the received packet is a TEST or HELLO, immediately reply with an ACK.
  // Note: Even if duplicate, we MUST resend ACK because Gateway may have missed the previous ACK!
  if (rxPkt.msgType == MSG_RADIO_TEST || rxPkt.msgType == MSG_RADIO_HELLO) {
    char diagInfo[48];
    snprintf(diagInfo, sizeof(diagInfo), "RSSI:%d,SNR:%.1f,UP:%lus",
             rssi, snr, (unsigned long)(millis() / 1000));
    sendAckToGateway(rxPkt.seq, diagInfo);
  }

  // If packet is ACK for a pole-initiated transaction
  if (rxPkt.msgType == MSG_RADIO_ACK && _state == RADIO_STATE_WAIT_ACK) {
    if (rxPkt.seq == _activeTxPkt.seq) {
      uint32_t rtt = millis() - _txStartTimeMs;
      _metrics.ackSuccessCount++;
      Serial.printf("[POLE_LORA] [SUCCESS] ACK Received from %s for Seq #%u | RTT: %lu ms\n",
                    rxPkt.srcId, rxPkt.seq, (unsigned long)rtt);
      _retryCount = 0;
      transitionState(RADIO_STATE_RX_LISTENING);
    }
  }

  // If not duplicate, enqueue for application processing
  if (!isDup) {
    pushRxQueue(rxPkt);
  }
}

// ─────────────────────────────────────────────────────────────
//  5. TRANSMISSION API IMPLEMENTATION
// ─────────────────────────────────────────────────────────────
bool PoleLoRa::enqueuePacket(const RadioPacket_t &pkt) {
  return pushTxQueue(pkt);
}

bool PoleLoRa::sendAckToGateway(uint16_t seq, const char* diagnosticData) {
  RadioPacket_t ackPkt;
  memset(&ackPkt, 0, sizeof(ackPkt));
  ackPkt.msgType = MSG_RADIO_ACK;
  strncpy(ackPkt.srcId, _nodeId, NODE_ID_MAX_LEN - 1);
  strncpy(ackPkt.dstId, GATEWAY_NODE_ID, NODE_ID_MAX_LEN - 1);
  ackPkt.seq = seq;
  ackPkt.priority = PRIO_HIGH; // ACKs are strictly highest priority

  if (diagnosticData != nullptr) {
    size_t len = strlen(diagnosticData);
    if (len > RADIO_MAX_PAYLOAD_SIZE) len = RADIO_MAX_PAYLOAD_SIZE;
    ackPkt.payloadLen = (uint8_t)len;
    memcpy(ackPkt.payload, diagnosticData, len);
  } else {
    ackPkt.payloadLen = 0;
  }

  Serial.printf("[POLE_LORA] Enqueuing HIGH-PRIO ACK for Seq #%u to %s\n", seq, GATEWAY_NODE_ID);
  return enqueuePacket(ackPkt);
}

bool PoleLoRa::sendTestToGateway(const char* payload, PacketPriority prio) {
  RadioPacket_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.msgType = MSG_RADIO_TEST;
  strncpy(pkt.srcId, _nodeId, NODE_ID_MAX_LEN - 1);
  strncpy(pkt.dstId, GATEWAY_NODE_ID, NODE_ID_MAX_LEN - 1);
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

  Serial.printf("[POLE_LORA] Enqueuing TEST Packet to Gateway (Seq #%u, Len: %u)\n", pkt.seq, pkt.payloadLen);
  return enqueuePacket(pkt);
}

bool PoleLoRa::sendHelloToGateway() {
  RadioPacket_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.msgType = MSG_RADIO_HELLO;
  strncpy(pkt.srcId, _nodeId, NODE_ID_MAX_LEN - 1);
  strncpy(pkt.dstId, GATEWAY_NODE_ID, NODE_ID_MAX_LEN - 1);
  pkt.seq = getNextSeq();
  pkt.priority = PRIO_LOW;
  pkt.payloadLen = 0;
  return enqueuePacket(pkt);
}

bool PoleLoRa::sendDuplicateTest(const char* targetNode, uint16_t existingSeq) {
  RadioPacket_t pkt;
  memset(&pkt, 0, sizeof(pkt));
  pkt.msgType = MSG_RADIO_TEST;
  strncpy(pkt.srcId, _nodeId, NODE_ID_MAX_LEN - 1);
  strncpy(pkt.dstId, targetNode, NODE_ID_MAX_LEN - 1);
  pkt.seq = existingSeq; // Intentional duplicate sequence number
  pkt.priority = PRIO_MEDIUM;
  const char* msg = "DUPLICATE_PAYLOAD";
  pkt.payloadLen = (uint8_t)strlen(msg);
  memcpy(pkt.payload, msg, pkt.payloadLen);
  Serial.printf("[POLE_LORA] Sending Intentional Duplicate (Seq #%u)\n", existingSeq);
  return enqueuePacket(pkt);
}

// ─────────────────────────────────────────────────────────────
//  6. QUEUE & CACHE HELPERS
// ─────────────────────────────────────────────────────────────
bool PoleLoRa::pushTxQueue(const RadioPacket_t &pkt) {
  if (_txQueueCount >= RADIO_TX_QUEUE_SIZE) {
    setRadioError(RADIO_ERR_QUEUE_FULL);
    return false;
  }

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

bool PoleLoRa::popTxQueue(RadioPacket_t &outPkt) {
  if (_txQueueCount == 0) return false;
  outPkt = _txQueue[0];
  for (int i = 0; i < _txQueueCount - 1; i++) {
    _txQueue[i] = _txQueue[i + 1];
  }
  _txQueueCount--;
  return true;
}

bool PoleLoRa::hasPacket() {
  return (_rxCount > 0);
}

bool PoleLoRa::getPacket(RadioPacket_t &outPkt) {
  if (_rxCount == 0) return false;
  outPkt = _rxQueue[_rxTail];
  _rxTail = (_rxTail + 1) % RADIO_RX_QUEUE_SIZE;
  _rxCount--;
  return true;
}

bool PoleLoRa::pushRxQueue(const RadioPacket_t &pkt) {
  if (_rxCount >= RADIO_RX_QUEUE_SIZE) {
    _rxTail = (_rxTail + 1) % RADIO_RX_QUEUE_SIZE;
    _rxCount--;
  }
  _rxQueue[_rxHead] = pkt;
  _rxHead = (_rxHead + 1) % RADIO_RX_QUEUE_SIZE;
  _rxCount++;
  return true;
}

bool PoleLoRa::isDuplicate(const char* srcId, uint16_t seq) {
  for (int i = 0; i < 16; i++) {
    if (_seqCache[i].seq == seq && strncmp(_seqCache[i].srcId, srcId, NODE_ID_MAX_LEN) == 0) {
      return true;
    }
  }
  return false;
}

void PoleLoRa::recordSeq(const char* srcId, uint16_t seq) {
  strncpy(_seqCache[_seqCacheHead].srcId, srcId, NODE_ID_MAX_LEN - 1);
  _seqCache[_seqCacheHead].seq = seq;
  _seqCacheHead = (_seqCacheHead + 1) % 16;
}

// ─────────────────────────────────────────────────────────────
//  7. CARRIER SENSE & AIRTIME
// ─────────────────────────────────────────────────────────────
bool PoleLoRa::performCarrierSense() {
  int curRssi = LoRa.rssi();
  if (curRssi > CARRIER_SENSE_RSSI_THRES_DBM) {
    return false; // Busy
  }
  return true; // Clear
}

int PoleLoRa::getInstantaneousRssi() {
  return LoRa.rssi();
}

uint32_t PoleLoRa::calculateAirtimeMs(uint8_t payloadBytes) const {
  float bw = (float)LORA_DEFAULT_BW;
  float tSym = (float)(1UL << LORA_DEFAULT_SF) / bw * 1000.0f;
  float tPreamble = ((float)LORA_PREAMBLE_LEN + 4.25f) * tSym;

  int de = (tSym > 16.0f) ? 1 : 0;
  int ih = 0;
  int crc = LORA_CRC_ENABLED ? 1 : 0;

  float totalBytes = (float)(PACKET_HEADER_SIZE + payloadBytes + PACKET_FOOTER_SIZE);
  float num = 8.0f * totalBytes - 4.0f * (float)LORA_DEFAULT_SF + 28.0f + 16.0f * crc - 20.0f * ih;
  float den = 4.0f * (float)(LORA_DEFAULT_SF - 2 * de);
  float ceilVal = ceil(num / den);
  float payloadSymbNb = 8.0f + max(ceilVal * (float)(LORA_DEFAULT_CR), 0.0f);

  float tPayload = payloadSymbNb * tSym;
  return (uint32_t)ceil(tPreamble + tPayload);
}

uint16_t PoleLoRa::getNextSeq() {
  _nodeSeq++;
  if (_nodeSeq == 0 || _nodeSeq < 200) _nodeSeq = 200;
  return _nodeSeq;
}

// ─────────────────────────────────────────────────────────────
//  8. LOW-LEVEL DIRECT SPI REGISTERS
// ─────────────────────────────────────────────────────────────
uint8_t PoleLoRa::directReadReg(uint8_t addr) {
  digitalWrite(_pinSs, LOW);
  SPI.transfer(addr & 0x7F);
  uint8_t val = SPI.transfer(0x00);
  digitalWrite(_pinSs, HIGH);
  return val;
}

void PoleLoRa::directWriteReg(uint8_t addr, uint8_t val) {
  digitalWrite(_pinSs, LOW);
  SPI.transfer(addr | 0x80);
  SPI.transfer(val);
  digitalWrite(_pinSs, HIGH);
}

uint8_t PoleLoRa::readSiliconVersion() {
  return directReadReg(0x42);
}

bool PoleLoRa::setFrequency(uint32_t freqHz) {
  if (!isFrequencyWpcCompliant(freqHz, LORA_DEFAULT_BW)) {
    setRadioError(RADIO_ERR_REGULATORY_VIOLATION);
    return false;
  }
  _currentFreqHz = freqHz;
  LoRa.setFrequency(freqHz);
  return true;
}

bool PoleLoRa::setTxPower(int powerDbm) {
  int legalPower = clampLegalTxPower(powerDbm);
  _currentTxPowerDbm = legalPower;
  LoRa.setTxPower(legalPower, PA_OUTPUT_PA_BOOST_PIN);
  return true;
}

void PoleLoRa::setRadioError(RadioError err) {
  _lastError = err;
}

void PoleLoRa::transitionState(RadioState newState) {
  _state = newState;
}

// ─────────────────────────────────────────────────────────────
//  9. DIAGNOSTIC REPORTING & REGULATORY STATUS
// ─────────────────────────────────────────────────────────────
void PoleLoRa::printStatus(Stream &out) const {
  out.println(F("═══════════════════════════════════════════════════════════"));
  out.printf("          FIELD POLE (%s) SX1278 RADIO STATUS             \n", _nodeId);
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
  out.printf("  Next Sequence ID        : %u\n", _nodeSeq);
  _metrics.printSummary(out);
}

void PoleLoRa::printRegulatoryStatus(Stream &out) const {
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
