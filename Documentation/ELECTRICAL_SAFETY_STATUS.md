# MSCB Smart Pole & Feeder Automation System: Electrical Safety Status & Field Mandates

**Document ID**: MSCB-QA-SAF-01  
**Classification**: Electrical Safety Verification & High-Voltage Gate Audit  
**System Title**: MSCB Smart Pole & Feeder Automation System (System_R1 / V10)  
**Safety Auditor**: Senior Industrial Electrical Safety & Switchgear Engineer  
**Date**: November 2026  
**Safety Gate Status**: **BENCH DC VERIFIED — LIVE 415 VAC MAINS PENDING FIELD SIGN-OFF**  

---

## 1. High-Voltage Verification Boundary & Safety Rules

The MSCB Smart Pole System controls high-energy 415 VAC three-phase distribution feeders capable of fatal electric shock, catastrophic arc flash, and transformer damage if improperly switched.

> [!CAUTION]
> **CRITICAL ELECTRICAL SAFETY MANDATE**:
> A software test PASS does **NOT** constitute authorization for live high-voltage switching.
> Under Indian Central Electricity Authority (CEA) Regulations, energizing 415 VAC switchgear requires on-site inspection, physical insulation resistance testing, and formal sign-off by a licensed electrical inspector.

---

## 2. Safety Systems Verification Matrix

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                            ELECTRICAL SAFETY SUBSYSTEM AUDIT                                │
├──────────────────────┬──────────────────────────────┬──────────────────┬────────────────────┤
│ Safety Mechanism     │ Engineering Implementation   │ Target Protection│ Audit Status       │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Galvanic Isolation   │ Optocoupler PC817 modules    │ 2.5 kV Isolation │ VERIFIED (Bench DC)│
│                      │ with isolated JD-VCC rail    │ Logic & Coil     │ 100% DC Pass       │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Fail-Safe Boot Lock  │ Firmware drives relay pins   │ Prevents relay   │ VERIFIED (Software/│
│                      │ to HIGH/OFF on instruction 1 │ energizing on pwr│ Bench DC Pass)     │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Contactor Supervision│ Auxiliary dry contacts polled│ Detects welded or│ VERIFIED (Software/│
│                      │ with 50 ms debounce window   │ jammed contactor │ Bench DC Pass)     │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Inrush Staggering    │ 500 ms sequential delay      │ Prevents busbar  │ VERIFIED (Software/│
│                      │ between feeder actuations    │ voltage dips     │ Bench DC Pass)     │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Hardware Overcurrent │ Instant trip at $> 32.0$ A   │ Wiring & breaker │ VERIFIED (Software/│
│ Protection           │ RMS sustained for $> 100$ ms │ thermal overload │ Bench DC Pass)     │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Manual Override      │ Mechanical 3-position toggle │ Manual operation │ PENDING PHYSICAL   │
│ Switch               │ (AUTO / OFF / MANUAL)        │ during MCU outage│ FIELD INSTALLATION │
├──────────────────────┼──────────────────────────────┼──────────────────┼────────────────────┤
│ Three-Phase Mains    │ 415 VAC 3-phase busbars with │ Arc flash & phase│ STRICTLY PENDING   │
│ Live Energization    │ HRC fuses and lightning MOVs │ short-circuit    │ LICENSED SIGN-OFF  │
└──────────────────────┴──────────────────────────────┴──────────────────┴────────────────────┘
```

---

## 3. Mandatory Field Pre-Commissioning Checklist

Before connecting any MSCB Field Pole Node to a live 415 VAC rural feeder line:
1. **Insulation Resistance (Megger Test)**: Minimum $50\text{ M}\Omega$ between phase conductors and earth at 1,000 V DC.
2. **Earth Pit Resistance**: Measured loop impedance $\le 2.0\ \Omega$ at the substation/pole grounding electrode.
3. **Contactor Coil Snubbers**: RC snubber networks ($0.1\ \mu\text{F} + 100\ \Omega$) must be installed across all 230 VAC contactor coils to suppress inductive kickback.
4. **Current Transformer Secondary Termination**: CT burden resistors must be securely soldered; never allow CT secondaries to open-circuit while primary is energized.
5. **Physical Interlock Verification**: Confirm that setting the manual selector switch to "OFF" mechanically breaks contactor coil power regardless of microcontroller relay pin state.
