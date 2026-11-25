# MSCB SMART POLE SYSTEM — R3.2.2 RESIDUAL DEFECT FIX REPORT
**CRITICAL RESIDUAL FIXING, PROTOCOL HARDENING & SECURITY REMEDIATION**

**Document Version:** 1.0.0  
**Baseline Branch:** `r3.2.1-critical-mesh-truth-repair`  
**Baseline HEAD SHA:** `65cccfa715d9d4f85ee829b1ab7c343e5093007c`  
**Target Release Branch:** `r3.2.2-critical-residual-fixes`  
**Target Hardware:** ESP32-WROOM-32 (Field Pole) | ESP32-S3 (Gateway) | SX1278 LoRa @ 433.92 MHz  

---

## 1. Executive Summary

MSCB Release R3.2.2 addresses all residual defects identified in the R3.2.1 baseline across routing integrity, hop identity authentication, alert queue resilience, actuator production locking, physical authorization electrical safety, dynamic pin persistence, TLS communication, and metrology reporting truthfulness.

Every repair has been implemented at the production firmware level, verified using native C++ test suites (271 tests passing, 0 failing), and confirmed to build without warnings or errors on official `arduino-cli` compilers for ESP32 and ESP32-S3.

---

## 2. Summary of Residual Defects & Technical Resolutions

| Defect ID | Severity | Module / Files | Description | Resolution Status |
|---|---|---|---|---|
| **DEF-322-01** | **P0** | `PoleMeshRouter.cpp/.h` | Mesh routing rank overflow, equal/higher rank acceptance, cycle loops, nextHop forwarding validation | **RESOLVED & VERIFIED** |
| **DEF-322-02** | **P0** | `Protocol.h`, `CommandSecurity.cpp/.h`, `PoleMeshRouter.cpp/.h`, `GwMeshCoordinator.cpp` | Grandchild misidentification as direct child, unauthenticated hop identity | **RESOLVED & VERIFIED** |
| **DEF-322-03** | **P0** | `GwHeartbeatMonitor.cpp/.h`, `GwTelegramBot.cpp` | Gateway alert queue dropping critical child offline/recovery events upon overflow | **RESOLVED & VERIFIED** |
| **DEF-322-04** | **P0** | `BuildConfig.h`, `PoleActuators.cpp/.h` | Production relay safety lock bypassable via runtime bench test override | **RESOLVED & VERIFIED** |
| **DEF-322-05** | **P0** | `PoleBoardPins.h`, `PoleActuators.cpp/.h` | GPIO 39 input-only floating vulnerability, debounce count bypass | **RESOLVED & VERIFIED** |
| **DEF-322-06** | **P1** | `FieldPole_ESP32.ino`, `PoleActuators.h`, `PoleRadioDriver.cpp/.h`, `PoleMetrology.cpp/.h` | Dynamic pin configuration limited to relays, overwriting on boot | **RESOLVED & VERIFIED** |
| **DEF-322-07** | **P1** | `GwTelegramCert.h`, `GwTelegramBot.cpp/.h`, `GwWiFiManager.cpp`, `GwConfig.h` | Insecure TLS (`client.setInsecure()`), missing Root CA verification, untrusted admin IDs | **RESOLVED & VERIFIED** |
| **DEF-322-08** | **P1** | `PoleMetrology.cpp/.h` | Metrology diagnostic report fabricating `0.0V`/`0.0A` when ADC disconnected | **RESOLVED & VERIFIED** |

---

## 3. Deep-Dive Defect Analysis & Source Code Remediation

### 3.1 DEF-322-01: Strict LoRa Mesh Routing and Monotonic Rank Safety
- **Root Cause:**
  1. `myRank = neighbors[bestIdx].rank + 1` could overflow when `rank == 255`, wrapping back to 0.
  2. Equal or higher rank candidate parents were not strictly rejected in `updateNeighborsFromBeacon`.
  3. Hop limits (`maxHops == 0` or `maxHops > 6`) were insufficiently checked.
  4. Uplink unicast frames were forwarded by bystander nodes whose ID did not match `nextHop`.
- **Implementation Details:**
  - Added monotonic rank safety checks: Gateway rank is fixed at `0`, connected field poles must have `1 <= rank <= 6`.
  - Rejection of rank values `>= MESH_MAX_HOPS`, boundary values `254` and `255`, and candidates with `rank >= myRank`.
  - Transitive cycle loop check rejecting candidate parents pointing back to this node or any known descendant.
  - Strict nextHop checking: Bystander nodes drop frames where `nextHop != 0 && nextHop != broadcast && nextHop != myNodeId` and increment `packetsDroppedRouting`.
  - Primary and backup parents revalidated independently to ensure they are strictly distinct (`primaryParent != backupParent`).

### 3.2 DEF-322-02: Authenticated Immediate-Hop Identity & Direct Child Tracking
- **Root Cause:**
  - Multi-hop frames retain their original `srcAddress`. When a grandchild packet was relayed by a direct child, the parent inspected `srcAddress` and registered the grandchild as a direct child.
- **Implementation Details:**
  - Bumped `MSCB_PROTOCOL_VERSION` to `0x02`.
  - Expanded `MeshPacketHeader` (packed) to include `uint16_t previousHopAddress` and `uint32_t hopAuthToken`.
  - Added cryptographic hop authentication: `computeHopAuthToken(prevHop, nextHop, seq, type)` using HMAC-SHA256 over immediate routing metadata.
  - In `handleReceivedWireFrame`: Verified `hopAuthToken`. Grandchild frames (`srcAddress != previousHopAddress`) update descendant routing and touch existing children, but NEVER create new direct child records.
  - Broadcast nextHop is excluded from proving direct-child identity.
  - Implemented event propagation frames `PKT_ALERT_CHILD_OFFLINE` (0x15) and `PKT_ALERT_CHILD_RECOVERED` (0x16) from Field Pole to Gateway.

### 3.3 DEF-322-03: Gateway Alert Queue Non-Dropping Reliability & Pending Retry
- **Root Cause:**
  - In `GwHeartbeatMonitor`, when the alert queue was full (`count >= GW_ALERT_QUEUE_CAPACITY`), incoming alert events were dropped without retry, while `reportedState` was updated, permanently losing alerts.
- **Implementation Details:**
  - `queueAlert()` now returns an explicit `bool` indicating queue insertion success or failure.
  - `recordChildOfflineFromParent` and `recordChildRecoveredFromParent` verify queue insertion; if full, events are retained in a per-pole pending alert buffer (`hasPendingAlert() == true`).
  - Implemented `drainPendingAlerts()`: when queue space is freed (via `popNextAlert`), pending alerts are retried and inserted into the queue.

### 3.4 DEF-322-04: Permanent Production Relay Safety Lock
- **Root Cause:**
  - In R3.2.1, `areRelaysLockedDisabled()` checked `if (benchTestOverride && isLocalPhysicalAuthorized()) return false;` before `#if defined(MSCB_RELAYS_LOCKED_DISABLED)`, allowing runtime bypass in production firmware.
- **Implementation Details:**
  - Refactored `areRelaysLockedDisabled()`:
    ```cpp
    bool PoleActuators::areRelaysLockedDisabled() const {
    #if defined(MSCB_COMMISSIONING_LVDC_PROFILE) && (MSCB_COMMISSIONING_LVDC_PROFILE == 1)
        if (benchTestOverride && isLocalPhysicalAuthorized() && isDiagnosticPassed()) {
            return false;
        }
        return true;
    #elif defined(MSCB_RELAYS_LOCKED_DISABLED) && (MSCB_RELAYS_LOCKED_DISABLED != 0)
        return true;
    #else
        return relaysLockedDisabled;
    #endif
    }
    ```
  - In production builds (`MSCB_COMMISSIONING_LVDC_PROFILE == 0` and `MSCB_RELAYS_LOCKED_DISABLED == 1`), `areRelaysLockedDisabled()` unconditionally returns `true`.
  - `setRelaysLockedDisabled()` and `setBenchTestOverride()` are strict no-ops in production firmware.
  - Relay switching is physically and logically prohibited in production builds under all circumstances.

### 3.5 DEF-322-05: GPIO 39 Physical Authorization Electrical Design & Continuous Debounce
- **Root Cause:**
  - ESP32 GPIO 39 (Sensor_VN) is input-only without internal pull-up or pull-down resistors. An open jumper left the pin floating, vulnerable to RF noise.
  - Software debounce relied on simple loop iteration counts (`>= 3`) rather than continuous elapsed time.
- **Implementation Details:**
  - Specified mandatory external hardware conditioning: External 10 kOhm pull-up resistor to 3.3V, 100 nF ceramic filter capacitor to GND, 1 kOhm series resistor, Active-LOW physical jumper connecting pin to GND.
  - Fail-safe state: Open circuit or cut wire pulls pin to HIGH (locked / unauthorized).
  - Implemented continuous elapsed-time debounce (`>= 50ms`) combined with wrap-safe timeout (`millis() - grantedMs >= POLE_PHYSICAL_AUTH_TIMEOUT_MS`).
  - Removing the physical jumper instantly de-authorizes the session and latches an emergency shutdown if any relay is energized.

### 3.6 DEF-322-06: Complete Dynamic Pin Configuration (Option A)
- **Root Cause:**
  - Dynamic pin configuration previously only applied relay pins and was overwritten during boot by hardcoded constants. LoRa and Metrology drivers did not support runtime pin reassignment.
- **Implementation Details:**
  - Expanded `PoleRadioDriver`: Added `configurePins(nss, rst, dio0)` and accessors `getNssPin()`, `getRstPin()`, `getDio0Pin()`.
  - Expanded `PoleMetrology`: Added `configurePins(decoderEn, decoderA0, decoderA1, sync, drdyPins[3])`.
  - Updated `FieldPole_ESP32.ino`: Reads `PoleHardwarePinConfig` from NVS storage; if valid CRC and schema, applies dynamic pin configurations to Actuators, Radio, and Metrology BEFORE peripheral initialization; falls back safely to `getDefaultHardwarePinConfig()`.

### 3.7 DEF-322-07: Gateway Telegram TLS Hardening & Authorization
- **Root Cause:**
  - `client.setInsecure()` was called in 6 places, completely disabling TLS certificate validation.
  - Hardcoded test ID `123456789LL` existed alongside primary admin ID `5505296197LL`.
- **Implementation Details:**
  - Created `GwTelegramCert.h` containing DigiCert Global Root CA PEM certificate.
  - Removed all calls to `client.setInsecure()`.
  - Applied `client.setCACert(TELEGRAM_CA_ROOT_CERT)` across `GwTelegramBot` and `GwWiFiManager`.
  - Enforced NTP time synchronization verification (`time(nullptr) >= 1700000000`) before establishing TLS connections.
  - Enforced strict sender `from_id` allowlist check against `MSCB_TELEGRAM_ADMIN_ALLOWLIST`.

### 3.8 DEF-322-08: Metrology Diagnostic Truth & NaN Reporting
- **Root Cause:**
  - Disconnected ADC channels returned `0.0V` and `0.0A` instead of honest diagnostic tokens, masquerading hardware failure as zero load.
- **Implementation Details:**
  - Removed `0.0` substitution in `PoleMetrology.cpp`.
  - Disconnected or failed ADCs output IEEE 754 `NAN`.
  - Diagnostic formatting prints `"-NAN-"` and validity token `MEAS_SENSOR_UNAVAILABLE`.
