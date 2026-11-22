# 01 — MSCB Smart Pole Automation System: Project Overview

**Document ID**: MSCB-DOC-01  
**Classification**: Engineering Technical Handover  
**System Code**: MSCB_SmartPole_System_R1  
**Firmware Baseline**: Gateway `GW_V10` / Field Pole `POLE_V10`  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Executive Summary

The **MSCB Smart Pole Automation System** is an industrial-grade rural power distribution and municipal street light automation platform. The system provides resilient, bi-directional wireless supervisory control, fault management, and electrical metrology over a self-forming, self-healing 433 MHz LoRa Directed Acyclic Graph (DAG) mesh network.

The system is deployed in rural and semi-urban agricultural feeders where cellular connectivity is erratic or non-existent, and where local electrical distribution infrastructure suffers from unbalance, line surges, phase loss, and overcurrent faults. By combining a high-performance central gateway with distributed field pole nodes, the system achieves centralized oversight via a cloud-linked Telegram Bot while maintaining complete local autonomous operation at each pole.

---

## 2. Core Operational Scope & Specifications

### 2.1 Network & Node Capacity
- **Master Gateway Controller**: 1 Central Gateway coordinating the mesh, managing dynamic registrations, persisting network topology, and bridging field telemetry to Telegram over Wi-Fi.
- **Maximum Field Pole Capacity**: Up to **50 distributed Field Pole Nodes** per Gateway cluster.
- **Feeder Channels per Pole**: **6 independently switched feeders** per pole (Feeder 1 to Feeder 6).
- **Total Switched Feeders**: $50 \text{ Poles} \times 6 \text{ Feeders} = \mathbf{300 \text{ Controlled Feeder Circuits}}$.
- **Analog Measurement Channels per Pole**:
  - **18 Independent Feeder Current Channels**: 6 Feeders $\times$ 3 Phases (Phase R, Phase Y, Phase B).
  - **3 Common Bus Voltage Channels**: 3 Phases (Phase R, Phase Y, Phase B) monitored at the incoming busbar.
  - **Total Analog Channels per Pole**: $\mathbf{21 \text{ Active Measurement Channels}}$.
- **System-Wide Metrology Points**:
  - **Current Channels Across Mesh**: $50 \times 18 = \mathbf{900 \text{ Current Channels}}$.
  - **Voltage Channels Across Mesh**: $50 \times 3 = \mathbf{150 \text{ Voltage Channels}}$.
  - **Total Network Metrology Scope**: $\mathbf{1,050 \text{ Continuous Analog Monitoring Channels}}$.

### 2.2 Silicon & Controller Tiers
| Attribute | Master Central Gateway | Secondary Field Pole Node |
| :--- | :--- | :--- |
| **Microcontroller** | Espressif ESP32-S3-WROOM-1 / WROOM-2 N16R8 | Espressif ESP32-D0WDQ6 (ESP-WROOM-32) |
| **CPU Architecture** | Dual-Core 32-bit Xtensa® LX7 @ 240 MHz | Dual-Core 32-bit Xtensa® LX6 @ 240 MHz |
| **Embedded Flash** | 16 MB High-Speed Octal / Quad SPI | 4 MB Quad SPI Flash |
| **Embedded RAM** | 512 KB SRAM + **8 MB Octal PSRAM (OPI)** | 520 KB SRAM (No external PSRAM) |
| **RF Transceiver** | Semtech SX1278 (Ai-Thinker RA-02) 433 MHz | Semtech SX1278 (Ai-Thinker RA-02) 433 MHz |
| **Cloud Uplink** | 2.4 GHz 802.11 b/g/n Wi-Fi + HTTPS Telegram API | None (Pure RF LoRa Mesh Node) |
| **Relay Actuation** | None (Pure supervisory controller) | 6x Optocoupler-isolated relays / contactor drivers |
| **Metrology Hardware**| Internal SAR ADC (Unused) | 3x Texas Instruments ADS131M08 24-bit $\Delta\Sigma$ ADCs |
| **Development Tool** | Arduino IDE 2.3.x (ESP32 Core 3.3.11) | Arduino IDE 2.3.x (ESP32 Core 3.3.11) |

---

## 3. Regulatory Compliance: Indian WPC G.S.R. 347(E)

The sub-GHz wireless mesh operates strictly within the license-exempt frequency band defined by the **Ministry of Communications, Government of India**, under Gazette Notification **G.S.R. 347(E)** (dated 18 May 2022) for Short Range Devices (SRD):

1. **Allocated Frequency Band**: $433.050\text{ MHz} \text{ to } 434.790\text{ MHz}$.
2. **Designated Carrier Frequency**: **433.920 MHz** (Center frequency of the ISM window).
3. **Effective Radiated Power (ERP) Limit**: **10 mW (+10.0 dBm)** maximum.
   - Gateway and Pole conducted transmit power is set in firmware to `+10 dBm` (`LORA_TXPOW = 10`).
   - Paired with unity-gain or 2.15 dBi 1/4-wave whip antennas with minimum 0.5 dB cable insertion loss, ensuring radiated emissions remain $\le +10\text{ dBm ERP}$.
4. **Bandwidth & Modulation**: 125.0 kHz channel bandwidth (`LORA_BW = 125E3`), Spreading Factor 10 (`LORA_SF = 10`), Coding Rate 4/5 (`LORA_CR = 5`).
5. **Duty Cycle Limitation**: Bounded to $\le 10.0\%$ hourly duty cycle. In normal operation, node beaconing (30s) and telemetry (15s–60s) utilize $< 4.5\%$ of channel capacity.

---

## 4. Key Engineering Deliverables & Verified Subsystems

1. **Dual-Tier Solid Firmware Architecture**:
   - Independent compilation targets for Gateway (`Sender/`) and Field Pole (`Resivers/`).
   - Clean modular release baseline in `MSCB_SmartPole_System_R1/`.
2. **Directed Acyclic Graph (DAG) LoRa Mesh Protocol**:
   - Dynamic parent election based on composite link quality (RSSI, SNR, Packet Delivery Ratio).
   - Primary and backup parent diversity with loop suppression and rank monotonicity.
   - Automatic parent failure failover within 3 failed transmission attempts.
   - Autonomous orphan recovery state machine with exponential backoff.
3. **Hardware-Enforced Silicon Safety**:
   - Elimination of strapping hazards (eliminated GPIO 12 flash brownout and GPIO 15 boot PWM chattering).
   - Strict hardware-level lockout of relay coils during power-on initialization.
4. **21-Channel Metrology Engine**:
   - 3x ADS131M08 simultaneous-sampling 24-bit delta-sigma ADCs synchronized by a hardware `/SYNC` pulse.
   - 18 independent feeder current channels + 3 bus voltage channels computed via True-RMS DSP.
5. **Robust Electrical Feeder Supervision**:
   - 5-stage command execution state machine (Sent $\rightarrow$ Accepted $\rightarrow$ Relay Changed $\rightarrow$ Feedback Confirmed $\rightarrow$ State Verified).
   - Auxiliary contact feedback detection for welded contacts, coil dropouts, and external trips.
6. **Encrypted Interactive Telegram Bot Administration**:
   - 20 multi-level interactive menus, paginated list navigation, and dynamic inline keyboards.
   - Multi-stage administrative approval/rejection for dynamic node commissioning.
   - Wi-Fi credential update engine with automated rollback and recovery SoftAP fallback.

---

## 5. Document Navigation Index

| Document | Title | Purpose |
| :--- | :--- | :--- |
| [01_PROJECT_OVERVIEW.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/01_PROJECT_OVERVIEW.md) | Project Overview | High-level system specifications and regulatory boundaries |
| [02_SYSTEM_ARCHITECTURE.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/02_SYSTEM_ARCHITECTURE.md) | System Architecture | End-to-end multi-tier architectural topology and data flows |
| [03_HARDWARE_ARCHITECTURE.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/03_HARDWARE_ARCHITECTURE.md) | Hardware Architecture | Schematics, pinouts, silicon hazards, and wiring diagrams |
| [04_GATEWAY_FIRMWARE.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/04_GATEWAY_FIRMWARE.md) | Gateway Firmware | ESP32-S3 firmware breakdown, modules, and cooperative tasks |
| [05_POLE_FIRMWARE.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/05_POLE_FIRMWARE.md) | Pole Firmware | ESP32 field pole firmware, state machines, and fail-safes |
| [06_LORA_PROTOCOL.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/06_LORA_PROTOCOL.md) | LoRa Protocol | Binary frame encoding, CRC-16 CCITT, and packet codecs |
| [07_MESH_NETWORK.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/07_MESH_NETWORK.md) | Mesh Network | Routing algorithms, link metrics, DAG parent selection, failover |
| [08_REGISTRATION_AND_DELETION.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/08_REGISTRATION_AND_DELETION.md) | Registration & Deletion | Dynamic node discovery, Telegram approval, and revocation |
| [09_SENSOR_AND_ADC_MAPPING.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/09_SENSOR_AND_ADC_MAPPING.md) | Sensor & ADC Mapping | Complete 21-channel physical-to-firmware channel mapping |
| [10_CALIBRATION_MANUAL.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/10_CALIBRATION_MANUAL.md) | Calibration Manual | Procedures for 18 current CTs and 3 bus voltage sensors |
| [11_FEEDER_CONTROL.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/11_FEEDER_CONTROL.md) | Feeder Control | Actuation state machine, interlocking, and contactor monitoring |
| [12_TELEGRAM_USER_GUIDE.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/12_TELEGRAM_USER_GUIDE.md) | Telegram User Guide | Admin manual for 25 Telegram Bot operational workflows |
| [13_SECURITY_MANUAL.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/13_SECURITY_MANUAL.md) | Security Manual | Cryptographic protection, replay defense, and access controls |
| [14_WIFI_AND_CONFIGURATION.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/14_WIFI_AND_CONFIGURATION.md) | Wi-Fi & Configuration | Wi-Fi management, safe switching, and recovery SoftAP |
| [15_FIRMWARE_UPDATE_GUIDE.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/15_FIRMWARE_UPDATE_GUIDE.md) | Firmware Update Guide | Dual-partition OTA and LoRa mesh firmware distribution |
| [16_ARDUINO_IDE_SETUP.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/16_ARDUINO_IDE_SETUP.md) | Arduino IDE Setup | Exact IDE settings, board packages, and compilation rules |
| [17_INSTALLATION_GUIDE.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/17_INSTALLATION_GUIDE.md) | Installation Guide | Enclosure, antenna mounting, and field electrical wiring |
| [18_COMMISSIONING_GUIDE.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/18_COMMISSIONING_GUIDE.md) | Commissioning Guide | 18-step field commissioning protocol and sign-off template |
| [19_TROUBLESHOOTING.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/19_TROUBLESHOOTING.md) | Troubleshooting | Diagnostic matrix for 19 electrical, RF, and system faults |
| [20_MAINTENANCE_MANUAL.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/20_MAINTENANCE_MANUAL.md) | Maintenance Manual | Preventive inspection routines and component life cycles |
| [21_TEST_REPORTS.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/21_TEST_REPORTS.md) | Test Reports | Verified results across unit, integration, and field suites |
| [22_HARDWARE_BOM.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/22_HARDWARE_BOM.md) | Hardware BOM | Detailed bills of materials for 1, 10, 20, and 50 nodes |
| [23_DEPLOYMENT_STATUS.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/23_DEPLOYMENT_STATUS.md) | Deployment Status | Production readiness audit and electrical safety status |
| [24_CHANGELOG.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/24_CHANGELOG.md) | Changelog | Architectural progression and milestone history (P06-P25) |
| [25_FINAL_HANDOVER.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/25_FINAL_HANDOVER.md) | Final Handover | Master engineering sign-off, contracts, and roadmap |
