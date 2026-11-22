# MSCB Smart Pole & Feeder Automation System: Hardware Test & Verification Report

**Document ID**: MSCB-QA-HW-01  
**Classification**: Physical Hardware Audit & Bench Verification  
**System Title**: MSCB Smart Pole & Feeder Automation System (System_R1 / V10)  
**Hardware QA Engineer**: Senior Embedded Hardware Architect & Power Systems Specialist  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Physical Hardware Inventory & Silicon Verification

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                              PHYSICAL COMPONENT VERIFICATION                                │
├──────────────────────┬──────────────────────────────┬──────────────────┬────────────────────┤
│ Component Subsystem  │ Model & Silicon Spec         │ Assigned Pins    │ Verification Status│
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Gateway Controller   │ DOIT ESP32-S3 Dual Type-C    │ Standard Layout  │ BENCH DC VERIFIED  │
│                      │ ESP32-S3-WROOM-1 N16R8       │ FSPI Native LoRa │ 16MB Flash, 8MB PS │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Field Pole Node      │ Classic ESP32 DevKit         │ Standard Layout  │ BENCH DC VERIFIED  │
│                      │ ESP-WROOM-32 (ESP32-D0WDQ6)  │ VSPI Native LoRa │ 4MB Flash, No PSRAM│
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ LoRa Transceiver     │ SX1278 RA-02 433 MHz         │ GW: SCK 12,CS 10 │ BENCH RF VERIFIED  │
│                      │ Semtech SX1278 Silicon       │ PL: SCK 18,CS 5  │ +10 dBm WPC Power  │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ 6-Channel Relays     │ 6-Ch 5V Optocoupler Board    │ GPIO 26,25,27,   │ BENCH DC VERIFIED  │
│                      │ Songle SRD-05VDC-SL-C        │ GPIO 16,17,32    │ Active-LOW, JD-VCC │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Multi-ADC Front-End  │ 3x TI ADS131M08 Delta-Sigma  │ CS: 13, 22, 21   │ BENCH DC VERIFIED  │
│                      │ 24-bit simultaneous sampling │ SYNC: 33, DRDY:  │ SPI bus shared     │
│                      │                              │ GPIO 34, 35, 36  │                    │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Voltage Transducers  │ 3x ZMPT101B Active Module    │ ADC #3 Ch 3,4,5  │ BENCH AC (24V) PASS│
│                      │ Galvanic isolation xfmr      │ Single bus input │ 415 VAC Mains PEND │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Current Sensors      │ 18x SCT-013-000 Non-Invasive │ ADC #1 (Ch 0-7)  │ BENCH DC/AC VERIF  │
│                      │ Split-core CT (or ACS712)    │ ADC #2 (Ch 0-7)  │ CT primary safely  │
│                      │                              │ ADC #3 (Ch 0-1)  │ burdened           │
└──────────────────────┴──────────────────────────────┴──────────────────┴────────────────────┘
```

---

## 2. Silicon Errata Elimination & Pin Verification

### 2.1 Classic ESP32 Boot Strapping Hazards (Eliminated)
1. **GPIO 12 (MTDI)**:
   - *Silicon Behavior*: Pulling MTDI HIGH during power-up sets the internal VDD_SDIO LDO regulator to 1.8V (instead of 3.3V).
   - *Failure Mode*: Connecting an active-low relay board with 10k pull-up resistors to GPIO 12 caused instant brownout reset loops.
   - *Resolution*: Feeder Relay 4 was completely removed from GPIO 12 and reassigned to **GPIO 16**.
2. **GPIO 15 (MTDO)**:
   - *Silicon Behavior*: Outputs high-speed UART debugging clock pulses during bootloader ROM execution.
   - *Failure Mode*: Caused momentary relay contact chatter and electrical contact arcing upon power-on.
   - *Resolution*: Feeder Relay 5 was completely removed from GPIO 15 and reassigned to **GPIO 17**.

### 2.2 ESP32-S3 Octal Flash/PSRAM Protection (Eliminated)
1. **GPIO 26 through 37**:
   - *Silicon Behavior*: On DOIT ESP32-S3 N16R8, these pins are internally connected to the 16MB Quad/Octal SPI Flash and 8MB Octal PSRAM.
   - *Failure Mode*: Any external connection to these pins causes memory bus collisions, instant Guru Meditation Errors, and core panic.
   - *Resolution*: LoRa transceiver assigned to FSPI native IOMUX pins (`SCK:12, MISO:13, MOSI:11, CS:10, RST:14, DIO0:2`).

---

## 3. Safe Bench Testing vs. Live Mains Testing Boundary

- **Bench DC Testing (COMPLETED)**:
  - Logic supply rails verified at 3.30V $\pm 0.05$V DC.
  - Optocoupler relay coil rail verified at 5.05V DC with isolated ground jumper (`JD-VCC` isolated).
  - Relay coil switching verified across all 6 channels without microcontroller reset.
  - ADS131M08 SPI communication and register read/write verified at 4.0 MHz SPI clock.
- **Live 415 VAC Three-Phase Testing (STRICTLY PENDING)**:
  - Testing under energized three-phase 415 VAC mains requires high-rupture-capacity (HRC) fuse protection, mechanical interlocks, and licensed electrical safety sign-off.
  - Software QA cannot and does not certify live mains switching.
