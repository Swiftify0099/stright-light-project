# MSCB Smart Pole & Feeder Automation System: Sensor Calibration & Metrology Report

**Document ID**: MSCB-QA-CAL-01  
**Classification**: Electrical Metrology & Calibration Verification  
**System Title**: MSCB Smart Pole & Feeder Automation System (System_R1 / V10)  
**Lead Metrology Engineer**: Principal Power Systems Instrumentation Specialist  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. 21-Channel Metrology Architecture Overview

Each Pole Node monitors:
1. **18 Independent Current Channels**:
   - 6 Feeders $\times$ 3 Phases ($R, Y, B$).
   - Current ranges: 0 - 50.0 A RMS (SCT-013-000 split-core CTs) or 0 - 20.0 A (ACS712-20A).
2. **3 Common Bus Voltage Channels**:
   - 3 Phases ($V_R, V_Y, V_B$) via ZMPT101B potential transformers.
   - Voltage range: 0 - 300 VAC RMS.

Total channels across 50 nodes: **900 Current Channels + 150 Voltage Channels = 1,050 Total Channels**.

---

## 2. Dynamic Calibration Engine & Formulas

### 2.1 Dynamic Zero-Crossing Offset Tracking
Temperature variations and op-amp drift cause ADC DC offsets to shift over time. The firmware dynamically calculates the DC offset across full integer AC cycles:
$$V_{offset} = \frac{1}{M \cdot N_{samples}} \sum_{k=1}^{M \cdot N_{samples}} v_{raw}[k]$$
This offset is subtracted from each instantaneous sample prior to squaring, eliminating spurious current readings when feeders are unpowered.

### 2.2 Independent Per-Channel Gain Calibration
Because analog sensor tolerances vary by $\pm 3\%$ (transformers, burden resistors, ADC input dividers), the system strictly rejects a global calibration factor. Each of the 21 channels maintains an independent calibration record in NVS:

```text
Calibration Record Structure (StorageTypes.h):
- uint16_t zeroOffset    : Dynamic zero-current ADC code
- float    gainFactor    : Engineering unit conversion scalar (A/code or V/code)
- float    phaseShiftDeg : Phase angle compensation (degrees)
- uint32_t calTimestamp  : Epoch timestamp of last calibration
- uint32_t checksumCRC32 : Record integrity protection
```

### 2.3 True-RMS & Real Active Power Formulation
True Root-Mean-Square (RMS) is computed over integer 50 Hz mains cycles (20 ms period):
$$I_{RMS} = \sqrt{\frac{1}{N} \sum_{k=1}^{N} \left(i_{inst}[k] - I_{offset}\right)^2} \times K_{gain,I}$$
$$V_{RMS} = \sqrt{\frac{1}{N} \sum_{k=1}^{N} \left(v_{inst}[k] - V_{offset}\right)^2} \times K_{gain,V}$$

Real Active Power ($P$) is computed from instantaneous sample products:
$$P = \frac{1}{N} \sum_{k=1}^{N} v_{inst}[k] \cdot i_{inst}[k]$$
Apparent Power ($S = V_{RMS} \times I_{RMS}$) and Power Factor ($\text{PF} = \frac{P}{S}$) are derived without assuming ideal sinusoidal waveforms.

---

## 3. Sensor Fault Detection & Health Diagnostics

The firmware continually validates ADC readings against physical boundaries:
1. **Disconnected Sensor / Open Circuit**:
   - If an ADC pin reads floating noise with high variance and zero 50 Hz fundamental frequency correlation, the channel state is marked `DISCONNECTED`.
2. **ADC Chip Communication Failure**:
   - If ADS131M08 registers fail to respond to SPI read commands or `/DRDY` interrupt stops firing, the entire ADC subsystem flags `ADC_SPI_FAULT`.
3. **Overcurrent Trip**:
   - If any phase current exceeds **32.0 A RMS** for $> 100$ ms, the feeder relay is automatically tripped to protect downstream wiring.
