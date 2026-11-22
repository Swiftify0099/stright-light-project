# 20 — MSCB Smart Pole Automation System: Preventive Maintenance & Operations Manual

**Document ID**: MSCB-DOC-20  
**Classification**: Industrial Maintenance & Operational Standards  
**Cycle**: Daily, Weekly, Monthly, and Annual Maintenance Protocols  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Preventive Maintenance Schedule

To guarantee uninterrupted service across 50 distribution poles and 300 controlled feeders, maintenance technicians must execute the following structured routines:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    SCHEDULED PREVENTIVE MAINTENANCE CADENCE                 │
├───────────┬───────────────────────────────┬─────────────────────────────────┤
│ Cadence   │ Maintenance Tasks             │ Standard / Verification Criteria│
├───────────┼───────────────────────────────┼─────────────────────────────────┤
│ Daily     │ 1. Review Telegram Dashboard. │ Check offline node count = 0.   │
│ (Remote)  │ 2. Audit Active Fault Alerts. │ Resolve unacknowledged alarms.  │
│           │ 3. Check Bus Voltage Balance. │ Phase unbalance < 10%.          │
├───────────┼───────────────────────────────┼─────────────────────────────────┤
│ Weekly    │ 1. Review Event History Logs. │ Audit 64-entry circular log.    │
│ (Remote)  │ 2. Audit RF Link Margins.     │ Flag nodes with RSSI < -115dBm. │
│           │ 3. Check Duty Cycle Usage.    │ Confirm airtime < 8.0%.         │
├───────────┼───────────────────────────────┼─────────────────────────────────┤
│ Monthly   │ 1. Cabinet Physical Audit.    │ Inspect door seals & breathers. │
│ (Field)   │ 2. Antenna & Cable Audit.     │ Check SMA tight; clean bird dirt│
│           │ 3. Power Supply Ripple Check. │ 5V logic ripple < 50 mV peak.   │
│           │ 4. Thermal Scan (IR Gun).     │ Contactor terminal temp < 65°C. │
├───────────┼───────────────────────────────┼─────────────────────────────────┤
│ Annual    │ 1. Complete Sensor Metrology. │ Re-calibrate 21 channels against│
│ (Field)   │                               │ certified Yokogawa standard.    │
│           │ 2. Contactor Contact Wear.    │ Inspect silver tips for pitting.│
│           │ 3. Lightning Arrestor Check.  │ Test gas tube sparkover voltage.│
│           │ 4. Cryptographic Key Rotation.│ Update network encryption keys. │
└───────────┴───────────────────────────────┴─────────────────────────────────┘
```

---

## 2. Power Supply & Battery/Capacitor Inspection Protocol

1. **DC Voltage Tolerances**:
   - Primary Logic Supply (`SMPS 1`): Must measure $5.00\text{ V} \pm 0.15\text{ V}$.
   - Relay Coil Supply (`SMPS 2`): Must measure $5.00\text{ V} \pm 0.25\text{ V}$.
   - ESP32 Onboard LDO Output: Must measure $3.30\text{ V} \pm 0.05\text{ V}$.
2. **AC Ripple Measurement**:
   - Using an oscilloscope with 20 MHz bandwidth limit, measure peak-to-peak ripple on the 3.3V rail.
   - Ripple exceeding **80 mV pk-pk** indicates degraded filter electrolytic capacitors; replace the SMPS unit.

---

## 3. Sensor Re-Calibration Schedule

- **Routine Interval**: Every 12 months.
- **Mandatory Re-Calibration Events**:
  - Following a direct lightning strike on the utility line.
  - Following a major phase short-circuit fault exceeding 100A.
  - When metrology error exceeds $\pm 1.5\%$ compared to a certified clamp meter.
- **Procedure**: Execute the 3-phase calibration protocol documented in [10_CALIBRATION_MANUAL.md](file:///c:/Users/panka/OneDrive/Desktop/PS/Documentation/10_CALIBRATION_MANUAL.md).

---

## 4. Cryptographic Key Rotation Procedure

Network encryption keys (`MESH_NET_KEY`) should be rotated every 12 months or immediately following the termination of field maintenance personnel:

1. In Telegram, access `/security keys`.
2. Generate an ephemeral 128-bit candidate key.
3. The Gateway broadcasts an encrypted `KEY_UPDATE` frame to all 50 registered poles.
4. Each pole acknowledges receipt; the Gateway commits the new key to NVS on the next hour boundary.
5. Old keys are archived in NVS rollback bank for 48 hours to handle delayed nodes.

---

## 5. NVS Database Backup & Disaster Recovery

1. **Weekly Automated Backup**: Every Sunday at 02:00 UTC, the Gateway compiles its registered node list, calibration parameters, and network topology into a JSON string and archives it to FATFS partition.
2. **Manual Export via Telegram**:
   - Send `/backup export`.
   - The Gateway sends a document file `mscb_backup_YYYYMMDD.json` directly to the Admin Telegram chat.
3. **Disaster Recovery**:
   - If a Gateway controller is replaced, upload `mscb_backup.json` via USB or Telegram `/backup restore`. All 50 nodes and their cryptographic bindings are restored without visiting the field poles.
