# 12 — MSCB Smart Pole Automation System: Telegram Bot Administrator Manual

**Document ID**: MSCB-DOC-12  
**Classification**: Operations & Field User Manual  
**Interface**: Secure Telegram Bot (HTTPS API)  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Introduction & Authentication Security

The Master Gateway provides an encrypted, mobile-accessible administrative user interface via Telegram. All system management—from dynamic device registration to high-voltage feeder switching—is conducted through interactive inline keyboards and persistent action keyboards.

### 1.1 Security Policy & Session Management
- **Sender Identity Authentication**: Every callback and message strictly verifies the user's Telegram User ID (`from_id`), not just `chat_id`. Unauthorized users are immediately rejected.
- **Single Active Admin**: Only authorized Telegram Chat IDs provisioned during commissioning (`MSCB_TELEGRAM_ADMIN_ALLOWLIST`) can interact with the bot.
- **Inactivity Timeout**: Administrator sessions expire automatically after **15 minutes** of inactivity (`TG_SESSION_TIMEOUT_MS = 900000`).
- **Sensitive Button Expiration**: High-impact buttons (e.g. Delete Pole, All-Off, Wi-Fi Change) expire after **60 seconds** (`TG_BUTTON_TTL_MS = 60000`).
- **Rate Limiting**: Commands are capped at **5 requests per 10 seconds** (`TG_RATE_LIMIT_MAX_CMDS = 5`).
- **Webhook Conflict Elimination**: Gateway automatically purges any dangling webhooks on boot/connect to ensure flawless long-polling.

---

## 2. Interactive Menu Layout & Numeric Fallback

The bot provides two synchronized input mechanisms:
1. **Interactive Inline & Reply Keyboards**: 14 structured main menu options arranged in clean, responsive rows for Android, iOS, and Desktop Telegram clients.
2. **Deterministic Numeric Fallback**: Operators in poor network areas or field conditions can type raw numbers `1` through `14` to directly open the respective dashboard or submenu without looping to the welcome message.

```
┌──────┬───────────────────────────────┬──────────────────────────────────────────┐
│ Key  │ Button Text                   │ Action / Destination Screen              │
├──────┼───────────────────────────────┼──────────────────────────────────────────┤
│  1   │ 📊 Dashboard                  │ Network summary, pole count, alarms      │
│  2   │ 💡 Poles Directory           │ Paginated list of all registered poles   │
│  3   │ ⚡ 3-Phase Voltage            │ Real 3-phase bus voltage & frequency     │
│  4   │ 🔌 Feeder Control             │ Select Pole & Feeder for ON/OFF switching│
│  5   │ 🟢 All Feeders ON             │ 2-Step Network-wide ALL ON confirmation │
│  6   │ 🔴 All Feeders OFF            │ 2-Step Network-wide ALL OFF confirmation│
│  7   │ 📋 Pole Registrations         │ Paginated pending queue (Approve/Reject) │
│  8   │ 🗑️ Delete Pole                │ 2-Step Revocation & Permanent Blacklist │
│  9   │ ⏱️ Feeder Timer / Schedule    │ Recurring daily schedules & Auto-OFF     │
│ 10   │ 🕒 Mesh Time Sync             │ Broadcast NTP/RTC time sync across mesh  │
│ 11   │ 📶 Wi-Fi Settings             │ Safe non-destructive Wi-Fi credential cfg│
│ 12   │ 🔄 Firmware OTA               │ Gateway & Pole cryptographic OTA status  │
│ 13   │ 🚨 Alarm Log                  │ System electrical and mesh alarms audit  │
│ 14   │ ⚙️ System Settings            │ Heartbeat & network configuration        │
└──────┴───────────────────────────────┴──────────────────────────────────────────┘
```

> [!NOTE]
> Typing `/start`, `/menu`, or `/help` opens the main menu and refreshes the persistent ReplyKeyboard. Unknown text inputs receive a concise help prompt without spamming menus or entering recursive loops.

---

## 3. Comprehensive 25-Workflow Administrator Guide

### Workflow 1: Opening the Bot
- Open your Telegram mobile or desktop client.
- Search for the provisioned bot username (e.g., `@MSCB_SmartPole_Bot`).
- Send the `/start` command.

### Workflow 2: Administrator Authentication
- If your Telegram User ID is in the approved admin registry, the bot initializes your session and displays the welcome banner with system uptime, firmware version (`GW_V10`), and Wi-Fi SSID.
- If unauthenticated, the bot prompts: `⛔ Access Denied. Contact System Administrator.`

### Workflow 3: Navigating the Main Dashboard
- Press the `📊 Dashboard` button.
- Displays network summary: Total Nodes Registered, Online/Offline count, Active Feeders (out of 300), and Global Alarm status.

### Workflow 4: Discovering New Poles
- When an unprovisioned pole powers on, it transmits `JOIN_REQ`.
- The Gateway automatically sends a Telegram alert:
  `🆕 UNREGISTERED POLE DETECTED! Hardware ID: A4:CF:12:89:BC:01`
- Navigate to `📋 Registrations` from the main menu.

### Workflow 5: Approving or Rejecting a Device
- In the Registration menu, view the pending hardware list.
- Press `[✅ Approve]` to grant a logical ID (`POLE-xxxx`), assign 6 feeders, and provision mesh cryptographic keys.
- Press `[❌ Reject]` to drop the request and purge the pending queue.

### Workflow 6: Viewing Pole Directory
- Press `💡 Poles` on the dashboard.
- Browse through registered poles using paginated navigation buttons: `[◀ Prev]` and `[Next ▶]`.

### Workflow 7: Inspecting Detailed Pole Status
- Tap any specific pole (e.g., `[📍 POLE-0001]`).
- Displays: Operational State, Active Feeder Count, Last Heard Time, Signal Strength (RSSI/SNR), and Mesh Hop Rank.

### Workflow 8: Viewing Network Topology
- Press `🕸️ Network Topology`.
- Renders an ASCII DAG tree showing parent-child hierarchy from Gateway Root to Edge nodes.

### Workflow 9: Inspecting Primary & Backup Parents
- In the Pole Detail screen, examine the routing block:
  - `Primary Parent : POLE-0002 (Rank 1, Cost 25)`
  - `Backup Parent  : POLE-0004 (Rank 2, Cost 52, Path Diverse)`

### Workflow 10: Monitoring 3-Phase Bus Voltage
- Tap `⚡ Voltage Dashboard`.
- Displays real-time True-RMS bus voltage for Phase R, Phase Y, Phase B, grid frequency (Hz), and phase sequence status (`RYB_NORMAL`).

### Workflow 11: Monitoring 18-Channel Feeder Current
- Tap `📈 Current Dashboard`.
- Select pole to view all 18 current channels (6 Feeders $\times$ 3 Phases R/Y/B). Values display with 0.01A precision.

### Workflow 12: Inspecting Feeder Status
- Tap `🔌 Feeder Management`.
- Displays status of all 6 feeders on the selected pole: Relay State (ON/OFF), Contactor Auxiliary Feedback, and MCB Status.

### Workflow 13: Controlling an Individual Feeder
- Select target feeder (e.g., `[FDR-0001: Feeder 1]`).
- Press `[⚡ Turn ON]` or `[⭕ Turn OFF]`.
- Option to select timed auto-off countdown: `[1 Hour]`, `[2 Hours]`, `[4 Hours]`.

### Workflow 14: Executing Group Control
- Tap `👥 Group Control`.
- Select target feeder mask (e.g. All Street Lights across Pole Cluster).
- Gateway dispatches staggered switching commands (500ms delay between actuations).

### Workflow 15: All-Pole Master Control
- Tap `🌐 All Poles Control`.
- Select `[🚨 EMERGENCY ALL OFF]` to de-energize all 300 feeders simultaneously in 0 ms.

### Workflow 16: Deactivating a Feeder (Maintenance Mode)
- In Feeder Detail screen, press `[🔒 Deactivate Feeder]`.
- Locks relay open in hardware; feeder will reject all automated schedules and group switching until reactivated.

### Workflow 17: Permanent Pole Deletion
- Tap `🗑️ Delete Pole` in the Pole Settings menu.
- Bot displays confirmation warning: `⚠️ Confirm permanent deletion of POLE-0005?`
- Press `[🔴 CONFIRM DELETE]`. Gateway triggers Two-Phase Commit revocation and blacklists the hardware MAC.

### Workflow 18: Viewing Revocation History
- Tap `📜 Revocation History`.
- Lists blacklisted MAC addresses, revocation transaction UUIDs, and decommission dates.

### Workflow 19: Configuring Wi-Fi Credentials
- Send command `/wifi` or tap `📶 Wi-Fi Settings`.
- Enter new SSID and Password. The Gateway initiates a 20-second non-destructive connection verification before committing to NVS.

### Workflow 20: Configuring System Timeouts
- Tap `⏱️ Timeouts`.
- Adjust: Heartbeat Interval (default 30s), Node Offline Threshold (default 180s), and LoRa ACK Timeout (default 5000ms).

### Workflow 21: Configuring Electrical Thresholds
- Tap `⚙️ Thresholds`.
- Configure Overcurrent Trip Amperes (default 25.0A), Overvoltage Limit (260 VAC), and Undervoltage Limit (180 VAC).

### Workflow 22: Viewing Real-Time Fault Alerts
- When an electrical or RF anomaly occurs, the bot pushes an instant alert:
  `🚨 FAULT DETECTED: POLE-0003 Feeder 2 Overcurrent (28.4A)! Breaker Tripped.`
- Press `[Acknowledge Alert]` to clear notification buzzer.

### Workflow 23: Auditing Event History Log
- Tap `📜 Event History`.
- Browse the 64-entry circular audit log containing timestamps, fault codes, admin actions, and failover records.

### Workflow 24: Remote Node Configuration
- Tap `🔧 Node Config`.
- Push global configuration updates (e.g. beacon intervals, reporting rates) over the LoRa mesh.

### Workflow 25: Monitoring Firmware Update Status
- Tap `🔄 Firmware Management`.
- View Gateway local firmware version (`GW_V10`), OTA partition status, and remote pole mesh update progress.

---

## 3. Command Execution Feedback Terminology

Operators must understand the critical distinctions between the five stages reported during feeder switching:

```
┌─────────────────────────────────┬───────────────────────────────────────────┐
│ Status Displayed in Telegram    │ Physical Electrical Meaning               │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 1. 📤 Command Sent              │ Packet transmitted by Gateway; RF transit │
│                                 │ in progress across mesh.                  │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 2. 📥 Command Accepted          │ Field Pole received packet, validated     │
│                                 │ HMAC, and verified safety interlocks.     │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 3. ⚙️ Relay Output Changed       │ Microcontroller asserted GPIO pin; pilot   │
│                                 │ optocoupler energized.                    │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 4. 🧲 Contactor Feedback Confirmed│ Auxiliary dry contact physically closed/   │
│                                 │ opened; contactor movement confirmed.     │
├─────────────────────────────────┼───────────────────────────────────────────┤
│ 5. ✅ Electrical State Verified  │ True-RMS metrology confirmed load current │
│                                 │ flowing (>0.1A for ON, 0A for OFF).       │
└─────────────────────────────────┴───────────────────────────────────────────┘
```
