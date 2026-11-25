# MSCB SMART POLE SYSTEM — R3.2.2 IMPLEMENTATION PLAN
**FIELD POLE REGISTRATION RELIABILITY & TELEGRAM MESSAGE SPAM MITIGATION**

**Document Version:** 1.0.0  
**Baseline Branch:** `r3.2.1-critical-mesh-truth-repair`  
**Baseline Commit:** `65cccfa715d9d4f85ee829b1ab7c343e5093007c`  
**Target Release Branch:** `r3.2.2-registration-telegram-reliability`  
**Author:** Senior ESP32 Firmware Engineer & Security Auditor  

---

## 1. Executive Summary & Defect Scope

This implementation plan details the architectural and source-code repairs for the **MSCB Smart Pole System (R3.2.2)**. The release targets two critical production blockers identified in code audits:
1. **Field Pole Registration Reliability Failure**: Poles remain trapped in an unapproved or retrying loop, failure to verify RF packet transmission, absence of Gateway `REG_RESP_PENDING_APPROVAL` acknowledgments, out-of-order state mutations, and inability to update runtime mesh router identity (`myNodeId`) without requiring a physical reboot.
2. **Gateway Telegram Notification & Alert Spam**: Flooding administrators with repeated notifications for the same revoked device heartbeat or reappearance packet, lack of alert deduplication on ongoing incidents, single-buffer overwrites, lack of burst throttling during multi-pole offline cascades, and missing automated one-time registration notifications for newly discovered field poles.

---

## 2. Root Cause Analysis Matrix

| ID | Component | Defect & Root Cause | Architectural Repair |
|---|---|---|---|
| **RCA-322-01** | `PoleRegistrationClient.cpp` | `triggerRegistrationRequest()` calls `sendUplinkPacket(...)` and unconditionally sets state to `REG_CLIENT_REQUEST_SENT` without checking LoRa radio readiness or send result. All-zero `authTag` sent. | Verify `poleRadio.isDetected()`, check `sendUplinkPacket()` return value, compute HMAC-SHA256 authentication tag via `CommandSecurity`, fail closed if key unprovisioned, implement exponential retry backoff. |
| **RCA-322-02** | `GwMeshCoordinator.cpp` | `processIncomingWireFrame()` explicitly excludes `REG_RESP_PENDING_APPROVAL` from transmitting an RF response (`if (status != REG_RESP_PENDING_APPROVAL)`). Unapproved poles never receive acknowledgment and continuously resend every 12s. | Gateway immediately transmits an authenticated `REG_RESP_PENDING_APPROVAL` wire frame correlating MAC, session token, and nonce. Field Pole enters `PENDING_ADMIN_APPROVAL` and halts rapid join retries. |
| **RCA-322-03** | `PoleMeshRouter.cpp`, `PoleRegistrationClient.cpp` | `PoleMeshRouter::begin()` initializes `myNodeId` at boot. Upon receiving approval, `poleStorage.saveRegistration()` persists new ID, but `PoleMeshRouter.myNodeId` is never updated at runtime. Node continues transmitting frames with old/default ID until manual reboot. | Implement `PoleMeshRouter::setNodeIdentity(newId, newParent, newRank)` invoked immediately upon receiving `REG_RESP_APPROVED`. Updates router identity, resets self-loop prevention, flushes duplicate cache, and triggers first authenticated heartbeat handshake without reboot. |
| **RCA-322-04** | `GwTelegramBot.cpp`, `GwNodeRegistry.cpp` | `approvePending()` removes pending entry from array before delivery is confirmed. `GwTelegramBot` accesses pointer `p` after memory shift, and informs admin response was dispatched without checking `sendRegisterResponse()` return status. | Cache draft data before array mutation, verify actual radio transmission result, maintain `POLE_STATUS_PENDING_ACK` in registry, retry unacknowledged approval packets, and transition to `POLE_STATUS_ACTIVE` only upon receiving the first authenticated heartbeat handshake from assigned Pole ID and MAC. |
| **RCA-322-05** | `GwNodeRegistry.h`, `GwTelegramBot.cpp` | `PendingRegistration::notifiedAdmin` is declared and initialized to `false`, but never checked or used by `GwTelegramBot`. Admins receive no alert when new poles request registration. | Implement `hasPendingRegistrationAlert()`, `getNextPendingRegistrationAlertText()`, and `markPendingRegistrationNotified()`. Gateway issues exactly one Telegram notification per new pending MAC. Repeated retries from same MAC update metrics without duplicate notifications. Retained across offline intervals. |
| **RCA-322-06** | `GwNodeRegistry.cpp`, `GwTelegramBot.cpp` | Gateway calls `setReappearedRevokedAlert()` on every heartbeat and reappearance packet from a revoked pole. Single static buffer `reappearedAlertBuf` is overwritten and spammed on every heartbeat cycle. | Implement incident-based deduplication tracker keyed by MAC and event type. Subsequent heartbeats update diagnostic counters and timestamps only. One alert emitted per incident. Incident rearmed only after explicit reset or 1-hour quiet period. |
| **RCA-322-07** | `GwHeartbeatMonitor.cpp`, `GwTelegramBot.cpp` | Cascade pole outages generate bursts of up to 50 individual Telegram messages, hitting rate limits and flooding admin chats. `queueAlert()` drops alerts when full but updates `reportedState`, permanently losing transitions. | Return explicit bool from `queueAlert()`, defer `reportedState` update until successfully queued. In `GwTelegramBot`, throttle alerts to at most 1 batch per 3 seconds, grouping concurrent outages into a consolidated digest message. |
| **RCA-322-08** | `GwTelegramBot.cpp` | `broadcastAdminAlert()` iterates allowlist without deduplicating identical chat IDs or checking against `primaryAdminChatId`. Duplicate messages sent to identical chat IDs. | Deduplicate all admin destinations into a unique set before dispatching messages. Ensure zero duplicate sends per alert. |

---

## 3. Detailed Component Design & Specifications

### 3.1 Field Pole Registration Client State Machine (`FieldPole_ESP32`)
Introduce explicit state machine in `StorageTypes.h` and `PoleRegistrationClient.h`:
```cpp
enum NodeRegClientState : uint8_t {
  REG_CLIENT_UNREGISTERED           = 0,
  REG_CLIENT_JOIN_REQUEST_SENT      = 1,
  REG_CLIENT_PENDING_APPROVAL       = 2,
  REG_CLIENT_REGISTERED             = 3,
  REG_CLIENT_REVOKED_LOCKED         = 4,
  REG_CLIENT_REJECTED               = 5,
  REG_CLIENT_FAULT                  = 6,
  REG_CLIENT_DISCOVERING_PARENT     = 7,
  REG_CLIENT_READY_TO_REQUEST       = 8,
  REG_CLIENT_REQUEST_TRANSMITTED    = 9,
  REG_CLIENT_WAITING_FOR_GATEWAY    = 10,
  REG_CLIENT_APPROVAL_RECEIVED      = 11,
  REG_CLIENT_WAITING_FOR_HANDSHAKE  = 12,
  REG_CLIENT_REGISTRATION_ERROR     = 13
};
```
- **Radio Readiness Check**: Verify `poleRadio.isDetected()` prior to transmission. If radio fails, transition to `REG_CLIENT_REGISTRATION_ERROR`.
- **Fail-Closed Authentication**: Check `cmdSecurity.isKeyProvisioned()`. If false, fail with `REGISTRATION_AUTH_KEY_UNAVAILABLE`.
- **Transmission Verification**: Only set state to `REG_CLIENT_REQUEST_TRANSMITTED` / `REG_CLIENT_WAITING_FOR_GATEWAY` if `poleMesh.sendUplinkPacket()` returns `true`.
- **Exponential Backoff**: If transmission fails, retry interval doubles (5s -> 10s -> 20s -> max 60s).
- **Pending State Behavior**: Upon receipt of `REG_RESP_PENDING_APPROVAL`, node enters `REG_CLIENT_PENDING_APPROVAL`. Periodic retries back off to 60s quiet probe or await Gateway downlink.

### 3.2 Gateway Pending Approval & Acknowledgment (`Gateway_ESP32S3`)
- In `GwMeshCoordinator::processIncomingWireFrame()`:
  - Remove restriction `status != REG_RESP_PENDING_APPROVAL`.
  - Always transmit response frame when `status == REG_RESP_PENDING_APPROVAL` with `sessionToken`, `nonce`, and authenticated tag.
- In `GwNodeRegistry::handleJoinRequest()`:
  - Verify payload HMAC-SHA256 authentication tag if key provisioned.
  - Return `REG_RESP_PENDING_APPROVAL`.

### 3.3 Runtime Mesh Identity Reinitialization without Reboot
- In `PoleMeshRouter`:
  ```cpp
  bool setNodeIdentity(uint16_t newId, uint16_t newParent, uint8_t newRank);
  ```
  - Updates `myNodeId = newId`.
  - Updates `primaryParent = newParent`, `myRank = newRank`.
  - Flushes stale duplicate cache entries.
  - Updates self-loop prevention filter.
- In `PoleRegistrationClient::handleRegisterResponse`:
  - When `resp.status == REG_RESP_APPROVED`, validate MAC, nonce, session token, and auth tag.
  - Call `poleStorage.saveRegistration(...)`.
  - Call `poleMesh.setNodeIdentity(resp.assignedPoleId, resp.assignedParent, rank)`.
  - Call `poleHeartbeat.sendHeartbeat()` to dispatch immediate first handshake ping to Gateway.

### 3.4 Multi-hop Registration Downlink Route
- In `PoleMeshRouter::handleReceivedWireFrame`:
  - Inspect `PKT_REGISTER_RESPONSE` frames. If payload MAC matches local MAC, deliver locally regardless of `header.dstAddress`.
  - If payload MAC does not match local MAC, forward downstream to `nextHop`.

### 3.5 One-Time Telegram Registration Notification
- In `GwNodeRegistry`:
  - Track `PendingRegistration::notifiedAdmin`.
  - Expose `hasPendingRegistrationAlert()` and `markPendingRegistrationNotified(idx)`.
- In `GwTelegramBot::service()`:
  - Check for unnotified pending registrations.
  - Dispatch formatted alert with inline instructions.
  - Mark `notifiedAdmin = true` upon verified transmission.
  - Retain alert across Wi-Fi reconnects.

### 3.6 Telegram Revocation & Heartbeat Alert Spam Mitigation
- In `GwNodeRegistry`:
  - Introduce `RevokedIncidentTracker` with MAC, occurrences, lastSeen, lastAlertEpoch, and activeIncident flag.
  - `setReappearedRevokedAlert()` triggers an alert event ONLY on initial incident detection or after a 3600-second rearm window.
  - Subsequent heartbeats from revoked nodes increment incident counter and log locally without Telegram notification.
- In `GwHeartbeatMonitor`:
  - `queueAlert()` returns `bool`.
  - `checkHealth()` only advances `reportedState` when `queueAlert()` succeeds.
- In `GwTelegramBot`:
  - Throttle broadcasts to 1 message every 3000 ms.
  - Batch concurrent offline/recovery events into a consolidated digest.
  - Deduplicate admin chat IDs.

---

## 4. Test Suite Strategy (AG through AL)

New executable test runner `test_r3_2_2_registration_telegram.cpp` covering:
- **Section AG**: Registration Request Reliability (MAC, radio readiness, TX failure, nonce, auth tag).
- **Section AH**: Pending Approval Flow (Gateway response, state change, backoff, one-time Telegram alert, persistence).
- **Section AI**: Approval & Runtime Identity (ID assignment, `setNodeIdentity` without reboot, frame source truth, first handshake).
- **Section AJ**: Rejection & Revocation (rejection enforcement, revoked pole relay lockout, blacklist check).
- **Section AK**: Telegram Message Deduplication (100 heartbeats = 1 alert, digest batching, recipient dedup, rate limiting).
- **Section AL**: Telegram Service Regression (reconnect storm prevention, command dispatch, wizard flow).

---

## 5. Build & CI Verification Plan
1. Compile and execute native test suite `test_r3_2_2.exe` via MinGW `g++`.
2. Compile and execute existing `test_r3.exe` to guarantee zero regressions.
3. Verify firmware compilation for Gateway (`esp32:esp32:esp32s3`) via `arduino-cli`.
4. Verify firmware compilation for Field Pole (`esp32:esp32:esp32`) via `arduino-cli`.
5. Add GitHub Actions workflow `.github/workflows/r3-2-2-registration-telegram.yml`.
