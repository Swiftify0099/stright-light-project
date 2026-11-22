# MSCB Smart Pole & Feeder Automation System (System_R1)

**Architecture**: Modular ESP32-S3 Gateway + ESP32 Field Pole Mesh Automation  
**Platform**: Arduino IDE 2.x / ESP32 Arduino Core 3.3.11  
**Author**: Senior ESP32 Embedded Firmware Engineer & Arduino IDE Architect  
**Release**: System_R1 (Prompt 06 Modular Architecture Baseline)  

---

## 1. System Overview

The **MSCB Smart Pole & Feeder Automation System** is an industrial-grade rural power distribution and street lighting monitoring mesh network. It solves three critical operational challenges:
1. Automated control and telemetry for up to **50 distributed field pole nodes** across a clustered LoRa mesh network.
2. Independent switching and overload protection for **6 feeder relay channels per pole** (300 total switched feeders).
3. Simultaneous three-phase electrical telemetry across **21 analog measurement channels per pole** (18 independent feeder current readings + 3 common bus voltage readings), managed by three ADS131M08 24-bit delta-sigma ADCs.

---

## 2. Directory Layout & Workspace Map

```text
MSCB_SmartPole_System_R1/
│
├── Gateway_ESP32S3/                # Main Gateway Firmware Tier
│   ├── Gateway_ESP32S3.ino         # Gateway skeleton with safe startup & diagnostics
│   ├── GwConfig.h                  # Gateway configuration, sizing & credential templates
│   └── GwBoardPins.h               # Verified ESP32-S3 N16R8 hardware pinout
│
├── FieldPole_ESP32/                # Secondary Field Pole Firmware Tier
│   ├── FieldPole_ESP32.ino         # Field Pole skeleton with safe relay lock & diagnostics
│   ├── PoleConfig.h                # Pole configuration & 21-channel electrical sizing
│   └── PoleBoardPins.h             # Verified ESP32-WROOM-32 hardware pinout
│
├── MSCBShared/                     # Single Source of Truth for Both Tiers
│   ├── BuildConfig.h               # Global system constants, limits & safety switches
│   ├── NetworkTypes.h              # Telemetry frames, priorities, states & data types
│   ├── Protocol.h                  # Binary mesh framing & CRC-16 CCITT validation
│   └── library.properties          # Arduino IDE compliant library descriptor
│
├── Documentation/                  # Authoritative Engineering Reference
│   ├── HardwareInventory.md        # Comprehensive bill of materials & silicon analysis
│   ├── PinMapping.md               # Verified vs forbidden GPIO allocations
│   ├── ArduinoIDESetup.md          # Arduino IDE 2.x FQBN, tools menus & library inventory
│   └── KnownRisks.md               # Electrical, strapping & SPI contention mitigations
│
├── Tests/                          # Independent Unit & Diagnostic Verification
│   ├── ProtocolTests/              # Binary serialization & CRC-16 vector verification
│   │   └── ProtocolTests.ino
│   └── HardwareTests/              # Safe GPIO state audit without energizing coils
│       └── HardwareTests.ino
│
└── README.md                       # This Master System Guide
```

---

## 3. Confirmed Hardware Specifications

| Subsystem | Main Gateway Controller | Secondary Field Pole Node |
| :--- | :--- | :--- |
| **Microcontroller** | DOIT ESP32-S3 Dual Type-C DevKit | Classic ESP32 ESP-WROOM-32 |
| **Silicon Module** | ESP32-S3-WROOM-1/2 N16R8 | ESP32-D0WDQ6 (WROOM-32) |
| **Flash / PSRAM** | 16 MB Flash / 8 MB Octal PSRAM | 4 MB Flash / No PSRAM |
| **Wireless Transceiver**| SX1278 RA-02 433 MHz LoRa | SX1278 RA-02 433 MHz LoRa |
| **Cloud Connectivity** | 2.4 GHz Wi-Fi + Telegram Bot | None (LoRa Mesh only) |
| **Relay Switching** | None (Central Controller) | 6 Feeder Relays (Active-LOW, Opto-isolated) |
| **Analog Channels** | None | 21 Channels (18 Current + 3 Voltage) |
| **ADC Architecture** | Internal SAR ADC (Unused) | 3x ADS131M08 (24-bit Delta-Sigma via SPI) |

---

## 4. Hardware Pin Mapping Highlights

### Main Gateway (ESP32-S3 N16R8)
- **FSPI Native LoRa**: SCK=12, MISO=13, MOSI=11, NSS=10, RST=14, DIO0=2.
- **Forbidden Octal Memory Pins**: GPIO 26 to 37 (Internally bonded to Flash/PSRAM).
- **USB Native Port**: GPIO 19 (D-), GPIO 20 (D+).

### Secondary Pole (ESP32 Classic)
- **VSPI Native LoRa**: SCK=18, MISO=19, MOSI=23, NSS=5 (10k pull-up), RST=14, DIO0=4.
- **6 Feeder Relays**: GPIO 26 (F1), GPIO 25 (F2), GPIO 27 (F3), GPIO 16 (F4), GPIO 17 (F5), GPIO 32 (F6).
  *(Resolved: GPIO 12 and 15 completely eliminated to prevent boot looping and coil chatter).*
- **3x ADS131M08 ADCs**:
  - Chip Selects: ADC1=GPIO 13, ADC2=GPIO 22, ADC3=GPIO 21.
  - Common Sync: GPIO 33.
  - Data Ready Inputs (Using Input-Only Pins): DRDY1=GPIO 34, DRDY2=GPIO 35, DRDY3=GPIO 36.

---

## 5. Build & Quick Start Instructions

### Opening in Arduino IDE 2.x
1. **Gateway**: Open `Gateway_ESP32S3/Gateway_ESP32S3.ino`.
   - Select Board: `ESP32S3 Dev Module`.
   - Select PSRAM: `OPI PSRAM`.
   - Select Flash Size: `16MB (128Mb)`.
   - Click **Verify** (Ctrl+R).
2. **Field Pole**: Open `FieldPole_ESP32/FieldPole_ESP32.ino`.
   - Select Board: `ESP32 Dev Module`.
   - Select Flash Size: `4MB (32Mb)`.
   - Click **Verify** (Ctrl+R).

---

## 6. Safety Compliance Rules (Prompt 06)

1. **All Electrical Switching Outputs are Locked Inactive**: Firmwares drive all relay pins to HIGH/OFF on the first instruction cycle of boot.
2. **Zero RF Transmission**: LoRa radio stays offline during this structural skeleton phase.
3. **No Mains Connection**: Testing is strictly restricted to low-voltage bench DC (3.3V / 5.0V).
4. **No Plaintext Secrets**: Wi-Fi credentials and Telegram Bot tokens are represented as configuration placeholders.
