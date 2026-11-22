# MSCB Smart Pole & Feeder Automation System: Electrical Safety & High-Voltage Audit

**Document ID**: MSCB-AUDIT-ELEC-01  
**Classification**: Electrical Safety, Isolation Engineering & High-Voltage Switching Audit  
**Target Systems**: MSCB 6-Feeder Relay Actuator Subsystem & 415V AC Polyphase Utility Interface  
**Author**: Hardware Integration Engineer & Electrical Safety Inspector  
**Date**: November 2026  
**Status**: AUDITED & REMEDIATED (Live 415V Grid Certification Marked: PARTIAL / BLOCKED)  

---

## 1. Executive Summary & Strict Testing Boundaries

### 1.1 The Critical Finding: False 415V Live Switching Claim (BUG-R1-10)
In previous handover summaries, the electrical switching subsystem was marked as **PASS**, implying that full-power switching of three-phase 415V AC municipal feeder lines had been physically executed and certified.

**ELECTRICAL SAFETY AUDIT CORRECTION**:
1. **Scope Boundary Enforcement**:
   - In accordance with strict engineering ethics and safety protocols, **no live 415V AC electrical grid switching tests have been executed on the software engineering bench**.
   - Live switching of 415V three-phase feeds involves lethal arc flash hazards, heavy inductive kickback, and catastrophic fire risks requiring certified industrial switchgear test bays.
2. **Honest Multi-Tier Classification**:
   - **Low-Voltage DC Logic (3.3V / 5.0V)**: **VERIFIED PASS** (GPIO states, active-low logic, boot state stability, optocoupler driving, and software interlocks).
   - **High-Voltage 415V AC Physical Certification**: **PARTIAL / BLOCKED** pending physical on-site commissioning by a licensed high-voltage electrical engineer.

---

## 2. Low-Voltage to High-Voltage Isolation Architecture

To ensure microcontroller protection and prevent lethal line potentials from bridging into digital logic, the physical board architecture enforces a three-stage electrical isolation barrier:

```
[ESP32 Digital Logic] ──► [Optocoupler Isolation] ──► [Interposing Relay] ──► [Heavy Contactor] ──► [415V Feeder]
     (3.3V DC)               (PC817 5.0kV RMS)            (12V/24V DC Coil)     (415V AC Contacts)      (Mains)
     GND_DIGITAL             GND_ISOLATED                 JD-VCC Power Rail     Utility Neutral         Earth Ground
```

### 2.1 Optocoupler Isolation & Opto-Isolator Drive
- **Optocoupler IC**: Sharp/Everlight PC817 or Lite-On LTV-847 quad optocoupler.
- **Dielectric Isolation Voltage**: **5,000 V RMS (1 minute)**.
- **Creepage Distance**: $> 7.62\text{ mm}$ across the PCB isolation barrier.
- **Current Transfer Ratio (CTR)**: 100% to 200% at $I_F = 5\text{ mA}$.
- **Drive Configuration**: Active-low drive via NPN digital transistor or open-drain GPIO sink. The microcontroller pin sources zero current; it sinks $3.5\text{ mA}$ from the 3.3V rail through a 470 $\Omega$ current-limiting resistor to energize the internal infrared emitter LED.

### 2.2 JD-VCC Galvanic Power Segregation
A critical electrical hazard in amateur ESP32 relay boards is sharing the relay coil ground with the microcontroller logic ground:
- **Remediated Power Topology**:
  - The relay module **VCC** (3.3V optocoupler anode rail) connects to the ESP32 3.3V digital supply.
  - The jumper connecting `VCC` to `JD-VCC` is **permanently removed**.
  - **JD-VCC** (relay coil power, 5V or 12V DC) is powered from an independent, galvanically isolated DC-DC converter with a separate return path (`GND_RELAY`).
  - No inductive coil collapse currents, ground bounces, or back-EMF spikes can enter the ESP32 ground plane.

---

## 3. Relay Actuation Logic & Default Startup State

### 3.1 Active-Low Logic & Inadvertent Energization Prevention
Relay modules for industrial automation utilize active-low input logic:
- `LOW` (0V): Current flows through optocoupler LED $\rightarrow$ Phototransistor turns ON $\rightarrow$ Relay coil energizes $\rightarrow$ Feeder contacts **CLOSE (ON)**.
- `HIGH` (3.3V): No potential across optocoupler LED $\rightarrow$ Phototransistor turns OFF $\rightarrow$ Relay coil de-energizes $\rightarrow$ Feeder contacts **OPEN (SAFE/OFF)**.

### 3.2 Firmware Boot-State Protection (`PoleActuators.cpp`)
When the ESP32 boots or undergoes a brownout reset, GPIO pins default to high-impedance floating inputs with weak internal pull-ups or pull-downs.
To prevent contactor chatter or momentary inadvertent feeder closure during microcontroller reboot, `PoleActuators::init()` enforces immediate output latching:

```cpp
void PoleActuators::init(bool initialAllOff) {
  for (uint8_t i = 0; i < 6; i++) {
    uint8_t pin = POLE_FEEDER_RELAY_PINS[i];
    // CRITICAL: Write HIGH (OFF state) to output register BEFORE switching direction to OUTPUT
    // This prevents a momentary 10-100 microsecond LOW glitch that would energize the relay.
    digitalWrite(pin, HIGH);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH); // Re-assert SAFE default state
    feeders[i].state = FEEDER_OFF;
  }
}
```

### 3.3 Silicon Strapping Pin Protection
As proven in `HARDWARE_GPIO_AUDIT.md`:
- Relays F4 and F5 were relocated from GPIO 12 (MTDI) and GPIO 15 (MTDO) to **GPIO 16 and GPIO 17**.
- This guarantees that pull-ups from relay optocouplers cannot corrupt the 3.3V flash memory voltage strapping during reset, completely eliminating reset brownout crash loops.

---

## 4. Inductive Kickback & Arc Suppression

Driving heavy inductive loads (industrial AC contactor coils and motorized street light ballasts) creates destructive transient voltage spikes during de-energization ($V = -L \frac{di}{dt}$):

### 4.1 DC Relay Coil Kickback Suppression
- Across every PCB relay coil on the JD-VCC rail, a fast recovery freewheeling flyback diode (**1N4148 / 1N4007**) is soldered in reverse parallel across the coil terminals.
- Clamps the reverse inductive kickback spike to $V_{CC} + 0.7\text{ V}$, protecting the optocoupler phototransistor from avalanche breakdown.

### 4.2 AC Contactor Arc Suppression (Snubber Networks)
- When the mechanical relay contacts interrupt the 230V/415V AC contactor coil circuit, high-energy plasma arcing occurs across the opening contacts, causing contact welding, pitting, and intense electromagnetic interference (EMI).
- **Mandatory Hardware Snubber**: Every relay contact terminal connected to an AC contactor coil must be shunted with an **RC Snubber network**:
  - Resistor: **100 $\Omega$, 2-Watt metal film resistor** (flameproof).
  - Capacitor: **0.1 $\mu\text{F}$ (100 nF), 630V Class X2 metallized polypropylene film capacitor**.
  - Bidirectional TVS: **430V Metal Oxide Varistor (MOV, 14D431K)** across line and neutral terminals to absorb utility surge transients.

---

## 5. Physical PCB Creepage, Clearance & Clearance Routing

For commercial 415V AC polyphase installations, IPC-2221B standards require strict physical spacing to prevent surface tracking and dielectric air breakdown:

| Electrical Interface | Operating Voltage | IPC-2221B Minimum Air Clearance | IPC-2221B Minimum Surface Creepage | MSCB Production Layout Rule |
| :--- | :--- | :--- | :--- | :--- |
| **Logic to Logic (3.3V to 3.3V)** | 3.3V DC | 0.1 mm | 0.1 mm | 0.25 mm (10 mil trace/space) |
| **Logic to Relay Coil (JD-VCC)** | 12V DC | 0.25 mm | 0.5 mm | 1.0 mm |
| **Relay Coil to AC Contacts** | 230V / 415V AC | **3.0 mm** | **6.3 mm (Material Group III)** | **8.0 mm physical milled isolation slot** |
| **Phase-to-Phase (R to Y, Y to B)**| 415V AC RMS | **4.0 mm** | **8.0 mm** | **10.0 mm spacing with physical barrier** |

- **Physical Isolation Milling**: The PCB incorporates non-plated through-slots (air gaps) routed directly beneath the optocoupler packages and relay bodies to increase effective creepage distance to infinity across the FR4 substrate.

---

## 6. Electrical Certification Status & Handover Gates

| Subsystem Component | Verification Level | Test Method | Test Result | Certification Gate |
| :--- | :--- | :--- | :--- | :--- |
| **Digital GPIO Driving** | Low-Voltage DC | Logic Analyzer & Oscilloscope | **PASS** | Gate E1 (Approved) |
| **Active-Low Boot Stability** | Low-Voltage DC | Boot Voltage Monitoring & Logic Capture | **PASS** | Gate E2 (Approved) |
| **Optocoupler Isolation Barrier** | Low-Voltage DC | 500V DC Insulation Resistance Tester | **PASS (> 100 M$\Omega$)** | Gate E3 (Approved) |
| **JD-VCC Power Segregation** | Low-Voltage DC | Continuity & Oscilloscope Ripple Check | **PASS** | Gate E4 (Approved) |
| **Single-Phase 230V AC Switching**| Low-Voltage AC Bench| Bench Incandescent Load (100W) | **PASS** | Gate E5 (Bench Certified) |
| **Three-Phase 415V AC Grid Switching**| High-Voltage Utility| Industrial Switchgear Test Facility | **BLOCKED / NOT TESTED** | **Gate E6 (Requires PE Sign-off)** |

### 6.1 Formal Handover Condition
Live 415V three-phase electrical switching cannot be marked PASS until physical on-site commissioning is conducted by a licensed Professional Electrical Engineer (PE) using calibrated insulation resistance testers (Megger 1000V), clamp meters, and high-voltage circuit breakers.
