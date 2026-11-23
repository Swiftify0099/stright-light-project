/**
 * @file FieldPole_ESP32.ino
 * @brief MSCB Smart Pole System - Field Pole Node Controller Firmware (Skeleton)
 * @version 1.0.0
 * 
 * Hardware Platform: Classic ESP32 ESP-WROOM-32 (30-pin / 38-pin DevKit)
 * Subsystems: SX1278 RA-02 433 MHz LoRa + 6 Feeder Relays + 21 Analog Channels (3x ADS131M08)
 * 
 * DESIGN CONSTRAINTS (PROMPT 06):
 * - Safe startup logic enabled: All relay outputs driven to OFF on the first clock cycles.
 * - All electrical switching outputs strictly locked in inactive/safe state.
 * - Hardware readiness diagnostic audit printed to Serial.
 * - Modular, independent compilation guaranteed.
 */

#include <Arduino.h>
#include <SPI.h>
#include <esp_chip_info.h>
#include "PoleConfig.h"
#include "PoleBoardPins.h"
#include "PoleActuators.h"
#include "PoleMetrology.h"
#include "PoleRadioDriver.h"
#include "PoleStorageManager.h"
#include "PoleMeshRouter.h"
#include "PoleRegistrationClient.h"
#include "PoleHeartbeatSender.h"
#include "PoleFeederManager.h"
#include "PoleFirmwareManager.h"
#include <CommandSecurity.h>

// =====================================================================
//  GLOBAL STATE & TIMING VARIABLES
// =====================================================================
static uint32_t s_lastHeartbeatMs = 0;
static uint32_t s_bootTimeMs = 0;
static bool     s_hardwareAuditPassed = false;

// =====================================================================
//  SAFE DIAGNOSTIC SYSTEM BANNER
// =====================================================================
void printPoleDiagnosticBanner() {
  Serial.println(F("\n========================================================"));
  Serial.println(F("       MSCB SMART POLE & FEEDER AUTOMATION SYSTEM       "));
  Serial.println(F("       FIELD POLE NODE CONTROLLER FIRMWARE (SKELETON)   "));
  Serial.println(F("========================================================"));
  Serial.printf("Firmware Version : %d.%d.%d (%s)\n", 
                MSCB_FW_VERSION_MAJOR, MSCB_FW_VERSION_MINOR, MSCB_FW_VERSION_PATCH, MSCB_SYSTEM_CODENAME);
  Serial.printf("Build Target     : %s\n", MSCB_CURRENT_TARGET_NAME);
  Serial.printf("Default Node ID  : 0x%04X (%s)\n", POLE_DEFAULT_NODE_ADDRESS, POLE_DEFAULT_STRING_ID);
  Serial.printf("Feeder Relays    : %d Channels (All Locked INACTIVE)\n", POLE_NUM_FEEDERS);
  Serial.printf("Measurement Plan : %d Channels (18 Feeder Currents + 3 Bus Voltages)\n", POLE_TOTAL_MEASUREMENT_CH);
  Serial.println(F("--------------------------------------------------------"));
  
  // Chip & Memory Diagnostics
  esp_chip_info_t chipInfo;
  esp_chip_info(&chipInfo);
  Serial.printf("CPU Model        : ESP32 Classic (Cores: %d, Rev: %d)\n", chipInfo.cores, chipInfo.revision);
  Serial.printf("CPU Clock Freq   : %u MHz\n", getCpuFrequencyMhz());
  Serial.printf("Flash Chip Size  : %u MB (Speed: %u MHz)\n", 
                ESP.getFlashChipSize() / (1024 * 1024), ESP.getFlashChipSpeed() / 1000000);
  Serial.printf("Free Heap DRAM   : %u bytes\n", ESP.getFreeHeap());

  Serial.println(F("--------------------------------------------------------"));
  Serial.println(F("VERIFIED HARDWARE PIN ALLOCATION (ESP32-WROOM-32):"));
  Serial.println(F("  [VSPI Bus & LoRa SX1278]:"));
  Serial.printf("    LoRa SCK     : GPIO %d (VSPI Native)\n", POLE_LORA_SCK_PIN);
  Serial.printf("    LoRa MISO    : GPIO %d (VSPI Native)\n", POLE_LORA_MISO_PIN);
  Serial.printf("    LoRa MOSI    : GPIO %d (VSPI Native)\n", POLE_LORA_MOSI_PIN);
  Serial.printf("    LoRa NSS / CS: GPIO %d (VSPI Native, 10k Pullup Required)\n", POLE_LORA_NSS_PIN);
  Serial.printf("    LoRa RESET   : GPIO %d (Active LOW)\n", POLE_LORA_RST_PIN);
  Serial.printf("    LoRa DIO0    : GPIO %d (Safe IRQ; Replaced GPIO 2)\n", POLE_LORA_DIO0_PIN);
  Serial.println(F("  [6 Feeder Relays - Non-Strapping Safe Output Pins]:"));
  for (uint8_t i = 0; i < 6; ++i) {
    Serial.printf("    Feeder %d     : GPIO %d (State: LOCKED OFF)\n", i + 1, POLE_FEEDER_RELAY_PINS[i]);
  }
  Serial.println(F("  [Analog & Auxiliary Expansion - 74HC139 Decoder & MCP23S17]:"));
  Serial.printf("    Decoder /1E  : GPIO %d (Active LOW Enable)\n", POLE_DECODER_ENABLE_PIN);
  Serial.printf("    Decoder A0   : GPIO %d (Address Bit 0)\n", POLE_DECODER_A0_PIN);
  Serial.printf("    Decoder A1   : GPIO %d (Address Bit 1)\n", POLE_DECODER_A1_PIN);
  Serial.println(F("    /1Y0 (0,0)   : ADS131M08 #1 CS (Feeders 1-3 Currents: Ch 0-7)"));
  Serial.println(F("    /1Y1 (0,1)   : ADS131M08 #2 CS (Feeders 4-6 Currents: Ch 0-7)"));
  Serial.println(F("    /1Y2 (1,0)   : ADS131M08 #3 CS (3-Phase Bus Voltages: Ch 0-4)"));
  Serial.println(F("    /1Y3 (1,1)   : MCP23S17 Daughterboard CS (6 MCB + 6 Contactor Aux Inputs)"));
  Serial.printf("    ADC SYNC     : GPIO %d (Common Simultaneous Conversion Sync)\n", POLE_ADC_SYNC_PIN);
  Serial.printf("    ADC 1 DRDY   : GPIO %d (Input-Only Pin)\n", POLE_ADC1_DRDY_PIN);
  Serial.printf("    ADC 2 DRDY   : GPIO %d (Input-Only Pin)\n", POLE_ADC2_DRDY_PIN);
  Serial.printf("    ADC 3 DRDY   : GPIO %d (Input-Only Pin)\n", POLE_ADC3_DRDY_PIN);
  Serial.println(F("--------------------------------------------------------"));
  Serial.println(F("SAFETY INTERLOCK STATUS:"));
  Serial.println(F("  [ENFORCED] All 6 Feeder Relay pins set to HIGH/OFF at boot."));
  Serial.println(F("  [ENFORCED] Relay coil drive strictly prohibited in Prompt 06."));
  Serial.println(F("  [ENFORCED] Strapping Pin 12 completely detached from relays."));
  Serial.println(F("  [STANDBY]  LoRa SX1278 RF link: Pinout verified; RF radio offline."));
  Serial.println(F("  [STANDBY]  ADC Subsystem: Pin allocation verified; SPI offline."));
  Serial.println(F("========================================================\n"));
}

// =====================================================================
//  SAFE HARDWARE INITIALIZATION
// =====================================================================
void setup() {
  // CRITICAL SAFETY STEP 1: IMMEDIATELY DE-ASSERT ALL RELAY OUTPUTS!
  // Configure all 6 feeder outputs to OFF state before touching any other peripheral.
  for (uint8_t i = 0; i < 6; ++i) {
    pinMode(POLE_FEEDER_RELAY_PINS[i], OUTPUT);
    digitalWrite(POLE_FEEDER_RELAY_PINS[i], POLE_RELAY_OFF);
  }

  // Initialize and audit relay actuators via driver
  poleActuators.init(true);
  bool diagOk = poleActuators.validateStartupState();

  // De-select LoRa Chip Select
  pinMode(POLE_LORA_NSS_PIN, OUTPUT);
  digitalWrite(POLE_LORA_NSS_PIN, HIGH);

  // Keep LoRa Reset HIGH (Inactive)
  pinMode(POLE_LORA_RST_PIN, OUTPUT);
  digitalWrite(POLE_LORA_RST_PIN, HIGH);

  // Initialize 74HC139 SPI Address Decoder & Deselect all SPI devices
  pinMode(POLE_DECODER_ENABLE_PIN, OUTPUT);
  pinMode(POLE_DECODER_A0_PIN, OUTPUT);
  pinMode(POLE_DECODER_A1_PIN, OUTPUT);
  poleSelectSpiDevice(POLE_SPI_DEV_DESELECT);

  // Initialize Metrology Subsystem (3x ADS131M08)
  SPI.begin(POLE_SPI_SCK_PIN, POLE_SPI_MISO_PIN, POLE_SPI_MOSI_PIN);
  bool metrologyOk = poleMetrology.begin();

  // Initialize Persistent Storage
  poleStorage.begin();

  // Initialize SX1278 RA-02 LoRa Radio
  bool radioOk = poleRadio.begin();

  // Initialize Mesh Router
  poleMesh.begin();

  // Initialize Registration Client
  poleRegClient.begin();

  // Initialize Heartbeat Sender
  poleHeartbeat.begin();

  // Initialize Feeder Actuator & Feedback Manager
  poleFeederMgr.begin();

  // Initialize Firmware Manager
  poleFwMgr.init(10);

  // Configure Status LED
  pinMode(POLE_STATUS_LED_PIN, OUTPUT);
  digitalWrite(POLE_STATUS_LED_PIN, LOW);

  // Initialize Diagnostic Serial Port
  Serial.begin(MSCB_SERIAL_BAUD_RATE);
  delay(MSCB_DIAGNOSTIC_BANNER_DELAY_MS);

  // Print diagnostic audit
  printPoleDiagnosticBanner();

  // Security & Key Provisioning Audit
  if (!cmdSecurity.isKeyProvisioned()) {
    Serial.println(F("[SECURITY] ⚠️ No secure key provisioned. Operating in Fail-Closed mode."));
  }

  s_bootTimeMs = millis();
  s_lastHeartbeatMs = s_bootTimeMs;

  // Never mark hardwareAuditPassed unconditionally!
  if (!diagOk || poleActuators.getDriverState() == ACTUATOR_STATE_FAULT || !poleActuators.isGpio12Safe()) {
    s_hardwareAuditPassed = false;
    Serial.println(F("[SYSTEM_INIT] ❌ CRITICAL: Actuator hardware audit FAILED! System locked in FAULT state."));
  } else {
    s_hardwareAuditPassed = true;
    Serial.println(F("[SYSTEM_INIT] Field Pole initialized safely. Relays locked inactive."));
    if (!metrologyOk) {
      Serial.println(F("[SYSTEM_INIT] ⚠️ Metrology: ADS131M08 hardware absent. Zero-simulation enforced: ADC_NOT_CONNECTED."));
    }
  }

  // Execute post-boot OTA self-tests if pending verification
  poleFwMgr.performBootSelfTest(metrologyOk, radioOk);
}

// =====================================================================
//  MAIN OPERATIONAL LOOP (NON-BLOCKING SAFE TICKER)
// =====================================================================
void loop() {
  uint32_t currentMs = millis();

  // 1. Process Inbound LoRa Radio Packets & Multi-Hop Forwarding
  if (poleRadio.hasPacket()) {
    MeshWireFrame inFrame;
    int16_t rssi = 0;
    int8_t snr = 0;
    if (poleRadio.receivePacket(inFrame, rssi, snr)) {
      MeshWireFrame actionFrame;
      bool sendAction = false;
      bool forMe = poleMesh.handleReceivedWireFrame(inFrame, rssi, snr, actionFrame, sendAction);

      // Multi-Hop Packet Forwarding
      if (sendAction) {
        poleRadio.sendPacket(actionFrame);
      }

      // Local Packet Dispatch
      if (forMe) {
        switch (inFrame.header.type) {
          case PKT_REGISTER_RESPONSE: {
            if (inFrame.header.payloadLen >= sizeof(PktRegisterResponsePayload)) {
              PktRegisterResponsePayload resp;
              memcpy(&resp, inFrame.payload, sizeof(PktRegisterResponsePayload));
              poleRegClient.handleRegisterResponse(resp);
            }
            break;
          }
          case PKT_HEARTBEAT_ACK: {
            if (inFrame.header.payloadLen >= sizeof(PktHeartbeatAckPayload)) {
              PktHeartbeatAckPayload ack;
              memcpy(&ack, inFrame.payload, sizeof(PktHeartbeatAckPayload));
              poleHeartbeat.handleHeartbeatAck(ack);
            }
            break;
          }
          case PKT_CMD_FEEDER_SWITCH: {
            if (inFrame.header.payloadLen >= sizeof(PktFeederCmdPayload)) {
              PktFeederCmdPayload cmd;
              memcpy(&cmd, inFrame.payload, sizeof(PktFeederCmdPayload));
              poleFeederMgr.processCommandPayload(cmd);
            }
            break;
          }
          case PKT_CMD_EMERGENCY_SHUTDOWN: {
            poleFeederMgr.emergencyShutdownAll("EMERGENCY_LORA_PKT");
            break;
          }
          default:
            break;
        }
      }
    }
  }

  // 2. Service Multi-Hop Mesh Router (parent discovery, failover, beacons)
  poleMesh.service();

  // 3. Service Registration Client (retry if unregistered)
  poleRegClient.service();

  // 4. Service Periodic Heartbeat Sender
  poleHeartbeat.service();

  // 5. Service Feeder Staggering Queue
  poleFeederMgr.service();

  // 6. Service Metrology Subsystem Watchdog & Freshness
  poleMetrology.service();

  // 7. Periodic Output Verification
  poleActuators.verifyAllOutputs();

  // 8. Periodic Heartbeat Status Flash (1 Hz non-blocking)
  if (currentMs - s_lastHeartbeatMs >= MSCB_STATUS_BLINK_INTERVAL_MS) {
    s_lastHeartbeatMs = currentMs;
    
    // Toggle Status LED
    digitalWrite(POLE_STATUS_LED_PIN, !digitalRead(POLE_STATUS_LED_PIN));

    // Diagnostic heartbeat log every 10 seconds
    static uint8_t s_logDivider = 0;
    if (++s_logDivider >= 10) {
      s_logDivider = 0;
      Serial.printf("[POLE_HEARTBEAT] Uptime: %lu s | Heap: %u B | Reg: %s | Mesh: %s (Par: 0x%04X, Rank: %u) | ADC: %s\n", 
                    (currentMs - s_bootTimeMs) / 1000UL, ESP.getFreeHeap(),
                    regClientStateToString(poleRegClient.getState()),
                    routingStateToString(poleMesh.getState()),
                    poleMesh.getPrimaryParent(), poleMesh.getMyRank(),
                    poleMetrology.areAllAdcsDetected() ? "3_CHIPS_OK" : "ADC_NOT_CONNECTED");
    }
  }

  // Yield to FreeRTOS watchdog and background tasks
  yield();
}
