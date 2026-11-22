# MSCB Smart Pole & Feeder Automation System: 21-Channel Metrology & Sensor Audit

**Document ID**: MSCB-AUDIT-MET-01  
**Classification**: Electrical Metrology, Analog Front-End (AFE) & Sensor Engineering Audit  
**Target System**: ESP32 Field Pole Node (3x Texas Instruments ADS131M08 24-Bit ADC Architecture)  
**Author**: Principal Metrology Engineer & Hardware Integration Specialist  
**Date**: November 2026  
**Status**: AUDITED & REMEDIATED  

---

## 1. Executive Summary & Critical Architectural Findings

### 1.1 The 21-Channel Metrology Architecture
Each MSCB Smart Pole controls and monitors **6 independent 3-phase feeders** powered from a common 3-phase 415V AC incoming utility bus. Full electrical visibility requires:
- **18 Independent Current Channels**: 6 feeders $\times$ 3 phases ($R, Y, B$)
- **3 Common Bus Voltage Channels**: 3 incoming bus phase-to-neutral voltages ($V_R, V_Y, V_B$)
- **Total Analog Measurement Channels**: **21 Channels**

To achieve high-precision polyphase metering, the Field Pole Node integrates **three Texas Instruments ADS131M08 8-channel simultaneous-sampling 24-bit delta-sigma ADCs** sharing a high-speed SPI bus with dedicated chip selects:
$$\text{Total Available Channels} = 3 \times 8 = 24 \text{ channels (21 active, 3 spare)}$$

### 1.2 Critical Sensor Electrical Incompatibility (BUG-R1-05)
Early prototype documentation and initial bill of materials (BOM Doc 22) listed:
- **Allegro ACS712-20A** Hall-effect current sensor breakout boards.
- **ZMPT101B** on-board op-amp voltage transformer breakout boards.

**CRITICAL METROLOGY AUDIT FINDING**:  
The **Allegro ACS712-20A is electrically and architecturally incompatible** with the Texas Instruments ADS131M08 ADC front end:

1. **Differential Input Range vs. Single-Ended Bias**:
   - The ADS131M08 features true differential analog inputs ($AINP - AINN$) with an internal 1.2V precision bandgap reference.
   - The maximum full-scale differential input voltage range is:
     $$V_{\text{FSR}} = \pm \frac{V_{\text{REF}}}{\text{Gain}} = \pm \frac{1.2\text{ V}}{1} = \pm 1.2\text{ V}$$
   - The ACS712-20A operates on a single 5.0V DC rail and outputs an analog signal biased at $V_{CC} / 2 = 2.5\text{ V DC}$ at zero current. At $\pm 20\text{ A}$, the output swings from $0.5\text{ V}$ to $4.5\text{ V}$ ($100\text{ mV/A}$ sensitivity).
   - If connected directly to the ADS131M08 single-ended input referenced to GND, the 2.5V DC quiescent bias **exceeds the 1.2V differential input range by 108%**, completely saturating the ADC digital modulator into hard clipping at positive full scale (`0x7FFFFF`).
2. **Absolute Maximum Ratings & Silicon Destruction Hazard**:
   - The ADS131M08 maximum analog input voltage rating is $AVDD + 0.3\text{ V} = 3.3\text{ V} + 0.3\text{ V} = 3.6\text{ V}$.
   - Under positive overload or fault surge conditions, the ACS712-20A outputs up to $4.8\text{ V}$, violating the absolute maximum rating of the ADC and causing parasitic substrate latch-up or permanent silicon gate destruction.
3. **Current Rating Inadequacy**:
   - The ACS712-20A has a continuous current rating of only 20A RMS. Commercial street lighting and municipal pole feeder circuits frequently encounter inrush surges and operating currents exceeding 32A to 63A per feeder.
   - Operating an ACS712-20A on 32A–63A feeders causes catastrophic thermal overheating and PCB trace fusion.

### 1.3 Remediated Analog Front-End (AFE) BOM
The sensor front end is fully remediated with passive, galvanically isolated current and voltage transformers matched to the ADS131M08 input range:

| Measurement Parameter | Defective Prototype Component | Remediated Production Component | Electrical Interface & Scaling |
| :--- | :--- | :--- | :--- |
| **Feeder Currents (Ch 1–18)** | ACS712-20A Hall Module | **YHDC SCT-013-000** (100A:50mA) or **ZMCT103C** (5A:2.5mA) Precision Micro CT | True differential input across a **12.0 $\Omega$ precision 0.1% metal-film burden resistor**. At 100A, $V_{\text{peak}} = 50\text{ mA} \times \sqrt{2} \times 12\text{ }\Omega = \pm 0.849\text{ V}$ ($\le 1.2\text{ V}$ FSR). |
| **Bus Voltages (Ch 19–21)** | Active ZMPT101B Op-Amp Board (Single-Ended 5V) | **Raw ZMPT101B Micro Precision Current-Type Voltage Transformer** (2mA:2mA) | Primary: 120 k$\Omega$ 2W metal-oxide current limiting resistor. Secondary: **300 $\Omega$ 0.1% burden resistor** connected differentially. At 250V RMS, $V_{\text{peak}} = \pm 0.884\text{ V}$ ($\le 1.2\text{ V}$ FSR). |

---

## 2. Canonical 21-Channel Hardware Pin & Register Mapping Matrix

The 21 physical signals are distributed across three ADS131M08 ICs to balance SPI bandwidth, thermal dissipation, and physical PCB trace routing.

```
       Incoming 3-Phase Bus (415V AC)
          │        │        │
         (R)      (Y)      (B)
          │        │        │
          ▼        ▼        ▼
       [Ch 19]  [Ch 20]  [Ch 21] ────────► ADS131M08 Chip #3 (Inputs 4-6)
       
  Feeder 1 (R,Y,B) ──► Ch 1, 2, 3   ──┐
  Feeder 2 (R,Y,B) ──► Ch 4, 5, 6   ──┼──► ADS131M08 Chip #1 (Inputs 0-7)
  Feeder 3 (R,Y,B) ──► Ch 7, 8      ──┘
  Feeder 3 (B)     ──► Ch 9         ──┐
  Feeder 4 (R,Y,B) ──► Ch 10, 11, 12──┼──► ADS131M08 Chip #2 (Inputs 0-7)
  Feeder 5 (R,Y,B) ──► Ch 13, 14, 15──┘
  Feeder 6 (R,Y,B) ──► Ch 16, 17, 18───► ADS131M08 Chip #3 (Inputs 0-2)
```

### 2.1 Complete Channel-to-Silicon Allocation Table

| Master Ch # | Target Peripheral | Subsystem / Feeder | Phase | Physical ADC Pin Pair | Sensor Type | Burden Resistor | Full-Scale Dynamic Range |
| :---: | :--- | :--- | :---: | :---: | :--- | :---: | :---: |
| **Ch 01** | **ADS131M08 #1** (CS: GPIO 13) | Feeder 1 Current | Phase R | `AIN0P / AIN0N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 02** | **ADS131M08 #1** (CS: GPIO 13) | Feeder 1 Current | Phase Y | `AIN1P / AIN1N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 03** | **ADS131M08 #1** (CS: GPIO 13) | Feeder 1 Current | Phase B | `AIN2P / AIN2N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 04** | **ADS131M08 #1** (CS: GPIO 13) | Feeder 2 Current | Phase R | `AIN3P / AIN3N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 05** | **ADS131M08 #1** (CS: GPIO 13) | Feeder 2 Current | Phase Y | `AIN4P / AIN4N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 06** | **ADS131M08 #1** (CS: GPIO 13) | Feeder 2 Current | Phase B | `AIN5P / AIN5N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 07** | **ADS131M08 #1** (CS: GPIO 13) | Feeder 3 Current | Phase R | `AIN6P / AIN6N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 08** | **ADS131M08 #1** (CS: GPIO 13) | Feeder 3 Current | Phase Y | `AIN7P / AIN7N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 09** | **ADS131M08 #2** (CS: GPIO 22) | Feeder 3 Current | Phase B | `AIN0P / AIN0N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 10** | **ADS131M08 #2** (CS: GPIO 22) | Feeder 4 Current | Phase R | `AIN1P / AIN1N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 11** | **ADS131M08 #2** (CS: GPIO 22) | Feeder 4 Current | Phase Y | `AIN2P / AIN2N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 12** | **ADS131M08 #2** (CS: GPIO 22) | Feeder 4 Current | Phase B | `AIN3P / AIN3N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 13** | **ADS131M08 #2** (CS: GPIO 22) | Feeder 5 Current | Phase R | `AIN4P / AIN4N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 14** | **ADS131M08 #2** (CS: GPIO 22) | Feeder 5 Current | Phase Y | `AIN5P / AIN5N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 15** | **ADS131M08 #2** (CS: GPIO 22) | Feeder 5 Current | Phase B | `AIN6P / AIN6N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 16** | **ADS131M08 #3** (CS: GPIO 21) | Feeder 6 Current | Phase R | `AIN0P / AIN0N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 17** | **ADS131M08 #3** (CS: GPIO 21) | Feeder 6 Current | Phase Y | `AIN1P / AIN1N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 18** | **ADS131M08 #3** (CS: GPIO 21) | Feeder 6 Current | Phase B | `AIN2P / AIN2N` | CT (100A:50mA) | 12.0 $\Omega$ 0.1% | 0 – 100.0 A RMS |
| **Ch 19** | **ADS131M08 #3** (CS: GPIO 21) | Common Bus Voltage| Phase R | `AIN4P / AIN4N` | VT (ZMPT101B 2mA) | 300.0 $\Omega$ 0.1%| 0 – 300.0 V RMS |
| **Ch 20** | **ADS131M08 #3** (CS: GPIO 21) | Common Bus Voltage| Phase Y | `AIN5P / AIN5N` | VT (ZMPT101B 2mA) | 300.0 $\Omega$ 0.1%| 0 – 300.0 V RMS |
| **Ch 21** | **ADS131M08 #3** (CS: GPIO 21) | Common Bus Voltage| Phase B | `AIN6P / AIN6N` | VT (ZMPT101B 2mA) | 300.0 $\Omega$ 0.1%| 0 – 300.0 V RMS |
| *Spare 1* | **ADS131M08 #2** (CS: GPIO 22) | Unused Spare | - | `AIN7P / AIN7N` | Tied to AGND via 10k | - | Reserved Expansion |
| *Spare 2* | **ADS131M08 #3** (CS: GPIO 21) | Unused Spare | - | `AIN3P / AIN3N` | Tied to AGND via 10k | - | Reserved Expansion |
| *Spare 3* | **ADS131M08 #3** (CS: GPIO 21) | Unused Spare | - | `AIN7P / AIN7N` | Tied to AGND via 10k | - | Reserved Expansion |

---

## 3. ADS131M08 Silicon Driver Configuration & SPI Timing

### 3.1 SPI Hardware Bus Configuration
The three ADS131M08 chips share the ESP32 VSPI peripheral:
- **SCK**: GPIO 18 (Bus Clock: 8.0 MHz, Mode 1: CPOL=0, CPHA=1)
- **MISO**: GPIO 19 (Master In Slave Out)
- **MOSI**: GPIO 23 (Master Out Slave In)
- **RESET**: GPIO 25 (Active-Low Hardware Reset, shared across all 3 chips)
- **DRDY**: GPIO 34 (Shared Data-Ready Interrupt, open-drain with 4.7 k$\Omega$ pull-up)
- **CS1, CS2, CS3**: GPIO 13, GPIO 22, GPIO 21 (Dedicated Active-Low Chip Selects)

### 3.2 Low-Level Register Configurations
Each ADS131M08 is initialized at boot with the following register values:

| Register Address | Register Name | Configured Hex | Operational Meaning |
| :---: | :--- | :---: | :--- |
| `0x01` | `CLOCK` | `0x000E` | High-Resolution Mode ($f_{\text{MOD}} = 4.096\text{ MHz}$), OSR = 1024, Sampling Rate $f_S = 4,000\text{ SPS}$ (80 samples/cycle at 50 Hz). |
| `0x02` | `MODE` | `0x0510` | 24-bit word resolution, Hamming code parity disabled, SPI CRC disabled for low bus latency. |
| `0x03` | `GAIN1` | `0x0000` | Pre-amplifier gain = 1 for Channels 0 to 3 ($\pm 1.2\text{ V}$ input range). |
| `0x04` | `GAIN2` | `0x0000` | Pre-amplifier gain = 1 for Channels 4 to 7 ($\pm 1.2\text{ V}$ input range). |
| `0x05` | `CFG` | `0x0600` | Internal voltage reference enabled ($1.2\text{ V}$ nominal), reference buffer powered. |

---

## 4. True-RMS Signal Processing & Zero-Simulation Mandate

### 4.1 True-RMS Mathematical Formulation
To eliminate waveform distortion errors caused by non-linear LED street light driver switching harmonics, the metrology engine uses digital True-RMS integration calculated over integer multiples of the 50 Hz fundamental power cycle (20 ms window):

$$I_{\text{RMS}} = \sqrt{\frac{1}{N} \sum_{n=1}^{N} \left( \left( S_I[n] - V_{\text{offset}} \right) \cdot K_{\text{cal}} \right)^2}$$

$$V_{\text{RMS}} = \sqrt{\frac{1}{N} \sum_{n=1}^{N} \left( \left( S_V[n] - V_{\text{offset}} \right) \cdot K_{\text{cal}} \right)^2}$$

Where:
- $N = 320$ samples (exactly 4 complete AC cycles at $f_S = 4,000\text{ SPS}$, $f_{\text{mains}} = 50\text{ Hz}$).
- $S[n]$ is the 24-bit two's complement integer signed conversion result.
- $V_{\text{offset}}$ is the measured digital DC zero-offset voltage.
- $K_{\text{cal}}$ is the independent channel calibration coefficient.

### 4.2 Strict Prohibition of Simulated Sensor Readings (Rule Zero-Simulation)
Under no circumstances may the firmware generate synthetic or fallback voltage/current values when hardware is absent or failing:

```cpp
// MANDATORY PRODUCTION ERROR HANDLING
enum MetrologyStatus : uint8_t {
  METROLOGY_OK = 0,
  METROLOGY_ERR_ADC_TIMEOUT = 1,
  METROLOGY_ERR_SENSOR_DISCONNECTED = 2,
  METROLOGY_ERR_CLIPPING_OVERLOAD = 3,
  METROLOGY_ERR_UNASSIGNED_OR_SPARE = 4
};

struct ChannelReading {
  float value;              // RMS value in Amperes or Volts
  MetrologyStatus status;   // Explicit operational health status
  uint32_t timestamp;       // Milliseconds
};
```

1. **Disconnected Sensor Detection**: If an input measures open circuit or residual noise below the noise floor threshold ($I_{\text{RMS}} < 0.05\text{ A}$ with zero AC phase correlation), status must report `METROLOGY_ERR_SENSOR_DISCONNECTED`.
2. **ADC Failure / SPI Timeout**: If `DRDY` does not assert within 500 $\mu$s, or SPI returns all `0x000000` or `0xFFFFFF`, status must report `METROLOGY_ERR_ADC_TIMEOUT` and value must be set to `0.0f` with an active alarm flag.
3. **Clipping / Overload**: If ADC samples reach `0x7FFFF0` (within 16 counts of positive full scale), status must report `METROLOGY_ERR_CLIPPING_OVERLOAD`.

---

## 5. Independent Per-Channel Calibration Specification

Every channel features independent non-volatile calibration parameters stored in NVS, protecting against analog component tolerances:

$$\text{Parameter Set} = \{ K_{\text{gain}}, V_{\text{offset}}, \Phi_{\text{phase\_corr}} \}$$

```cpp
struct ChannelCalibrationParams {
  float gainCorrection;       // Multiplicative gain factor (nominal 1.000000)
  int32_t zeroOffsetCounts;   // Additive DC zero count correction
  float phaseAngleDegrees;    // Phase shift compensation for CT lag (-2.5 deg to +2.5 deg)
  uint32_t calTimestamp;      // Unix timestamp of calibration event
  uint32_t calCrc32;          // Integrity checksum
};
```

### 5.1 Verification Status: Simulation vs. Physical Instruments
- **Software Driver Logic**: **VERIFIED PASS** (`test_prompt14.exe` passes 17/17 test vectors including RMS calculation, scaling, and fault reporting).
- **Physical Electrical Accuracy**: **NOT TESTED / PENDING REFERENCE INSTRUMENTATION**.
  - Accuracy classification under IEC 62053-21 (Class 1.0 or Class 0.5S) requires a calibrated Fluke 6100A Electrical Power Standard or MTE Calmet Three-Phase Reference Meter.
  - Until certified laboratory calibration is completed with real 415V/100A loads, physical measurement accuracy cannot be declared PASS.
