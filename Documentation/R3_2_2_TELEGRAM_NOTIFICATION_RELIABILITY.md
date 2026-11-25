# MSCB SMART POLE SYSTEM — R3.2.2 TELEGRAM NOTIFICATION RELIABILITY

**Date:** November 25, 2026  
**Component:** `Gateway_ESP32S3/GwTelegramBot.cpp`, `GwNodeRegistry.cpp`, `GwHeartbeatMonitor.cpp`  
**Target:** ESP32-S3 Gateway Controller  

---

## 1. Overview of Spam Mitigation Architecture

In previous firmware revisions, the Gateway ESP32-S3 risked notification storms during network events:
- Revoked poles emitting periodic heartbeats caused a new Telegram alert on every received frame.
- Simultaneous parent node loss caused up to 50 individual outage messages to fire simultaneously, hitting Telegram API rate limits (HTTP 429).
- Administrative broadcasts dispatched duplicate messages to duplicate chat IDs.

R3.2.2 establishes four complementary reliability layers:
1. **Incident-Based Deduplication (MAC & Event Keyed)**
2. **Digest Batching & Rate Throttling (>= 3s)**
3. **Queue State Tracking & Anti-Drop Retry**
4. **Recipient Set Deduplication**

---

## 2. Technical Implementation Details

### 2.1 Revoked Pole Incident Deduplication
In `GwNodeRegistry.cpp`:
```cpp
struct GwRevokedIncidentItem {
  uint8_t  mac[6];
  uint16_t poleId;
  uint32_t firstSeenEpoch;
  uint32_t lastSeenEpoch;
  uint32_t lastAlertEpoch;
  uint32_t occurrences;
  bool     activeIncident;
};
```
- `setReappearedRevokedAlert(mac, logicalId, poleId)` performs an exact 6-byte MAC search across active incidents.
- If an active incident exists, it increments `occurrences` and updates `lastSeenEpoch` without re-emitting a Telegram alert.
- Only when an incident is newly opened (or after an administrative reset or quiet period) is an alert placed on the notification queue.
- Verified in `test_r3_2_2.exe` Section AK.3: **100 repeated heartbeats generated exactly 1 alert (100% spam eliminated)**.

### 2.2 Alert Digest Batching & Throttling
In `GwTelegramBot::processNotificationQueue()`:
- Alerts are grouped into digest reports (`🚨 *MSCB SMART POLE STATUS DIGEST (N EVENTS)*`).
- Up to 12 events are consolidated into a single Telegram message.
- A minimum throttle interval of 3000 ms (`GW_TG_MIN_ALERT_INTERVAL_MS`) is strictly enforced between dispatches.
- If a message send returns an HTTP 429 error, the bot reads the `retry_after` parameter and delays the next dispatch accordingly.

### 2.3 Recipient Allowlist Deduplication
In `GwTelegramBot::broadcastAdminAlertDetailed()`:
- Maintains a visited array of int64_t chat IDs during iteration over `MSCB_TELEGRAM_ADMIN_ALLOWLIST` and `primaryAdminChatId`.
- Filters duplicate occurrences so that each authorized administrator receives exactly one copy per event.

### 2.4 Ambiguous Timeout Delivery Safety
- Network sockets can encounter timeouts after writing the HTTP request to Telegram servers.
- Blind plain-text resends caused duplicate message spam.
- In R3.2.2, ambiguous responses (empty body or timeout after write) transition to `TG_DELIVERY_UNKNOWN_AMBIGUOUS` without blind immediate retransmission.

---

## 3. Verification Summary

```text
Section AK: Telegram Message Deduplication & Rate Limiting
---------------------------------------------------------
[PASS] AK.1 First revoked device event creates a pending Telegram alert
[PASS] AK.2 hasReappearedRevokedAlert is true
[PASS] AK.3 100 repeated heartbeats from revoked pole generate 0 additional Telegram alerts (100% SPAM ELIMINATED!)
[PASS] AK.4 Diagnostic occurrences tracked accurately without message spam
[PASS] AK.5 A genuinely different revoked MAC generates its own distinct alert event
[PASS] AK.6 Reset / rearm allows one fresh alert event for re-emerged incident
[PASS] AK.7 broadcastAdminAlert executes with deduplicated recipient list
[PASS] AK.8 Multiple alerts queued in Heartbeat Monitor
[PASS] AK.9 All 3 concurrent events popped and formatted into one digest message
[PASS] AK.10 Queue drained cleanly after digest dispatch
```
