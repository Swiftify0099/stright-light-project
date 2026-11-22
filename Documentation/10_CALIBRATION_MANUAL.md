# 10 — MSCB Smart Pole Automation System: Sensor Metrology Calibration Manual

**Document ID**: MSCB-DOC-10  
**Classification**: Metrology Engineering & Quality Assurance  
**Target Hardware**: 18 Feeder Current CTs + 3 Bus Voltage Transformers per Pole  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

> [!CAUTION]
> **ELECTRICAL CALIBRATION SAFETY MANDATE**:
> Calibration procedures require injecting live alternating current and hazardous AC mains voltages into analog signal conditioning boards. All calibration MUST be conducted on an electrically isolated test bench by certified metrology technicians using an isolated AC source. Never perform live mains calibration in the field without formal arc-flash PPE.

---

## 1. Calibration Standards & Required Reference Instruments

To achieve Class 1.0 or Class 0.5 metrology accuracy, the following certified reference instruments must be utilized:

| Equipment Type | Minimum Specification | Recommended Model |
| :--- | :--- | :--- |
| **Reference Power Meter** | True-RMS AC Metrology Standard, Class 0.1 | Yokogawa WT310E / Fluke 1738 |
| **Precision AC Power Source**| 0-300 VAC, 0-60A AC, Pure Sine Wave (<0.2% THD)| Chroma 61500 Series AC Source |
| **Reference Multimeter** | 6.5 Digit True-RMS Multimeter | Keysight 34461A / Fluke 87V |
| **Isolation Transformer** | 230V / 230V 1:1 Ultra-Low Leakage Transformer | Topaz Ultra-Isolator 1.0 kVA |

---

## 2. True-RMS Digital Signal Processing (DSP) Formulation

Calibration operates on the raw 24-bit differential conversion values acquired by the ADS131M08 ADCs at 4 kSPS:

### 2.1 Zero-Offset Calibration ($V_{offset}$)
With inputs terminated in zero input (primary circuit de-energized, $I = 0\text{ A}$):
$$V_{offset} = \frac{1}{N} \sum_{n=1}^{N} V_{raw}(n)$$
The measured offset is stored in NVS and subtracted point-by-point from subsequent samples.

### 2.2 True-RMS Calculation with Offset Correction
$$V_{RMS} = \sqrt{\frac{1}{N} \sum_{n=1}^{N} \left( V_{raw}(n) - V_{offset} \right)^2}$$

### 2.3 Gain Correction Factor ($K_{gain}$)
With a steady-state reference signal applied:
$$K_{gain} = \frac{\text{Reference Value (from Yokogawa)}}{\text{Uncalibrated RMS Measured Value}}$$
$$\text{Calibrated RMS} = K_{gain} \times \text{Uncalibrated RMS}$$

---

## 3. Step-by-Step 21-Channel Calibration Protocol

### Phase 1: Zero-Current / Zero-Voltage Offset Nulling
1. Ensure all 6 feeder contactors are open and the main incoming 3-phase breaker is open.
2. Connect the ESP32 Field Pole to the isolated 5V bench power supply.
3. Access the Serial Calibration CLI at 115200 baud or dispatch the Telegram command: `/calib POLE-0001 ZERO`.
4. The system accumulates 1,280 samples over 20 AC cycles for each of the 24 channels and computes $V_{offset}$.
5. Confirm that raw noise offset remains below $\pm 50\text{ }\mu\text{V}$. The system automatically persists offsets to the `calib` NVS namespace.

### Phase 2: Common Bus Voltage Calibration (Channels 1 to 3)
1. Connect the isolated AC calibration source to Phase R, Y, B voltage inputs.
2. Set source voltage to exactly **230.0 VAC RMS @ 50.00 Hz**.
3. Verify the reference reading on the external power analyzer.
4. Issue command: `CALIB_VOLT <R_VOLT> <Y_VOLT> <B_VOLT>` (e.g. `CALIB_VOLT 230.0 230.0 230.0`).
5. Firmware computes individual gain multipliers:
   $$K_{gain\_VR} = \frac{230.0}{V_{meas\_R}}, \quad K_{gain\_VY} = \frac{230.0}{V_{meas\_Y}}, \quad K_{gain\_VB} = \frac{230.0}{V_{meas\_B}}$$
6. Adjust source to 180 VAC and 260 VAC to verify linearity across the entire operating band ($\pm 0.5\%$ linearity error).

### Phase 3: Feeder Current CT Calibration (Channels 4 to 21)
1. Thread the primary conductor of the reference current source through the current transformers of Feeder 1 (R, Y, B phases).
2. Inject a steady pure sinusoidal current of **10.00 A RMS @ 50 Hz**.
3. Allow thermal stabilization for 60 seconds.
4. Issue command: `CALIB_CURR <FEEDER_NUM> <IR_REF> <IY_REF> <IB_REF>` (e.g. `CALIB_CURR 1 10.00 10.00 10.00`).
5. Firmware calculates and stores the phase gain coefficients in NVS.
6. Repeat sequentially for Feeders 2 through 6.

---

## 4. Master Calibration Record Sheet Template

Every field pole must have a signed physical and digital calibration sheet before deployment:

```text
=================================================================================
             MSCB SMART POLE SYSTEM — 21-CHANNEL CALIBRATION CERTIFICATE
=================================================================================
Pole Identifier      : POLE-_______________      Hardware MAC: __________________
Date of Calibration  : YYYY-MM-DD                Ambient Temp: _____ °C (25±2°C)
Calibrated By (Lead) : ________________________  Signature   : __________________
Reference Instrument : Yokogawa WT310E S/N: _____ Cal Due Date: _________________
---------------------------------------------------------------------------------
Ch ID | Channel Name | Reference | Initial | Offset | Gain Coeff | Final | Status
---------------------------------------------------------------------------------
Ch 01 | Bus Volt R   | 230.0 VAC | _______ | ______ | __________ | _____ | [ ]PASS
Ch 02 | Bus Volt Y   | 230.0 VAC | _______ | ______ | __________ | _____ | [ ]PASS
Ch 03 | Bus Volt B   | 230.0 VAC | _______ | ______ | __________ | _____ | [ ]PASS
Ch 04 | F01 Curr R   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 05 | F01 Curr Y   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 06 | F01 Curr B   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 07 | F02 Curr R   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 08 | F02 Curr Y   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 09 | F02 Curr B   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 10 | F03 Curr R   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 11 | F03 Curr Y   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 12 | F03 Curr B   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 13 | F04 Curr R   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 14 | F04 Curr Y   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 15 | F04 Curr B   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 16 | F05 Curr R   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 17 | F05 Curr Y   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 18 | F05 Curr B   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 19 | F06 Curr R   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 20 | F06 Curr Y   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
Ch 21 | F06 Curr B   |  10.00 A  | _______ | ______ | __________ | _____ | [ ]PASS
---------------------------------------------------------------------------------
Calibration Verdict  : [  ] PASSED & SEALED        [  ] REJECTED (DRIFT > 1.0%)
=================================================================================
```
