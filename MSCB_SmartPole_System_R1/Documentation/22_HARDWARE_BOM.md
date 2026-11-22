# 22 — MSCB Smart Pole Automation System: Final Hardware Bill of Materials (BOM)

**Document ID**: MSCB-DOC-22  
**Classification**: Procurement & Hardware Engineering Specification  
**System Capacity**: 1 Master Gateway + Up to 50 Field Pole Nodes (300 Feeders, 1,050 Sensor Channels)  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

> [!NOTE]
> **NO INVENTED PRICING POLICY**:
> In accordance with strict engineering standards, this Bill of Materials specifies verified component part numbers, manufacturers, electrical tolerances, and exact unit quantities across deployment tiers. Commercial prices are subject to regional supplier quotes and are deliberately omitted to prevent out-of-date cost estimations.

---

## 1. Master Gateway Subsystem Bill of Materials

| Item # | Subsystem Component | Exact Part Number / Description | Manufacturer | Package / Mounting | Qty | Hardware Status |
| :---: | :--- | :--- | :--- | :--- | :---: | :--- |
| **GW-01** | Microcontroller DevKit | DOIT ESP32-S3 Dual Type-C (N16R8) | Espressif Systems | 44-pin DevKit | 1 | **Verified Existing** |
| **GW-02** | RF Transceiver Module | SX1278 RA-02 433 MHz LoRa Module | Ai-Thinker / Semtech | 16-pin SMD / DIP | 1 | **Verified Existing** |
| **GW-03** | Sub-GHz Antenna | 433 MHz 3.5 dBi Whip Antenna (SMA) | Linx / Taoglas | SMA-Male Connector | 1 | **Verified Existing** |
| **GW-04** | RF Cable Assembly | RG-178 / RG-316 SMA-Female to IPEX | Amphenol RF | 150 mm pigtail | 1 | **Verified Existing** |
| **GW-05** | Auxiliary Power Supply | 5V / 2.0A Regulated SMPS Module | Mean Well (IRM-10-5) | PCB Mount / Enclosed| 1 | **Verified Existing** |
| **GW-06** | AC Input Surge Arrestor | 14D471K Metal Oxide Varistor (MOV) | TDK / Epcos | Radial Lead (14mm) | 1 | **Confirmed Missing** |
| **GW-07** | AC Input Line Fuse | 1.0A 250V Fast-Blow Glass Fuse | Littelfuse | 5x20 mm with Holder | 1 | **Confirmed Missing** |
| **GW-08** | Enclosure Assembly | Weatherproof ABS / Polycarbonate Box | Fibox / Schneider | IP65 Wall-Mount | 1 | **Confirmed Missing** |

---

## 2. Per-Pole Subsystem Bill of Materials (Unit Pole Basis)

| Item # | Subsystem Component | Exact Part Number / Description | Manufacturer | Package / Specification | Qty | Hardware Status |
| :---: | :--- | :--- | :--- | :--- | :---: | :--- |
| **PL-01** | Microcontroller Board | ESP32-WROOM-32 (30-pin or 38-pin DevKit)| Espressif Systems | Dual-Core LX6 @ 240MHz | 1 | **Verified Existing** |
| **PL-02** | RF Transceiver Module | SX1278 RA-02 433 MHz LoRa Module | Ai-Thinker / Semtech | 16-pin Module | 1 | **Verified Existing** |
| **PL-03** | Outdoor Sub-GHz Antenna | 433 MHz 1/4-Wave Omni Whip / Collinear | Linx Technologies | N-Type / SMA Outdoor | 1 | **Confirmed Missing** |
| **PL-04** | RF Surge Arrestor | In-line Gas Discharge Tube Arrestor | PolyPhaser / Huber | SMA Female to Female | 1 | **Optional / Rec.** |
| **PL-05** | Delta-Sigma Metrology ADC| TI ADS131M08 (24-bit 8-Channel ADC) | Texas Instruments | 32-pin WQFN / Breakout | 3 | **Confirmed Missing** |
| **PL-06** | Feeder Current Sensors | SCT-013-000 Split-Core CT (1000:1) | YHDC | Split-core (50A:50mA) | 18 | **Confirmed Missing** |
| **PL-07** | Bus Voltage Sensors | ZMPT101B Precision Voltage Transformer | Microtransformer Co. | 2mA:2mA (4kV Isolation)| 3 | **Verified Existing** |
| **PL-08** | Precision Burden Resistors| 12.0 $\Omega$ 0.1% 25ppm Metal Film | Vishay / TE Connect. | Through-hole / 1206 | 18 | **Confirmed Missing** |
| **PL-09** | Voltage Burden Resistors | 300 $\Omega$ 0.1% 25ppm Metal Film | Vishay / Yageo | Through-hole / 1206 | 3 | **Confirmed Missing** |
| **PL-10** | Pilot Relay Board | 6-Channel Opto-Isolated Relay Module | Songle / Generic | PC817 Isolated, 5V coil| 1 | **Verified Existing** |
| **PL-11** | Main Power Contactors | 3-Pole 25A AC-3 Power Contactor (230V) | Schneider / ABB / L&T | 3P + 1NO Aux Contact | 6 | **Confirmed Missing** |
| **PL-12** | Feeder Protection MCBs | 3-Pole 32A C-Curve Miniature Breaker | Schneider (Acti9) / L&T| With OF Aux Contact | 6 | **Confirmed Missing** |
| **PL-13** | Logic Power Supply (SMPS1)| 5VDC / 1.5A Isolated Medical/Industrial| Mean Well (IRM-10-5) | Enclosed PCB Mount | 1 | **Confirmed Missing** |
| **PL-14** | Coil Power Supply (SMPS2) | 5VDC / 2.5A Isolated Industrial SMPS | Mean Well (IRM-15-5) | Enclosed PCB Mount | 1 | **Confirmed Missing** |
| **PL-15** | Transient Clamping Diodes| BAT54S Dual Schottky Clamping Diode | Nexperia / ON Semi | SOT-23 (3.3V Rail clamp)| 21 | **Confirmed Missing** |
| **PL-16** | Weatherproof Enclosure | IP66 Powder-Coated Steel Pole Cabinet | Rittal / Schneider | 600 x 400 x 200 mm | 1 | **Confirmed Missing** |

---

## 3. Scaled Multi-Pole Procurement Matrix (1, 10, 20, 50 Poles)

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                            SCALED HARDWARE PROCUREMENT QUANTITIES                           │
├──────────────────────────────────────┬─────────────┬─────────────┬─────────────┬────────────┤
│ Subsystem Component / Module         │ 1-Pole Net  │ 10-Pole Net │ 20-Pole Net │ 50-Pole Net│
│                                      │ (1 GW + 1PL)│ (1 GW + 10PL│ (1 GW + 20PL│(1 GW + 50PL│
├──────────────────────────────────────┼─────────────┼─────────────┼─────────────┼────────────┤
│ Master Gateway (ESP32-S3 N16R8)      │ 1           │ 1           │ 1           │ 1          │
│ Field Pole Controllers (ESP32)       │ 1           │ 10          │ 20          │ 50         │
│ SX1278 LoRa Modules (433 MHz)        │ 2           │ 11          │ 21          │ 51         │
│ Sub-GHz Outdoor Antennas (433 MHz)   │ 2           │ 11          │ 21          │ 51         │
│ TI ADS131M08 24-bit Metrology ADCs   │ 3           │ 30          │ 60          │ 150        │
│ Split-Core Current Transformers (CT) │ 18          │ 180         │ 360         │ 900        │
│ ZMPT101B Voltage Transformers        │ 3           │ 30          │ 60          │ 150        │
│ 12.0 Ω 0.1% Precision Burden Resistors│ 18          │ 180         │ 360         │ 900        │
│ 300 Ω 0.1% Precision Burden Resistors │ 3           │ 30          │ 60          │ 150        │
│ 6-Channel Optocoupler Relay Boards   │ 1           │ 10          │ 20          │ 50         │
│ 3-Pole 25A Heavy Power Contactors    │ 6           │ 60          │ 120         │ 300        │
│ 3-Pole 32A MCB Breakers with Aux     │ 6           │ 60          │ 120         │ 300        │
│ Dual Isolated 5V SMPS Assemblies     │ 1 (2 units) │ 10 (20 unit)│ 20 (40 unit)│ 50(100 unit│
│ BAT54S Clamping Diode Pairs          │ 21          │ 210         │ 420         │ 1,050      │
│ IP66 Heavy Distribution Cabinets     │ 1           │ 10          │ 20          │ 50         │
├──────────────────────────────────────┴─────────────┴─────────────┴─────────────┴────────────┤
│ TOTAL CONTROLLED FEEDER CIRCUITS     │ 6 Feeders   │ 60 Feeders  │ 120 Feeders │ 300 Feeders│
│ TOTAL ELECTRICAL MEASUREMENT CHANNELS│ 21 Channels │ 210 Channels│ 420 Channels│1,050 Chann.│
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Hardware Discontinuation & Prohibited Component Warning

1. **Allegro ACS712-20A / 5A Hall Effect Sensors**:
   - **Status**: **REJECTED / PROHIBITED**.
   - **Reason**: The ACS712 outputs a 5.0V centered analog signal ($2.5\text{ V} \pm 2.0\text{ V}$ peak). Connecting its output to an ESP32 or ADS131M08 forward-biases ESD clamping diodes, causing substrate injection and destroying the analog front-end.
2. **Direct Mains Resistor Dividers**:
   - **Status**: **REJECTED / PROHIBITED**.
   - **Reason**: Violates galvanic isolation standards. Transformer-isolated ZMPT101B modules are mandatory.
