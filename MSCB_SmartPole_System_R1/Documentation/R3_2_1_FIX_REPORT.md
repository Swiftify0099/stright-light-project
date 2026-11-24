# MSCB SMART POLE SYSTEM — R3.2.1 CRITICAL FIX REPORT

**Date:** November 24, 2026  
**System:** MSCB Smart Pole Automated Mesh & Telemetry Subsystem  
**Base Commit:** `d6def238c7d56f5bcbf6963b4e6bd6a5584a72d2` (`r3.2-real-hardware-truth-repair`)  
**Target Branch:** `r3.2.1-critical-mesh-truth-repair`  
**Status:** REPAIRED, HARDENED & VERIFIED (225/225 Native Tests Passing, 0 Compilation Errors)

---

## 1. Executive Summary

This engineering fix report documents the comprehensive root-cause repair, hardening, and verification of the MSCB Smart Pole System firmware for release **R3.2.1**. In strict adherence to the **Real Hardware Truth Policy**, all identified P0 and P1 gaps across LoRa mesh routing, dynamic child tracking, alert cascade deduplication, Telegram inline button limits, truthful auxiliary contact feedback, physical GPIO jumper safety authorization, and boot pin configuration have been repaired directly within production firmware source code.

Zero simulated hardware success, zero fake ACKs, and zero fabricated telemetry readings (such as synthetic 0.0V or 0.0A) are permitted. All missing sensors explicitly report `NAN`, `SENSOR_UNAVAILABLE`, or `FEEDBACK NOT CONNECTED`.

---

## 2. Root Cause Analysis & Architectural Repairs

### 2.1 P0: LoRa Mesh Self-Loop & Routing Cycle Prevention
* **Identified Defect:**
  * Mesh nodes lacked strict validation to reject self-originated packets received over the RF wire. When a packet sent by `myNodeId` was echoed back (via neighbor repeaters), it was processed as an externally received packet, causing routing loops, duplicate forwarding, and synthetic ACKs.
  * In `updateNeighborsFromBeacon()`, nodes could add themselves or downstream children as candidate parents, causing circular routing topologies where packets circled indefinitely.
* **Production Code Repair (`FieldPole_ESP32/PoleMeshRouter.cpp`, `PoleMeshRouter.h`):**
  * **Self-Loop Rejection:** Packets where `frame.header.srcAddress == myNodeId` are immediately dropped with reason `packetsDroppedSelfLoop++` without triggering forwarding or ACKs.
  * **Frame Validation:** Validates magic bytes (`MSCB_PROTOCOL_MAGIC_0/1`), protocol version, non-zero IDs, payload bounds (`<= MSCB_MAX_PAYLOAD_SIZE`), and hop count limits (`hopCount >= maxHops` drops frame with `packetsDroppedHopLimit++`).
  * **Duplicate Cache:** Implemented 32-slot circular duplicate suppression cache keyed by `(srcAddress, seqNumber)`.
  * **Monotonic Cycle Prevention:** `updateNeighborsFromBeacon()` strictly rejects any candidate beacon if `sender == myNodeId`, `sender` is found in `trackedChildren`, or `priParent == myNodeId`.
  * **Explicit Orphan State:** When both primary and backup parents fail (`triggerFailover()`), the router enters `ROUTING_ORPHAN` state and sets Gateway reachability to false until a clean parent beacon is discovered.

---

### 2.2 P0: Real Dynamic Child Tracking
* **Identified Defect:**
  * `onChildPacketReceived()` was declared in `PoleMeshRouter.h` but was never called upon wire frame arrival. No active child records were maintained.
* **Production Code Repair (`FieldPole_ESP32/PoleMeshRouter.cpp`, `MSCBShared/NetworkTypes.h`):**
  * Wire frames destined for Gateway (`dstAddress == MSCB_GATEWAY_NODE_ID`) where `nextHop == myNodeId` invoke `onChildPacketReceived()`.
  * Stores `childPoleId`, `lastSeqNumber`, `lastRssi`, `lastSnr`, `lastSeenMs`, and `state` (`CHILD_STATE_ONLINE`).
  * Inactivity timeout transitions: After 45s without packet, child enters `CHILD_STATE_SUSPECTED`. After 90s (`MESH_CHILD_TIMEOUT_MS`), child transitions to `CHILD_STATE_OFFLINE`. Subsequent valid packet restores `CHILD_STATE_ONLINE` in-place without duplicate allocation.
  * Capacity Management: Bounded at 12 children (`MESH_MAX_TRACKED_CHILDREN`). When table is full, safely evicts oldest offline child.

---

### 2.3 P0: Gateway Heartbeat, Alert Cascade & Bounded Queue
* **Identified Defect:**
  * Gateway heartbeat monitor previously reported `POLE_STATE_ONLINE` or default states when querying unprovisioned/unknown nodes.
  * Single alert string buffer in `GwHeartbeatMonitor` was overwritten by rapid events.
  * When a parent node went offline, all child nodes were falsely reported as direct power outages instead of cascade communication failures.
* **Production Code Repair (`Gateway_ESP32S3/GwHeartbeatMonitor.cpp`, `GwHeartbeatMonitor.h`):**
  * **Explicit States:** Added `POLE_STATE_UNKNOWN` (returned when querying nodes not in registry) and `POLE_STATE_UNREACHABLE_VIA_PARENT`.
  * **Cascade Failure Classification:** In `checkHealth()`, if a pole's elapsed time exceeds `offlineTimeoutSec`, its registered `primaryParent` is inspected. If the parent is offline, the pole is classified as `POLE_STATE_UNREACHABLE_VIA_PARENT` with explicit reason `Parent 0x%04X Offline`.
  * **Bounded Event Queue:** Implemented 16-event circular FIFO alert queue (`GwAlertEvent alertQueue[GW_ALERT_QUEUE_CAPACITY]`). Safe overflow prevents queue corruption or alert loss.
  * **Cooldown & Deduplication:** Per-pole cooldown (`alertCooldownSec`, configurable 10-60s) suppresses alert storms during flapping links.

---

### 2.4 P0: Telegram Menu Ergonomics (Strict <= 6 Buttons) & JSON Verification
* **Identified Defect:**
  * Several submenus (feeder list, schedule feeder selector, dashboard) exceeded Telegram's recommended mobile layout limit of 6 buttons per inline keyboard.
* **Production Code Repair (`Gateway_ESP32S3/GwTelegramBot.cpp`, `GwTelegramBot.h`):**
  * Strict pagination applied across all menus:
    * `showPoleFeederList()`: Page 0 displays Feeders 1..4 (4 buttons) + Next + Back = 6 buttons. Page 1 displays Feeders 5..6 (2 buttons) + Prev + Back = 4 buttons.
    * `showScheduleFeederSelect()`: Page 0 displays ALL Feeders + F1..F4 + Next = 6 buttons. Page 1 displays F5..F6 + Prev + Back = 4 buttons.
    * `showPoleDetail()`: 3 rows of 2 buttons = exactly 6 buttons.
  * **Authorization Enforcement:** Unauthorized users attempting callbacks or slash commands are strictly rejected with `"⛔ Access Denied"`.
  * **JSON Keyboard Verification:** Implemented `parseTelegramInlineKeyboard()` in test runner to parse actual emitted `InlineKeyboardMarkup` JSON, validating row counts, total button count `<= 6`, and callback bounds.

---

### 2.5 P0: Truthful Auxiliary Feedback & Metrology
* **Identified Defect:**
  * Unpopulated auxiliary feedback lines were previously displayed as `OPEN` or `HEALTHY`. Missing ADC channels were rendered as `0.0V` or `0.0A`.
* **Production Code Repair (`MSCBShared/NetworkTypes.h`, `FieldPole_ESP32/PoleFeederManager.cpp`, `PoleMetrology.cpp`, `Gateway_ESP32S3/GwTelegramBot.cpp`):**
  * Defined typed enums: `ContactorState` (`CLOSED`, `OPEN`, `UNAVAILABLE`), `McbState` (`HEALTHY`, `TRIPPED`, `UNAVAILABLE`), and `MeasurementValidity` (`MEAS_VALID`, `MEAS_SENSOR_UNAVAILABLE`).
  * In `PoleFeederManager`: When physical MCP23S17 / contactor feedback lines are disconnected or unconfigured, `getContactorState()` returns `CONTACTOR_UNAVAILABLE` and `getMcbState()` returns `MCB_UNAVAILABLE`. Command ACKs return `FEEDER_ACK_FEEDBACK_NOT_CONNECTED`.
  * In `PoleMetrology`: If ADC chips are missing or DRDY lines fail synchronization, phase voltages and currents return `NAN`, and validity is marked `MEAS_SENSOR_UNAVAILABLE`.
  * Telegram UI renders truthful text: `FEEDBACK NOT CONNECTED` and `SENSOR UNAVAILABLE` (never fabricated numbers).

---

### 2.6 P0: Hardware Physical Authorization & Relay Safety
* **Identified Defect:**
  * `setLocalPhysicalAuthorization()` in R3.2 was a software setter not connected to a physical hardware interlock.
* **Production Code Repair (`FieldPole_ESP32/PoleActuators.cpp`, `PoleActuators.h`, `PoleBoardPins.h`):**
  * **Dedicated Physical Jumper Pin:** Assigned dedicated physical input `POLE_PHYSICAL_AUTH_PIN` on GPIO 39. Configured active-low (physical jumper grounded = LOW).
  * **Debounce:** Requires 3 consecutive verified active samples before setting `physicalPinState = true`.
  * **Auto-Expiry:** Authorization session is bounded to a maximum of 5 minutes (`POLE_PHYSICAL_AUTH_TIMEOUT_MS = 300000UL`), after which authorization automatically expires.
  * **Emergency Revocation:** If the physical jumper is pulled while any relay is energized, authorization is revoked immediately and `emergencyShutdownAll("PHYSICAL_AUTH_LOST")` de-energizes all 6 relay coils instantly.
  * **Production Safety Lock Preserved:** Compile-time macro `MSCB_RELAYS_LOCKED_DISABLED = 1` remains strictly enforced. In production mode, relays cannot be energized remotely. Isolated DC bench testing requires explicit `benchTestOverride` combined with verified physical GPIO 39 authorization.

---

### 2.7 P1: Boot Pin Configuration Validation & Fallback
* **Identified Defect:**
  * If NVS pin configuration was corrupted, the system could attempt to drive unvalidated pins or fail silently.
* **Production Code Repair (`FieldPole_ESP32/FieldPole_ESP32.ino`, `PoleBoardPins.h`):**
  * Boot sequence loads `PoleHardwarePinConfig` from NVS and validates CRC32 and pin rules (rejects SPI flash GPIO 6..11, input-only GPIO 34..39 for output, and peripheral collisions).
  * If CRC32 is invalid or pin validation fails, firmware automatically rolls back to default factory mapping (`getDefaultHardwarePinConfig()`) and applies safe active-low pin initialization.

---

## 3. Summary of Source File Modifications

| Component | File Path | Nature of Repair |
|---|---|---|
| **Shared** | `MSCBShared/NetworkTypes.h` | Added `ContactorState`, `McbState`, `MeasurementValidity` enums and string formatters |
| **Shared** | `MSCBShared/Protocol.h` | Added `auxFeedbackValid` and `mcbStatusValid` fields to `PktHeartbeatPingPayload` |
| **Field Pole** | `FieldPole_ESP32/PoleBoardPins.h` | Defined `POLE_PHYSICAL_AUTH_PIN 39` and `POLE_PHYSICAL_AUTH_TIMEOUT_MS 300000UL` |
| **Field Pole** | `FieldPole_ESP32/PoleActuators.h/.cpp` | GPIO 39 physical auth with 3-sample debounce, 5-min auto-expiry, emergency trip on jumper loss, bench override |
| **Field Pole** | `FieldPole_ESP32/PoleFeederManager.h/.cpp` | Truthful feedback evaluation, returns `FEEDER_ACK_FEEDBACK_NOT_CONNECTED` when unpopulated |
| **Field Pole** | `FieldPole_ESP32/PoleMeshRouter.h/.cpp` | Self-loop drop, sequence/hop limit validation, cycle prevention, real child tracking table with timeouts |
| **Field Pole** | `FieldPole_ESP32/PoleMetrology.h/.cpp` | Returns `NAN` and `MEAS_SENSOR_UNAVAILABLE` on disconnected ADC chips |
| **Field Pole** | `FieldPole_ESP32/FieldPole_ESP32.ino` | Boot pin config loading with CRC32 check and automatic fallback to default profile |
| **Gateway** | `Gateway_ESP32S3/GwHeartbeatMonitor.h/.cpp` | `POLE_STATE_UNKNOWN`, bounded 16-event alert queue, cooldown, true cascade failure detection |
| **Gateway** | `Gateway_ESP32S3/GwTelegramBot.h/.cpp` | Strict <= 6 button menus, pagination, format string repairs, unauthorized user rejection |
| **Gateway** | `Gateway_ESP32S3/GwNodeRegistry.h/.cpp` | Stored aux and MCB validity flags in `RegisteredPole`; NaN handling in metrology |
| **Tests** | `Tests/ArduinoMock.h` | Converted static mock pin states and simulated time to shared globals across translation units |
| **Tests** | `Tests/test_r3_real_hardware_functional_repair.cpp` | Comprehensive test runner spanning Sections A through X (225 passing tests) |

---

## 4. Verification & Audit Results

1. **Native Test Execution:**
   * Executable: `test_r3.exe` (MinGW g++ C++11)
   * Result: **225 PASSED, 0 FAILED** (100% Pass Rate, Exit Code: 0)
2. **Gateway Firmware Compilation:**
   * Command: `arduino-cli compile --fqbn esp32:esp32:esp32s3 MSCB_SmartPole_System_R1\Gateway_ESP32S3`
   * Result: **SUCCESS (Exit Code: 0)**
   * Flash: 1,133,245 bytes (86%), RAM: 93,528 bytes (28%)
3. **Field Pole Firmware Compilation:**
   * Command: `arduino-cli compile --fqbn esp32:esp32:esp32 MSCB_SmartPole_System_R1\FieldPole_ESP32`
   * Result: **SUCCESS (Exit Code: 0)**
   * Flash: 329,956 bytes (25%), RAM: 28,292 bytes (8%)
4. **Git Tree Integrity:**
   * Active Branch: `r3.2.1-critical-mesh-truth-repair`
   * Legacy Directories: `Sender/` and `Resivers/` verified untouched (0 modifications).
