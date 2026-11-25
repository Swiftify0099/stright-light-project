# MSCB SMART POLE SYSTEM — R3.2.2 REGISTRATION FLOW TEST EVIDENCE

**Date:** November 25, 2026  
**Commit Branch:** `r3.2.2-registration-telegram-reliability`  
**Execution Environment:** Windows 11 x64, MinGW GCC/G++ 6.3.0, ESP32 Arduino Core 3.3.11  

---

## 1. Native Test Suite Execution Log (`test_r3_2_2.exe`)

### 1.1 Compilation
```powershell
g++ -O2 -std=c++11 -Wall -I. -IMSCB_SmartPole_System_R1/MSCBShared -IMSCB_SmartPole_System_R1/Gateway_ESP32S3 -IMSCB_SmartPole_System_R1/FieldPole_ESP32 -IMSCB_SmartPole_System_R1/Tests `
  MSCB_SmartPole_System_R1/Tests/test_r3_2_2_registration_telegram.cpp `
  MSCB_SmartPole_System_R1/MSCBShared/CommandSecurity.cpp `
  MSCB_SmartPole_System_R1/MSCBShared/ReplayProtection.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleStorageManager.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleActuators.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleMeshRouter.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleFeederManager.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleMetrology.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleTimeManager.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleRegistrationClient.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleRadioDriver.cpp `
  MSCB_SmartPole_System_R1/FieldPole_ESP32/PoleHeartbeatSender.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwNodeRegistry.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwHeartbeatMonitor.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwMeshCoordinator.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwTelegramBot.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwScheduleManager.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwWiFiManager.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwFirmwareManager.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwRadioDriver.cpp `
  MSCB_SmartPole_System_R1/Gateway_ESP32S3/GwNetworkQueue.cpp `
  -o MSCB_SmartPole_System_R1/Tests/test_r3_2_2.exe
```

### 1.2 Execution Output
```text
=====================================================================
  MSCB SMART POLE SYSTEM R3.2.2 - REGISTRATION & TELEGRAM VERIFICATION
=====================================================================

--- SECTION AG: REGISTRATION REQUEST RELIABILITY ---
  [PASS] AG.1 Multicast/broadcast MAC (01:00:5E:00:00:01) rejected
  [PASS] AG.2 All-zero MAC rejected
[REG_CLIENT] ❌ Registration failed: Invalid device MAC address!
  [PASS] AG.3 Failure reason recorded as REG_ERR_INVALID_MAC
[REG_CLIENT] ❌ Registration failed: LoRa radio not ready/detected!
  [PASS] AG.4 triggerRegistrationRequest fails when LoRa radio is not detected
  [PASS] AG.5 Failure reason recorded as REG_ERR_RADIO_UNAVAILABLE
  [PASS] AG.6 Unauthenticated all-zero authTag rejected
[REG_CLIENT] ❌ Registration failed closed: Authentication key unavailable!
  [PASS] AG.7 Registration fails closed when authentication key is unprovisioned
  [PASS] AG.8 Failure reason recorded as REG_ERR_AUTH_KEY_UNAVAILABLE
[REG_CLIENT] ⚠️ LoRa uplink send failed. State not marked transmitted; backing off.
  [PASS] AG.9 Failed radio transmission returns false
  [PASS] AG.10 Failed transmission does NOT create false REQUEST_SENT state
  [PASS] AG.11 Error reason recorded as REG_ERR_TX_FAILED
  [PASS] AG.12 Failed transmission triggers exponential backoff
[REG_CLIENT] 📡 Registration request transmitted successfully. Awaiting Gateway.
  [PASS] AG.13 Valid transmission transitions state to REG_CLIENT_REQUEST_TRANSMITTED
  [PASS] AG.14 Non-zero session token preserved across retries
  [PASS] AG.15 Correlated nonce increments monotonically
  [PASS] AG.16 Initial unapproved node router ID uses unassigned address 0xFFFE
  [PASS] AG.17 Gateway allocated pole ID 0x0002 does NOT collide with unassigned node

--- SECTION AH: PENDING APPROVAL FLOW ---
  [PASS] AH.1 Gateway accepts valid authenticated join request
  [PASS] AH.2 Gateway responds with explicit REG_RESP_PENDING_APPROVAL (never dropped!)
  [PASS] AH.3 Gateway pending response correlates exact MAC
  [PASS] AH.4 Gateway pending response correlates session token
  [PASS] AH.5 Gateway pending response carries valid HMAC-SHA256 signature
  [PASS] AH.6 No active Pole ID assigned prior to admin approval
  [PASS] AH.7 Pending registration record saved in NVS registry
[REG_CLIENT] ⏳ Gateway confirmed: PENDING_ADMIN_APPROVAL. Join retries halted.
  [PASS] AH.8 Field Pole transitions to REG_CLIENT_PENDING_APPROVAL
  [PASS] AH.9 Field Pole halts rapid 12-second join retries
  [PASS] AH.10 Retry interval backed off to >= 60 seconds
  [PASS] AH.11 Gateway enqueues exactly one Telegram notification for pending MAC
  [PASS] AH.12 hasPendingRegistrationAlert is true
  [PASS] AH.13 Alert text contains discovered MAC and signal metrics
  [PASS] AH.14 markPendingRegistrationNotified marks alert consumed
  [PASS] AH.15 Repeated requests from same MAC do NOT emit duplicate Telegram alerts
  [PASS] AH.16 Signal metrics updated in place without duplicate notifications
  [PASS] AH.17 Offline period preserves pending alert notification state

--- SECTION AI: APPROVAL & RUNTIME MESH IDENTITY ---
  [PASS] AI.1 Pending registration approved via approvePending
  [PASS] AI.2 Pending entry preserved until delivery confirmed
  [PASS] AI.3 Gateway response payload carries assigned Pole ID
  [PASS] AI.4 Radio transmission failure triggers retry backoff
  [PASS] AI.5 Field Pole rejects approval response with invalid session token
  [PASS] AI.6 Field Pole rejects approval response with corrupted authentication tag
[ACTUATOR] Safe LVDC commissioning profile active: contactor aux feedback initialized.
[REG_CLIENT] ✅ APPROVED (ID 0x0002, Par 0x0001, Rank 1). Awaiting first HB_ACK handshake...
  [PASS] AI.7 Field Pole accepts valid authenticated approval response
  [PASS] AI.8 Field Pole state becomes REG_CLIENT_REGISTERED
  [PASS] AI.9 Assigned Pole ID persisted in PoleStorageManager
[MESH] Runtime Node Identity updated: 0xFFFE -> 0x0002 (Parent: 0x0001, Rank: 1)
  [PASS] AI.10 PoleMeshRouter.myNodeId updated immediately at runtime (NO MANUAL REBOOT REQUIRED!)
  [PASS] AI.11 PoleMeshRouter no longer uses old unassigned default ID
  [PASS] AI.12 PoleMeshRouter primary parent updated to Gateway
  [PASS] AI.14 Approved pole exists in Gateway registry
  [PASS] AI.15 Pole status is POLE_STATUS_PENDING_ACK prior to first heartbeat
[REGISTRY] 🤝 Pole 0x0002 handshake confirmed -> POLE_STATUS_ACTIVE
  [PASS] AI.16 Gateway confirms handshake upon first heartbeat: status becomes POLE_STATUS_ACTIVE
  [PASS] AI.17 Destination pole accepts multi-hop response frame matching its MAC locally

--- SECTION AJ: REGISTRATION REJECTION & REVOCATION LOCKOUT ---
[REG_CLIENT] ⛔ Gateway response: REJECTED. Node cannot become active.
  [PASS] AJ.1 handleRegisterResponse returns false on rejection
  [PASS] AJ.2 Field Pole transitions to persistent REG_CLIENT_REJECTED
  [PASS] AJ.3 Rejected pole cannot become active
[REGISTRY] ✅ Pole 0x0002 (MAC AABBCCDDEE05) marked REVOKED (delivery pending).
  [PASS] AJ.4 Revoked MAC permanently blacklisted in NVS
[SECURITY] 🚨 Blocked JOIN_REQ from Revoked Pole MAC AA:BB:CC:DD:EE:05
  [PASS] AJ.5 Gateway rejects JOIN_REQ from blacklisted MAC with REG_RESP_REJECTED_REVOKED
[ACTUATOR] 🚨 EMERGENCY SHUTDOWN TRIGGERED! Reason: GATEWAY_REVOCATION_RESPONSE
[ACTUATOR] All 6 Feeder Relay Outputs DE-ENERGIZED immediately.
[REG_CLIENT] ⛔ Gateway response: PERMANENTLY REVOKED. Relays locked OFF.
  [PASS] AJ.6 Field Pole enters persistent REG_CLIENT_REVOKED_LOCKED
  [PASS] AJ.7 Relays locked OFF permanently upon revocation response

--- SECTION AK: TELEGRAM ALERT DEDUPLICATION & RATE LIMITING ---
  [PASS] AK.1 First revoked device event creates a pending Telegram alert
  [PASS] AK.2 hasReappearedRevokedAlert is true
  [PASS] AK.3 100 repeated heartbeats from revoked pole generate 0 additional Telegram alerts (100% SPAM ELIMINATED!)
  [PASS] AK.4 Diagnostic occurrences tracked accurately without message spam
  [PASS] AK.5 A genuinely different revoked MAC generates its own distinct alert event
  [PASS] AK.6 Reset / rearm allows one fresh alert event for re-emerged incident
[TELEGRAM_SEND to 5505296197]: TEST_ALERT_SINGLE_DELIVERY
[TELEGRAM_SEND to 123456789]: TEST_ALERT_SINGLE_DELIVERY
  [PASS] AK.7 broadcastAdminAlert executes with deduplicated recipient list
  [PASS] AK.8 Multiple alerts queued in Heartbeat Monitor
[TELEGRAM_SEND to 5505296197]: 🚨 *MSCB SMART POLE STATUS DIGEST (3 EVENTS)*

• Pole 0x0002 - OFFLINE
• Pole 0x0003 - OFFLINE
• Pole 0x0004 - RECOVERING

[TELEGRAM_SEND to 123456789]: 🚨 *MSCB SMART POLE STATUS DIGEST (3 EVENTS)*

• Pole 0x0002 - OFFLINE
• Pole 0x0003 - OFFLINE
• Pole 0x0004 - RECOVERING

  [PASS] AK.9 All 3 concurrent events popped and formatted into one digest message
  [PASS] AK.10 Queue drained cleanly after digest dispatch

--- SECTION AL: TELEGRAM SERVICE & MAIN LOOP REGRESSION ---
  [PASS] AL.1 Telegram bot marks offline cleanly when Wi-Fi disconnects
  [PASS] AL.2 Telegram bot restores online cleanly without notification storm
[TELEGRAM_KEYBOARD to 5505296197]: 📊 *MSCB GATEWAY STATUS & SYSTEM DASHBOARD*
━━━━━━━━━━━━━━━━━━━━━━
• Firmware Version: 1.0.0 (System_R1)
• System Clock: `2026-11-25 20:05:51 IST`
• System Uptime: 1 s
• Free Heap DRAM: 245000 B | PSRAM: 8000 KB
• Wi-Fi SSID: `` (RSSI: -65 dBm)
• IP Address: `192.168.1.100`
• LoRa Radio: FAULT_OFFLINE (TX: +8 dBm)
• LoRa Packets: TX 2 | RX 0
• Managed Active Poles: 1 / 50
• Pending Join Requests: 0
• Permanent Revocations: 1
• Security: FAIL-CLOSED AUTH ENFORCED
• Production Relays: SAFE LOCKED
━━━━━
KB: [[{"text":"🔄 Refresh","callback_data":"MENU:DASH"},{"text":"🔙 Main Menu","callback_data":"MENU:MAIN"}]]
  [PASS] AL.3 /status command responds with Gateway system metrics
[TELEGRAM_KEYBOARD to 5505296197]: 📋 *PENDING POLE REGISTRATIONS*
━━━━━━━━━━━━━━━━━━━━━━
✅ *All Clear!* No pending pole join requests.
When an unprovisioned pole powers on, its MAC address and signal metrics will appear here for wizard configuration.
KB: [[{"text":"🔄 Refresh","callback_data":"MENU:REG:0"},{"text":"🔙 Main Menu","callback_data":"MENU:MAIN"}]]
  [PASS] AL.4 /pending command renders pending discovery list
[TG] Callback data: MENU:POLES:0
[TELEGRAM_KEYBOARD to 5505296197]: 💡 *ACTIVE FIELD POLES (Page 1 of 1)*
━━━━━━━━━━━━━━━━━━━━━━
Total Active Nodes: 1 / 50
Tap any pole below to view detail, telemetry, or control feeders:
KB: [[{"text":"📍 Main Gate Light 🟢","callback_data":"P_SEL:2"}],[{"text":"🔙 Main Menu","callback_data":"MENU:MAIN"}]]
  [PASS] AL.5 Pole menu callback navigation works
  [PASS] AL.6 Physical relay lockout enforced (zero inadvertent mains energization)

=====================================================================
R3.2.2 TEST RUN COMPLETE: 69 PASSED, 0 FAILED
=====================================================================
```
