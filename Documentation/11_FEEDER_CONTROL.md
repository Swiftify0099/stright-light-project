# 11 — MSCB Smart Pole Automation System: Feeder Control & Electrical Actuation

**Document ID**: MSCB-DOC-11  
**Classification**: Electrical Automation & Actuation Safety  
**Feeders Managed**: 6 Independent Three-Phase Feeders per Pole (300 Network-Wide)  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Five-Stage Command Execution State Machine

In industrial switchgear automation, an unverified command (e.g. driving a relay output without feedback) can lead to unobserved contact welds, coil burnouts, or fire hazards. The MSCB Smart Pole architecture mandates a strict **Five-Stage Verification Lifecycle** for every feeder command:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    FIVE-STAGE FEEDER EXECUTION LIFECYCLE                    │
├───────┬─────────────────────────────┬───────────────────────────────────────┤
│ Stage │ Execution State             │ Criteria / Electrical Verification    │
├───────┼─────────────────────────────┼───────────────────────────────────────┤
│ 1     │ COMMAND_SENT                │ Gateway dispatches authenticated LoRa │
│       │                             │ packet with sequence number & nonce.  │
├───────┼─────────────────────────────┼───────────────────────────────────────┤
│ 2     │ COMMAND_ACCEPTED            │ Field Pole decodes frame, verifies    │
│       │                             │ HMAC tag, validates target feeder ID, │
│       │                             │ and confirms safety interlocks clear. │
├───────┼─────────────────────────────┼───────────────────────────────────────┤
│ 3     │ RELAY_OUTPUT_CHANGED        │ Microcontroller drives optocoupler    │
│       │                             │ GPIO pin (Active-LOW transition).     │
├───────┼─────────────────────────────┼───────────────────────────────────────┤
│ 4     │ CONTACTOR_FEEDBACK_CONFIRMED│ Contactor auxiliary dry contact       │
│       │                             │ closes/opens; debounced over 250 ms   │
│       │                             │ (5 consecutive 50ms samples).         │
├───────┼─────────────────────────────┼───────────────────────────────────────┤
│ 5     │ ELECTRICAL_STATE_VERIFIED   │ ADS131M08 True-RMS metrology confirms │
│       │                             │ current is flowing ($I > 0.1\text{ A}$)│
│       │                             │ for ON, or zero current for OFF.      │
└───────┴─────────────────────────────┴───────────────────────────────────────┘
```

```mermaid
stateDiagram-v2
    [*] --> CMD_STATE_IDLE
    CMD_STATE_IDLE --> CMD_STATE_COMMAND_SENT : Operator Initiates Command
    CMD_STATE_COMMAND_SENT --> CMD_STATE_COMMAND_ACCEPTED : HMAC & Target Verified
    CMD_STATE_COMMAND_ACCEPTED --> CMD_STATE_RELAY_OUTPUT_CHANGED : GPIO Asserted
    CMD_STATE_RELAY_OUTPUT_CHANGED --> CMD_STATE_CONTACTOR_FEEDBACK_PENDING : Wait 1000ms
    CMD_STATE_CONTACTOR_FEEDBACK_PENDING --> CMD_STATE_EXECUTION_VERIFIED : Auxiliary Contact Closed + Current > 0.1A
    CMD_STATE_CONTACTOR_FEEDBACK_PENDING --> CMD_STATE_COMMAND_FAILED : Auxiliary Mismatch (Weld / Coil Fail)
    CMD_STATE_EXECUTION_VERIFIED --> CMD_STATE_IDLE : ACK Dispatched
    CMD_STATE_COMMAND_FAILED --> CMD_STATE_IDLE : Fault Alert Dispatched
```

---

## 2. Electrical Protection & Interlock Architecture

### 2.1 Overcurrent Fast-Trip Interlock
Each feeder channel continuously monitors load current against the configured overcurrent threshold (`CONF_OVERCURRENT_THRESH_A = 25.0f`). If current exceeds the threshold continuously for 3.0 seconds (`CONF_OVERCURRENT_DUR_MS = 3000`):
1. The pole firmware immediately de-energizes the corresponding relay coil locally without waiting for Gateway authorization.
2. The feeder status transitions to `FEEDER_TRIPPED_OVERCURRENT`.
3. An emergency high-priority LoRa alert (`ALERT`) is broadcast to the Gateway.
4. The relay remains locked out in hardware until an administrator manually resets the fault via Telegram (`CMD:RESET_FAULT`).

### 2.2 MCB Auxiliary Trip Detection
Each feeder's manual Miniature Circuit Breaker (MCB) is fitted with an auxiliary dry contact wired to the ESP32:
- Contact is sampled every 50 ms.
- If the contact opens for $\ge 5$ consecutive samples (250 ms debounce), an MCB Trip event is flagged.
- The firmware immediately opens the feeder pilot relay to prevent unexpected reclosing when the manual breaker is reset.

### 2.3 Contactor Abnormality Classifications
The system detects four dangerous electromechanical failure modes:
1. **FEEDBACK_QUALITY_WELDED**: The relay output was commanded OFF, but the auxiliary contact remains CLOSED and current is still flowing. (Critical Safety Alert).
2. **FEEDBACK_QUALITY_COIL_FAIL**: The relay output was commanded ON, but the auxiliary contact failed to close within 1,000 ms. (Coil burnout / mechanical jam).
3. **FEEDBACK_QUALITY_UNEXPECTED_OPEN**: Contactor dropped open while relay was energized (vibration, power dip).
4. **FEEDBACK_QUALITY_UNEXPECTED_CLOSE**: Contactor closed without microcontroller activation (manual override / contact welding).

---

## 3. Group Switching & Inrush Surge Staggering

When switching multiple feeders across a pole or issuing group commands across the 50-node network:
- **Inrush Current Hazard**: Switching 6 three-phase inductive motor loads or LED street light banks simultaneously causes severe transformer voltage sag and trips upstream distribution breakers.
- **Staggering Rule**: The firmware enforces a mandatory **500 ms staggering interval** (`DEFAULT_GROUP_STAGGER_MS = 500`) between successive feeder actuations:
  $$t_{actuation}(k) = t_0 + (k \times 500\text{ ms}) \quad \text{for } k = 0, 1, \dots, 5$$
- During emergency shutoff (`ALL_FEEDERS_OFF`), all relays are de-energized simultaneously in 0 ms to ensure personnel safety.

---

## 4. Feeder Deactivation vs. Deletion

The system maintains an operational distinction between deactivating a feeder and removing it:

| Operational State | Electrical Relay State | Telegram UI Status | Telemetry Monitoring | Re-activation Method |
| :--- | :---: | :---: | :---: | :--- |
| **Active** | Controllable (ON/OFF) | `🟢 Feeder Active` | Full (18 Current Chs) | Normal Operation |
| **Deactivated** | Permanently Locked OFF | `⛔ Deactivated (Maintenance)` | Voltage & Offsets Only | Admin `/feeder activate` |
| **Removed** | De-provisioned | Hidden from Dashboard | Excluded from Telemetry| Re-commissioning required |

- **Deactivation Use Case**: Seasonal irrigation feeders (e.g. pumps offline during harvest) or street lights undergoing physical cable maintenance. Deactivated feeders reject all scheduled or group switching commands.
