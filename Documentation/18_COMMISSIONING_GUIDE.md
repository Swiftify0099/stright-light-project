# 18 — MSCB Smart Pole Automation System: Device Commissioning Guide

**Document ID**: MSCB-DOC-18  
**Classification**: Quality Assurance & Site Acceptance Testing (SAT)  
**Procedure**: Mandatory 18-Step Pole Commissioning Protocol  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Mandatory 18-Step Field Commissioning Protocol

Every new Field Pole Node installed on the distribution network must undergo the following sequential 18-step verification procedure before being energized with live mains power:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                 18-STEP MANDATORY FIELD COMMISSIONING PROTOCOL              │
├──────┬──────────────────────────────┬───────────────────────────────────────┤
│ Step │ Verification Procedure       │ Pass / Acceptance Criteria            │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 1    │ Verify Hardware Physical     │ Visual inspection: PCB clean, no      │
│      │ Assembly                     │ solder bridges, ICs seated properly.  │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 2    │ Verify Wiring & Grounding    │ Creepage > 6.3mm; JD-VCC jumper       │
│      │                              │ removed; earth bond resistance < 1.0Ω.│
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 3    │ Verify LoRa Transceiver      │ SX1278 responds to SPI version query; │
│      │ SPI Communication            │ 433.92 MHz carrier verified on bench. │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 4    │ Verify Antenna & VSWR        │ 433 MHz antenna connected; VSWR < 1.5;│
│      │                              │ surge arrestor grounded.              │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 5    │ Install Approved Firmware    │ Flash verified `POLE_V10` binary image│
│      │                              │ via USB Type-C (SHA-256 confirmed).   │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 6    │ Provision Device Identity    │ Derive hardware key from MAC; set     │
│      │                              │ provisional network access token.     │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 7    │ Register Device via LoRa     │ Power on; node enters DISCOVERING and │
│      │                              │ issues JOIN_REQ over mesh.            │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 8    │ Assign Logical Pole Name     │ Telegram Admin approves node; assigns │
│      │                              │ unique ID (e.g. `POLE-0005`).         │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 9    │ Assign Geographic Area       │ Set cluster / feeder zone metadata in │
│      │                              │ Gateway database (e.g. `Zone-A/Fdr2`).│
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 10   │ Verify Primary Parent ($P_1$) │ Confirm node locks to lowest-cost     │
│      │                              │ upstream DAG parent (RSSI > -110dBm). │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 11   │ Verify Backup Parent ($P_2$)  │ Confirm node discovers a path-diverse │
│      │                              │ secondary parent.                     │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 12   │ Verify Six Feeder Outputs    │ Dry-cycle test (mains off): energize  │
│      │                              │ Feeders 1-6; verify optocoupler LEDs. │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 13   │ Verify 18 Current Channels   │ Inject test current (10A AC); confirm │
│      │                              │ all 18 CT readings within ±1.0%.      │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 14   │ Verify 3 Voltage Channels    │ Apply 230 VAC calibration voltage;    │
│      │                              │ confirm Phase R, Y, B readings ±0.5%. │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 15   │ Verify MCB Auxiliary Inputs  │ Manually trip MCBs 1-6; verify dry    │
│      │                              │ contact detection within 250 ms.      │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 16   │ Verify Contactor Feedback    │ Confirm auxiliary contact transitions │
│      │                              │ match relay pilot coil commands.      │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 17   │ Verify Telegram Dashboard    │ View live telemetry on Telegram bot;  │
│      │                              │ verify voltage, current, and status.  │
├──────┼──────────────────────────────┼───────────────────────────────────────┤
│ 18   │ Record Acceptance & Seal     │ Sign physical commissioning sheet;    │
│      │                              │ apply tamper-evident cabinet seal.    │
└──────┴──────────────────────────────┴───────────────────────────────────────┘
```

---

## 2. Master Commissioning Sign-Off Record Template

Every pole installation must have an archived commissioning record:

```text
=================================================================================
             MSCB SMART POLE SYSTEM — SITE ACCEPTANCE TEST (SAT)
=================================================================================
Pole Identifier     : POLE-_______________      Serial Number  : ________________
Hardware MAC Address: ____________________      Firmware Build : POLE_V10
Geographic Location : Lat: _________ Long: _________ Feeder / Substation: _______
Commissioning Date  : YYYY-MM-DD                Weather Cond.  : ________________
Lead Field Engineer : ________________________  Licensed Elect.: ________________
---------------------------------------------------------------------------------
[ ] 01. Physical Assembly Verified              [ ] 10. Primary Parent Locked
[ ] 02. Galvanic Isolation Verified             [ ] 11. Backup Parent Path-Diverse
[ ] 03. SX1278 SPI Communication Verified       [ ] 12. 6 Feeder Outputs Dry-Cycled
[ ] 04. Antenna VSWR < 1.5 & Arrestor Bonded    [ ] 13. 18 CT Readings Audited
[ ] 05. Firmware POLE_V10 Flashed & Verified    [ ] 14. 3 Bus Voltages Audited
[ ] 06. MAC-Derived Security Key Provisioned    [ ] 15. MCB Trip Inputs Debounced
[ ] 07. Dynamic JOIN_REQ Acknowledged           [ ] 16. Contactor Feedback Matched
[ ] 08. Logical ID Assigned in Gateway          [ ] 17. Telegram Bot Verified
[ ] 09. Geographic Area Tagged in NVS           [ ] 18. Cabinet Sealed & Archived
---------------------------------------------------------------------------------
COMMISSIONING VERDICT: [  ] ACCEPTED INTO SERVICE    [  ] REJECTED (SEE REMARKS)
Remarks / Remediation Required: _________________________________________________
Engineer Signature: __________________________   Date: __________________________
=================================================================================
```
