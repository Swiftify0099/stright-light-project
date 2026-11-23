# MSCB Smart Pole & Feeder Automation System: Hardware Pin & GPIO Conflict Audit

**Document ID**: MSCB-AUDIT-HW-01  
**Classification**: Electrical & Silicon Engineering Audit  
**Target Microcontrollers**: Espressif ESP32-S3-WROOM-1/2 N16R8 (Gateway) & ESP32-D0WDQ6 WROOM-32 (Field Pole)  
**Author**: Principal Embedded Systems & Hardware Integration Engineer  
**Date**: November 2026  
**Status**: VERIFIED & REMEDIATED  

---

## 1. Executive Summary & Root Cause Investigation

### 1.1 The Critical Investigation: Feeder Relays vs. MCB Auxiliary Contact Collision

During an exhaustive source-code and schematic audit of the MSCB Smart Pole System R1, a **critical hardware pin collision hazard** was uncovered between the 6-feeder relay actuation subsystem and the MCB auxiliary feedback subsystem on the classic ESP32 Field Pole Node.

In the legacy prototype codebase:
- Feeder 4 Relay was originally mapped to **GPIO 12 (MTDI)**.
- Feeder 5 Relay was originally mapped to **GPIO 15 (MTDO)**.
- MCB Auxiliary Status Inputs were mapped across `GPIO 4, 16, 17, 21, 22`.

During the Prompt 06–08 refactoring, the engineering team correctly identified that GPIO 12 and GPIO 15 were lethal strapping pins:
1. **GPIO 12 (MTDI)** controls the internal LDO voltage for SPI flash memory. An active-low relay module with an optocoupler pull-up pulls GPIO 12 HIGH at reset, forcing the internal flash LDO from 3.3V down to 1.8V, permanently locking the ESP32 in a boot brownout crash loop (`flash read err, 1000`).
2. **GPIO 15 (MTDO)** outputs high-speed serial debug pulses and PWM clocks at boot, causing mechanical relay chatter and contactor arcing during power cycling.

To eliminate these strapping hazards, the team relocated:
- Feeder 4 Relay $\rightarrow$ **GPIO 16**
- Feeder 5 Relay $\rightarrow$ **GPIO 17**
- LoRa DIO0 IRQ $\rightarrow$ **GPIO 4** (relocated from download strapping pin GPIO 2)
- 3x ADS131M08 Chip Selects $\rightarrow$ **GPIO 13 (CS1), GPIO 22 (CS2), GPIO 21 (CS3)**

### 1.2 The Root Cause of the Collision

While the relay and ADC outputs were safely reassigned, the MCB auxiliary input mapping in system documentation (Doc 03 line 136) and configuration headers was **not updated**:
```text
MCBAUX --> |MCB Feedback: GPIO 4, 16, 17, 21, 22| CPU
```
Had physical hardware been wired according to this specification:
- **GPIO 4** (LoRa SX1278 DIO0 Packet Ready Interrupt) would be shorted to MCB Phase R feedback.
- **GPIO 16** (Feeder 4 Relay Output Driver) would be shorted to MCB Phase Y feedback.
- **GPIO 17** (Feeder 5 Relay Output Driver) would be shorted to MCB Phase B feedback.
- **GPIO 21** (ADS131M08 #3 Chip Select Output) would be shorted to MCB Feeder 4 feedback.
- **GPIO 22** (ADS131M08 #2 Chip Select Output) would be shorted to MCB Feeder 5 feedback.

When the ESP32 configured GPIO 16 and 17 as push-pull `OUTPUT` to energize contactor coils, an external ground from an MCB trip auxiliary switch would create a **direct silicon-level short circuit**, causing excessive current draw (> 40 mA), internal GPIO driver burnout, or permanent microcontroller destruction. Furthermore, asserting ADC Chip Selects on GPIO 21 and 22 would be corrupted by MCB switch transitions, causing catastrophic SPI bus contention and loss of 21-channel electrical metrology.

---

## 2. Silicon Pin Budget Analysis (ESP32-WROOM-32)

The ESP32-WROOM-32 module packages an ESP32-D0WDQ6 chip with 38 physical pins (30-pin and 38-pin DevKit boards). The total number of accessible GPIOs is 24:

```text
Total Exposed Physical Pins on DevKit: 38
  - Power & Ground: 3V3, GND (x3), EN, VIN (5V)                   = 6 pins
  - Embedded SPI Flash (Internal Bonded): GPIO 6, 7, 8, 9, 10, 11   = 6 pins [FORBIDDEN]
  - Dedicated UART0 Programming: GPIO 1 (TX0), GPIO 3 (RX0)        = 2 pins [RESERVED]
  - Input-Only Pins (No internal PU/PD): GPIO 34, 35, 36, 39       = 4 pins [INPUT ONLY]
  - General Purpose Bidirectional GPIOs: 0, 2, 4, 5, 12, 13, 14, 15,
                                         16, 17, 18, 19, 21, 22,
                                         23, 25, 26, 27, 32, 33     = 20 pins
```

### Pin Demand vs. Availability on Field Pole Node:
1. **6 Feeder Relay Outputs**: Requires 6 general-purpose outputs.
2. **LoRa SX1278 SPI Bus**: SCK, MISO, MOSI, NSS, RST, DIO0 = 6 pins (5 bidirectional + 1 input).
3. **3x ADS131M08 Metrology**:
   - Shared SPI bus (SCK:18, MISO:19, MOSI:23 shared with LoRa) = 0 extra pins.
   - Dedicated Chip Selects (CS1, CS2, CS3) = 3 output pins.
   - Common Synchronization (/SYNC) = 1 output pin.
   - Data Ready Interrupts (DRDY1, DRDY2, DRDY3) = 3 pins (mapped to Input-Only GPIO 34, 35, 36).
4. **Onboard Diagnostic LED**: GPIO 2 = 1 pin.

$$\text{Total Required Output Pins} = 6\ (\text{Relays}) + 2\ (\text{LoRa CS/RST}) + 4\ (\text{ADC CS/SYNC}) + 1\ (\text{LED}) = 13\ \text{Outputs}$$
$$\text{Total Required Input Pins} = 1\ (\text{LoRa DIO0}) + 3\ (\text{ADC DRDY}) = 4\ \text{Inputs}$$
$$\text{Total Bidirectional/Output Pins Consumed} = 13 + 3\ (\text{SPI Bus}) + 1\ (\text{LoRa DIO0}) = 17\ \text{Pins}$$

Out of the 20 bidirectional GPIOs:
- Strapping pins GPIO 0, 12, 15 cannot be safely used for relays or active-low inputs without complex external hardware.
- UART0 pins GPIO 1, 3 are reserved for debugging.
- **Available Remaining GPIOs on Native Silicon: Exactly 0 free output pins, and 1 input-only pin (GPIO 39 / VN)!**

### Definitive Architectural Conclusion:
It is **physically impossible** for an ESP32-WROOM-32 to monitor 6 independent MCB auxiliary inputs and 6 contactor auxiliary contacts using native GPIOs while simultaneously operating 6 feeder relays, 3 external ADCs, and LoRa SPI.

---

## 3. Engineering Resolution & Remediation Strategy

1. **Software Pin Isolation**: Direct native GPIO reading for MCBs is formally **disabled** in `PoleBoardPins.h`:
   ```cpp
   #define POLE_MCB_AUX_ENABLED    0
   #define POLE_MCB_PIN_DISABLED   255
   ```
2. **Compile-Time Static Conflict Assertions**: Added comprehensive `static_assert` statements in `PoleBoardPins.h` verifying:
   - No relay pin collides with any other relay pin.
   - No relay pin is mapped to GPIO 12, GPIO 15, or GPIO 0.
   - No relay pin is mapped to Input-Only GPIOs (34, 35, 36, 39).
   - No relay pin shares GPIO with ADC Chip Selects (13, 22, 21), SYNC (33), or LoRa (4, 5, 14, 18, 19, 23).
3. **Physical Hardware Roadmap for Auxiliary Contact Sensing**:
   - **Option A (Recommended for Production PCB)**: Add an **I2C 8-bit / 16-bit GPIO Expander** (Microchip MCP23017 or TI TCA9534) on dedicated I2C pins.
   - **Option B**: Interface auxiliary dry contacts through the 3 uncommitted spare analog channels on ADS131M08 #2 (Ch 7) and #3 (Ch 6, 7) via resistor divider networks.

---

## 4. Master GPIO Pinout Matrices

### 4.1 Gateway Controller: DOIT ESP32-S3-WROOM-1/2 N16R8 Dual Type-C

| Pin Name | GPIO | Native Peripheral | I/O Direction | Strapping / Hardware Role | Verification Status | Physical Hardware Connected |
| :--- | :---: | :--- | :---: | :--- | :---: | :--- |
| `GW_LORA_SCK` | **12** | FSPI (SPI2) SCK | OUTPUT | Normal GPIO (Native IOMUX) | **VERIFIED SAFE** | SX1278 LoRa SPI Clock |
| `GW_LORA_MISO` | **13** | FSPI (SPI2) MISO | INPUT | Normal GPIO (Native IOMUX) | **VERIFIED SAFE** | SX1278 LoRa SPI MISO |
| `GW_LORA_MOSI` | **11** | FSPI (SPI2) MOSI | OUTPUT | Normal GPIO (Native IOMUX) | **VERIFIED SAFE** | SX1278 LoRa SPI MOSI |
| `GW_LORA_NSS` | **10** | FSPI (SPI2) CS0 | OUTPUT | Normal GPIO (Native IOMUX) | **VERIFIED SAFE** | SX1278 Chip Select (Active LOW) |
| `GW_LORA_RST` | **14** | General GPIO | OUTPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 Hardware Reset |
| `GW_LORA_DIO0` | **2** | RTC_GPIO2 / IRQ | INPUT | Normal GPIO on S3 | **VERIFIED SAFE** | SX1278 Packet Ready Interrupt |
| `GW_LORA_DIO1` | **1** | RTC_GPIO1 / CAD | INPUT | Auxiliary LoRa CAD | **VERIFIED SAFE** | Optional CAD / Timeout Interrupt |
| `GW_STATUS_LED`| **21** | General GPIO | OUTPUT | Diagnostic Heartbeat | **VERIFIED SAFE** | Onboard Diagnostic LED |
| `GW_RGB_WS2812`| **48** | Onboard RMT | OUTPUT | Addressable RGB Data | **VERIFIED SAFE** | Onboard WS2812 RGB LED |
| `USB_DM` | **19** | Native USB D- | BIDI | USB PHY | **RESERVED** | Type-C Port 1 (Native CDC/JTAG) |
| `USB_DP` | **20** | Native USB D+ | BIDI | USB PHY | **RESERVED** | Type-C Port 1 (Native CDC/JTAG) |
| `UART0_TX` | **43** | UART0 TXD | OUTPUT | Boot log print | **RESERVED** | Type-C Port 2 (CH343P UART Bridge) |
| `UART0_RX` | **44** | UART0 RXD | INPUT | Firmware upload | **RESERVED** | Type-C Port 2 (CH343P UART Bridge) |
| `BOOT_KEY` | **0** | Strapping | INPUT | LOW = Download Mode | **RESTRICTED** | Onboard Pushbutton (10k Pullup) |
| `VDD_SPI` | **45** | Strapping | - | Internal LDO Voltage | **RESTRICTED** | Must remain LOW (Internal pull-down) |
| `ROM_LOG` | **46** | Strapping | - | Boot ROM Print Control | **RESTRICTED** | Must remain LOW at boot |
| **OCTAL FLASH**| **26–32**| SPI Flash Bus | - | Internally Bonded | **FORBIDDEN** | Bonded to 16MB Octal SPI Flash |
| **OCTAL PSRAM**| **33–37**| OPI PSRAM Bus | - | Internally Bonded | **FORBIDDEN** | Bonded to 8MB Octal SPI PSRAM |

> [!CAUTION]
> **ESP32-S3 OCTAL SILICON INTEGRITY**:
> In the N16R8 package, GPIOs 26 through 37 are permanently wired inside the module casing to Octal Flash and Octal PSRAM. Any software configuration or external connection to GPIO 26–37 will instantly crash the core (`Guru Meditation Error: IllegalInstruction / Cache disabled but memory accessed`). Compile-time static assertions in `GwBoardPins.h` enforce this boundary.

---

### 4.2 Field Pole Node: Classic ESP32 ESP-WROOM-32 (30/38 Pin DevKit)

> [!NOTE]
> **Physical Pin 24 Identification**:
> On the 30-pin ESP32 development board, physical board position 24 is board label **D26**, which routes to silicon **GPIO 26**.
> Firmware Feeder 1 is assigned to GPIO 26 (`POLE_RELAY_F0_PIN = 26`), matching physical pin 24.
> Silicon GPIO 24 is an internal SPI flash memory pin and is strictly forbidden.

| Pin Name | Physical Header Pin | Board Label | GPIO | Native Peripheral | I/O Direction | Strapping / Reset Role | Verification Status | Physical Hardware Connected |
| :--- | :---: | :---: | :---: | :--- | :---: | :--- | :---: | :--- |
| `POLE_LORA_SCK` | 15 | **D18** | **18** | VSPI SCK | OUTPUT | Normal GPIO (Shared SPI) | **VERIFIED SAFE** | SX1278 + 3x ADS131M08 SPI Clock |
| `POLE_LORA_MISO`| 14 | **D19** | **19** | VSPI MISO | INPUT | Normal GPIO (Shared SPI) | **VERIFIED SAFE** | SX1278 + 3x ADS131M08 SPI MISO |
| `POLE_LORA_MOSI`| 19 | **D23** | **23** | VSPI MOSI | OUTPUT | Normal GPIO (Shared SPI) | **VERIFIED SAFE** | SX1278 + 3x ADS131M08 SPI MOSI |
| `POLE_LORA_NSS` | 16 | **D5** | **5** | VSPI CS0 | OUTPUT | Glitches clock pulse at boot | **VERIFIED SAFE** | SX1278 CS (External 10k pull-up) |
| `POLE_LORA_RST` | 13 | **D14** | **14** | General GPIO | OUTPUT | Safe output | **VERIFIED SAFE** | SX1278 Hardware Reset |
| `POLE_LORA_DIO0`| 11 | **D4** | **4** | General GPIO / IRQ | INPUT | Safe general input | **VERIFIED SAFE** | SX1278 Packet Ready Interrupt |
| `POLE_RELAY_F0` | **24** | **D26** | **26** | DAC2 / RTC_IO7 | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 1 Contactor Driver (Physical Pin 24) |
| `POLE_RELAY_F1` | **23** | **D25** | **25** | DAC1 / RTC_IO6 | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 2 Contactor Driver (Physical Pin 23) |
| `POLE_RELAY_F2` | **25** | **D27** | **27** | General GPIO | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 3 Contactor Driver (Physical Pin 25) |
| `POLE_RELAY_F3` | **6** | **RX2** | **16** | UART2 RX / GPIO | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 4 Contactor Driver (Physical Pin 6) |
| `POLE_RELAY_F4` | **7** | **TX2** | **17** | UART2 TX / GPIO | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 5 Contactor Driver (Physical Pin 7) |
| `POLE_RELAY_F5` | **21** | **D32** | **32** | RTC_IO9 | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 6 Contactor Driver (Physical Pin 21) |
| `POLE_ADC1_CS` | 12 | **D13** | **13** | General GPIO | OUTPUT | Safe output | **VERIFIED SAFE** | ADS131M08 #1 Chip Select (Active LOW) |
| `POLE_ADC2_CS` | 27 | **D22** | **22** | I2C SCL / GPIO | OUTPUT | Safe output | **VERIFIED SAFE** | ADS131M08 #2 Chip Select (Active LOW) |
| `POLE_ADC3_CS` | 26 | **D21** | **21** | I2C SDA / GPIO | OUTPUT | Safe output | **VERIFIED SAFE** | ADS131M08 #3 Chip Select (Active LOW) |
| `POLE_ADC_SYNC` | 22 | **D33** | **33** | RTC_IO8 | OUTPUT | Safe output | **VERIFIED SAFE** | Common ADS131M08 /SYNC Pulse |
| `POLE_ADC1_DRDY`| 5 | **D34** | **34** | GPI (Input-Only) | INPUT | Input-Only (No pullup/pulldown)| **VERIFIED SAFE** | ADS131M08 #1 Data Ready (/DRDY) |
| `POLE_ADC2_DRDY`| 4 | **D35** | **35** | GPI (Input-Only) | INPUT | Input-Only (No pullup/pulldown)| **VERIFIED SAFE** | ADS131M08 #2 Data Ready (/DRDY) |
| `POLE_ADC3_DRDY`| 3 | **VP** | **36** | GPI / SENSOR_VP | INPUT | Input-Only (No pullup/pulldown)| **VERIFIED SAFE** | ADS131M08 #3 Data Ready (/DRDY) |
| `POLE_STATUS_LED`| 10 | **D2** | **2** | Strapping / LED | OUTPUT | Boot mode strapping pin | **RESTRICTED** | Onboard Blue LED (Active HIGH) |
| `SPARE_INPUT` | 28 | **VN** | **39** | GPI / SENSOR_VN | INPUT | Input-Only (No pullup/pulldown)| **UNCOMMITTED** | Available auxiliary analog/digital input |
| `UART0_TXD` | 1 | **TX0** | **1** | UART0 TXD | OUTPUT | Boot log output | **RESERVED** | CP2102 / CH340 USB-UART bridge |
| `UART0_RXD` | 2 | **RX0** | **3** | UART0 RXD | INPUT | Firmware upload | **RESERVED** | CP2102 / CH340 USB-UART bridge |
| **FLASH BUS** | - | - | **6–11**| SPI Flash Bus | - | Dedicated Flash Memory | **FORBIDDEN** | Bonded to 4MB SPI Flash (DO NOT TOUCH)|

---

## 5. Audit Conclusions & Hardware Sign-Off Status

1. **Software Pin Collisions**: **REMEDIATED (PASS)**. All pairwise collisions resolved, verified via compiler `static_assert` statements in `GwBoardPins.h` and `PoleBoardPins.h`.
2. **Strapping Pin Protections**: **REMEDIATED (PASS)**. Relays completely decoupled from GPIO 12 and GPIO 15.
3. **Physical MCB Auxiliary Contact Wiring**: **PENDING HARDWARE INTEGRATION (NOT TESTED)**. Physical contactor and MCB auxiliary contacts must not be connected to native ESP32 pins. External I2C expander hardware is required before connecting auxiliary contacts in the field.
