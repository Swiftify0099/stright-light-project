# MSCB SMART POLE SYSTEM — R3.2.1 TEST EVIDENCE

**Date:** November 24, 2026  
**Commit Branch:** `r3.2.1-critical-mesh-truth-repair`  
**Base Commit:** `d6def238c7d56f5bcbf6963b4e6bd6a5584a72d2`  
**Execution Environment:** Windows 11 x64, MinGW GCC 6.3.0, Arduino CLI 1.x (ESP32 Core 3.3.11)

---

## 1. Native Functional Test Runner Execution (`test_r3.exe`)

### Compilation Command:
```powershell
C:\MinGW\bin\g++.exe -std=c++11 -Wall -I. -I..\FieldPole_ESP32 -I..\Gateway_ESP32S3 -I..\MSCBShared `
  -o test_r3.exe test_r3_real_hardware_functional_repair.cpp `
  ..\MSCBShared\CommandSecurity.cpp `
  ..\MSCBShared\ReplayProtection.cpp `
  ..\FieldPole_ESP32\PoleStorageManager.cpp `
  ..\FieldPole_ESP32\PoleActuators.cpp `
  ..\FieldPole_ESP32\PoleMeshRouter.cpp `
  ..\FieldPole_ESP32\PoleFeederManager.cpp `
  ..\FieldPole_ESP32\PoleMetrology.cpp `
  ..\FieldPole_ESP32\PoleTimeManager.cpp `
  ..\FieldPole_ESP32\PoleRegistrationClient.cpp `
  ..\FieldPole_ESP32\PoleRadioDriver.cpp `
  ..\Gateway_ESP32S3\GwNodeRegistry.cpp `
  ..\Gateway_ESP32S3\GwHeartbeatMonitor.cpp `
  ..\Gateway_ESP32S3\GwMeshCoordinator.cpp `
  ..\Gateway_ESP32S3\GwTelegramBot.cpp `
  ..\Gateway_ESP32S3\GwScheduleManager.cpp `
  ..\Gateway_ESP32S3\GwWiFiManager.cpp `
  ..\Gateway_ESP32S3\GwFirmwareManager.cpp `
  ..\Gateway_ESP32S3\GwRadioDriver.cpp `
  ..\Gateway_ESP32S3\GwNetworkQueue.cpp
```

### Execution Command:
```powershell
.\test_r3.exe
```

### Execution Log Output (Excerpts across all sections):
```
=====================================================================
  MSCB SMART POLE SYSTEM R3 - REAL HARDWARE FUNCTIONALITY REPAIR TEST 
=====================================================================

--- SECTION A: COMMAND AUTHENTICATION ROOT CAUSE REPAIR ---
  [PASS] A.1 Security key provisioned successfully
  [PASS] A.2 Gateway created valid authenticated command envelope
  [PASS] A.3 Correct arguments (env, targetPoleId, REAL_UTC_EPOCH) returns AUTH_SUCCESS
  [PASS] A.4 Inverted arguments (env, uptime, poleId) correctly fails authentication!
  [PASS] A.5 Expired command returns AUTH_ERR_COMMAND_EXPIRED
  [PASS] A.6 Clock < 1700000000 returns AUTH_ERR_TIME_UNSYNCED
  [PASS] A.7 Destination mismatch returns AUTH_ERR_DEST_MISMATCH
  [PASS] A.8 Replay of identical sequence number returns AUTH_ERR_REPLAY_DETECTED
  [PASS] A.9 Tampered HMAC tag returns AUTH_ERR_HMAC_INVALID

--- SECTION B: ABSOLUTE MESH TIME SYNCHRONIZATION ---
  [PASS] B.1 Authenticated time sync packet created
  [PASS] B.2 Time sync packet carries real UTC epoch
  [PASS] B.3 Field Pole validates time sync HMAC successfully
  [PASS] B.4 Tampered time sync packet rejected by HMAC
  [PASS] B.5 PoleTimeManager starts unsynchronized
  [PASS] B.6 getCurrentEpoch() returns 0 when unsynced
  [PASS] B.7 PoleTimeManager is synchronized after valid sync packet
  [PASS] B.8 Current epoch correctly derived from anchor

--- SECTION C: REAL RELAY ACTUATION & PHYSICAL PIN MAPPING ---
  [PASS] C.1 Feeder 1 Physical Header Position == 24
  [PASS] C.2 Feeder 1 Board Label == 'D26'
  [PASS] C.3 Feeder 1 GPIO == 26 (Pin 24 preserved!)
  [PASS] C.4 Feeder 2 GPIO == 25
  [PASS] C.5 Feeder 3 GPIO == 27
  [PASS] C.6 Feeder 4 GPIO == 16
  [PASS] C.7 Feeder 5 GPIO == 17
  [PASS] C.8 Feeder 6 GPIO == 32
  [PASS] C.9 GPIO 24 is never assigned to any relay output
  [PASS] C.10 Actuator diagnostic self-test passed
  [PASS] C.11 areRelaysLockedDisabled() enforces centralized compile-time safety lock
  [PASS] C.12 Emergency shutdown immediately de-energizes all outputs

--- SECTION D: SINGLE FEEDER FLOW & TRUTHFUL TELEGRAM REPORTING ---
  [PASS] D.1 FEEDER_ACK_COMPLETED string verified
  [PASS] D.2 FEEDER_ACK_FEEDBACK_NOT_CONNECTED string verified
  [PASS] D.3 FEEDER_ACK_REJECTED_TIME_UNSYNCED string verified
  [PASS] D.4 FEEDER_ACK_REJECTED_LOCKED string verified (COMMAND_REJECTED)

--- SECTION E: PER-POLE ALL FEEDERS CONTROL (MASK 0x3F) ---
  [PASS] E.1 Only F1 arrived: Tracker does NOT mark command completed
  [PASS] E.2 F1..F5 arrived (0x1F): Command is STILL NOT marked complete
  [PASS] E.3 All 6 feeders arrived (0x3F): Command is marked COMPLETE!

--- SECTION F: BOUNDED NETWORK-WIDE COMMAND QUEUE ---
  [PASS] F.1 NetworkQueue is idle initially
  [PASS] F.2 Approval without mandatory name/location rejected!
  [PASS] F.3 Approval with mandatory name & location succeeds
  [PASS] F.4 Assigned Pole ID == 0x0002
  [PASS] F.5 Pole found in registry
  [PASS] F.6 Custom Display Name persisted
  [PASS] F.7 Custom Location persisted
  [PASS] F.8 NetworkQueue batch started successfully
  [PASS] F.9 NetworkQueue marks batch in progress
  [PASS] F.10 Targeted poles count tracked
  [PASS] F.11 Network queue correctly ingested feeder ACK
  [PASS] F.12 Batch cancelled cleanly

--- SECTION G: REGISTRATION WIZARD & REVOCATION ---
  [PASS] G.1 Pending node found by MAC
  [PASS] G.2 Wizard step reached review (step 4)
  [PASS] G.3 Draft display name saved
  [PASS] G.4 Draft location saved
  [PASS] G.5 Custom feeder name saved
  [PASS] G.6 Second pole approved from wizard drafts
  [PASS] G.7 Unique Pole ID 0x0003 allocated
  [PASS] G.8 Custom feeder name transferred to active registered pole
  [PASS] G.9 Pole 0x0003 revoked and deleted successfully
  [PASS] G.10 MAC address permanently blacklisted
  [PASS] G.11 Revoked pole removed from active registry
  [PASS] G.12 Revocation record retrievable from blacklist
  [PASS] G.13 Join request from revoked node returns REG_RESP_REJECTED_REVOKED
  [PASS] G.14 Reappearance security alert triggered

--- SECTION H: REAL METROLOGY (NO SYNTHETIC 50Hz/0V DATA) ---
  [PASS] H.1 getGridFrequency() returns NAN when measured frequency is unavailable (no fake 50.0Hz!)
  [PASS] H.2 getBusVoltageR() returns NAN when measured voltage is unavailable

--- SECTION I: R3.1 NVS A/B TRANSACTIONAL PERSISTENCE & POWER CUT RESILIENCE ---
  [PASS] I.1 Active NVS slot index is binary 0 or 1
  [PASS] I.2 Generation counter monotonically incremented
  [PASS] I.3 Registry contains active nodes before simulation
  [PASS] I.4 Corrupted slot recovers valid previous generation without wiping state to zero
  [PASS] I.5 Mid-write power failure triggers save failure without corrupting active slot
  [PASS] I.6 Registry safely remains on committed slot after interrupted write

--- SECTION J: R3.1 AUTHENTICATED REVOCATION & HMAC VERIFICATION ---
  [PASS] J.1 Gateway successfully generates authenticated Revoke Command
  [PASS] J.2 Revoke command payload contains non-zero HMAC-SHA256 authentication tag
  [PASS] J.3 Field Pole validates HMAC on Revoke Command with AUTH_SUCCESS
  [PASS] J.4 All-zero auth tag strictly rejected by Field Pole HMAC verifier
  [PASS] J.5 Revoke command sent to Node 0x0002 rejected by Node 0x0003
  [PASS] J.6 Pole 0x0002 deleted and marked revoked
  [PASS] J.7 Revoked pole retains deliveryStatus == REVOKE_DELIVERY_PENDING until ACK arrives
  [PASS] J.8 Delivery confirmed updates deliveryStatus to REVOKE_DELIVERY_CONFIRMED
  [PASS] J.9 unrevokePole succeeds
  [PASS] J.10 Un-revocation preserves unrevokeAdmin audit trail
  [PASS] J.11 Un-revocation preserves unrevokeReason audit trail
  [PASS] J.12 Un-revocation preserves unrevokeTimestamp

--- SECTION K: R3.1 FIELD POLE LOCKDOWN ON HEARTBEAT ACK ---
  [PASS] K.1 Pole registration client transitions to REG_CLIENT_REVOKED_LOCKED
  [PASS] K.2 Registration cleared on lockdown

--- SECTION L: R3.1 TIME-SYNC SAFETY (OFF PERMITTED, ON BLOCKED) ---
  [PASS] L.1 Feeder ON command strictly BLOCKED when clock is unsynchronized
  [PASS] L.2 Authenticated Feeder OFF command PERMITTED when clock is unsynchronized
  [PASS] L.3 Authenticated Emergency Shutdown PERMITTED when clock is unsynchronized

--- SECTION M: R3.1 TELEGRAM REGISTRATION WIZARD & CONTROLS ---
  [PASS] M.1 Telegram wizard marked active for pending MAC
  [PASS] M.2 Active wizard tracks exact MAC
  [PASS] M.3 Pending node found
  [PASS] M.4 /reg_name saved draft display name
  [PASS] M.5 /reg_loc saved draft location
  [PASS] M.6 /reg_area saved draft area
  [PASS] M.7 /reg_area saved draft landmark
  [PASS] M.8 /reg_f saved custom feeder name
  [PASS] M.9 Active wizard reset after confirmation
  [PASS] M.10 Pole registered in active registry
  [PASS] M.11 Initial status is POLE_STATUS_PENDING_ACK
  [PASS] M.12 Registered pole uses custom display name
  [PASS] M.13 Feeder name query routes directly to RegisteredPole.feederNames single source of truth
  [PASS] M.14 Mesh handshake confirms POLE_STATUS_ACTIVE

--- SECTION N: R3.1 MCP23S17 ACTIVE-LOW POLARITY & SENSOR FEEDBACK VALIDITY ---
  [PASS] N.1 Active-low MCP23S17 pin pulled to GND maps to logical closed (1)
  [PASS] N.2 All open contacts correctly map to logical 0
  [PASS] N.3 Feedback validity flags accurately report hardware disconnection without fake OK status

--- SECTION O: R3.1 500ms FEEDER STAGGER INTERVAL ---
  [PASS] O.1 POLE_FEEDER_STAGGER_INTERVAL_MS is exactly 500ms

--- SECTION P: R3.1 ADS131M08 3-CHIP SYNCHRONIZATION ---
  [PASS] P.1 Raw ADC frame reading requires all 3 DRDY lines LOW simultaneously
  [PASS] P.2 Asynchronous/partial DRDY condition safely ignored

--- SECTION Q: R3.1 DYNAMIC SCHEDULING & FEEDER CONTROLS ---
  [PASS] Q.1 Feeder 1 returns custom name from RegisteredPole
  [PASS] Q.2 Feeder 2 returns custom name from RegisteredPole
  [PASS] Q.3 Feeder 3 defaults to F3 when unassigned
  [PASS] Q.4 clearSchedules resets ruleCount to 0
  [PASS] Q.5 Successfully armed 30m Auto-OFF timer on Pole 0x0005 Feeder 1
  [PASS] Q.6 Schedule rule count is 1
  [PASS] Q.7 Rule correctly records target pole 0x0005 and 30m duration
  [PASS] Q.8 addSchedule rejects invalid hour (25)
  [PASS] Q.9 addSchedule rejects invalid minute (65)
  [PASS] Q.10 Valid recurring schedule added (18:30 - 06:00)
  [PASS] Q.11 Schedule rule count is now 2
  [PASS] Q.12 clearSchedules successfully cleared all rules

--- SECTION R: R3.2 SAFE LVDC COMMISSIONING PROFILE & LOCAL PHYSICAL AUTH ---
  [PASS] R.1 Relays locked disabled by default
  [PASS] R.2 Remote unlock blocked without local physical authorization
  [PASS] R.3 Local physical authorization flag active
  [PASS] R.4 Local physical authorization revokable
  [PASS] R.5 FEEDER_ACK_CONTACTOR_CLOSED maps to 'CONTACTOR_CLOSED'
  [PASS] R.6 FEEDER_ACK_MCB_TRIPPED maps to 'MCB_TRIPPED'
  [PASS] R.7 FEEDER_ACK_COMMAND_TIMEOUT maps to 'COMMAND_TIMEOUT'
  [PASS] R.8 FEEDER_ACK_HARDWARE_FAULT maps to 'HARDWARE_FAULT'

--- SECTION S: R3.2 PIN CONFIGURATION & HARDWARE PROFILE ---
  [PASS] S.1 MSCB_HW_PROFILE_IDENTIFIER is 'ESP32_WROOM32_DOIT_POLE_R3_2'
  [PASS] S.2 MSCB_HW_CONFIG_VERSION is 0x0302
  [PASS] S.3 F1 relay is GPIO 26 (Pin 24, D26)
  [PASS] S.4 F2 relay is GPIO 25
  [PASS] S.5 F3 relay is GPIO 27
  [PASS] S.6 F4 relay is GPIO 16
  [PASS] S.7 F5 relay is GPIO 17
  [PASS] S.8 F6 relay is GPIO 32
  [PASS] S.9 Default pin configuration passes validation
  [PASS] S.10 Pin configuration CRC32 computed successfully
  [PASS] S.11 Forbidden SPI flash pin 6 rejected
  [PASS] S.12 Input-only pin 34 for relay output rejected
  [PASS] S.13 Peripheral collision on LoRa NSS pin 5 rejected
  [PASS] S.14 Pin configuration safely applied when all relays are OFF

--- SECTION T: R3.2.1 TELEGRAM MENU ERGONOMICS & JSON STRUCTURE ---
  [PASS] T.1 Main menu parsed valid JSON with <= 6 buttons
  [PASS] T.2 Main menu has valid distinct callbacks
  [PASS] T.3 Poles menu parsed valid JSON with <= 6 buttons
  [PASS] T.4 Pole detail menu parsed valid JSON with <= 6 buttons
  [PASS] T.5 Pole feeder list Page 0 has exactly 6 buttons
  [PASS] T.6 Pole feeder list Page 0 callbacks valid
  [PASS] T.7 Pole feeder list Page 1 has exactly 4 buttons
  [PASS] T.8 Feeder control menu has exactly 4 buttons
  [PASS] T.9 Voltage dashboard parsed valid JSON with <= 6 buttons
  [PASS] T.10 Feeder pole select menu has <= 6 buttons
  [PASS] T.11 Registrations menu has <= 6 buttons
  [PASS] T.12 Delete pole menu has <= 6 buttons
  [PASS] T.13 Delete confirm menu has exactly 2 buttons
  [PASS] T.14 Confirm all poles menu has exactly 2 buttons
  [PASS] T.15 Feeder schedule menu has <= 6 buttons
  [PASS] T.16 Schedule pole select menu has <= 6 buttons
  [PASS] T.17 Schedule feeder select Page 0 has exactly 6 buttons
  [PASS] T.18 Schedule feeder select Page 1 has exactly 4 buttons
  [PASS] T.19 Schedule action select menu has exactly 6 buttons
  [PASS] T.20 Settings menu has <= 6 buttons
  [PASS] T.21 Unauthorized callback rejected with Access Denied
  [PASS] T.22 Unauthorized slash command rejected with Access Denied

--- SECTION U: R3.2.1 MULTI-HOP MESH & CHILD TRACKING ---
  [PASS] U.1 Mesh router drops self-originating packet (loop prevention)
  [PASS] U.2 packetsDroppedSelfLoop incremented
  [PASS] U.3 Frame exceeding hop limit dropped
  [PASS] U.4 packetsDroppedHopLimit incremented
  [PASS] U.5 Malformed frame (invalid magic) dropped
  [PASS] U.6 packetsDroppedMalformed incremented
  [PASS] U.7 First valid frame forwarded once
  [PASS] U.8 Duplicate frame (same src and seq) dropped
  [PASS] U.9 packetsDroppedDuplicate incremented
  [PASS] U.10 Self cannot be added as candidate parent neighbor (cycle prevention)
  [PASS] U.11 Candidate with primary parent pointing to this node rejected (cycle prevention)
  [PASS] U.12 Forwarded child packet registered in trackedChildren
  [PASS] U.13 Tracked child ID is 0x0008
  [PASS] U.14 Initial child state is CHILD_STATE_ONLINE
  [PASS] U.15 Child lastSeqNumber correctly recorded
  [PASS] U.16 Child link metrics accurately recorded
  [PASS] U.17 Inactive child transitioned to CHILD_STATE_OFFLINE
  [PASS] U.18 Subsequent valid packet restores child to CHILD_STATE_ONLINE
  [PASS] U.19 Child record updated in-place without duplicate entries
  [PASS] U.20 Child node 0x0008 rejected as parent (cycle prevention)

--- SECTION V: R3.2.1 GATEWAY HEARTBEAT & CASCADE FAILURE ---
  [PASS] V.1 Unregistered pole query returns POLE_STATE_UNKNOWN (never false ONLINE)
  [PASS] V.2 Pole 10 is ONLINE
  [PASS] V.3 Pole 20 is ONLINE
  [PASS] V.4 Direct parent Pole 10 classified POLE_STATE_OFFLINE
  [PASS] V.5 Descendant Pole 20 classified POLE_STATE_UNREACHABLE_VIA_PARENT (Cascade Failure)
  [PASS] V.6 Gateway heartbeat monitor has queued alert events
  [PASS] V.7 Alert queue count is bounded at GW_ALERT_QUEUE_CAPACITY
  [PASS] V.8 First alert event correctly identified Pole 10 direct OFFLINE
  [PASS] V.9 Second alert event correctly identified Pole 20 UNREACHABLE_VIA_PARENT
  [PASS] V.10 Cascade alert reason explicitly identifies parent dependency
  [PASS] V.11 Deduplication and cooldown suppressed duplicate alert storm

--- SECTION W: R3.2.1 PHYSICAL AUTHORIZATION & RELAY SAFETY ---
  [PASS] W.1 MSCB_RELAYS_LOCKED_DISABLED production lock preserved (1)
  [PASS] W.2 areRelaysLockedDisabled() returns true
  [PASS] W.3 Remote unlock rejected without local physical authorization
  [PASS] W.4 Software setter alone cannot grant physical auth while GPIO 39 is open (HIGH)
  [PASS] W.5 Physical auth granted when GPIO 39 is grounded (LOW) and debounced
  [PASS] W.6 Physical authorization automatically expired after 5 minutes
  [PASS] W.7 Test relay energized in authorized low-voltage bench mode
  [PASS] W.8 Physical auth revoked immediately when jumper removed
  [PASS] W.9 Emergency shutdown instantly de-energized relay upon auth loss
  [PASS] W.10 ContactorState string CLOSED
  [PASS] W.11 ContactorState string OPEN
  [PASS] W.12 ContactorState string UNAVAILABLE
  [PASS] W.13 McbState string HEALTHY
  [PASS] W.14 McbState string TRIPPED
  [PASS] W.15 McbState string UNAVAILABLE
  [PASS] W.16 MeasurementValidity string SENSOR_UNAVAILABLE
  [PASS] W.17 Disconnected aux hardware returns CONTACTOR_UNAVAILABLE (never fake OPEN or CLOSED)
  [PASS] W.18 Disconnected aux hardware returns MCB_UNAVAILABLE (never fake HEALTHY)

--- SECTION X: R3.2.1 METROLOGY TRUTH & BOOT PIN CONFIG ---
  [PASS] X.1 Missing ADC returns NAN for phase voltage (never fabricated 0.0V)
  [PASS] X.2 Missing ADC returns NAN for feeder current (never fabricated 0.0A)
  [PASS] X.3 Measurement validity returns MEAS_SENSOR_UNAVAILABLE
  [PASS] X.4 Telegram detail shows 'FEEDBACK NOT CONNECTED' when aux valid flag is false
  [PASS] X.5 Telegram detail shows 'SENSOR UNAVAILABLE' when voltage sensor is missing
  [PASS] X.6 Corrupted CRC detected
  [PASS] X.7 Default pin mapping passes strict hardware validation
  [PASS] X.8 Safe default configuration applied cleanly

=====================================================================
TEST RUN COMPLETE: 225 PASSED, 0 FAILED
=====================================================================
```
**Exit Code:** `0`

---

## 2. Gateway ESP32-S3 Firmware Compilation

### Build Command:
```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" `
  compile --fqbn esp32:esp32:esp32s3 MSCB_SmartPole_System_R1\Gateway_ESP32S3
```

### Build Output:
```text
Sketch uses 1133245 bytes (86%) of program storage space. Maximum is 1310720 bytes.
Global variables use 93528 bytes (28%) of dynamic memory, leaving 234152 bytes for local variables. Maximum is 327680 bytes.
```
**Exit Code:** `0` (Zero compiler warnings treated as errors, clean binary generation)

---

## 3. Field Pole ESP32 Firmware Compilation

### Build Command:
```powershell
& "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" `
  compile --fqbn esp32:esp32:esp32 MSCB_SmartPole_System_R1\FieldPole_ESP32
```

### Build Output:
```text
Sketch uses 329956 bytes (25%) of program storage space. Maximum is 1310720 bytes.
Global variables use 28292 bytes (8%) of dynamic memory, leaving 299388 bytes for local variables. Maximum is 327680 bytes.
```
**Exit Code:** `0` (Zero compiler warnings treated as errors, clean binary generation)

---

## 4. Test Summary Matrix

| Metric | Target | Actual Result | Verification Status |
|---|---|---|---|
| Native Test Executable | Exit Code 0 | Exit Code 0 | **PASSED** |
| Tests Passing | 100% | 225 / 225 (0 Failed) | **PASSED** |
| Gateway Target | `esp32:esp32:esp32s3` | Flash 86%, RAM 28% | **PASSED** |
| Field Pole Target | `esp32:esp32:esp32` | Flash 25%, RAM 8% | **PASSED** |
| Legacy Directories Integrity | 0 modifications | Untouched (`Sender/`, `Resivers/`) | **PASSED** |
| Production Relay Lock | Preserved (`= 1`) | Confirmed (`MSCB_RELAYS_LOCKED_DISABLED = 1`) | **PASSED** |
