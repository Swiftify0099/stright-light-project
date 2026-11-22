/*
 * =====================================================================
 *  GATEWAY_P2P_TEST.INO — MAIN GATEWAY LORA POINT-TO-POINT TEST BENCH
 *  Target Hardware : ESP32-S3-WROOM-N16R8 + SX1278 RA-02 433 MHz
 *  Prompt Stage    : PROMPT 07 — SX1278 Radio Driver Development
 *
 *  TEST SCENARIOS COVERED:
 *  - Test 1 : Gateway LoRa Initialization & Silicon Verification
 *  - Test 3 : Gateway to Field Node Transmission
 *  - Test 5 : Bidirectional Reliable ACK & RTT Measurement
 *  - Test 6 : No ACK Timeout & Retry Scheduler Verification
 *  - Test 7 : Duplicate Packet Filtering Verification
 *  - Test 8 : Gateway Radio Reinitialization & Error Recovery
 *  - Test 10: Controlled Packet Loss Simulation
 *  - Test 11: Long-Duration Communication Stability Run
 * =====================================================================
 */

#include <Arduino.h>
#include "GwLoRa.h"

#define TARGET_POLE_NODE "POLE_01"

// Long-duration test state
static bool     s_longDurationTestActive = false;
static uint16_t s_longDurationCyclesRemaining = 0;
static uint32_t s_lastCycleTriggerMs = 0;
static uint16_t s_longDurationTotal = 0;

void printMenu() {
  Serial.println(F("\n═══════════════════════════════════════════════════════════"));
  Serial.println(F("      PROMPT 07: GATEWAY LORA P2P DIAGNOSTIC BENCH         "));
  Serial.println(F("═══════════════════════════════════════════════════════════"));
  Serial.println(F(" [1] Test 1 : Verify Gateway LoRa Init & Silicon 0x12"));
  Serial.println(F(" [3] Test 3 : Transmit Test Packet to POLE_01"));
  Serial.println(F(" [5] Test 5 : Bidirectional Ping-Pong (Send & Await ACK)"));
  Serial.println(F(" [6] Test 6 : Trigger No-ACK Timeout Test (to POLE_99)"));
  Serial.println(F(" [7] Test 7 : Send Duplicate Packet to Test Deduplication"));
  Serial.println(F(" [8] Test 8 : Execute Gateway Radio Hardware Restart"));
  Serial.println(F(" [a] Test 10: Toggle 20% Artificial Packet Loss"));
  Serial.println(F(" [b] Test 11: Run Long-Duration Stability Test (20 cycles)"));
  Serial.println(F(" [s] Status : Display Radio Quality Metrics & Duty Cycle"));
  Serial.println(F(" [r] Regs   : Display Indian WPC G.S.R. 347(E) Compliance"));
  Serial.println(F(" [h] Help   : Redisplay this Command Menu"));
  Serial.println(F("═══════════════════════════════════════════════════════════"));
}

void setup() {
  Serial.begin(115200);
  uint32_t startMs = millis();
  while (!Serial && (millis() - startMs < 2000)) {
    delay(10);
  }

  Serial.println(F("\n[BOOT] Gateway Diagnostic Test Node Booting..."));
  Serial.printf("[BOOT] CPU: ESP32-S3 | Flash: 16MB | PSRAM: 8MB\n");

  // Initialize Gateway SX1278 Radio Driver
  if (!gwRadio.begin(LORA_DEFAULT_FREQ, LORA_DEFAULT_TXPOW)) {
    Serial.println(F("[FATAL] Gateway Radio Initialization FAILED!"));
  } else {
    Serial.println(F("[INIT] Gateway Radio Initialized Successfully."));
  }

  printMenu();
}

void loop() {
  // Service the non-blocking radio engine (handles LBT, async TX, ACKs, timeouts)
  gwRadio.service();

  // Check for received packets from Field Poles
  if (gwRadio.hasPacket()) {
    RadioPacket_t rxPkt;
    if (gwRadio.getPacket(rxPkt)) {
      Serial.printf("[GW_APP] [PACKET IN] Type: %s | Src: %s | Seq: %u | RSSI: %d dBm | SNR: %.1f dB | Len: %u\n",
                    getMsgTypeName(rxPkt.msgType), rxPkt.srcId, rxPkt.seq, rxPkt.rssi, rxPkt.snr, rxPkt.payloadLen);
      if (rxPkt.payloadLen > 0) {
        char payloadStr[RADIO_MAX_PAYLOAD_SIZE + 1];
        memcpy(payloadStr, rxPkt.payload, rxPkt.payloadLen);
        payloadStr[rxPkt.payloadLen] = '\0';
        Serial.printf("[GW_APP] Payload: \"%s\"\n", payloadStr);
      }
    }
  }

  // Handle Long-Duration Stability Test execution (non-blocking)
  if (s_longDurationTestActive) {
    if (gwRadio.getState() == RADIO_STATE_RX_LISTENING) {
      if ((millis() - s_lastCycleTriggerMs) >= 1500) {
        s_lastCycleTriggerMs = millis();
        s_longDurationCyclesRemaining--;
        uint16_t cycleNum = s_longDurationTotal - s_longDurationCyclesRemaining;

        char testPayload[32];
        snprintf(testPayload, sizeof(testPayload), "CYCLE_%u/%u", cycleNum, s_longDurationTotal);
        Serial.printf("[TEST 11] Running Cycle %u / %u...\n", cycleNum, s_longDurationTotal);
        gwRadio.sendTestPacket(TARGET_POLE_NODE, testPayload, PRIO_MEDIUM);

        if (s_longDurationCyclesRemaining == 0) {
          s_longDurationTestActive = false;
          Serial.println(F("\n[TEST 11] Long-Duration Stability Test FINISHED!"));
          gwRadio.printStatus(Serial);
        }
      }
    }
  }

  // Process User Keyboard Commands from Serial Monitor
  if (Serial.available()) {
    char cmd = Serial.read();
    while (Serial.available() && (Serial.peek() == '\r' || Serial.peek() == '\n')) {
      Serial.read();
    }

    switch (cmd) {
      case '1': {
        Serial.println(F("\n--- [TEST 1] Gateway LoRa Initialization Verification ---"));
        uint8_t ver = gwRadio.readSiliconVersion();
        Serial.printf("Silicon Version Register 0x42 : 0x%02X (Expected: 0x12)\n", ver);
        Serial.printf("Current Radio State           : %s\n", gwRadio.getStateString());
        Serial.printf("Carrier Frequency             : %.3f MHz\n", (float)gwRadio.getFrequency() / 1e6);
        Serial.printf("Conducted Power               : %d dBm (ERP: %.2f dBm)\n",
                      gwRadio.getTxPower(), calculateErpDbm(gwRadio.getTxPower()));
        if (ver == 0x12) {
          Serial.println(F("[RESULT] Test 1: PASS - Silicon and SPI confirmed."));
        } else {
          Serial.println(F("[RESULT] Test 1: FAIL - Invalid Version."));
        }
        break;
      }

      case '3': {
        Serial.println(F("\n--- [TEST 3] Gateway to Node Transmission ---"));
        const char* msg = "P2P_HELLO_NODE";
        if (gwRadio.sendTestPacket(TARGET_POLE_NODE, msg, PRIO_MEDIUM)) {
          Serial.printf("Test packet queued for %s. Payload: \"%s\"\n", TARGET_POLE_NODE, msg);
          Serial.println(F("[RESULT] Test 3: TX Queued (Monitoring airtime & async completion...)"));
        } else {
          Serial.println(F("[RESULT] Test 3: FAILED to enqueue packet!"));
        }
        break;
      }

      case '5': {
        Serial.println(F("\n--- [TEST 5] Bidirectional Ping-Pong (Send & Await ACK) ---"));
        const char* msg = "PING_REQ_RTT";
        if (gwRadio.sendTestPacket(TARGET_POLE_NODE, msg, PRIO_HIGH)) {
          Serial.printf("Sent PING to %s. Awaiting high-priority ACK...\n", TARGET_POLE_NODE);
        } else {
          Serial.println(F("[RESULT] Test 5: FAILED to queue PING."));
        }
        break;
      }

      case '6': {
        Serial.println(F("\n--- [TEST 6] Trigger No-ACK Timeout Test ---"));
        const char* phantomNode = "POLE_99"; // Non-existent node
        Serial.printf("Transmitting packet to non-existent node %s...\n", phantomNode);
        gwRadio.sendTestPacket(phantomNode, "PHANTOM_PING", PRIO_MEDIUM);
        Serial.println(F("Watchdog started. Verifying timeout and 3 retries..."));
        break;
      }

      case '7': {
        Serial.println(F("\n--- [TEST 7] Send Duplicate Packet ---"));
        uint16_t dupSeq = 555;
        RadioPacket_t pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.msgType = MSG_RADIO_TEST;
        strncpy(pkt.srcId, GATEWAY_NODE_ID, NODE_ID_MAX_LEN - 1);
        strncpy(pkt.dstId, TARGET_POLE_NODE, NODE_ID_MAX_LEN - 1);
        pkt.seq = dupSeq;
        pkt.priority = PRIO_MEDIUM;
        const char* dmsg = "DUP_PAYLOAD_TEST";
        pkt.payloadLen = strlen(dmsg);
        memcpy(pkt.payload, dmsg, pkt.payloadLen);

        Serial.printf("Transmitting packet #1 with Seq #%u...\n", dupSeq);
        gwRadio.enqueuePacket(pkt);
        delay(100);
        Serial.printf("Transmitting duplicate packet #2 with Seq #%u...\n", dupSeq);
        gwRadio.enqueuePacket(pkt);
        Serial.println(F("[RESULT] Test 7: Duplicate packets sent. Check Node log for deduplication."));
        break;
      }

      case '8': {
        Serial.println(F("\n--- [TEST 8] Gateway Radio Hardware Restart ---"));
        if (gwRadio.reinitialize()) {
          Serial.println(F("[RESULT] Test 8: PASS - Gateway radio successfully reinitialized."));
        } else {
          Serial.println(F("[RESULT] Test 8: FAIL - Reinitialization error!"));
        }
        break;
      }

      case 'a': {
        static bool dropEnabled = false;
        dropEnabled = !dropEnabled;
        uint8_t rate = dropEnabled ? 20 : 0;
        gwRadio.setArtificialDropRate(rate);
        Serial.printf("\n--- [TEST 10] Controlled Packet Loss Simulation: %s (Rate: %u%%) ---\n",
                      dropEnabled ? "ENABLED" : "DISABLED", rate);
        break;
      }

      case 'b': {
        Serial.println(F("\n--- [TEST 11] Starting Long-Duration Stability Test (20 cycles) ---"));
        s_longDurationTotal = 20;
        s_longDurationCyclesRemaining = 20;
        s_lastCycleTriggerMs = millis() - 2000;
        s_longDurationTestActive = true;
        break;
      }

      case 's': {
        gwRadio.printStatus(Serial);
        break;
      }

      case 'r': {
        gwRadio.printRegulatoryStatus(Serial);
        break;
      }

      case 'h':
      default: {
        printMenu();
        break;
      }
    }
  }
}
