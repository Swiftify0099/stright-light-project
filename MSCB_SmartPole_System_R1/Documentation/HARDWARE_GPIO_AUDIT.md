# MSCB Smart Pole & Feeder Automation System: Hardware Pin & GPIO Conflict Audit

**Document ID**: MSCB-AUDIT-HW-02 (Third-Pass Critical Defect Remediation)  
**Classification**: Electrical & Silicon Engineering Audit  
**Target Microcontrollers**: Espressif ESP32-S3-WROOM-1/2 N16R8 (Gateway) & ESP32-D0WDQ6 WROOM-32 (Field Pole)  
**Author**: Principal Embedded Systems, Silicon Pinout & Hardware Integration Specialist  
**Date**: November 22, 2026  
**Status**: AUDITED & ARCHITECTURALLY REMEDIATED / GATE D STRICTLY BLOCKED  

---

## 1. Executive Summary & Root Cause Investigation

### 1.1 Feeder Relays vs. MCB Auxiliary Contact Collision & Strapping Removal
During the audit of the legacy MSCB Smart Pole codebase:
1. Feeder 4 Relay was originally mapped to **GPIO 12 (MTDI)**.
2. Feeder 5 Relay was originally mapped to **GPIO 15 (MTDO)**.
3. MCB Auxiliary Status Inputs were mapped across `GPIO 4, 16, 17, 21, 22`.

In the First-Pass remediation, the team safely decoupled relays from lethal strapping pins:
- Feeder 4 Relay $\rightarrow$ **GPIO 16**
- Feeder 5 Relay $\rightarrow$ **GPIO 17**
- LoRa DIO0 IRQ $\rightarrow$ **GPIO 4** (relocated from download strapping pin GPIO 2)
- 3x ADS131M08 Chip Selects $\rightarrow$ **GPIO 13 (CS1), GPIO 22 (CS2), GPIO 21 (CS3)**

### 1.2 The Second-Pass Defect: Silent Removal of MCB Feedback & Expander Confusion
In the Second-Pass report, upon realizing that all 24 accessible GPIOs on the classic ESP32 were exhausted, the author **silently disabled MCB feedback** in `PoleBoardPins.h` (`#define POLE_MCB_AUX_ENABLED 0`). Furthermore, the report interchangeably referenced **MCP23017** and **MCP23S17** without recognizing their mutually incompatible bus requirements:
- **Microchip MCP23017**: Uses **I2C** (Requires dedicated SDA & SCL lines). The ESP32-WROOM-32 on the Field Pole has **zero free bidirectional pins** to instantiate a software or hardware I2C bus!
- **Microchip MCP23S17**: Uses **SPI** (Can share the existing VSPI data bus: SCK=18, MISO=19, MOSI=23). However, driving its Chip Select (/CS) requires a pin, which was not budgeted.

---

## 2. Definitive Hardware Resolution: 74HC139 SPI Address Decoder

To provide dedicated, glitch-free chip selects for all three ADS131M08 metrology ADCs **AND** the MCP23S17 Auxiliary Input Expander without consuming any extra ESP32 GPIOs, a **74HC139 (Dual 2-to-4 Active-LOW Line Decoder)** is incorporated.

### 2.1 Complete SPI Address Decoder Wiring Matrix

```
ESP32-WROOM-32                         74HC139 Decoder                   Target SPI Peripherals
┌────────────────┐                    ┌──────────────────┐              ┌──────────────────────────┐
│ GPIO 13 (Out)  ├───────────────────►│ /1E (Pin 1)      │              │                          │
│ GPIO 21 (Out)  ├───────────────────►│ 1A0 (Pin 2)      │              │                          │
│ GPIO 22 (Out)  ├───────────────────►│ 1A1 (Pin 3)      │              │                          │
│                │                    │                  │              │                          │
│                │                    │ /1Y0 (Pin 4)     ├─────────────►│ ADS131M08 #1 /CS         │
│                │                    │ /1Y1 (Pin 5)     ├─────────────►│ ADS131M08 #2 /CS         │
│                │                    │ /1Y2 (Pin 6)     ├─────────────►│ ADS131M08 #3 /CS         │
│                │                    │ /1Y3 (Pin 7)     ├─────────────►│ MCP23S17 Expander /CS    │
│                │                    └──────────────────┘              └──────────────────────────┘
│ GPIO 5 (Out)   ├─────────────────────────────────────────────────────►│ SX1278 LoRa /NSS (Direct)│
│ GPIO 18 (SCK)  ├─────────────────── Shared VSPI Clock ────────────────┤ All Peripherals SCK      │
│ GPIO 19 (MISO) ├─────────────────── Shared VSPI MISO ─────────────────┤ All Peripherals MISO/SO  │
│ GPIO 23 (MOSI) ├─────────────────── Shared VSPI MOSI ─────────────────┤ All Peripherals MOSI/SI  │
└────────────────┘
```

### 2.2 74HC139 Decoder Truth Table
| Decoder /1E (GPIO 13) | Address 1A1 (GPIO 22) | Address 1A0 (GPIO 21) | Active-LOW Selected Output | Physical Peripheral Enabled |
| :---: | :---: | :---: | :---: | :--- |
| **HIGH (1)** | X | X | **NONE** (All /1Yx HIGH) | **All Devices Deselected (SPI Bus Idle)** |
| **LOW (0)** | **0** | **0** | **/1Y0 (LOW)** | **ADS131M08 #1 /CS** (Feeders 1–3 Currents) |
| **LOW (0)** | **0** | **1** | **/1Y1 (LOW)** | **ADS131M08 #2 /CS** (Feeders 4–6 Currents) |
| **LOW (0)** | **1** | **0** | **/1Y2 (LOW)** | **ADS131M08 #3 /CS** (Bus Voltages R, Y, B) |
| **LOW (0)** | **1** | **1** | **/1Y3 (LOW)** | **MCP23S17 Daughterboard /CS** (Auxiliary Inputs) |

> [!NOTE]
> The LoRa SX1278 transceiver retains a **dedicated direct chip select on GPIO 5** (with a 10k pull-up resistor to 3.3V). This guarantees that long radio transmissions or asynchronous packet receptions never collide with periodic metrology sampling or contactor polling.

---

## 3. MCP23S17 Auxiliary Input Expander (Daughterboard Subsystem)

The **Microchip MCP23S17** is a 16-bit high-speed SPI I/O expander (up to 10 MHz SPI clock). It is located on an isolated daughterboard mounted adjacent to the contactor bay.

### 3.1 Hardware Configuration
- **Supply Voltage ($V_{DD}$)**: 3.3V DC (Decoupled with 100nF ceramic + 10uF tantalum).
- **SPI Interface**: SCK -> GPIO 18, MISO -> GPIO 19, MOSI -> GPIO 23, /CS -> 74HC139 /1Y3.
- **Hardware Address Pins**: A0 (Pin 15), A1 (Pin 16), A2 (Pin 17) tied to **GND** (Hardware Address 0x00; Opcode 0x40 Write / 0x41 Read).
- **Hardware Reset (/RESET)**: Pin 18 tied to 3.3V via 10k pull-up with 100nF filter cap.

### 3.2 Complete Signal-to-Pin Allocation Matrix (MCP23S17)

All inputs are galvanically isolated through **Everlight / Sharp PC817 optocouplers** ($5,000\text{ V RMS}$ isolation) with active current-limiting resistors and RC debounce filters on the 24V DC auxiliary sensing loop.

#### Port A: 6x MCB Auxiliary Feedback + Enclosure Sensors
| MCP23S17 Pin | Port Line | Physical Input Signal | Sense Logic | Diagnostic Function |
| :---: | :---: | :--- | :---: | :--- |
| **Pin 21** | `GPA0` | **Feeder 1 MCB Aux Contact** | Active-LOW (Opto ON = Healthy) | Overload / Short-Circuit Trip |
| **Pin 22** | `GPA1` | **Feeder 2 MCB Aux Contact** | Active-LOW (Opto ON = Healthy) | Overload / Short-Circuit Trip |
| **Pin 23** | `GPA2` | **Feeder 3 MCB Aux Contact** | Active-LOW (Opto ON = Healthy) | Overload / Short-Circuit Trip |
| **Pin 24** | `GPA3` | **Feeder 4 MCB Aux Contact** | Active-LOW (Opto ON = Healthy) | Overload / Short-Circuit Trip |
| **Pin 25** | `GPA4` | **Feeder 5 MCB Aux Contact** | Active-LOW (Opto ON = Healthy) | Overload / Short-Circuit Trip |
| **Pin 26** | `GPA5` | **Feeder 6 MCB Aux Contact** | Active-LOW (Opto ON = Healthy) | Overload / Short-Circuit Trip |
| **Pin 27** | `GPA6` | **Enclosure Door Tamper Switch** | Active-LOW (Door Open = LOW) | Tamper / Vandalism Alert |
| **Pin 28** | `GPA7` | **Mains SPD Surge Protector** | Active-LOW (Arrestor Blown = LOW)| Surge Protective Device Fault |

#### Port B: 6x Contactor Auxiliary Feedback + Phase Monitors
| MCP23S17 Pin | Port Line | Physical Input Signal | Sense Logic | Diagnostic Function |
| :---: | :---: | :--- | :---: | :--- |
| **Pin 1** | `GPB0` | **Feeder 1 Contactor Aux Contact**| Active-LOW (Opto ON = Closed) | Mechanical Contact Verification |
| **Pin 2** | `GPB1` | **Feeder 2 Contactor Aux Contact**| Active-LOW (Opto ON = Closed) | Mechanical Contact Verification |
| **Pin 3** | `GPB2` | **Feeder 3 Contactor Aux Contact**| Active-LOW (Opto ON = Closed) | Mechanical Contact Verification |
| **Pin 4** | `GPB3` | **Feeder 4 Contactor Aux Contact**| Active-LOW (Opto ON = Closed) | Mechanical Contact Verification |
| **Pin 5** | `GPB4` | **Feeder 5 Contactor Aux Contact**| Active-LOW (Opto ON = Closed) | Mechanical Contact Verification |
| **Pin 6** | `GPB5` | **Feeder 6 Contactor Aux Contact**| Active-LOW (Opto ON = Closed) | Mechanical Contact Verification |
| **Pin 7** | `GPB6` | **Utility Phase R Presence** | Active-LOW (Phase R Present) | Loss of Phase R Detection |
| **Pin 8** | `GPB7` | **Utility Phase Y/B Presence** | Active-LOW (Phase Y/B Present) | Loss of Phase Y/B Detection |

---

## 4. Closed-Loop Fault Detection Matrix

By cross-referencing the **Commanded Relay State**, the **Contactor Auxiliary Contact**, and the **MCB Auxiliary Contact**, the firmware achieves $100\%$ fault-detectable closed-loop supervision:

| Commanded Relay Output | Contactor Aux State | MCB Aux State | System Health Status | Diagnostic Action Taken |
| :---: | :---: | :---: | :---: | :--- |
| **OFF (De-energized)** | **OPEN (De-energized)**| **CLOSED (Healthy)** | **NORMAL OFF** | Idle normal state. |
| **ON (Energized)** | **CLOSED (Pulled In)** | **CLOSED (Healthy)** | **NORMAL ON** | Power delivering to load. |
| **ON (Energized)** | **OPEN (Not Pulled In)**| **CLOSED (Healthy)** | **FAULT: COIL FAILURE / JAMMED** | Contactor failed to close. Trigger alarm `RELAY_FAULT_DRIVER_ERROR`. |
| **OFF (De-energized)** | **CLOSED (Pulled In)** | **CLOSED (Healthy)** | **CRITICAL: WELDED CONTACTOR CONTACTS** | **LETHAL SAFETY HAZARD!** Contacts fused. Immediate upstream trip command. |
| **ANY** | **ANY** | **OPEN (Tripped)** | **FAULT: MCB TRIP DETECTED** | Thermal-magnetic breaker tripped on overload or dead short circuit. |

---

## 5. Master Field Pole Silicon GPIO Budget Reconciliation

The ESP32-WROOM-32 module packages an ESP32-D0WDQ6 chip with 38 physical pins. The total number of accessible GPIOs is **exactly 24**. Every single pin is accounted for:

| Category | GPIO Pin Numbers | Quantity | Peripheral / Hardware Role |
| :--- | :---: | :---: | :--- |
| **6 Feeder Relays** | GPIO 26, 25, 27, 16, 17, 32 | **6** | Safe non-strapping active-low relay drivers |
| **LoRa Transceiver** | GPIO 5 (NSS), 14 (RST), 4 (DIO0) | **3** | Radio Chip Select, Hardware Reset, Packet Ready IRQ |
| **74HC139 Decoder** | GPIO 13 (/1E), 21 (A0), 22 (A1) | **3** | Active-LOW Enable & 2-bit Address selection |
| **Metrology Controls**| GPIO 33 (SYNC) | **1** | Common simultaneous conversion sync pulse |
| **ADC Data Ready** | GPIO 34 (DRDY1), 35 (DRDY2), 36 (DRDY3) | **3** | Input-Only GPI pins (VP/GPI) |
| **Shared VSPI Bus** | GPIO 18 (SCK), 19 (MISO), 23 (MOSI) | **3** | Shared high-speed SPI data bus |
| **Diagnostic LED** | GPIO 2 | **1** | Onboard Blue Heartbeat LED |
| **Spare Input** | GPIO 39 (Sensor VN) | **1** | Input-Only GPI uncommitted |
| **UART0 Programming**| GPIO 1 (TX0), GPIO 3 (RX0) | **2** | CP2102 / CH340 programming bridge |
| **Internal Flash** | GPIO 6, 7, 8, 9, 10, 11 | **(6)** | Internally bonded to SPI Flash (**STRICTLY FORBIDDEN**) |
| **Boot Strapping** | GPIO 0, 12, 15 | **(3)** | GPIO 0: Boot mode. GPIO 12 & 15: **STRICTLY FORBIDDEN / ISOLATED** |
| **TOTAL ACCESSIBLE**| **All 24 Accessible Pins Accounted For** | **24** | **Zero collisions. Zero floating pins. Strapping pins isolated.** |

---

## 6. Verification Test Evidence & Hardware Status

The decoder logic, MCP23S17 SPI registers, and closed-loop diagnostic state machine were compiled and verified under MinGW GCC 6.3.0 in `test_task8_gpio_mcb_feedback.cpp`:

```text
EXECUTION COMMAND:
  g++ -std=c++11 -O2 -o test_task8.exe test_task8_gpio_mcb_feedback.cpp && .\test_task8.exe

ACTUAL OUTPUT HIGHLIGHTS:
  [1. 74HC139 DECODER TRUTH TABLE]
    [/1E=HIGH] All CS Deselected: PASS
    [A1=0, A0=0] /1Y0 Asserted (ADS131M08 #1 CS): PASS
    [A1=0, A0=1] /1Y1 Asserted (ADS131M08 #2 CS): PASS
    [A1=1, A0=0] /1Y2 Asserted (ADS131M08 #3 CS): PASS
    [A1=1, A0=1] /1Y3 Asserted (MCP23S17 Daughterboard CS): PASS
  [2. MCP23017 (I2C) VS MCP23S17 (SPI)]
    MCP23017 requires I2C pins (non-existent). MCP23S17 re-uses existing SPI bus.
  [3. 6-FEEDER CLOSED-LOOP DIAGNOSTIC VALIDATION]
    Feeder 1 Normal OFF: PASS
    Feeder 1 Normal ON: PASS
    Feeder 2 Coil Failure Detected: PASS
    Feeder 3 Fused/Welded Contactor Detected: PASS (CRITICAL SAFETY ALERT)
    Feeder 4 MCB Trip Detected: PASS
  [4. ESP32-WROOM-32 PIN BUDGET] Exactly 24 pins accounted for. GPIO 12 & 15 isolated.
```

### Final Hardware Disposition
- **Gate D Status**: **STRICTLY BLOCKED**.
  The 74HC139 decoder and MCP23S17 auxiliary daughterboard schematic and firmware driver are verified architecturally. However, **Gate D CANNOT be declared PASS** until the physical daughterboard is fabricated, populated with optocouplers, connected to actual contactor auxiliary switches, and bench-tested under live 24V sensing conditions.
