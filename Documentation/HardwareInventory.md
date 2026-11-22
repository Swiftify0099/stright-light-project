# Hardware Inventory & Silicon Verification Report

**Project**: MSCB Smart Pole & Feeder Automation System  
**Release**: System_R1 (Prompt 06 Modular Architecture)  
**Author**: Senior ESP32 Embedded Firmware Engineer & Arduino IDE Architect  
**Date**: November 2026  

---

## 1. Executive Summary

This document specifies the authoritative hardware inventory, silicon characteristics, peripheral assignments, and component constraints for the MSCB Smart Pole System. The system comprises two distinct embedded controller tiers:

1. **Main Gateway Controller**: Central aggregation node running on an advanced dual-core ESP32-S3 with 16MB Flash and 8MB Octal PSRAM, hosting SX1278 433 MHz LoRa, dual Type-C USB interfaces, 2.4 GHz Wi-Fi, and Telegram Bot cloud connectivity.
2. **Secondary Field Pole Node**: Field automation controller running on an ESP32-WROOM-32, managing 6 independent Feeder Relay channels, an SX1278 LoRa mesh transceiver, and a high-precision 21-channel analog measurement subsystem based on three ADS131M08 delta-sigma ADCs.

---

## 2. Main Gateway Controller Hardware Inventory

| Parameter | Specification | Engineering Verification Notes |
| :--- | :--- | :--- |
| **Microcontroller Board** | DOIT ESP32-S3 Dual Type-C DevKit | Dual Type-C ports (Native USB-CDC + UART Bridge) |
| **Core Module** | ESP32-S3-WROOM-1 / WROOM-2 N16R8 | Dual-Core Xtensa® 32-bit LX7 @ 240 MHz |
| **Flash Memory** | 16 MB Quad/Octal SPI Flash | Configured via `FlashSize=16M` in Arduino IDE |
| **PSRAM Memory** | 8 MB Octal SPI (OPI) PSRAM | Requires `PSRAM=opi`. Internal pins GPIO 33-37 |
| **SRAM** | 512 KB Internal SRAM | High throughput FreeRTOS buffer allocation |
| **USB Interface 1** | Native USB 2.0 Full Speed (Type-C) | GPIO 19 (D-), GPIO 20 (D+). Native USB CDC / JTAG |
| **USB Interface 2** | USB-to-UART Bridge (Type-C) | CH343P / CP2102 bridge wired to GPIO 43 (TX) / 44 (RX) |
| **Wireless RF Subsystem** | Semtech SX1278 (Ai-Thinker RA-02) | 433 MHz LoRa transceiver module via FSPI (SPI2) |
| **Wi-Fi Connectivity** | Integrated 2.4 GHz 802.11 b/g/n | Internal PCB antenna with diversity / Telegram Bot link |
| **Power Supply Input** | 5.0 VDC via USB Type-C or Vin pin | Onboard 3.3V LDO regulator (minimum 800 mA rating) |
| **Operating Voltage** | 3.3 VDC Logic Level | All digital I/O is 3.3V strictly (NOT 5V tolerant) |

### Silicon Allocation & Pin Constraints (ESP32-S3 N16R8)
- **Octal PSRAM / Flash Internal Range (FORBIDDEN)**: GPIO 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37. Any firmware reference to these pins will cause immediate memory corruption or CPU abort.
- **Native USB Range (RESERVED)**: GPIO 19, GPIO 20.
- **Strapping Pins**: GPIO 0 (Boot mode), GPIO 45 (VDD_SPI voltage), GPIO 46 (ROM log).
- **LoRa Native IOMUX (FSPI)**: SCK=12, MISO=13, MOSI=11, CS=10, RST=14, DIO0=2.

---

## 3. Secondary Field Pole Node Hardware Inventory

| Parameter | Specification | Engineering Verification Notes |
| :--- | :--- | :--- |
| **Microcontroller Board** | Standard ESP32 ESP-WROOM-32 DevKit | 30-pin or 38-pin classic ESP32 development module |
| **Core Silicon** | ESP32-D0WDQ6 / ESP32-D0WD | Dual-Core Xtensa® 32-bit LX6 @ 240 MHz |
| **Flash Memory** | 4 MB Quad SPI Flash | Configured via `FlashSize=4M` |
| **PSRAM Memory** | None (Standard WROOM-32) | `PSRAM=disabled` |
| **SRAM** | 520 KB Internal SRAM | Dedicated to mesh routing, buffers, and ADC tasks |
| **Wireless RF Subsystem** | Semtech SX1278 (Ai-Thinker RA-02) | 433 MHz LoRa transceiver module via VSPI (SPI3) |
| **Relay Driver Subsystem**| 6-Channel Relay Board | Optically isolated, Active-LOW drive, 10A/250VAC contacts |
| **Analog ADC Subsystem** | 3x ADS131M08 (Candidate ADC) | 24-bit, 8-channel simultaneous sampling Delta-Sigma |
| **Total Analog Channels** | 21 Active Channels per Pole | 18 Feeder Currents (3 phases * 6) + 3 Bus Voltages |
| **Current Sensor Front-End**| ACS712-20A / Precision CT Sensors | 18 channels with analog attenuation & clamping |
| **Voltage Sensor Front-End**| ZMPT101B Precision Transformers | 3 channels (R, Y, B phases) with anti-aliasing filter |
| **Power Supply Input** | 5.0 VDC Regulated Rail | Dual isolated rails: 5V logic / 5V relay coil |

### Silicon Allocation & Pin Constraints (ESP32 Classic)
- **SPI Flash Internal Range (FORBIDDEN)**: GPIO 6, 7, 8, 9, 10, 11.
- **Input-Only Pins (GPI)**: GPIO 34, 35, 36 (VP), 39 (VN). No pull-up/pull-down; strictly input only. Mapped to ADC DRDY interrupt lines.
- **Strapping Hazards (CRITICAL)**:
  - **GPIO 12 (MTDI)**: If sampled HIGH at reset, flash voltage drops to 1.8V, bricking boot. **Strictly detached from relay outputs in R1 architecture!**
  - **GPIO 15 (MTDO)**: Outputs boot debug pulses; causes relay chatter if connected to coils. Removed from relay outputs.
  - **GPIO 5**: Outputs clock glitch at power-up. LoRa NSS requires an external 10k pull-up resistor.
  - **GPIO 2**: Connected to onboard LED and strapping. LoRa DIO0 remapped to GPIO 4.

---

## 4. Overall Network Scale & Dimensioning

- **Maximum Mesh Nodes**: 50 Field Pole Nodes + 1 Central Gateway.
- **Feeder Channels per Node**: 6 independently switched feeders.
- **Total Feeder Switch Points across Network**: $50 \times 6 = 300$ controlled feeders.
- **Analog Measurement Points across Network**: $50 \times 21 = 1,050$ real-time electrical monitoring channels.
- **LoRa Topology**: Clustered multi-hop mesh with hop limit $H_{max} = 6$.
- **Operating RF Frequency**: 433.00 MHz (SX1278 PA_BOOST up to +20 dBm, BW=125 kHz, SF=10, CR=4/5).
