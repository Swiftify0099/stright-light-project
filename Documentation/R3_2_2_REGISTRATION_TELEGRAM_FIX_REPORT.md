# MSCB SMART POLE SYSTEM — R3.2.2 REGISTRATION & TELEGRAM FIX REPORT

**Date:** November 25, 2026  
**Baseline Branch:** `r3.2.1-critical-mesh-truth-repair`  
**Baseline Commit:** `65cccfa715d9d4f85ee829b1ab7c343e5093007c`  
**Release Branch:** `r3.2.2-registration-telegram-reliability`  
**Target Microcontrollers:** ESP32-S3 (Gateway Coordinator) & ESP32-WROOM-32 (Field Pole Nodes)  
**Security Status:** Fail-Closed Cryptographic Authentication (HMAC-SHA256) & Hardware Production Relay Lock Enforced  

---

## 1. Executive Summary

This report documents the architectural remediation and production verification of two critical issues identified in production code audits:
1. **Field Pole Registration Reliability Failure**: Field poles failed to complete the onboarding handshake reliably due to unverified radio transmissions, missing Gateway `REG_RESP_PENDING_APPROVAL` acknowledgments, and failure to reinitialize the runtime mesh router identity without a manual power reboot.
2. **Gateway Telegram Notification & Alert Spam**: Excessive and repeated alerts sent to administrators due to lack of deduplication for recurring revoked device heartbeats, unthrottled cascade outages, and redundant dispatches to duplicate admin chat IDs.

All fixes were implemented in production C++ firmware across `FieldPole_ESP32`, `Gateway_ESP32S3`, and `MSCBShared`. Verification confirmed **100% pass rate** across both native unit test suites:
- `test_r3_2_2_registration_telegram.cpp`: **69 PASSED, 0 FAILED** (100% Pass)
- `test_r3_real_hardware_functional_repair.cpp`: **271 PASSED, 0 FAILED** (100% Pass)
- **Total Assertions Verified:** **340 PASSED, 0 FAILED**

---

## 2. Root Cause Analysis & Detailed Fixes

### 2.1 P0 — Field Pole Registration Flow Repairs

#### Defect 1: Unverified LoRa Transmission in `triggerRegistrationRequest()`
- **Root Cause:** In `PoleRegistrationClient.cpp`, `triggerRegistrationRequest()` invoked `poleMesh.sendUplinkPacket(...)` but ignored its boolean return status, immediately transitioning to `REG_CLIENT_REQUEST_SENT`. Unprovisioned devices sent an all-zero `authTag` and bypassed crypto validation.
- **Remediation:**
  - Added pre-transmission validation: LoRa radio detection (`poleRadio.isDetected()`), unicast non-zero MAC check (`mscbIsValidMac()`), and cryptographic key availability check (`cmdSecurity.isKeyProvisioned()`). If the network key is unprovisioned, the node enters `REG_CLIENT_REGISTRATION_ERROR` with reason `REG_ERR_AUTH_KEY_UNAVAILABLE` (fail-closed).
  - Only update state to `REG_CLIENT_REQUEST_TRANSMITTED` / `REG_CLIENT_WAITING_FOR_GATEWAY` if `poleMesh.sendUplinkPacket()` returns `true`.
  - Implemented exponential backoff on transmission failures (5s -> 10s -> 20s -> max 60s).

#### Defect 2: Gateway Dropped `REG_RESP_PENDING_APPROVAL` Acknowledgment
- **Root Cause:** In `GwMeshCoordinator::processIncomingWireFrame()`, the gateway only transmitted responses when `status != REG_RESP_PENDING_APPROVAL`. Unapproved field poles received zero feedback, causing rapid retries every 12 seconds.
- **Remediation:**
  - Removed the exclusion check. Gateway now dispatches an explicit, authenticated `REG_RESP_PENDING_APPROVAL` wire frame carrying the node's MAC address, session token, and correlated nonce.
  - Upon receiving this response, `PoleRegistrationClient` transitions to `REG_CLIENT_PENDING_APPROVAL` and halts rapid 12-second retries, entering a quiet 60-second backoff state while awaiting administrator review.

#### Defect 3: Runtime Mesh Identity Reinitialization Without Reboot
- **Root Cause:** `PoleMeshRouter::begin()` initialized `myNodeId` at boot time from NVS (`0xFFFE` unassigned). When a pole was approved and assigned an ID (e.g., `0x0002`), `poleStorage.saveRegistration()` persisted the ID to NVS, but `PoleMeshRouter.myNodeId` remained unchanged at runtime. The router continued transmitting packets under the unassigned address until a physical power cycle.
- **Remediation:**
  - Implemented `PoleMeshRouter::setNodeIdentity(uint16_t newId, uint16_t newParent, uint8_t newRank)`.
  - When `PoleRegistrationClient::handleRegisterResponse()` receives `REG_RESP_APPROVED`, it immediately calls `setNodeIdentity()`, updating `myNodeId`, `primaryParent`, and `myRank`, clearing stale duplicate cache entries, and updating self-loop filters.
  - The node transitions to `REG_CLIENT_WAITING_FOR_HANDSHAKE` and transmits an immediate authenticated heartbeat to confirm the handshake with the Gateway.

#### Defect 4: Reverse Routing Breadcrumb for Multi-Hop Registration Responses
- **Root Cause:** In multi-hop mesh deployments, join requests from unassigned child nodes were forwarded by relay nodes. When Gateway sent responses to unassigned addresses, intermediate nodes dropped the frames.
- **Remediation:**
  - Added a bounded reverse-routing breadcrumb table in `PoleMeshRouter` (`ReverseRouteEntry reverseRouteTable[16]`) tracking MAC, session token, and return hop.
  - Relay nodes record reverse routes when forwarding join requests.
  - When a `PKT_REGISTER_RESPONSE` arrives, intermediate relays forward the packet down the recorded reverse hop towards the child.
  - The destination child node matches the response payload MAC address directly against its hardware MAC (`memcmp(regResp->macAddress, poleRegClient.getMacAddress(), 6) == 0`), accepting the packet locally even before its assigned node ID is active.

#### Defect 5: Handshake Confirmation on First Authenticated Heartbeat
- **Root Cause:** Approving a pending pole in Telegram immediately marked it active without confirming end-to-end RF delivery. If the response packet was lost in transit, the gateway treated the pole as active while the pole remained unregistered.
- **Remediation:**
  - Approving a pending pole sets its registry state to `POLE_STATUS_PENDING_ACK`.
  - `GwMeshCoordinator` listens for the first authenticated heartbeat from the approved pole ID matching its registered MAC address, cryptographic HMAC signature, and session token.
  - Only upon receipt of this authenticated heartbeat does the gateway invoke `gwRegistry.confirmPoleHandshake()`, transitioning the pole to `POLE_STATUS_ACTIVE`.

---

### 2.2 P0 — Gateway Telegram Notification & Spam Mitigation

#### Defect 6: Reappeared Revoked Pole Alert Spam
- **Root Cause:** Whenever a revoked pole transmitted a heartbeat or reappearance alert, the gateway invoked `setReappearedRevokedAlert()`, overwriting a single buffer and broadcasting repeated Telegram messages on every cycle.
- **Remediation:**
  - Implemented an incident-based deduplication tracker keyed by MAC address in `GwNodeRegistry` (`revokedIncidents[GW_MAX_REVOKED_INCIDENTS]`).
  - The first appearance of a revoked MAC creates an active incident and enqueues a single Telegram alert.
  - Subsequent heartbeats (tested up to 100 continuous pings) increment internal occurrence counters and update `lastSeenEpoch` without dispatching additional Telegram messages.
  - Alerts are re-armed only upon administrative acknowledgment or a 1-hour quiet period.

#### Defect 7: Unthrottled Outage Alert Bursts & Cascade Storms
- **Root Cause:** In a parent failure event, all descendant poles timed out simultaneously, emitting up to 50 individual alerts in quick succession, triggering Telegram API rate limits (HTTP 429).
- **Remediation:**
  - Implemented a consolidated status digest batching system in `GwTelegramBot::processNotificationQueue()`.
  - Concurrent alerts are batched into a single formatted digest message (up to 12 events per message) with a minimum 3-second throttle between dispatches.
  - Defers updating `reportedState` until the alert is successfully enqueued in `GwHeartbeatMonitor`, preventing lost state transitions on full queues.

#### Defect 8: Duplicate Admin Recipient Messages
- **Root Cause:** `GwTelegramBot::broadcastAdminAlert()` looped through the admin allowlist without checking for duplicate chat IDs between `primaryAdminChatId` and allowlist entries.
- **Remediation:**
  - Added unique recipient deduplication using an array of visited chat IDs, ensuring each administrator receives exactly one message per notification event.

---

## 3. Verification & Test Evidence

### 3.1 Test Suite Summary
| Test Suite | File | Tests Run | Passed | Failed | Status |
|---|---|---|---|---|---|
| **R3.2.2 Registration & Telegram** | `test_r3_2_2_registration_telegram.cpp` | 69 | 69 | 0 | **PASSED (100%)** |
| **R3 Real Hardware Functional** | `test_r3_real_hardware_functional_repair.cpp` | 271 | 271 | 0 | **PASSED (100%)** |
| **Total** | | **340** | **340** | **0** | **100% VERIFIED** |

### 3.2 Key Verified Sections
1. **Section AG (Registration Request Reliability):** Unicast MAC validation, radio readiness check, fail-closed crypto check, transmission verification, exponential backoff.
2. **Section AH (Pending Approval Flow):** Gateway `REG_RESP_PENDING_APPROVAL` dispatch, nonce correlation, 60s retry backoff, one-time Telegram admin notification.
3. **Section AI (Approval & Runtime Mesh Identity):** Immediate `PoleMeshRouter::setNodeIdentity()` update without power reboot, frame source truth, multi-hop return routing, first heartbeat handshake confirmation.
4. **Section AJ (Rejection & Revocation Safety):** Fail-closed blacklist, `REG_RESP_REJECTED_REVOKED` response, instant relay lockout.
5. **Section AK (Telegram Deduplication):** 100 repeated revoked heartbeats produce exactly 1 Telegram alert, digest consolidation of concurrent events, admin recipient deduplication.
6. **Section AL (Telegram Bot Lifecycle):** Clean offline/online transitions without notification storms, `/status`, `/pending`, and menu navigation regressions.

---

## 4. Hardware Safety & Compliance Statement
- **Production Relay Lockout:** Physical relay control remains permanently disabled by default (`MSCB_RELAYS_LOCKED_DISABLED = 1`, `MSCB_COMMISSIONING_LVDC_PROFILE = 0` on `ARDUINO`).
- **Cryptographic Integrity:** Zero unauthenticated or all-zero auth tags accepted. Fail-closed security enforced across all registration and command flows.
- **No Mains Connection:** Verified for low-voltage DC bench testing only. High-voltage 230V/415V mains connections remain strictly prohibited during development.
