# 09 — MSCB Smart Pole Automation System: Sensor Channel & ADC Hardware Mapping

**Document ID**: MSCB-DOC-09  
**Classification**: Electrical Metrology & ADC Hardware Mapping  
**Metrology Architecture**: 3x Texas Instruments ADS131M08 (24-bit $\Delta\Sigma$)  
**Channels per Pole**: 21 Active Channels (18 Current + 3 Voltage)  
**Total System Channels**: 1,050 Channels across 50 Poles  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Single Pole 21-Channel Authoritative Mapping

Each Field Pole Node hosts three 8-channel ADS131M08 delta-sigma ADCs on a shared SPI bus, providing 24 physical differential inputs. 21 channels are actively assigned to electrical metrology, with 3 uncommitted spare channels.

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                              21-CHANNEL AUTHORITATIVE SENSOR MAPPING TABLE                                   │
├────┬─────────┬─────────┬───────┬──────────────┬─────────────┬──────┬─────────────┬──────────┬───────────────┤
│ChID│Channel  │ADC      │ADC In │Sensor Model  │Range        │Unit  │Nominal Gain │Firmware  │Telemetry      │
│    │Name     │Device   │Channel│              │             │      │/ Calibration│Variable  │Field Name     │
├────┼─────────┼─────────┼───────┼──────────────┼─────────────┼──────┼─────────────┼──────────┼───────────────┤
│01  │Bus Volt │ADC #3   │AIN3P/N│ZMPT101B Xfmr │0 - 300 VAC  │VAC   │400.0 V/V    │vrmsR     │busVoltageR    │
│    │Phase R  │(CS:G21) │Ch 3   │              │             │      │             │          │               │
│02  │Bus Volt │ADC #3   │AIN4P/N│ZMPT101B Xfmr │0 - 300 VAC  │VAC   │400.0 V/V    │vrmsY     │busVoltageY    │
│    │Phase Y  │(CS:G21) │Ch 4   │              │             │      │             │          │               │
│03  │Bus Volt │ADC #3   │AIN5P/N│ZMPT101B Xfmr │0 - 300 VAC  │VAC   │400.0 V/V    │vrmsB     │busVoltageB    │
│    │Phase B  │(CS:G21) │Ch 5   │              │             │      │             │          │               │
├────┼─────────┼─────────┼───────┼──────────────┼─────────────┼──────┼─────────────┼──────────┼───────────────┤
│04  │F01 Curr │ADC #1   │AIN0P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[0][0│feederCurrent  │
│    │Phase R  │(CS:G13) │Ch 0   │              │             │      │             │]         │[0][0]         │
│05  │F01 Curr │ADC #1   │AIN1P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[0][1│feederCurrent  │
│    │Phase Y  │(CS:G13) │Ch 1   │              │             │      │             │]         │[0][1]         │
│06  │F01 Curr │ADC #1   │AIN2P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[0][2│feederCurrent  │
│    │Phase B  │(CS:G13) │Ch 2   │              │             │      │             │]         │[0][2]         │
├────┼─────────┼─────────┼───────┼──────────────┼─────────────┼──────┼─────────────┼──────────┼───────────────┤
│07  │F02 Curr │ADC #1   │AIN3P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[1][0│feederCurrent  │
│    │Phase R  │(CS:G13) │Ch 3   │              │             │      │             │]         │[1][0]         │
│08  │F02 Curr │ADC #1   │AIN4P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[1][1│feederCurrent  │
│    │Phase Y  │(CS:G13) │Ch 4   │              │             │      │             │]         │[1][1]         │
│09  │F02 Curr │ADC #1   │AIN5P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[1][2│feederCurrent  │
│    │Phase B  │(CS:G13) │Ch 5   │              │             │      │             │]         │[1][2]         │
├────┼─────────┼─────────┼───────┼──────────────┼─────────────┼──────┼─────────────┼──────────┼───────────────┤
│10  │F03 Curr │ADC #1   │AIN6P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[2][0│feederCurrent  │
│    │Phase R  │(CS:G13) │Ch 6   │              │             │      │             │]         │[2][0]         │
│11  │F03 Curr │ADC #1   │AIN7P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[2][1│feederCurrent  │
│    │Phase Y  │(CS:G13) │Ch 7   │              │             │      │             │]         │[2][1]         │
│12  │F03 Curr │ADC #2   │AIN0P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[2][2│feederCurrent  │
│    │Phase B  │(CS:G22) │Ch 0   │              │             │      │             │]         │[2][2]         │
├────┼─────────┼─────────┼───────┼──────────────┼─────────────┼──────┼─────────────┼──────────┼───────────────┤
│13  │F04 Curr │ADC #2   │AIN1P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[3][0│feederCurrent  │
│    │Phase R  │(CS:G22) │Ch 1   │              │             │      │             │]         │[3][0]         │
│14  │F04 Curr │ADC #2   │AIN2P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[3][1│feederCurrent  │
│    │Phase Y  │(CS:G22) │Ch 2   │              │             │      │             │]         │[3][1]         │
│15  │F04 Curr │ADC #2   │AIN3P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[3][2│feederCurrent  │
│    │Phase B  │(CS:G22) │Ch 3   │              │             │      │             │]         │[3][2]         │
├────┼─────────┼─────────┼───────┼──────────────┼─────────────┼──────┼─────────────┼──────────┼───────────────┤
│16  │F05 Curr │ADC #2   │AIN4P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[4][0│feederCurrent  │
│    │Phase R  │(CS:G22) │Ch 4   │              │             │      │             │]         │[4][0]         │
│17  │F05 Curr │ADC #2   │AIN5P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[4][1│feederCurrent  │
│    │Phase Y  │(CS:G22) │Ch 5   │              │             │      │             │]         │[4][1]         │
│18  │F05 Curr │ADC #2   │AIN6P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[4][2│feederCurrent  │
│    │Phase B  │(CS:G22) │Ch 6   │              │             │      │             │]         │[4][2]         │
├────┼─────────┼─────────┼───────┼──────────────┼─────────────┼──────┼─────────────┼──────────┼───────────────┤
│19  │F06 Curr │ADC #3   │AIN0P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[5][0│feederCurrent  │
│    │Phase R  │(CS:G21) │Ch 0   │              │             │      │             │]         │[5][0]         │
│20  │F06 Curr │ADC #3   │AIN1P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[5][1│feederCurrent  │
│    │Phase Y  │(CS:G21) │Ch 1   │              │             │      │             │]         │[5][1]         │
│21  │F06 Curr │ADC #3   │AIN2P/N│SCT-013 (CT)  │0 - 50.0 A   │A RMS │83.333 A/V   │instI[5][2│feederCurrent  │
│    │Phase B  │(CS:G21) │Ch 2   │              │             │      │             │]         │[5][2]         │
├────┼─────────┼─────────┼───────┼──────────────┼─────────────┼──────┼─────────────┼──────────┼───────────────┤
│--  │Spare 1  │ADC #2   │AIN7P/N│Unconnected   │--           │--    │--           │--        │--             │
│--  │Spare 2  │ADC #3   │AIN6P/N│Unconnected   │--           │--    │--           │--        │--             │
│--  │Spare 3  │ADC #3   │AIN7P/N│Unconnected   │--           │--    │--           │--        │--             │
└────┴─────────┴─────────┴───────┴──────────────┴─────────────┴──────┴─────────────┴──────────┴───────────────┘
```

---

## 2. Master 50-Pole Network Mapping Architecture (1,050 Channels)

Across the entire 50-node network, channels are referenced globally using structured hierarchical addressing:

$$\text{Global Channel ID} = (\text{Pole\_Number} - 1) \times 21 + \text{Local\_Channel\_ID}$$

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                   50-POLE NETWORK SENSOR EXPANSION MATRIX                   │
├───────┬────────────┬─────────────────────────────┬──────────────────────────┤
│ Pole  │ Pole ID    │ 3 Bus Voltage Channels      │ 18 Feeder Current Chs    │
├───────┼────────────┼─────────────────────────────┼──────────────────────────┤
│ Pole 1│ POLE-0001  │ Ch 0001 (R), 0002(Y), 0003(B)│ Ch 0004 to Ch 0021       │
│ Pole 2│ POLE-0002  │ Ch 0022 (R), 0023(Y), 0024(B)│ Ch 0025 to Ch 0042       │
│ Pole 3│ POLE-0003  │ Ch 0043 (R), 0044(Y), 0045(B)│ Ch 0046 to Ch 0063       │
│ ...   │ ...        │ ...                         │ ...                      │
│ Pole 49POLE-0049   │ Ch 1009 (R), 1010(Y), 1011(B)│ Ch 1012 to Ch 1029       │
│ Pole 50POLE-0050   │ Ch 1030 (R), 1031(Y), 1032(B)│ Ch 1033 to Ch 1050       │
├───────┴────────────┼─────────────────────────────┼──────────────────────────┤
│ TOTALS             │ 150 Bus Voltage Channels    │ 900 Feeder Current Chs   │
│                    │ (50 Poles x 3 Phases)       │ (50 Poles x 18 Phases)   │
│                    ├─────────────────────────────┴──────────────────────────┤
│                    │ GRAND TOTAL: 1,050 CONTINUOUS SENSOR CHANNELS          │
└────────────────────┴────────────────────────────────────────────────────────┘
```

---

## 3. Physical Signal Conditioning & Burden Calculations

### 3.1 Current Transformer (CT) Burden & Gain Calculation
- **Sensor Ratio**: 1000:1 (50A primary yields 50mA secondary RMS, or 70.7mA peak).
- **Burden Resistor ($R_B$)**: Selected as **12.0 $\Omega$ (0.1% tolerance, 25 ppm/°C)**.
- **Peak Voltage Drop across Burden**:
  $$V_{peak} = I_{peak} \times R_B = 0.0707\text{ A} \times 12.0\text{ }\Omega = \mathbf{0.848\text{ V}}$$
- This fits within the ADS131M08 True Differential input linear range of $\mathbf{\pm 1.2\text{ V}}$ with gain $G = 1$, providing a $41\%$ safety margin against line overcurrent peaks.
- **Conversion Factor**:
  $$K_I = \frac{50\text{ A}}{0.600\text{ V RMS}} = \mathbf{83.333\text{ A/V}}$$

### 3.2 Voltage Transformer (ZMPT101B) Scaling Calculation
- **Nominal Grid Voltage**: 230 VAC RMS (Peak $V_{pk} = 230 \times \sqrt{2} = 325.27\text{ V}$).
- **Primary Limiting Resistor**: 120 k$\Omega$ high-voltage metal film resistor ($I_{prim} = 1.91\text{ mA}$).
- **ZMPT101B Ratio**: 1:1 current-type precision transformer ($I_{sec} = 1.91\text{ mA}$).
- **Secondary Burden Resistor**: 300 $\Omega$ precision resistor ($0.1\%$).
- **Secondary Output Voltage**:
  $$V_{sec\_RMS} = 1.91\text{ mA} \times 300\text{ }\Omega = 0.575\text{ V RMS} \quad (V_{pk} = 0.813\text{ V})$$
- **Conversion Factor**:
  $$K_V = \frac{230.0\text{ VAC}}{0.575\text{ V}} = \mathbf{400.0\text{ V/V}}$$
- Fits comfortably within the ADS131M08 $\pm 1.2\text{ V}$ differential input span.
