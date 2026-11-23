# MSCB Smart Pole & Feeder Automation System: 21-Channel Metrology & Sensor Audit

**Document ID**: MSCB-AUDIT-MET-05 (Fifth-Pass Comprehensive Independent Audit)  
**Classification**: Electrical Metrology, Analog Front-End (AFE) & Sensor Engineering Audit  
**Target System**: ESP32 Field Pole Node (3x Texas Instruments ADS131M08 24-Bit ADC Architecture)  
**Author**: Principal Analog Design Engineer, Embedded Security Engineer, LoRa Network Engineer, and Independent Verification Lead  
**Date**: November 22, 2026  
**Status**: AUDITED & REMEDIATED / PHYSICAL AFE HARDWARE STRICTLY BLOCKED / CT BOM WITHHELD  

---

## 1. Executive Summary & Critical Architectural Realignment

### 1.1 The 21-Channel Active Metrology Architecture
Each MSCB Smart Pole controls and monitors **6 independent 3-phase feeders** powered from a common 3-phase 415V AC incoming utility bus:
- **18 Independent Current Channels**: 6 feeders $\times$ 3 phases ($R, Y, B$)
- **3 Common Bus Voltage Channels**: 3 incoming bus phase-to-neutral voltages ($V_R, V_Y, V_B$)
- **Total Active Analog Measurement Channels**: **21 Channels**

Polyphase metering is performed by **three Texas Instruments ADS131M08 8-channel simultaneous-sampling 24-bit delta-sigma ADCs** sharing a high-speed SPI bus with dedicated chip selects:
$$\text{Physical ADC Capacity} = 3 \times 8 = 24 \text{ physical differential channels (21 active, 3 spare)}$$

> [!IMPORTANT]
> **STRICT TERMINOLOGY REALIGNMENT**:
> The system possesses **21 active sensor measurement channels** and **3 spare ADC channels**, yielding a **24 physical ADC channel input capacity**. Earlier claims of "24 independent sensor channels" are hereby formally retracted.

### 1.2 Safe Termination and Diagnostic Monitoring for Spare Inputs
The 3 unused ADC channels are:
1. **ADC #2 Channel 7** (AIN7P / AIN7N)
2. **ADC #3 Channel 6** (AIN6P / AIN6N)
3. **ADC #3 Channel 7** (AIN7P / AIN7N)

To prevent CMOS floating-gate oscillation, substrate current injection, and digital crosstalk into active metrology channels:
1. **Hardware Termination**: Both differential inputs (AINP and AINN) are shorted together and connected directly to AGND through $100\ \Omega$ balancing resistors.
2. **Software MUX Configuration**: During ADC initialization, the `CHx_CFG` register MUX bits are programmed to `001` (internal input shorted to mid-supply/AGND for noise and offset measurement).
3. **Diagnostic Monitoring**: During boot and background health checks, firmware periodically samples these spare channels to verify that input noise remains within expected Gaussian thermal limits ($\le \pm 50\ \mu\text{V}$). Any excessive deviation triggers an automated diagnostic warning (`ADC_DIAG_CROSSTALK_WARNING`).

---

## 2. Sensor Selection Matrix & Clamp Conflict Analysis (All 18 Current Channels)

### 2.1 Refutation of Blanket 10A CT & Exposure of Stage-1 Clamp Conflict
In prior drafts, the 10A ZMCT103C was approved for all 18 channels, which risked core saturation and burning on heavy distribution feeders carrying $16\text{ A} - 50\text{ A}$. The Fourth-Pass report substituted YHDC SCT-013-000 ($2000:1$) for heavy feeders but introduced a **critical circuit conflict**: it specified an antiparallel diode clamp across the burden limiting differential excursion to $\pm 0.70\text{ V}$, while simultaneously selecting burden resistors that produce up to $0.8591\text{ V}_{\text{peak}}$!

### 2.2 Exhaustive 18-Channel Current & Burden Analysis (Canonical Channels 04–21)

Canonical Channel Mapping Rule:
- **Channels 01–03**: Common Bus Voltage (Phases R, Y, B) via ZMPT101B Voltage Transformers
- **Channels 04–21**: Feeder Currents (Feeders 1 to 6, Phases R, Y, B) via Current Transformers

Evaluated using exact CT turns ratio, maximum specified primary current, burden resistance tolerance ($\pm 1.0\%$), and silicon diode conduction curves:

| Canonical Ch # | Feeder & Phase | Feeder Application | Design Current | Approved CT Model | Turns Ratio | Burden Resistor | Peak Secondary Current | Peak Burden Voltage (Nom / $+1\%$) | Stage 1 Clamp ($\pm 0.70\text{V}$) | Conduction & Waveform Distortion |
| :---: | :--- | :--- | :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Ch 04–06** | Feeder 1 (R, Y, B) | Municipal Street Lights | $0 - 8\text{ A RMS}$ | **ZMCT103C** | $1000:1$ | $50.0\ \Omega$ | $11.31\text{ mA}_{\text{pk}}$ | $0.5657\text{ V} / 0.5713\text{ V}$ | Partial Conduction ($+55^\circ\text{C}$) | Non-linear temp drift |
| **Ch 04–06\***| Feeder 1 (10A max) | Municipal Street Lights | $10\text{ A RMS}$ | **ZMCT103C** | $1000:1$ | $50.0\ \Omega$ | $14.14\text{ mA}_{\text{pk}}$ | $0.7071\text{ V} / 0.7142\text{ V}$ | **CLIPPED** ($> 0.70\text{V}$) | **SEVERE CLIPPING** |
| **Ch 07–09** | Feeder 2 (R, Y, B) | Agricultural Pump 1 | $5 - 28\text{ A RMS}$ | **SCT-013-000** | $2000:1$ | $30.0\ \Omega$ | $19.80\text{ mA}_{\text{pk}}$ | $0.5940\text{ V} / 0.5999\text{ V}$ | Partial Conduction ($+55^\circ\text{C}$) | Non-linear temp drift |
| **Ch 10–12** | Feeder 3 (R, Y, B) | Agricultural Pump 2 | $5 - 28\text{ A RMS}$ | **SCT-013-000** | $2000:1$ | $30.0\ \Omega$ | $19.80\text{ mA}_{\text{pk}}$ | $0.5940\text{ V} / 0.5999\text{ V}$ | Partial Conduction ($+55^\circ\text{C}$) | Non-linear temp drift |
| **Ch 13–15** | Feeder 4 (R, Y, B) | Heavy Distribution Feeder | $10 - 45\text{ A RMS}$ | **SCT-013-000** | $2000:1$ | $27.0\ \Omega$ | $31.82\text{ mA}_{\text{pk}}$ | **$0.8591\text{ V} / 0.8677\text{ V}$** | **CLIPPED** ($> 0.70\text{V}$) | **SEVERE CLIPPING** |
| **Ch 16–18** | Feeder 5 (R, Y, B) | Commercial Feeder | $10 - 45\text{ A RMS}$ | **SCT-013-000** | $2000:1$ | $27.0\ \Omega$ | $31.82\text{ mA}_{\text{pk}}$ | **$0.8591\text{ V} / 0.8677\text{ V}$** | **CLIPPED** ($> 0.70\text{V}$) | **SEVERE CLIPPING** |
| **Ch 19–21** | Feeder 6 (R, Y, B) | Auxiliary / Spare Feeder | $0 - 30\text{ A RMS}$ | **SCT-013-000** | $2000:1$ | $30.0\ \Omega$ | $21.21\text{ mA}_{\text{pk}}$ | $0.6364\text{ V} / 0.6428\text{ V}$ | Partial Conduction ($+55^\circ\text{C}$) | Non-linear temp drift |

### 2.3 Identification of Distortion & Severe Clipping
1. **Feeders 4 & 5 (Channels 13–18)**: Under normal 45A full load, peak burden voltage reaches **$0.8591\text{ V}$** ($0.8677\text{ V}$ at $+1\%$ tolerance). Because this exceeds the $\pm 0.70\text{ V}$ diode threshold, the clamp conducts heavily on every AC cycle. The waveform is flattened at the peaks, introducing massive 3rd and 5th harmonic distortion and making True-RMS metrology impossible.
2. **Feeder 1 at 10A Overload (Channels 04–06)**: Produces $0.7071\text{ V}_{\text{peak}}$, also exceeding the $0.70\text{ V}$ clamp.
3. **Feeders 2, 3, 6 (Channels 07–12, 19–21)**: Peak burden voltages ($0.594\text{ V} - 0.636\text{ V}$) operate in the diode's exponential conduction knee. Inside an outdoor IP66 pole enclosure at $+55^\circ\text{C}$, the diode forward knee drops by $-2\text{ mV}/^\circ\text{C}$ ($\Delta V = -60\text{ mV}$), shifting the conduction knee down to $\sim 0.49\text{ V}$. Significant forward leakage current ($>100\ \mu\text{A}$) shunts the burden resistor, causing severe temperature-dependent calibration drift.

### 2.4 Nexperia BAV199 Architecture & Resolution of FSR Contradiction
To protect against open-circuit CT secondary hazards without clipping normal AC waveforms:
1. **Nexperia BAV199 Internal Topology**:
   - Package: SOT-23 / TO-236AB (Rev 8).
   - Pinout: Pin 1 = Anode Diode 1 ($A_1$), Pin 2 = Cathode Diode 2 ($K_2$), Pin 3 = Center tap ($K_1/A_2$).
   - Internal Topology: Two diodes connected in **SERIES** inside a single package between Pin 1 and Pin 2.
2. **Package Count, Pin Connections & Reverse Orientation**:
   - Because a single BAV199 package conducts only in one forward direction (Pin 1 to Pin 2), bidirectional clamping across differential burden terminals ($AINP, AINM$) **REQUIRES TWO PACKAGES PER CHANNEL**:
     - **Package A (Forward Clamp)**: Pin 1 $\rightarrow AINP$, Pin 2 $\rightarrow AINM$ (Pin 3 NC).
     - **Package B (Reverse Clamp)**: Pin 1 $\rightarrow AINM$, Pin 2 $\rightarrow AINP$ (Pin 3 NC).
   - **Board Package Count**:
     - 18 Current Channels $\times 2 = \mathbf{36\text{ packages}}$ (72 diodes).
     - 3 Voltage Channels (if secondary clamped) $\times 2 = \mathbf{6\text{ packages}}$ (12 diodes).
     - Total Active Channels (21 channels): $\mathbf{42\text{ packages}}$ (84 discrete diodes).
3. **Guaranteed Clamp Voltage & Temperature Derating**:
   - Guaranteed Nexperia Datasheet $V_F$ (Rev 8 per single diode):
     - $V_F \le 0.90\text{ V}$ at $I_F = 1.0\text{ mA}$
     - $V_F \le 1.00\text{ V}$ at $I_F = 10.0\text{ mA}$
     - $V_F \le 1.10\text{ V}$ at $I_F = 50.0\text{ mA}$
   - Series Pair Clamp Voltage:
     - At $I_F = 10\text{ mA}$ (Fault current): $V_{\text{clamp,max}} = 2 \times 1.00\text{ V} = \mathbf{2.00\text{ V}}$ (guaranteed maximum).
     - At $I_F = 1\text{ mA}$: $V_{\text{clamp,max}} = 2 \times 0.90\text{ V} = \mathbf{1.80\text{ V}}$.
     - Nominal Clamp ($+25^\circ\text{C}$): $\approx \pm 1.40\text{ V}$ (conduction knee $\approx 1.10\text{ V}$).
   - Temperature Sensitivity ($-2.0\text{ mV}/^\circ\text{C}$ per diode $\implies \mathbf{-4.0\text{ mV}/^\circ\text{C}}$ for series pair):
     - At $+25^\circ\text{C}$: Knee $\approx 1.10\text{ V}$.
     - At $+55^\circ\text{C}$: Knee $= 1.10\text{ V} - 0.12\text{ V} = \mathbf{0.98\text{ V}}$.
     - At $+70^\circ\text{C}$: Knee $= 1.10\text{ V} - 0.18\text{ V} = \mathbf{0.92\text{ V}}$.
4. **Resolution of Contradiction Between $\pm 1.40\text{V}$ Clamp and ADS131M08 $\pm 1.20\text{V}$ FSR**:
   - **Identified Contradiction**: The Fourth/Fifth-Pass draft claimed that a $\pm 1.40\text{ V}$ clamp "fits inside ADS131M08 $\pm 1.20\text{ V}$ differential FSR". This was mathematically contradictory: $1.40\text{ V} > 1.20\text{ V}$.
   - **Resolution & Rigorous Boundary Definition**:
     - **Linear Metrology Span**: $\pm 1.20\text{ V}$ differential FSR (Gain = 1). Input signals must remain $\le \pm 1.20\text{ V}$ to prevent digital code clipping.
     - **Overvoltage Protection Ceiling**: Dual-series clamp ($\pm 1.40\text{ V}$ nominal, $\le 2.00\text{ V}$ max at 10 mA). The clamp's role is **protective** against open-secondary CT spikes, sitting **ABOVE** the linear measurement FSR ($\pm 1.20\text{ V}$), but **SAFELY BELOW** the silicon damage threshold (ADS131M08 single-ended absolute maximum $-1.60\text{ V}$ to $+3.60\text{ V}$, differential absolute maximum $\sim 5.2\text{ V}$).
     - **Thermal Defect on Feeders 4 & 5**: At $+70^\circ\text{C}$ enclosure ambient, the knee drops to $0.92\text{ V}$. Feeders 4 & 5 produce $0.8677\text{ V}_{\text{peak}}$ ($+1\%$), leaving only **$52\text{ mV}$ of margin** to the knee! This will induce sub-threshold leakage ($>1\ \mu\text{A}$) and non-linear distortion.
     - **Required Fix**: Reduce burden resistor on Feeders 4 & 5 from $27\ \Omega$ to $20\ \Omega$ ($V_{\text{peak}} = 0.636\text{ V}$), restoring $284\text{ mV}$ of margin at $+70^\circ\text{C}$.
5. **Gate B & CT BOM Disposition**:
   - Approval is **STRICTLY WITHHELD**. **Gate B remains STRICTLY BLOCKED** until a complete, fully tested AFE daughterboard with verified thermal margins and active sub-rail sink regulation is fabricated and bench-tested.

---

## 3. Analog Front-End (AFE) RMS Fault & Shunt Regulator Failure Analysis

### 3.1 Corrected 50V RMS Fault Peak Calculation
In the Fourth-Pass report, a $50\text{ V}_{\text{RMS}}$ fault was erroneously calculated as if it were $50\text{ V}_{\text{DC}}$ ($50.0\text{ V}$). The correct physical peak voltage is:
$$V_{\text{peak}} = V_{\text{RMS}} \times \sqrt{2} = 50.0\text{ V} \times 1.41421356 = \mathbf{70.7107\text{ V}}$$

### 3.2 Manufacturer Datasheet Diode Forward Voltage (Nexperia BAV199 Rev 8)
An assumed fixed forward voltage of $0.65\text{ V}$ is invalid under fault currents. Per the official Nexperia BAV199 datasheet:
- $V_F \le 0.90\text{ V}$ at $I_F = 1.0\text{ mA}$
- $V_F \le 1.00\text{ V}$ at $I_F = 10.0\text{ mA}$
- $V_F \le 1.05\text{ V} - 1.10\text{ V}$ at $I_F = 15.0\text{ mA}$

Steering overvoltage to a $+2.50\text{ V}$ sub-rail yields:
$$V_{\text{pin,pos}} = 2.50\text{ V} + 1.05\text{ V} = \mathbf{+3.55\text{ V}} \quad (\text{Abs Max Limit: } +3.60\text{ V; Remaining Margin: ONLY } 50\text{ mV!})$$
$$V_{\text{pin,neg}} = 0.00\text{ V} - 1.05\text{ V} = \mathbf{-1.05\text{ V}} \quad (\text{Abs Min Limit: } -1.60\text{ V})$$

### 3.3 Series Resistor Fault Currents ($R_{\text{series}} = 4.70\text{ k}\Omega$)
- **Positive Clamp Peak Current**:
  $$I_{\text{pos,peak}} = \frac{70.71\text{ V} - 3.55\text{ V}}{4700\ \Omega} = \mathbf{14.289\text{ mA}} \quad (\mathbf{+42.9\% \text{ over } \pm 10.0\text{ mA continuous abs max!}})$$
- **Negative Clamp Peak Current**:
  $$I_{\text{neg,peak}} = \frac{70.71\text{ V} - 1.05\text{ V}}{4700\ \Omega} = \mathbf{14.821\text{ mA}} \quad (\mathbf{+48.2\% \text{ over } \pm 10.0\text{ mA continuous abs max!}})$$
- **Differential Fault ($100\text{ V}_{\text{peak}}$ across $9.40\text{ k}\Omega$)**:
  $$I_{\text{diff,peak}} = \frac{100.0\text{ V} - 4.60\text{ V}}{9400\ \Omega} = \mathbf{10.149\text{ mA}} \quad (\mathbf{Over } 10.0\text{ mA limit!})$$

### 3.4 LM4040 2.5V Rail Current Absorption & Multi-Channel Failure
The Fourth-Pass proposed absorbing fault currents with an LM4040-2.5 precision micropower shunt reference. Per the Texas Instruments LM4040 datasheet:
- Maximum Continuous Operating Reverse Current: **$15.0\text{ mA}$**
- Absolute Maximum Reverse Current: **$20.0\text{ mA}$**

Evaluating simultaneous fault injection from multiple channels:

| Fault Scenario | Channels Faulting | Injected Current ($I_{\text{inj}}$) | LM4040 Sink Capacity Used | LM4040 Operating Status | Sub-Rail Voltage ($V_{\text{sub}}$) | Pin Voltage ($V_{\text{pin}}$) | Silicon Hazard |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| Single Channel | 1 | $14.29\text{ mA}$ | **$95.3\%$** | Marginal Regulation | $+2.50\text{ V}$ | $+3.55\text{ V}$ | Zero Margin |
| Two Channels | 2 | $28.58\text{ mA}$ | **$190.5\%$** | **Thermal Overload / Lost Reg**| $> +3.00\text{ V}$ | $> +4.05\text{ V}$ | **SILICON BLOWOUT** |
| Single Feeder | 3 | $42.87\text{ mA}$ | **$285.8\%$** | **Catastrophic Failure** | $> +3.50\text{ V}$ | $> +4.55\text{ V}$ | **SILICON BLOWOUT** |
| All Feeder R-Phases| 6 | $85.73\text{ mA}$ | **$571.5\%$** | **Catastrophic Failure** | $> +4.00\text{ V}$ | $> +5.05\text{ V}$ | **SILICON BLOWOUT** |
| All Active Channels| 21 | $300.0\text{ mA}$ | **$2000\%$** | **Instant Destruction** | Shoots to Fault V | Shoots to Fault V | **TOTAL DESTRUCTION** |

### 3.5 AVDD Unpowered Fault Hazard
When the node is powered off ($\text{AVDD} = 0.0\text{ V}$):
- The LM4040 is unpowered and cannot shunt current.
- Texas Instruments ADS131M08 Table 7.1 specifies an absolute maximum input voltage of **$+0.30\text{ V}$** when unpowered.
- Injected current flows through the internal ESD diodes into the unpowered $+3.3\text{ V}$ rail, clamping at $\sim +0.60\text{ V}$. This violates the $+0.30\text{ V}$ unpowered absolute maximum rating, causing permanent gate-oxide breakdown.

### 3.6 Gate B Authoritative Disposition
> [!CAUTION]
> **GATE B REMAINS STRICTLY BLOCKED**:
> Designing at the ADS131M08 absolute maximum continuous input current ($\pm 10.0\text{ mA}$) as an operational target is an unacceptable violation of electrical engineering safety standards. Sinking fault currents into an unbuffered LM4040 reference destroys regulation upon multi-channel faults. Gate B cannot pass until an active high-current shunt sink (TL431 + external BJT bypass transistor or discrete TVS array) is fabricated and surge-tested on physical hardware.

---

## 4. ZMPT101B Voltage Transformer Front End & Surge Withstand Refutation

### 4.1 Primary Resistor Sizing & Operational Analysis ({\\text{pri}} = 150.0\\text{ k}\\Omega$, {\\text{sec}} = 100.0\\ \\Omega$)

**Exact Manufacturer Variant Identification**:
- **Manufacturer**: Qingxian Zeming Langxi Electronic Devices Co., Ltd.
- **Part Number**: ZMPT101B (Raw 4-pin PCB through-hole micro voltage transformer).
- **Prohibited Modules**: Generic blue active breakout modules containing LM358 operational amplifiers and onboard trim potentiometers are **STRICTLY PROHIBITED** due to severe temperature drift, phase noise, and poor linearity.
- **Rated Input Current**: .0\\text{ mA RMS}$ (1:1 turns ratio, 1000:1000 turns)
- **Sampling Resistor**: .0\\ \\Omega$ (0.1% metal film, 25 ppm/°C)
- **Published Linearity**: $\\le 0.2\\%$ (tested at .0\\text{ mA}$)
- **Published Phase Error**: $\\le 20'$ (tested strictly at .0\\text{ mA}$, \\ \\Omega$ burden)
- **Dielectric Strength**: \\text{ VAC} / 1\\text{ min}$ (galvanic isolation)
- **Max Continuous Thermal Current**: .0\\text{ mA RMS}$

Performance across utility grid operating voltages:

| Operating Condition | Utility Voltage | Primary Current ({\\text{pri}}$) | Secondary Current ({\\text{sec}}$) | Burden Voltage ({\\text{sec}}$) | ADC Differential Peak ({\\text{pk}}$) | Total Power Dissipation | Power per Resistor (\\times 50\\text{k}$) | $\\%$ of Rated Current | Accuracy Status |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Nominal Grid** | \\text{ V RMS}$ | .533\\text{ mA}$ | .533\\text{ mA}$ | .3\\text{ mV RMS}$ | .8\\text{ mV}_{\\text{pk}}$ | .353\\text{ W}$ | .118\\text{ W}$ (.0\\times$ derated) | .7\\%$ | Unverified (Bench Cal Pending) |
| **High Grid Line** | \\text{ V RMS}$ | .000\\text{ mA}$ | .000\\text{ mA}$ | .0\\text{ mV RMS}$ | .8\\text{ mV}_{\\text{pk}}$ | .600\\text{ W}$ | .200\\text{ W}$ (.0\\times$ derated) | **.0\\%$** | Exact Datasheet Calibration Point |
| **Phase-to-Phase Fault**| \\text{ V RMS}$ | .767\\text{ mA}$ | .767\\text{ mA}$ | .7\\text{ mV RMS}$ | .3\\text{ mV}_{\\text{pk}}$ | .148\\text{ W}$ | .383\\text{ W}$ (.2\\times$ derated) | **.3\\%$** | **UNVERIFIED BY MANUFACTURER** |

### 4.2 Refutation of Class 1.0 / Phase Claims at 415V
At 415V RMS across \\text{ k}\\Omega$, the transformer operates at **.3\\%$ of its manufacturer rated current**.
- The manufacturer datasheet explicitly bounds its phase error guarantee ($\\le 20'$) to the .0\\text{ mA}$ test point.
- Above 2.0 mA, magnetic core permeability drops, non-linear excitation current increases, and secondary phase angle shifts.
- While the transformer is thermally safe (continuous limit is 10 mA), **claiming Class 1.0 or phase accuracy at 415V without manufacturer test evidence and multi-point bench calibration is physically unfounded**.
- Furthermore, **NO Class 1.0 metrology claim can be made for any part of the system** prior to full end-to-end multi-point calibration on a physical test bench!

### 4.3 Refutation of Summed Resistor Surge Ratings
The Fourth-Pass report claimed that three 1000V overload resistors in series automatically provide a \\text{ V}$ surge withstand. This claim violates electrical physics:
1. **Resistive vs Capacitive Voltage Division**: Under steady-state 50 Hz, voltage divides uniformly (.3\\%$ per resistor). However, under fast IEC 61000-4-5 .2/50\\ \\mu\\text{s}$ transients (spectral components up to $\\sim 1\\text{ MHz}$), parasitic stray capacitance to the PCB ground plane ( \\approx 1.0\\text{ pF}$) and parallel pad capacitance ( \\approx 0.2\\text{ pF}$) dominate.
2. **Non-Uniform Voltage Ladder**: The series chain acts as a distributed RC transmission line ladder. High-frequency displacement currents are shunted to ground, causing the first resistor (closest to the mains terminal) to absorb **\\% - 75\\%$ of the total surge impulse step**!
   V_{\\text{res1}} \\approx 0.65 \\times 2500\\text{ V} = \\mathbf{1,625\\text{ V}}
   This **EXCEEDS** the individual \\text{ V}$ surge overload rating of the ROX2S resistor!
3. **Creepage and Clearance**: Spanning $>30\\text{ mm}$ with .5\\text{ mm} \\times 25\\text{ mm}$ PCB isolation slots satisfies IEC 61010-1 Cat III creepage (.0\\text{ mm}$) and clearance (.0\\text{ mm}$), but does not equalize dynamic capacitive impulse distribution.
4. **Physical Surge Status**: Mathematical summation is rejected; **physical surge generator validation remains strictly PENDING**.

---

## 5. Comprehensive 21-Channel Metrology Calibration Protocol

### 5.1 Clear Separation of Metrology Test Instruments
To avoid confusion between source generation and measurement verification, the calibration facility MUST strictly separate equipment roles:
1. **Precision AC Signal Source (Generation)**: **Fluke 6100A / 6105A Electrical Power Standard** (or Chroma 61500 series).
   - Generates stable, low-distortion (<0.05% THD) sinusoidal AC test voltages (0–300 VAC RMS) and currents (0–50 A RMS) with programmable phase displacement.
2. **Reference Metrology Instrument (Verification)**: **Yokogawa WT310E Precision Digital Power Meter** (Class 0.1 accuracy).
   - Independently senses and measures the actual True-RMS voltage, current, active power, and phase angle delivered to the Device Under Test (DUT).

### 5.2 Canonical 21-Channel Mapping
- **Channels 01 to 03**: Common Bus Voltage (Phases R, Y, B) via ZMPT101B $\\rightarrow$ ADC #3 (Pins AIN3, AIN4, AIN5).
  - *Mains Isolation Rule*: 230VAC connects exclusively to the transformer primary resistor ladder; direct 230VAC connection to ADC input pins is strictly prohibited and physically isolated by .0\\text{ kV}$ transformer barrier.
- **Channels 04 to 21**: Feeder Currents (Feeders 1 to 6, Phases R, Y, B) via CTs:
  - Ch 04–06: Feeder 1 (ZMCT103C, 8A/10A) $\\rightarrow$ ADC #1 (AIN0–AIN2)
  - Ch 07–09: Feeder 2 (SCT-013-000, 28A) $\\rightarrow$ ADC #1 (AIN3–AIN5)
  - Ch 10–12: Feeder 3 (SCT-013-000, 28A) $\\rightarrow$ ADC #1 (AIN6–AIN7), ADC #2 (AIN0)
  - Ch 13–15: Feeder 4 (SCT-013-000, 45A) $\\rightarrow$ ADC #2 (AIN1–AIN3)
  - Ch 16–18: Feeder 5 (SCT-013-000, 45A) $\\rightarrow$ ADC #2 (AIN4–AIN6)
  - Ch 19–21: Feeder 6 (SCT-013-000, 30A) $\\rightarrow$ ADC #3 (AIN0–AIN2)

### 5.3 Multi-Point Calibration Protocol

#### Phase 1: Zero-Offset Nulling (All Channels)
1. De-energize all AC sources ( = 0.0\\text{ V}$,  = 0.0\\text{ A}$).
2. Sample 1,280 points over 20 AC cycles (4 kSPS) across all channels.
3. Compute and store DC offset {\\text{offset}} = \\frac{1}{N} \\sum V_{\\text{raw}}(n)$ in NVS. Confirm offset noise $\\le \\pm 50\\ \\mu\\text{V}$.

#### Phase 2: Multi-Point Voltage Gain & Linearity Calibration (Channels 01–03)
From the Fluke 6100A source, inject test voltages verified on Yokogawa WT310E:
- **Point 1 (80% Nominal)**: .0\\text{ VAC RMS @ 50.00 Hz}$
- **Point 2 (100% Nominal)**: .0\\text{ VAC RMS @ 50.00 Hz}$ (Primary Gain Reference)
- **Point 3 (113% High Line)**: .0\\text{ VAC RMS @ 50.00 Hz}$
- **Point 4 (130% Maximum Operating)**: .0\\text{ VAC RMS @ 50.00 Hz}$
Compute multi-point gain coefficients and verify linearity error $\\le \\pm 0.5\\%$.

#### Phase 3: Multi-Point Current Gain & Linearity Calibration (Channels 04–21)
Inject sinusoidal currents across the dynamic range using the Fluke 6100A, verified on Yokogawa WT310E:
- **Feeder 1 (Ch 04–06, ZMCT103C, 8A Nom / 10A Max)**:
  .40\\text{ A}$ (5%), .80\\text{ A}$ (10%), .60\\text{ A}$ (20%), .00\\text{ A}$ (50%), .00\\text{ A}$ (100%), .00\\text{ A}$ (125%).
- **Feeders 2 & 3 (Ch 07–12, SCT-013-000, 28A Max)**:
  .40\\text{ A}$ (5%), .80\\text{ A}$ (10%), .60\\text{ A}$ (20%), .00\\text{ A}$ (50%), .00\\text{ A}$ (100%).
- **Feeders 4 & 5 (Ch 13–18, SCT-013-000, 45A Max)**:
  .25\\text{ A}$ (5%), .50\\text{ A}$ (10%), .00\\text{ A}$ (20%), .50\\text{ A}$ (50%), .00\\text{ A}$ (100%).
- **Feeder 6 (Ch 19–21, SCT-013-000, 30A Max)**:
  .50\\text{ A}$ (5%), .00\\text{ A}$ (10%), .00\\text{ A}$ (20%), .00\\text{ A}$ (50%), .00\\text{ A}$ (100%).

#### Phase 4: Phase Displacement Angle Calibration
Measure and calibrate transformer phase displacement against Yokogawa WT310E:
- Condition 1: Unity Power Factor ($\\cos\\phi = 1.000, \\theta = 0.00^\\circ$)
- Condition 2: Inductive Load ($\\cos\\phi = 0.800\\text{ lagging}, \\theta = +36.87^\\circ$)
- Condition 3: Low Power Factor Inductive ($\\cos\\phi = 0.500\\text{ lagging}, \\theta = +60.00^\\circ$)
Firmware applies FIR / fractional-delay sample phase compensation to ensure accurate active, reactive, and apparent power metrology.

---

## 6. Summary of Metrology Gate Dispositions

1. **Gate B (AFE Input Protection)**: **STRICTLY BLOCKED** (50Vrms peak current reaches 14.29 mA, exceeding 10 mA abs max; LM4040 fails under 2+ simultaneous channels; dual-series clamp requires reduced burden resistors to guarantee thermal margin).
2. **Gate H (Metrology Sizing & Burden)**: **PARTIAL / BLOCKED** (CT burden clamp thermal headroom defect on Feeders 4 & 5; ZMPT101B operated at 138.3% rated current at 415V without manufacturer accuracy specification).
3. **Gate I (Physical Metrology Calibration)**: **NOT TESTED** (Class 1.0 unclaimable before physical multi-point calibration on Fluke 6100A / Yokogawa WT310E bench).
4. **Gate N (Live 415V Grid Safety Interlock)**: **STRICTLY BLOCKED**.
