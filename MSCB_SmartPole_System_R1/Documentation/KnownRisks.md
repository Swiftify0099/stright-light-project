# Critical Hardware Risks & Engineering Mitigation Strategy

**Project**: MSCB Smart Pole & Feeder Automation System  
**Release**: System_R1 (Prompt 06 Modular Architecture)  
**Author**: Senior ESP32 Embedded Firmware Engineer & Arduino IDE Architect  
**Date**: November 2026  

---

## 1. Executive Safety Warning

This document outlines critical electrical, silicon, timing, and firmware hazards identified across the MSCB Smart Pole System. Every engineer working on this codebase must adhere strictly to these engineering mitigations before connecting physical field hardware or mains equipment.

---

## 2. Silicon & Strapping Pin Hazards

### Risk 1: Classic ESP32 GPIO 12 (MTDI) Boot Strapping Latch
- **Hazard Description**: GPIO 12 controls the internal LDO voltage regulator (`VDD_SDIO`) supplying the external SPI Flash chip. If GPIO 12 is sampled HIGH when the ESP32 resets, `VDD_SDIO` switches to 1.8V instead of the standard 3.3V. This causes flash memory read brownouts, trapping the ESP32 in an infinite reboot loop (`rst:0x10 (RTCWDT_RTC_RESET),boot:0x12 (SPI_FAST_FLASH_BOOT)`).
- **Previous Code Flaw**: In the old project, Feeder 4 Relay was assigned to GPIO 12. Most optocoupler-isolated relay boards feature active-low inputs with internal pull-ups tied to VCC, which pulls GPIO 12 HIGH at reset.
- **R1 Mitigation**: Feeder 4 Relay has been relocated to **GPIO 16**. GPIO 12 is left unconnected and excluded from all peripheral mappings.

### Risk 2: GPIO 15 (MTDO) & GPIO 5 Boot PWM Glitches
- **Hazard Description**: GPIO 15 outputs high-speed ROM boot debugging messages (115200 baud) during the first milliseconds of power-up. GPIO 5 outputs a high-frequency clock signal during boot strap evaluation. When connected directly to sensitive relay coils or contactor drivers, this signal chatter causes audible clicking and momentary contact closures.
- **Previous Code Flaw**: Feeder 5 Relay was assigned to GPIO 15.
- **R1 Mitigation**: Feeder 5 Relay is reassigned to **GPIO 17**. LoRa NSS on GPIO 5 is fitted with an external 10k pull-up resistor to 3.3V to ensure the LoRa transceiver stays de-selected throughout reset.

### Risk 3: ESP32-S3 Octal PSRAM & Flash Internal Bus (GPIO 26-37)
- **Hazard Description**: In the ESP32-S3-WROOM-1/2 N16R8 module, 16MB Flash and 8MB PSRAM utilize high-speed Octal SPI (OPI). GPIOs 26 through 32 are dedicated to SPI Flash, and GPIOs 33 through 37 are internally bonded to Octal PSRAM.
- **Hazard Consequence**: Attempting to set `pinMode()` or `digitalWrite()` on any pin between GPIO 26 and GPIO 37 will corrupt memory pointers or cause an immediate Kernel Panic (`Guru Meditation Error: Core 0 panic'ed (Cache disabled but cached memory region accessed)`).
- **R1 Mitigation**: Compile-time `static_assert` statements in `GwBoardPins.h` enforce a complete exclusion zone on GPIO 26-37.

---

## 3. Electrical & Signal Conditioning Hazards

### Risk 4: 5V Analog Sensor Outputs Connected to 3.3V ADC Inputs
- **Hazard Description**: The ACS712 current sensor and certain active ZMPT101B modules operate from a 5.0 VDC rail. The ACS712 quiescent zero-current output is $V_{CC}/2 = 2.5\text{ V}$, with bidirectional peak swings reaching 0.5V to 4.5V. The absolute maximum input voltage rating for ESP32 GPIOs and the ADS131M08 ADC analog inputs is:
  $$V_{IN(max)} = AVDD + 0.3\text{ V} \approx 3.6\text{ V}$$
  Applying a 5V signal directly to these pins will forward-bias the internal ESD protection diodes, inject parasitic substrate currents, and permanently destroy the analog front-end channels.
- **R1 Mitigation**:
  1. A strict policy is enacted: **NO 5V sensor outputs may be wired directly to ESP32 or ADS131M08 pins**.
  2. Signal conditioning stages MUST incorporate precision 0.1% resistor dividers, rail-to-rail operational amplifier buffers powered by 3.3V, and ultra-fast clamping Schottky diodes (e.g., BAT54S) tied to the 3.3V rail.

### Risk 5: Relay Coil Inductive Flyback & Ground Bounce
- **Hazard Description**: De-energizing relay coils produces large inductive back-EMF voltage spikes ($V = -L \cdot di/dt$) reaching several hundred volts. If ground loops exist between the relay coil power supply and the ESP32 digital logic, ground bounce can crash the processor or latch up the SPI bus.
- **R1 Mitigation**:
  1. All 6 relay channels must use optical isolation (opto-isolators) with the relay power supply (`JD-VCC`) strictly galvanically isolated from the ESP32 3.3V logic rail.
  2. Flyback freewheeling diodes must be positioned across every coil.
  3. Startup sequence drives all 6 relays to the de-energized state during the very first instruction cycle of `setup()`.

---

## 4. Multi-ADC & High-Speed SPI Contention Hazards

### Risk 6: Shared SPI Bus Conflict between SX1278 and 3x ADS131M08
- **Hazard Description**:
  - The SX1278 LoRa module operates in **SPI Mode 0** (CPOL=0, CPHA=0), with transactions typically bounded at 8-10 MHz.
  - The ADS131M08 delta-sigma ADC operates in **SPI Mode 1** (CPOL=0, CPHA=1), requiring continuous framing and strict chip-select timing.
  - If the LoRa transceiver transmits a long packet while the ADC is continuously streaming 24-bit samples, SPI bus contention will cause dropped samples or truncated RF frames.
- **R1 Mitigation**:
  1. Dedicated `/CS` lines are assigned for each device: `POLE_LORA_NSS_PIN (GPIO 5)`, `POLE_ADC1_CS_PIN (GPIO 13)`, `POLE_ADC2_CS_PIN (GPIO 22)`, and `POLE_ADC3_CS_PIN (GPIO 21)`.
  2. All SPI access is protected using explicit `SPI.beginTransaction(SPISettings(...))` and `SPI.endTransaction()` wrappers with FreeRTOS mutexes to guarantee mode switching integrity.
  3. ADS131M08 DRDY interrupts are assigned to input-only pins (`GPIO 34, 35, 36`).

### Risk 7: Delta-Sigma Clock Drift across 3 ADC Chips
- **Hazard Description**:
  The ADS131M08 utilizes an internal delta-sigma modulator running from an on-chip oscillator. If three independent ADS131M08 chips operate on independent internal oscillators, their sampling frequencies will drift slightly relative to each other (e.g. by 20 to 50 ppm). Over time, phase angle measurements between Feeder 1 (ADC #1) and Voltage (ADC #3) will become corrupted, leading to false power factor ($\cos\phi$) and reactive power readings.
- **R1 Mitigation**:
  1. In the final hardware design, a single master clock (CLKIN) must be distributed to all three ADS131M08 devices.
  2. A common hardware synchronization pin (`POLE_ADC_SYNC_PIN`, GPIO 33) is wired to the `/SYNC` input of all three ADCs so that all 24 conversion channels start their conversion cycles simultaneously.

---

## 5. High-Voltage Mains Safety Protocol

- **Hazard**: 3-Phase 415 VAC / 230 VAC mains power can cause fatal electric shock, arc flash, and equipment destruction.
- **Rules**:
  1. All initial testing in Prompt 06 and Prompt 07 must be performed exclusively under low-voltage DC bench supplies (3.3V and 5.0V).
  2. No mains voltages may be applied to the system until full isolation barriers, creepage clearances (> 6.3 mm), and certified enclosures have been formally approved.
