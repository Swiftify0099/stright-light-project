# MSCB SMART POLE SYSTEM R3.2 — INITIAL BLOCKER AUDIT REPORT

**Date**: November 2026  
**Repository**: `https://github.com/Swiftify0099/stright-light-project`  
**Base Commit**: `d98fc92e98936547b3d6e35df5fb652d3e583a3a`  
**Working Branch**: `r3.2-real-hardware-truth-repair` (branched from `r3.1-critical-blocker-repair`)  
**Lead Embedded Systems Architect**: Senior Embedded Systems Engineer & Metrology Specialist  

---

## 1. Executive Summary & Verification Premise

A rigorous repository audit of the R3.1 branch (`r3.1-critical-blocker-repair`) was conducted. While R3.1 successfully executes 124 host-side native assertions with zero linking errors, **host test pass rates do not equal physical field operation**. 

This audit identifies the exact software-to-hardware divergence points, architectural blockers, pin conflicts, and safety requirements prior to implementing R3.2.

---

## 2. Identified Engineering Blockers & Root Causes

### Blocker 1: Production Relay Safety Lock & Commissioning Architecture
- **Root Cause**: `MSCB_RELAYS_LOCKED_DISABLED 1` in `BuildConfig.h` blocks all relay ON commands unconditionally. While this protects 415V utility infrastructure from accidental activation, there was no safe, isolated low-voltage (LV) DC commissioning architecture with physical hardware authorization.
- **Divergence**: GPIO output readback is a register check, NOT physical contactor closure. In R3.1, a command without auxiliary feedback returned `FEEDBACK_NOT_CONNECTED`, but the full lifecycle required distinct truth states:
  - `COMMAND_RECEIVED`
  - `COMMAND_ACCEPTED`
  - `RELAY_OUTPUT_ENERGIZED`
  - `CONTACTOR_CLOSED`
  - `MCB_TRIPPED`
  - `COMMAND_REJECTED`
  - `FEEDBACK_UNAVAILABLE`
  - `COMMAND_TIMEOUT`
  - `HARDWARE_FAULT`
- **R3.2 Rectification**:
  1. Default production safety remains fail-closed (`MSCB_RELAYS_LOCKED_DISABLED 1`).
  2. Implement an isolated low-voltage DC commissioning profile (`MSCB_COMMISSIONING_LVDC_PROFILE`) requiring BOTH compile-time configuration AND explicit local physical authorization (e.g. physical jumper or factory provisioning token).
  3. Remote Telegram commands can NEVER bypass the safety lock.
  4. Emergency OFF and individual OFF remain unblocked at all times regardless of locks or time synchronization.

---

### Blocker 2: Pin Mapping Discrepancy & Single Source of Truth
- **Root Cause**: An old documentation/legacy reference in `Resivers/config.h` assigned feeders to strapping/flash pins `12, 13, 14, 15, 27`. In contrast, `PoleBoardPins.h` rightly mapped them to safe non-strapping pins:
  - **Feeder 1**: GPIO 26 (Physical Pin 24, Board Label D26)
  - **Feeder 2**: GPIO 25 (Physical Pin 23, Board Label D25)
  - **Feeder 3**: GPIO 27 (Physical Pin 25, Board Label D27)
  - **Feeder 4**: GPIO 16 (Physical Pin 6, Board Label RX2)
  - **Feeder 5**: GPIO 17 (Physical Pin 7, Board Label TX2)
  - **Feeder 6**: GPIO 32 (Physical Pin 21, Board Label D32)
- **R3.2 Rectification**:
  1. Affirm `PoleBoardPins.h` as the sole authoritative hardware mapping source.
  2. Add hardware profile identifier (`MSCB_HW_PROFILE_ESP32_WROOM32_REV1`) and configuration versioning.
  3. Enforce compile-time assertions forbidding strapping pins (0, 2, 12, 15) and SPI flash pins (6-11).
  4. Implement transactional NVS pin configuration validation with CRC32 checks and safe fallback.
  5. Strictly prevent runtime pin reassignment while relays are energized.

---

### Blocker 3: Telegram UI Menu Overload
- **Root Cause**: `GwTelegramBot.cpp` had 14 buttons packed into the main menu, making it heavy and difficult to navigate on mobile devices.
- **R3.2 Rectification**:
  - Restructure navigation so **EVERY menu and sub-menu contains at most 6 buttons**:
    - **Main Menu**: 6 buttons (`[📋 Pole Directory]`, `[⚡ ALL Feeders ON]`, `[🛑 ALL Feeders OFF]`, `[📋 Registrations]`, `[📊 System Status]`, `[⚙️ Settings]`).
    - **Pole Directory**: Paginated (4 poles per page + `[Prev]` `[Next]` `[Back]`, total <= 6 buttons).
    - **Pole Details**: 6 buttons (`[⚡ ALL ON]`, `[🛑 ALL OFF]`, `[🔌 Feeder List]`, `[📈 Live Metrology]`, `[⚙️ Config]`, `[🔙 Back]`).
    - **Feeder List**: 6 feeder selector buttons (`[F1]`, `[F2]`, `[F3]`, `[F4]`, `[F5]`, `[F6]` with state indicators) + Back button.
    - **Feeder Details**: 4 buttons (`[⚡ Turn ON]`, `[🛑 Turn OFF]`, `[🔄 Refresh]`, `[🔙 Back]`).
    - **Dangerous Actions**: 2-step confirmation buttons (`[✅ Confirm]`, `[❌ Cancel]`).

---

### Blocker 4: Intermediate Packet Forwarding & LoRa Mesh Integrity
- **Root Cause**: While `PoleMeshRouter` handles duplicate suppression and hop counting, packet forwarding on intermediate nodes must preserve the original packet header (`srcAddress`, `dstAddress`, `seqNumber`, auth context) and strictly increment `hopCount` without altering payload or authentication tags.
- **R3.2 Rectification**:
  - Forwarding preserves immutable source address, destination address, sequence number, and HMAC tag.
  - Intermediate ACKs are distinguished from final feeder execution ACKs.

---

### Blocker 5: Parent-Child Heartbeat & Network Failure Differentiation
- **Root Cause**: If an intermediate parent pole goes offline, the Gateway would mark all descendant poles as individually failed devices, triggering alert spam.
- **R3.2 Rectification**:
  - Intermediate parent poles monitor child heartbeats locally.
  - When a parent pole (e.g. Pole A) goes offline, the Gateway marks Pole A as OFFLINE and marks its child descendants as `UNREACHABLE_VIA_PARENT`, rather than independently failed.
  - Implement alert cooldown (default 300s), fault confirmation count (default 3 pings), and recovery alerts.

---

### Blocker 6: Outdated "Skeleton" Banners in Production Firmware
- **Root Cause**: `FieldPole_ESP32.ino` and `Gateway_ESP32S3.ino` retained legacy "(Skeleton)" banners and file headers, creating confusion as to whether the codebase is an empty template or production firmware.
- **R3.2 Rectification**:
  - Update banners and comments in `.ino` sketches to truthfully declare:
    - `MSCB SMART POLE SYSTEM R3.2 — PRODUCTION FIELD POLE CONTROLLER`
    - `MSCB SMART POLE SYSTEM R3.2 — PRODUCTION MAIN GATEWAY CONTROLLER`
  - Remove contradictory claims in `README.md`.

---

## 3. Implementation Plan by Phase

1. **Phase 2**: Critical Relay Control Repair (Safe LVDC Commissioning, Truth States, Aux Feedback).
2. **Phase 3**: Unified Pin Mapping, Hardware Profile & NVS Config Integrity.
3. **Phase 4**: Streamlined Telegram UI (Max 6 Buttons Per Menu View).
4. **Phase 5**: LoRa Mesh Routing, Intermediate Forwarding & Loop Prevention.
5. **Phase 6**: Parent-Child Heartbeat Tracking & Unreachable Tree Classification.
6. **Phase 7**: Strict Registration Lifecycle & Permanent Revocation.
7. **Phase 8**: Sensor Acquisition Integrity (Fail-Closed, Zero Synthetic Data).
8. **Phase 9**: Truthful Command ACK Flow (Intermediate vs Final).
9. **Phase 10**: Dynamic Scheduling & Time-Sync Gating.
10. **Phase 11-13**: Test Expansion, Verification, and R3.2 Hardware Truth Report.
