# Authoritative Pin Mapping & GPIO Verification Report

**Project**: MSCB Smart Pole & Feeder Automation System  
**Release**: System_R1 (Prompt 06 Modular Architecture)  
**Author**: Senior ESP32 Embedded Firmware Engineer & Arduino IDE Architect  
**Date**: November 2026  

---

## 1. Overview & Verification Methodology

Pin assignments across ESP32 families require rigorous analysis of silicon multiplexing (IOMUX), strapping pin latch states during reset, internal boot ROM behaviors, and flash/PSRAM bus sharing.

In the old project (`Sender/` and `Resivers/`), hazardous pin assignments were identified, notably:
1. Feeder 4 Relay was assigned to **GPIO 12 (MTDI)**. An active-low relay board with an optocoupler pull-up pulls GPIO 12 HIGH at reset, forcing the ESP32 internal LDO to deliver 1.8V instead of 3.3V to the SPI flash, resulting in an unrecoverable boot loop (`flash read err, 1000`).
2. Feeder 5 Relay was assigned to **GPIO 15 (MTDO)**, which outputs PWM/debug pulses at boot, causing relay chatter during MCU power cycling.
3. LoRa DIO0 was wired to **GPIO 2**, which conflicts with the ESP32 download mode strapping pin and the onboard LED.

The **System_R1** architecture completely resolves all known GPIO conflicts and implements compile-time static assertions (`static_assert`) to ensure invalid pin configurations cannot be compiled.

---

## 2. Main Gateway Controller Pinout (ESP32-S3 N16R8)

### Target Board: ESP32-S3-WROOM-1 / WROOM-2 N16R8 DOIT Dual Type-C

| Pin Name | GPIO | Native Peripheral | Direction | Boot / Strapping Role | Verified Status | Purpose / Connected Hardware |
| :--- | :---: | :--- | :---: | :--- | :---: | :--- |
| **LORA_SCK** | **12** | FSPI (SPI2) SCK | OUTPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 LoRa SPI Clock (Native IOMUX) |
| **LORA_MISO** | **13** | FSPI (SPI2) MISO | INPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 LoRa SPI MISO (Native IOMUX) |
| **LORA_MOSI** | **11** | FSPI (SPI2) MOSI | OUTPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 LoRa SPI MOSI (Native IOMUX) |
| **LORA_NSS** | **10** | FSPI (SPI2) CS0 | OUTPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 LoRa Chip Select (Active LOW) |
| **LORA_RST** | **14** | General GPIO | OUTPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 LoRa Hardware Reset (Active LOW) |
| **LORA_DIO0** | **2** | RTC_GPIO2 / IRQ | INPUT | Normal GPIO (S3) | **VERIFIED SAFE** | SX1278 Packet Ready Interrupt |
| **STATUS_LED**| **21** | General GPIO | OUTPUT | Normal GPIO | **VERIFIED SAFE** | Diagnostic Heartbeat Status LED |
| **RGB_WS2812**| **48** | Onboard RMT | OUTPUT | Normal GPIO | **VERIFIED SAFE** | Onboard Addressable WS2812 RGB LED |
| **UART0_TX** | **43** | UART0 TXD | OUTPUT | Boot log print | **RESERVED** | Type-C Port 2 (USB-UART Bridge) |
| **UART0_RX** | **44** | UART0 RXD | INPUT | Firmware flash | **RESERVED** | Type-C Port 2 (USB-UART Bridge) |
| **USB_DM** | **19** | Native USB D- | BIDI | USB PHY | **RESERVED** | Type-C Port 1 (Native USB-CDC/JTAG) |
| **USB_DP** | **20** | Native USB D+ | BIDI | USB PHY | **RESERVED** | Type-C Port 1 (Native USB-CDC/JTAG) |
| **BOOT_KEY** | **0** | Strapping | INPUT | LOW = Download | **RESTRICTED** | Onboard Boot Button (Must pull HIGH) |
| **VDD_SPI** | **45** | Strapping | - | Flash Voltage | **RESTRICTED** | Must remain LOW (Internal pull-down) |
| **ROM_LOG** | **46** | Strapping | - | ROM print | **RESTRICTED** | Must remain LOW at boot |
| **INTERNAL** | **26-32**| SPI Flash Bus | - | Dedicated Flash | **FORBIDDEN** | Bonded to 16MB Flash (DO NOT USE) |
| **INTERNAL** | **33-37**| Octal PSRAM Bus | - | Dedicated PSRAM | **FORBIDDEN** | Bonded to 8MB PSRAM (DO NOT USE) |

---

## 3. Secondary Field Pole Controller Pinout (ESP32 Classic)

### Target Board: ESP32 ESP-WROOM-32 (30-pin / 38-pin DevKit)

| Pin Name | GPIO | Native Peripheral | Direction | Boot / Strapping Role | Verified Status | Purpose / Connected Hardware |
| :--- | :---: | :--- | :---: | :--- | :---: | :--- |
| **LORA_SCK** | **18** | VSPI SCK | OUTPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 LoRa SPI Clock (Native VSPI) |
| **LORA_MISO** | **19** | VSPI MISO | INPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 LoRa SPI MISO (Native VSPI) |
| **LORA_MOSI** | **23** | VSPI MOSI | OUTPUT | Normal GPIO | **VERIFIED SAFE** | SX1278 LoRa SPI MOSI (Native VSPI) |
| **LORA_NSS** | **5** | VSPI CS0 | OUTPUT | Outputs clock @ boot | **VERIFIED SAFE** | SX1278 LoRa CS (External 10k pull-up) |
| **LORA_RST** | **14** | General GPIO | OUTPUT | Safe output | **VERIFIED SAFE** | SX1278 LoRa Reset (Active LOW) |
| **LORA_DIO0** | **4** | General GPIO / IRQ | INPUT | Safe input | **VERIFIED SAFE** | SX1278 Packet Ready Interrupt |
| **RELAY_F0** | **26** | DAC2 / RTC_IO7 | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 1 / Main Street Light Relay |
| **RELAY_F1** | **25** | DAC1 / RTC_IO6 | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 2 Relay |
| **RELAY_F2** | **27** | RTC_IO17 | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 3 Relay |
| **RELAY_F3** | **16** | UART2 RX / GPIO | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 4 Relay (Replaced dangerous GPIO 12) |
| **RELAY_F4** | **17** | UART2 TX / GPIO | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 5 Relay (Replaced dangerous GPIO 15) |
| **RELAY_F5** | **32** | RTC_IO9 | OUTPUT | Safe non-strapping | **VERIFIED SAFE** | Feeder 6 Relay |
| **ADC1_CS** | **13** | General GPIO | OUTPUT | Safe output | **VERIFIED SAFE** | ADS131M08 #1 Chip Select (Feeder 1-3 Currents)|
| **ADC2_CS** | **22** | I2C SCL / GPIO | OUTPUT | Safe output | **VERIFIED SAFE** | ADS131M08 #2 Chip Select (Feeder 4-6 Currents)|
| **ADC3_CS** | **21** | I2C SDA / GPIO | OUTPUT | Safe output | **VERIFIED SAFE** | ADS131M08 #3 Chip Select (3-Phase Bus Voltages)|
| **ADC_SYNC** | **33** | RTC_IO8 | OUTPUT | Safe output | **VERIFIED SAFE** | Common ADS131M08 Simultaneous Sync Pulse |
| **ADC1_DRDY**| **34** | GPI (Input-Only) | INPUT | Input Only (No PU/PD)| **VERIFIED SAFE** | ADS131M08 #1 Data Ready Interrupt |
| **ADC2_DRDY**| **35** | GPI (Input-Only) | INPUT | Input Only (No PU/PD)| **VERIFIED SAFE** | ADS131M08 #2 Data Ready Interrupt |
| **ADC3_DRDY**| **36** | GPI / SENSOR_VP | INPUT | Input Only (No PU/PD)| **VERIFIED SAFE** | ADS131M08 #3 Data Ready Interrupt |
| **STATUS_LED**| **2** | Strapping / LED | OUTPUT | Strapping pin | **RESTRICTED** | Onboard Blue LED (Active HIGH) |
| **INTERNAL** | **6-11**| SPI Flash Bus | - | Dedicated Flash | **FORBIDDEN** | Bonded to 4MB Flash (DO NOT USE) |

---

## 4. Key Engineering Improvements over Legacy Code

1. **Elimination of GPIO 12 MTDI Failure Risk**:
   In the old design, Feeder 4 Relay was assigned to GPIO 12. If the optocoupler pulled GPIO 12 to VCC at boot, the flash memory voltage dropped to 1.8V, rendering the node permanently bricked until power-cycled. In System_R1, Feeder 4 is assigned to **GPIO 16**, completely resolving this silicon trap.
2. **Elimination of GPIO 15 Relay Chatter**:
   In the old design, Feeder 5 Relay was assigned to GPIO 15, which outputs high-speed serial boot messages at power-up. In System_R1, Feeder 5 is assigned to **GPIO 17**, ensuring static low/high stability at reset.
3. **Decoupling LoRa Interrupt from Boot Strapping (GPIO 2 to GPIO 4)**:
   GPIO 2 on classic ESP32 must remain LOW during boot to enter download mode. In the old design, SX1278 DIO0 was wired to GPIO 2. An incoming packet or floating line during boot could force GPIO 2 HIGH, preventing firmware uploads. In System_R1, LoRa DIO0 is remapped to **GPIO 4**.
4. **Utilization of Input-Only Pins for ADC Interrupts**:
   GPIO 34, 35, 36 (VP), and 39 (VN) cannot be configured as outputs. By mapping the three ADS131M08 Data Ready (`/DRDY`) signals to GPIO 34, 35, and 36, all general-purpose bidirectional GPIOs are preserved for relay and chip-select outputs.
