# 10 — MSCB Smart Pole Automation System: Sensor Metrology Calibration Manual

**Document ID**: MSCB-DOC-10  
**Classification**: Metrology Engineering & Quality Assurance  
**Target Hardware**: 18 Feeder Current CTs + 3 Bus Voltage Transformers per Pole  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation (Fifth-Pass Realigned)  

> [!CAUTION]
> **ELECTRICAL CALIBRATION SAFETY MANDATE**:
> Calibration procedures require injecting alternating current and hazardous AC mains voltages into analog signal conditioning boards. All calibration MUST be conducted on an electrically isolated test bench by certified metrology technicians using an isolated AC source. Never apply 230VAC directly to ADC pins. Never perform live mains calibration in the field without formal arc-flash PPE.

---

## 1. Calibration Standards & Separation of Instrumentation

To achieve verified metrology accuracy and prevent equipment conflation, calibration requires strict separation between the precision AC signal source and the reference measurement instrument:

| Equipment Role | Minimum Specification | Dedicated Instrument Model | Function & Calibration Standard |
| :--- | :--- | :--- | :--- |
| **Precision Signal Source** | 0–300 VAC, 0–60A AC, Pure Sine Wave (<0.05% THD) | **Fluke 6100A / 6105A Electrical Power Standard** | Generates ultra-stable voltage, current, and phase test signals. |
| **Reference Measurement Meter** | True-RMS AC Metrology Standard, Class 0.1 | **Yokogawa WT310E Precision Digital Power Meter** | Measures True-RMS V, I, P, and Phase Angle as the calibration standard. |
| **Reference Multimeter** | 6.5 Digit True-RMS DMM | Keysight 34461A / Fluke 8846A | Bench monitoring of DC offsets and reference rails. |
| **Isolation Transformer** | 230V / 230V 1:1 Ultra-Low Leakage Transformer | Topaz Ultra-Isolator 1.0 kVA | Galvanic isolation between mains grid and test bench. |

---

## 2. Canonical 21-Channel Hardware Mapping

All firmware (`PoleBoardPins.h`), telemetry packets, calibration registers, BOM, and documentation strictly use the following single canonical channel mapping:

| Canonical Ch | Signal Name | Transducer | Ratio | Burden | Full-Scale Nom | ADC Chip & Input |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Ch 01** | Common Bus Volt R | ZMPT101B VT | 1:1 (2mA) | $100\ \Omega$ | 230.0 VAC ($216.8\text{ mV}_{\text{pk}}$) | ADC #3 (AIN3P / AIN3N) |
| **Ch 02** | Common Bus Volt Y | ZMPT101B VT | 1:1 (2mA) | $100\ \Omega$ | 230.0 VAC ($216.8\text{ mV}_{\text{pk}}$) | ADC #3 (AIN4P / AIN4N) |
| **Ch 03** | Common Bus Volt B | ZMPT101B VT | 1:1 (2mA) | $100\ \Omega$ | 230.0 VAC ($216.8\text{ mV}_{\text{pk}}$) | ADC #3 (AIN5P / AIN5N) |
| **Ch 04** | Feeder 1 Current R | ZMCT103C CT | 1000:1 | $50.0\ \Omega$ | 8.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #1 (AIN0P / AIN0N) |
| **Ch 05** | Feeder 1 Current Y | ZMCT103C CT | 1000:1 | $50.0\ \Omega$ | 8.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #1 (AIN1P / AIN1N) |
| **Ch 06** | Feeder 1 Current B | ZMCT103C CT | 1000:1 | $50.0\ \Omega$ | 8.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #1 (AIN2P / AIN2N) |
| **Ch 07** | Feeder 2 Current R | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 20.0 A ($0.424\text{ V}_{\text{pk}}$) | ADC #1 (AIN3P / AIN3N) |
| **Ch 08** | Feeder 2 Current Y | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 20.0 A ($0.424\text{ V}_{\text{pk}}$) | ADC #1 (AIN4P / AIN4N) |
| **Ch 09** | Feeder 2 Current B | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 20.0 A ($0.424\text{ V}_{\text{pk}}$) | ADC #1 (AIN5P / AIN5N) |
| **Ch 10** | Feeder 3 Current R | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 20.0 A ($0.424\text{ V}_{\text{pk}}$) | ADC #1 (AIN6P / AIN6N) |
| **Ch 11** | Feeder 3 Current Y | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 20.0 A ($0.424\text{ V}_{\text{pk}}$) | ADC #1 (AIN7P / AIN7N) |
| **Ch 12** | Feeder 3 Current B | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 20.0 A ($0.424\text{ V}_{\text{pk}}$) | ADC #2 (AIN0P / AIN0N) |
| **Ch 13** | Feeder 4 Current R | SCT-013-000 CT | 2000:1 | $20.0\ \Omega$ | 40.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #2 (AIN1P / AIN1N) |
| **Ch 14** | Feeder 4 Current Y | SCT-013-000 CT | 2000:1 | $20.0\ \Omega$ | 40.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #2 (AIN2P / AIN2N) |
| **Ch 15** | Feeder 4 Current B | SCT-013-000 CT | 2000:1 | $20.0\ \Omega$ | 40.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #2 (AIN3P / AIN3N) |
| **Ch 16** | Feeder 5 Current R | SCT-013-000 CT | 2000:1 | $20.0\ \Omega$ | 40.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #2 (AIN4P / AIN4N) |
| **Ch 17** | Feeder 5 Current Y | SCT-013-000 CT | 2000:1 | $20.0\ \Omega$ | 40.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #2 (AIN5P / AIN5N) |
| **Ch 18** | Feeder 5 Current B | SCT-013-000 CT | 2000:1 | $20.0\ \Omega$ | 40.0 A ($0.566\text{ V}_{\text{pk}}$) | ADC #2 (AIN6P / AIN6N) |
| **Ch 19** | Feeder 6 Current R | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 25.0 A ($0.530\text{ V}_{\text{pk}}$) | ADC #3 (AIN0P / AIN0N) |
| **Ch 20** | Feeder 6 Current Y | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 25.0 A ($0.530\text{ V}_{\text{pk}}$) | ADC #3 (AIN1P / AIN1N) |
| **Ch 21** | Feeder 6 Current B | SCT-013-000 CT | 2000:1 | $30.0\ \Omega$ | 25.0 A ($0.530\text{ V}_{\text{pk}}$) | ADC #3 (AIN2P / AIN2N) |
| **Spare 1** | Spare Analog Ch 1 | Unused | N/A | AGND short | 0.0 V | ADC #2 (AIN7P / AIN7N) |
| **Spare 2** | Spare Analog Ch 2 | Unused | N/A | AGND short | 0.0 V | ADC #3 (AIN6P / AIN6N) |
| **Spare 3** | Spare Analog Ch 3 | Unused | N/A | AGND short | 0.0 V | ADC #3 (AIN7P / AIN7N) |

---

## 3. True-RMS Digital Signal Processing (DSP) Formulation

Calibration operates on the raw 24-bit differential conversion values acquired by the ADS131M08 ADCs at 4 kSPS:

### 3.1 Zero-Offset Calibration ($V_{\text{offset}}$)
With inputs terminated in zero input (primary circuit de-energized, $I = 0\text{ A}$):
$$V_{\text{offset}} = \frac{1}{N} \sum_{n=1}^{N} V_{\text{raw}}(n)$$
The measured offset is stored in NVS and subtracted point-by-point from subsequent samples.

### 3.2 True-RMS Calculation with Offset Correction
$$V_{\text{RMS}} = \sqrt{\frac{1}{N} \sum_{n=1}^{N} \left( V_{\text{raw}}(n) - V_{\text{offset}} \right)^2}$$

### 3.3 Gain Correction Factor ($K_{\text{gain}}$)
With a steady-state reference signal applied:
$$K_{\text{gain}} = \frac{\text{Reference Value (from Yokogawa)}}{\text{Uncalibrated RMS Measured Value}}$$
$$\text{Calibrated RMS} = K_{\text{gain}} \times \text{Uncalibrated RMS}$$

### 3.4 Phase Angle Compensation ($\Delta\theta$)
$$\Delta\theta = \theta_{\text{measured}} - \theta_{\text{ref}}$$
Phase displacement is stored in NVS to compensate for core phase shift across power factors ($\cos\phi = 1.0, 0.8, 0.5$).

---

## 4. Step-by-Step 21-Channel Multi-Point Calibration Protocol

### Phase 1: Zero-Current / Zero-Voltage Offset Nulling (All 21 Channels)
1. Ensure all 6 feeder contactors and incoming breakers are de-energized.
2. Connect ESP32 Field Pole to isolated 5V bench power supply.
3. Issue CLI command: `CALIB_ZERO`.
4. Accumulate 1,280 samples over 20 AC cycles (4 kSPS) across all channels.
5. Verify noise offset is below $\pm 50\ \mu\text{V}$. Offsets persist to NVS.

### Phase 2: Common Bus Voltage Multi-Point Calibration (Channels 01 to 03)
Connect Fluke 6100A AC voltage source across Phase R, Y, B voltage inputs, verified on Yokogawa WT310E:
1. **Point 1 (80% Nominal)**: Inject $180.0\text{ VAC RMS @ 50.00 Hz}$. Record linearity.
2. **Point 2 (100% Nominal)**: Inject $230.0\text{ VAC RMS @ 50.00 Hz}$. Issue command: `CALIB_VOLT 230.0 230.0 230.0`.
3. **Point 3 (113% High Line)**: Inject $260.0\text{ VAC RMS @ 50.00 Hz}$. Verify linearity error $\le \pm 0.5\%$.
4. **Point 4 (130% Max Line)**: Inject $300.0\text{ VAC RMS @ 50.00 Hz}$. Verify no clipping and core linearity.

### Phase 3: Feeder Current Multi-Point Calibration (Channels 04 to 21)
Thread primary conductor from Fluke 6100A current source through CTs, verified on Yokogawa WT310E:
1. **Feeder 1 (Ch 04–06, Street Light)**: Inject $0.4\text{ A}$ (5%), $0.8\text{ A}$ (10%), $4.0\text{ A}$ (50%), $8.0\text{ A}$ (100%), $10.0\text{ A}$ (125%). Issue: `CALIB_CURR 1 8.00 8.00 8.00`.
2. **Feeders 2 & 3 (Ch 07–12, Pump 1 & 2)**: Inject $1.0\text{ A}$ (5%), $2.0\text{ A}$ (10%), $10.0\text{ A}$ (50%), $20.0\text{ A}$ (100%). Issue: `CALIB_CURR 2 20.00 20.00 20.00` and `CALIB_CURR 3 20.00 20.00 20.00`.
3. **Feeders 4 & 5 (Ch 13–18, Heavy/Commercial)**: Inject $2.0\text{ A}$ (5%), $4.0\text{ A}$ (10%), $20.0\text{ A}$ (50%), $40.0\text{ A}$ (100%). Issue: `CALIB_CURR 4 40.00 40.00 40.00` and `CALIB_CURR 5 40.00 40.00 40.00`.
4. **Feeder 6 (Ch 19–21, Aux Feeder)**: Inject $1.25\text{ A}$ (5%), $2.5\text{ A}$ (10%), $12.5\text{ A}$ (50%), $25.0\text{ A}$ (100%). Issue: `CALIB_CURR 6 25.00 25.00 25.00`.

### Phase 4: Phase Angle Calibration
1. Inject $230.0\text{ V}$ and $10.0\text{ A}$ at $\cos\phi = 1.000$ ($\theta = 0^\circ$). Record phase error.
2. Adjust phase to $\cos\phi = 0.800$ lagging ($\theta = +36.87^\circ$).
3. Adjust phase to $\cos\phi = 0.500$ lagging ($\theta = +60.00^\circ$).
4. Compute and store phase delay calibration factors in NVS.

---

## 5. Master Calibration Record Sheet Template

Every field pole must have a signed physical and digital calibration sheet before deployment:

```text
=================================================================================
             MSCB SMART POLE SYSTEM — 21-CHANNEL CALIBRATION CERTIFICATE
=================================================================================
Pole Identifier      : POLE-_______________      Hardware MAC: __________________
Date of Calibration  : YYYY-MM-DD                Ambient Temp: _____ °C (25±2°C)
Calibrated By (Lead) : ________________________  Signature   : __________________
Signal Generator     : Fluke 6100A S/N: _______  Cal Due Date: __________________
Reference Analyzer   : Yokogawa WT310E S/N: ___  Cal Due Date: __________________
---------------------------------------------------------------------------------
Ch ID | Channel Name | Reference | Initial | Offset | Gain Coeff | Final | Status
---------------------------------------------------------------------------------
Ch 01 | Bus Volt R   | 230.0 VAC | _______ | ______ | __________ | _____ | [ ]PASS
Ch 02 | Bus Volt Y   | 230.0 VAC | _______ | ______ | __________ | _____ | [ ]PASS
Ch 03 | Bus Volt B   | 230.0 VAC | _______ | ______ | __________ | _____ | [ ]PASS
Ch 04 | F01 Curr R   |   8.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 05 | F01 Curr Y   |   8.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 06 | F01 Curr B   |   8.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 07 | F02 Curr R   |  20.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 08 | F02 Curr Y   |  20.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 09 | F02 Curr B   |  20.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 10 | F03 Curr R   |  20.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 11 | F03 Curr Y   |  20.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 12 | F03 Curr B   |  20.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 13 | F04 Curr R   |  40.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 14 | F04 Curr Y   |  40.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 15 | F04 Curr B   |  40.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 16 | F05 Curr R   |  40.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 17 | F05 Curr Y   |  40.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 18 | F05 Curr B   |  40.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 19 | F06 Curr R   |  25.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 20 | F06 Curr Y   |  25.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 21 | F06 Curr B   |  25.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
---------------------------------------------------------------------------------
Calibration Verdict  : [  ] PASSED & SEALED        [  ] REJECTED (DRIFT > 1.0%)
=================================================================================
```

> [!WARNING]
> **COMPLIANCE DISCLAIMER**:
> Class 1.0 (IEC 62053-21) accuracy CANNOT be claimed or declared for any unit prior to completion of the full physical multi-point calibration on certified bench equipment. Gate I is marked **NOT TESTED**.
