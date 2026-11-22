# 08 — MSCB Smart Pole Automation System: Dynamic Registration & Deletion Engine

**Document ID**: MSCB-DOC-08  
**Classification**: Device Lifecycle & Access Control  
**Firmware Baseline**: Gateway `GW_V10` / Field Pole `POLE_V10`  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Dynamic Node Registration Workflow

To eliminate hardcoded node lists and simplify field commissioning, new field poles undergo a dynamic zero-touch discovery and administrative authorization protocol:

```mermaid
sequenceDiagram
    autonumber
    participant Node as New Field Pole (ESP32)
    participant Parent as Neighbor Relay Node
    participant GW as Master Gateway (ESP32-S3)
    actor Admin as Telegram Administrator

    Note over Node: State: DISCOVERING (Relays Locked Inactive)
    Node->>Parent: Broadcast JOIN_REQ (Hardware MAC, Nonce, HMAC)
    Parent->>GW: Forward Multi-Hop JOIN_REQ (Link RSSI/SNR)
    Note over GW: Validate HMAC & Nonce; Check Blacklist
    GW->>Admin: Telegram Alert: "🆕 New Pole Detected! MAC: A4:CF:12:..."
    Note over Admin: Admin presses [Approve] button in Telegram
    Admin->>GW: Telegram Callback: REG_APPROVE:A4CF12...
    Note over GW: Assign Unique Logical ID: POLE-0005 & FDR-0025..0030
    GW->>Parent: Unicast JOIN_ACCEPT (Logical ID, Mesh Key, Feeder Mask)
    Parent->>Node: Deliver JOIN_ACCEPT
    Note over Node: Persist ID & Keys to NVS; Transition to ACTIVE
    Note over Node: Relays Unlocked; Telemetry Cadence Starts
    Node->>GW: Telemetry Beacon (Initial 21-Channel Readings)
    GW->>Admin: Telegram Confirmation: "✅ Pole POLE-0005 Active!"
```

---

## 2. Dynamic ID Allocation & Feeder Naming

When an administrator approves a node in Telegram, the Gateway executes automated logical ID generation:

1. **Pole Logical ID Generation**:
   - Sequential, zero-padded identifier: `POLE-0001` through `POLE-0050`.
   - Guaranteed collision-free across restarts via persistent NVS tracking.
2. **Feeder Identifier Allocation**:
   - Each pole is automatically provisioned with 6 logical feeders:
     $$\text{Feeder ID Base} = (Node\_Index - 1) \times 6 + 1$$
     $$\text{Feeder IDs} = \text{FDR-}(Base) \dots \text{FDR-}(Base + 5)$$
   - For example, `POLE-0002` receives `FDR-0007` through `FDR-0012`.
3. **Capacity Ceiling Enforcement**:
   - The Gateway strictly enforces a 50-node limit (`MAX_NODES = 50`).
   - If a 51st unprovisioned node attempts registration, the Gateway rejects the request with code `REG_ERR_CAPACITY_FULL` and alerts the administrator.

---

## 3. Node Deletion & Two-Phase Cryptographic Revocation

When a pole is decommissioned, damaged, or physically compromised, an administrator triggers a permanent deletion workflow. To ensure clean network cleanup, the system uses a **Two-Phase Commit (2PC) Revocation Protocol**:

### 3.1 Two-Phase Revocation Sequence

```mermaid
sequenceDiagram
    autonumber
    actor Admin as Telegram Administrator
    participant GW as Gateway Registry
    participant LoRa as LoRa Mesh
    participant Target as Revoked Pole (POLE-0008)
    participant Children as Downstream Child Nodes

    Admin->>GW: Telegram: Select [Delete POLE-0008]
    GW->>Admin: Confirmation Dialog: "⚠️ Confirm permanent deletion?"
    Admin->>GW: Press [CONFIRM_DELETE_POLE-0008]
    Note over GW: Phase 1: Prepare Deletion Transaction (UUID)
    GW->>LoRa: Transmit Unicast DELE_REQ (Target: POLE-0008)
    LoRa->>Target: Deliver DELE_REQ
    Note over Target: 1. De-energize all 6 relay coils immediately
    Note over Target: 2. Erase NVS credentials (keys, IDs, routes)
    Note over Target: 3. Set Persistent Revocation Flag in Flash
    Target->>LoRa: Transmit DELE_ACK (Wipe Confirmed)
    LoRa->>GW: Deliver DELE_ACK
    Note over GW: Phase 2: Commit Deletion & Broadcast
    GW->>LoRa: Broadcast REVOKE_BCAST (Blacklist HW MAC & POLE-0008)
    Children->>Children: Detect Parent Revocation; Drop POLE-0008 as Parent
    Children->>Children: Trigger Orphan Recovery & Promote Backup Parents
    GW->>Admin: Telegram Alert: "⛔ POLE-0008 Deleted & Revoked Permanently!"
```

### 3.2 Permanent Blacklisting & Re-Commissioning Protection
1. **Gateway Blacklist**: The Gateway appends the revoked node's physical MAC address to the persistent `revoc` NVS namespace (`MAX_REVOKED_NODES = 50`). Any future `JOIN_REQ` from this hardware is silently rejected without notifying the admin.
2. **Pole Anti-Replay Lock**: The revoked pole's internal flash is flagged with `REVOKED_PERMANENT`. Even if rebooted or power-cycled, the node will never transmit registration probes or energize relay coils. Re-commissioning requires physical factory reprogramming over USB.

### 3.3 Downstream Child Impact Mitigation
When an intermediate relay node is deleted:
1. All downstream children detect the loss of parent beacons within 90 seconds (`PARENT_TIMEOUT_MS = 90000`).
2. Children immediately promote their pre-configured Backup Parents (`P_2`) to Primary (`P_1`), preserving electrical switching states without dropping downstream feeders.
3. If no backup parent is available, children enter controlled Orphan State and issue Route Requests (`RREQ`) to find new cluster heads.
