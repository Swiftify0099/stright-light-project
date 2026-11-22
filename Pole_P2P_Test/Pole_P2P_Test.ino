/*
 * =====================================================================
 *  POLE_P2P_TEST.INO — SECONDARY FIELD POLE LORA POINT-TO-POINT TEST BENCH
 *  Target Hardware : ESP32-WROOM-32 + SX1278 RA-02 433 MHz
 *  Prompt Stage    : PROMPT 07 — SX1278 Radio Driver Development
 *
 *  TEST SCENARIOS COVERED:
 *  - Test 2 : Secondary LoRa Initialization & Silicon Verification
 *  - Test 4 : Node to Gateway Transmission
 *  - Test 5 : Automatic Reliable ACK Generation & Transmission
 *  - Test 7 : Duplicate Packet Reception & Filtering
 *  - Test 9 : Field Pole Radio Reinitialization & Error Recovery
 *  - Test 10: Controlled Packet Loss Simulation
 * =====================================================================
 */

#include <Arduino.h>
#include "PoleLoRa.h"

#define MY_POLE_ID "POLE_01"

void printMenu() {
  Serial.println(F("\n═══════════════════════════════════════════════════════════"));
  Serial.println(F("      PROMPT 07: FIELD POLE LORA P2P DIAGNOSTIC BENCH       "));
  Serial.println(F("═══════════════════════════════════════════════════════════"));
  Serial.println(F(" [2] Test 2 : Verify Secondary LoRa Init & Silicon 0x12"));
  Serial.println(F(" [4] Test 4 : Transmit Test Packet from POLE_01 to GATEWAY"));
  Serial.println(F(" [7] Test 7 : Send Intentional Duplicate Packet to Gateway"));
  Serial.println(F(" [9] Test 9 : Execute Field Pole Radio Hardware Restart"));
  Serial.println(F(" [a] Test 10: Toggle 20% Artificial Packet Loss"));
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

  Serial.println(F("\n[BOOT] Secondary Field Pole Diagnostic Node Booting..."));
  Serial.printf("[BOOT] CPU: ESP32-WROOM-32 | Node ID: %s\n", MY_POLE_ID);

  // Initialize Secondary Pole SX1278 Radio Driver
  if (!poleRadio.begin(MY_POLE_ID, LORA_DEFAULT_FREQ, LORA_DEFAULT_TXPOW)) {
    Serial.println(F("[FATAL] Field Pole Radio Initialization FAILED!"));
  } else {
    Serial.println(F("[INIT] Field Pole Radio Initialized Successfully. Listening for Gateway..."));
  }

  printMenu();
}

void loop() {
  // Service the non-blocking radio engine (handles continuous RX, automatic ACKs, LBT)
  poleRadio.service();

  // Check for received packets from Gateway
  if (poleRadio.hasPacket()) {
    RadioPacket_t rxPkt;
    if (poleRadio.getPacket(rxPkt)) {
      Serial.printf("[POLE_APP] [PACKET IN] Type: %s | Src: %s | Seq: %u | RSSI: %d dBm | SNR: %.1f dB | Len: %u\n",
                    getMsgTypeName(rxPkt.msgType), rxPkt.srcId, rxPkt.seq, rxPkt.rssi, rxPkt.snr, rxPkt.payloadLen);
      if (rxPkt.payloadLen > 0) {
        char payloadStr[RADIO_MAX_PAYLOAD_SIZE + 1];
        memcpy(payloadStr, rxPkt.payload, rxPkt.payloadLen);
        payloadStr[rxPkt.payloadLen] = '\0';
        Serial.printf("[POLE_APP] Payload: \"%s\"\n", payloadStr);
      }
      // Note: Automatic ACK was already queued and handled by poleRadio.service()
    }
  }

  // Process User Keyboard Commands from Serial Monitor
  if (Serial.available()) {
    char cmd = Serial.read();
    while (Serial.available() && (Serial.peek() == '\r' || Serial.peek() == '\n')) {
      Serial.read();
    }

    switch (cmd) {
      case '2': {
        Serial.println(F("\n--- [TEST 2] Secondary LoRa Initialization Verification ---"));
        uint8_t ver = poleRadio.readSiliconVersion();
        Serial.printf("Silicon Version Register 0x42 : 0x%02X (Expected: 0x12)\n", ver);
        Serial.printf("Current Radio State           : %s\n", poleRadio.getStateString());
        Serial.printf("Carrier Frequency             : %.3f MHz\n", (float)poleRadio.getFrequency() / 1e6);
        Serial.printf("Conducted Power               : %d dBm (ERP: %.2f dBm)\n",
                      poleRadio.getTxPower(), calculateErpDbm(poleRadio.getTxPower()));
        if (ver == 0x12) {
          Serial.println(F("[RESULT] Test 2: PASS - Silicon and SPI confirmed."));
        } else {
          Serial.println(F("[RESULT] Test 2: FAIL - Invalid Version."));
        }
        break;
      }

      case '4': {
        Serial.println(F("\n--- [TEST 4] Node to Gateway Transmission ---"));
        const char* msg = "POLE_TELEMETRY_SAMPLE";
        if (poleRadio.sendTestToGateway(msg, PRIO_MEDIUM)) {
          Serial.printf("Test packet queued for GATEWAY. Payload: \"%s\"\n", msg);
          Serial.println(F("[RESULT] Test 4: TX Queued (Monitoring airtime & async completion...)"));
        } else {
          Serial.println(F("[RESULT] Test 4: FAILED to enqueue packet!"));
        }
        break;
      }

      case '7': {
        Serial.println(F("\n--- [TEST 7] Send Duplicate Packet to Gateway ---"));
        uint16_t dupSeq = 777;
        poleRadio.sendDuplicateTest(GATEWAY_NODE_ID, dupSeq);
        delay(100);
        poleRadio.sendDuplicateTest(GATEWAY_NODE_ID, dupSeq);
        Serial.println(F("[RESULT] Test 7: Duplicate packets dispatched to Gateway."));
        break;
      }

      case '9': {
        Serial.println(F("\n--- [TEST 9] Secondary Node Radio Hardware Restart ---"));
        if (poleRadio.reinitialize()) {
          Serial.println(F("[RESULT] Test 9: PASS - Field Pole radio successfully reinitialized."));
        } else {
          Serial.println(F("[RESULT] Test 9: FAIL - Reinitialization error!"));
        }
        break;
      }

      case 'a': {
        static bool dropEnabled = false;
        dropEnabled = !dropEnabled;
        uint8_t rate = dropEnabled ? 20 : 0;
        poleRadio.setArtificialDropRate(rate);
        Serial.printf("\n--- [TEST 10] Controlled Packet Loss Simulation: %s (Rate: %u%%) ---\n",
                      dropEnabled ? "ENABLED" : "DISABLED", rate);
        break;
      }

      case 's': {
        poleRadio.printStatus(Serial);
        break;
      }

      case 'r': {
        poleRadio.printRegulatoryStatus(Serial);
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
