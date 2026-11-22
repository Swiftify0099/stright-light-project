# 04 — MSCB Smart Pole Automation System: Gateway Firmware Documentation

**Document ID**: MSCB-DOC-04  
**Classification**: Embedded Software Architecture & Module Reference  
**Platform**: ESP32-S3-WROOM-1 / WROOM-2 N16R8 (16MB Flash, 8MB Octal PSRAM)  
**Firmware Version**: `GW_V10`  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Gateway Firmware Overview & Execution Architecture

The Gateway firmware runs on an Espressif ESP32-S3 dual-core microcontroller. It acts as the root of the LoRa mesh network (`ROOT_RANK = 0`) and manages cloud synchronization via Wi-Fi and Telegram Bot API.

### 1.1 Cooperative Task Scheduling Architecture

The Gateway firmware operates without blocking RTOS delays. `Sender.ino` implements a high-efficiency cooperative scheduler driven by hardware timers and `millis()` timestamps:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       GATEWAY COOPERATIVE SCHEDULER (loop)                  │
├───────────────────────┬───────────────────┬─────────────────────────────────┤
│ Execution Task        │ Frequency / Cycle │ Implementation / Function       │
├───────────────────────┼───────────────────┼─────────────────────────────────┤
│ LoRa Packet Receive   │ Continuous (fast) │ loraMgr.processIncomingLoRa()   │
│ Root Beacon Broadcast │ 30 seconds        │ discoveryMgr.update()           │
│ Central Alert Check   │ 100 ms            │ alertMgr.update()               │
│ Outgoing Cmd Queue    │ 50 ms             │ loraMgr.processCommandQueue()   │
│ Telegram Bot Polling  │ 1,000 ms          │ tgMgr.handleTelegramUpdates()   │
│ Wi-Fi Watchdog & AP   │ 1,000 ms          │ wifiCfgMgr.update()             │
│ Node Timeout Check    │ 10,000 ms         │ heartbeatMgr.checkNodeTimeouts()│
│ Time Sync Broadcast   │ 10 minutes        │ processTimeSyncPush()           │
│ NVS State Persistence │ 60 seconds        │ centralStorage.flushDirty()     │
└───────────────────────┴───────────────────┴─────────────────────────────────┘
```

---

## 2. Comprehensive Gateway Module Reference

### 2.1 Core Orchestration & Application
- **File Path**: `Sender/Sender.ino`
- **Module Name**: Gateway Main Orchestrator
- **Purpose**: System bootstrap, peripheral initialization, cooperative scheduler execution, and global time synchronization.
- **Dependencies**: `<Arduino.h>`, `config.h`, `Types.h`, all Gateway subsystem managers.
- **Important Functions**:
  - `setup()`: Initializes NVS storage, cryptography, node registry, alert manager, mesh router, LoRa radio, Wi-Fi, and Telegram.
  - `loop()`: Executes non-blocking cooperative tasks.
  - `processTimeSyncPush()`: Broadcasts UTC epoch time to mesh nodes.
- **Inputs**: Incoming LoRa packets, Telegram API HTTP updates, Serial CLI commands.
- **Outputs**: RF mesh commands, Telegram Bot responses, Serial diagnostic telemetry.
- **Related Hardware**: ESP32-S3, SX1278 RA-02, Wi-Fi 2.4 GHz.
- **Related Tests**: `tests/test_prompt22_integration.exe` (Scenario A).

### 2.2 RF Physical Transceiver & Packet Codec
- **File Path**: `Sender/LoRaManager.cpp` / `Sender/LoRaManager.h`
- **Module Name**: Gateway LoRa Radio & Queue Manager
- **Purpose**: Hardware control of SX1278 transceiver over FSPI, priority queue management, carrier frequency configuration, and RF packet transmission.
- **Dependencies**: `<SPI.h>`, `<LoRa.h>`, `config.h`, `PacketCodec.h`, `AuthSecurity.h`.
- **Important Classes**: `LoRaManager` (instantiated as `loraMgr`).
- **Important Functions**:
  - `bool init()`: Configures FSPI pins (SCK=12, MISO=13, MOSI=11, CS=10, RST=14, DIO0=2), sets 433.92 MHz, SF10, BW125k, CR4/5, +10 dBm TX power.
  - `void processIncomingLoRa()`: Reads packet FIFO, checks CRC, validates framing, passes payload to routing engine.
  - `bool enqueueCommand(...)`: Inserts high/medium/low priority commands into bounded ring buffer.
  - `void processCommandQueue()`: Dispatches queued frames adhering to airtime constraints.
- **Inputs**: SX1278 SPI interrupts, application command requests.
- **Outputs**: SPI transactions to SX1278, radio TX frames.
- **Related Hardware**: Semtech SX1278 RA-02.
- **Related Tests**: `tests/test_prompt09.exe`, `tests/test_prompt23_field_validation.exe`.

### 2.3 Mesh Routing & Topology Management
- **File Path**: `Sender/MeshManager.cpp` / `Sender/MeshManager.h`
- **Module Name**: Gateway DAG Mesh Root Engine
- **Purpose**: Manages mesh routing table, next-hop resolution, packet forwarding, hop decrementing, and topology database.
- **Dependencies**: `config.h`, `NetworkTypes.h`, `GwRegistry.h`.
- **Important Classes**: `MeshManager` (instantiated as `meshMgr`).
- **Important Functions**:
  - `void init()`: Sets Gateway fixed Rank to 0 (`ROOT_RANK`).
  - `String resolveNextHop(const String &dst)`: Looks up routing table to determine next-hop child node.
  - `void handleMeshPacket(ParsedPacket &pkt)`: Routes incoming telemetry, ACKs, or multi-hop transit packets.
  - `void updateRoute(const String &nodeId, const String &nextHop, uint8_t rank, int rssi)`: Updates dynamic DAG routing table.
- **Inputs**: Decoded LoRa mesh packets.
- **Outputs**: Forwarded mesh packets, routing table metrics.
- **Related Tests**: `tests/test_mesh_routing.exe`.

### 2.4 Node Registry & Dynamic Commissioning
- **File Path**: `Sender/GwRegistry.cpp` / `Sender/GwRegistry.h`
- **Module Name**: Gateway Node Database & Life Cycle Manager
- **Purpose**: Tracks active registered nodes (up to 50 nodes), pending approval queues, logical-to-hardware ID mappings, and parent links.
- **Dependencies**: `RegistrationTypes.h`, `CentralStorage.h`, `config.h`.
- **Important Classes**: `GwRegistry` (instantiated as `gwRegistry`).
- **Important Functions**:
  - `bool registerNode(const String &hwId, const String &poleId, ...)`: Commits new node record to RAM and NVS.
  - `bool approvePendingNode(const String &hwId)`: Promotes node from pending approval queue to active registry.
  - `bool deleteNode(const String &poleId)`: Removes node from active registry and triggers revocation broadcast.
  - `RegisteredNode* getNode(const String &poleId)`: Retrieves pole metadata, last-seen timestamp, and electrical telemetry cache.
- **Inputs**: `JOIN_REQ` packets, Telegram admin approval/rejection clicks.
- **Outputs**: `JOIN_ACCEPT` packets, persistent NVS updates.
- **Related Tests**: `tests/test_registration_engine.exe`, `tests/test_prompt12.exe`.

### 2.5 Revocation & Deletion Journaling
- **File Path**: `Sender/RevocationManager.cpp` / `Sender/RevocationManager.h`
- **Module Name**: Cryptographic Revocation & Decommissioning Engine
- **Purpose**: Executes two-phase commit deletion of pole nodes, issues cryptographic revocation tokens, and blacklists rogue or decommissioned hardware.
- **Dependencies**: `CentralStorage.h`, `LoRaManager.h`, `CryptoManager.h`.
- **Important Classes**: `RevocationManager` (instantiated as `revocationMgr`).
- **Important Functions**:
  - `bool initiateRevocation(const String &poleId, const String &adminId)`: Creates revocation transaction, logs UUID.
  - `void broadcastRevocation(const String &hwId)`: Transmits multi-hop `REVOKE_BROADCAST` to wipe node credentials.
  - `bool isRevoked(const String &hwId)`: Verifies hardware blacklist to block re-registration attempts.
- **Inputs**: Telegram admin deletion confirmation.
- **Outputs**: Signed revocation frames, NVS blacklist updates.
- **Related Tests**: `tests/test_prompt12.exe` (Scenario D).

### 2.6 Feeder Supervisory Controller
- **File Path**: `Sender/FeederManager.cpp` / `Sender/FeederManager.h`
- **Module Name**: Central Feeder Command Dispatcher & State Tracker
- **Purpose**: Dispatches feeder switching commands (F1-F6), coordinates group switching staggering (500 ms delay), and tracks 5-stage command execution.
- **Dependencies**: `LoRaManager.h`, `Types.h`, `GwRegistry.h`.
- **Important Classes**: `FeederManager` (instantiated as `feederMgr`).
- **Important Functions**:
  - `bool sendFeederCommand(const String &poleId, uint8_t feederIdx, bool turnOn, uint32_t durationSec)`: Formats and enqueues switching command.
  - `bool sendGroupFeederCommand(uint8_t feederMask, bool turnOn)`: Dispatches staggered group commands across all 50 poles.
  - `void handleFeederAck(const String &poleId, uint8_t feederIdx, CommandExecutionState state)`: Updates Telegram UI with verification status.
- **Inputs**: Telegram operator button clicks.
- **Outputs**: Dispatched `CMD` frames, Telegram status updates.
- **Related Tests**: `tests/test_prompt15_feeder_control.exe`.

### 2.7 Alert Aggregation & Incident Management
- **File Path**: `Sender/AlertManager.cpp` / `Sender/AlertManager.h`
- **Module Name**: Fault Detection & Alert Storm Suppression Engine
- **Purpose**: Evaluates 21-channel telemetry against electrical thresholds, detects grid-wide brownouts, suppresses alert storms, and notifies Telegram.
- **Dependencies**: `TelegramNotificationManager.h`, `GwRegistry.h`, `config.h`.
- **Important Classes**: `AlertManager` (instantiated as `alertMgr`).
- **Important Functions**:
  - `void processTelemetryFaults(const String &poleId, const NodeTelemetry &telem)`: Audits overcurrent, undervoltage, phase unbalance, and MCB trips.
  - `void update()`: Evaluates 10-second aggregation window; merges multi-node outages into single alerts.
  - `void dispatchAlert(const IncidentAlert &alert)`: Enqueues formatted Markdown alert to Telegram.
- **Inputs**: Node telemetry frames.
- **Outputs**: High-priority Telegram alert messages.
- **Related Tests**: `tests/test_prompt18.exe`, `tests/test_prompt22_integration.exe` (Scenario H).

### 2.8 Telegram Cloud Engine & User Interface
- **File Paths**:
  - `Sender/GwTelegram.cpp` / `Sender/GwTelegram.h`
  - `Sender/TelegramMenuManager.cpp` / `Sender/TelegramMenuManager.h`
  - `Sender/TelegramMenus.cpp` / `Sender/TelegramMenus.h`
  - `Sender/TelegramCallbackManager.cpp` / `Sender/TelegramCallbackManager.h`
  - `Sender/TelegramSessionManager.cpp` / `Sender/TelegramSessionManager.h`
  - `Sender/TelegramNotificationManager.cpp` / `Sender/TelegramNotificationManager.h`
- **Module Name**: Telegram Bot Multi-Screen Interface Subsystem
- **Purpose**: Implements 20 interactive dashboard menus, inline keyboards, callback routing, admin session timeout enforcement (15 min), and rate limiting.
- **Dependencies**: `<WiFiClientSecure.h>`, `<UniversalTelegramBot.h>`, `<ArduinoJson.h>`, `GwRegistry.h`.
- **Important Classes**:
  - `GwTelegram` (`tgMgr`): Main bot polling loop and HTTPS message dispatcher.
  - `TelegramMenuManager` (`menuMgr`): Generates 20 screen templates.
  - `TelegramCallbackManager` (`callbackMgr`): Parses incoming inline keyboard callbacks.
  - `TelegramSessionManager` (`sessionMgr`): Validates administrator authentication tokens.
- **Inputs**: Incoming Telegram webhook/poll JSON updates.
- **Outputs**: Rendered Telegram Markdown messages, inline keyboards.
- **Related Tests**: `tests/test_prompt20.exe`, `tests/test_telegram_bot.cpp`.

### 2.9 Wi-Fi & System Configuration Manager
- **File Path**: `Sender/WiFiConfigManager.cpp` / `Sender/WiFiConfigManager.h`
- **Module Name**: Secure Wi-Fi & Fallback SoftAP Manager
- **Purpose**: Manages multi-stage Wi-Fi credential updates, connection verification, automated rollback on failure, and fallback Recovery Access Point (`MSCB-GW-RECOVERY`).
- **Dependencies**: `<WiFi.h>`, `CentralStorage.h`, `config.h`.
- **Important Classes**: `WiFiConfigManager` (instantiated as `wifiCfgMgr`).
- **Important Functions**:
  - `void init()`: Connects to stored SSID; activates fallback AP if 3 consecutive failures occur.
  - `bool testNewCredentials(const String &ssid, const String &pass)`: Non-destructive 20-second connection validation.
  - `void commitStable()`: Persists verified Wi-Fi credentials to NVS.
  - `void processSerialCommands()`: Interactive CLI for emergency field recovery.
- **Inputs**: Telegram `/wifi` commands, Serial CLI inputs.
- **Outputs**: Wi-Fi station association, SoftAP activation.
- **Related Tests**: `tests/test_prompt17.exe`.

### 2.10 Persistent NVS Storage Engine
- **File Path**: `Sender/CentralStorage.cpp` / `Sender/CentralStorage.h`
- **Module Name**: Gateway Non-Volatile Storage (NVS) Manager
- **Purpose**: Handles structured persistence of node records, network routing states, Wi-Fi credentials, and event logs with CRC32 verification and flash wear leveling.
- **Dependencies**: `<Preferences.h>`, `StorageTypes.h`, `config.h`.
- **Important Classes**: `CentralStorage` (instantiated as `centralStorage`).
- **Important Functions**:
  - `bool saveNodeRecord(const RegisteredNode &node)`: Stores binary node record with CRC32.
  - `bool loadAllNodes(std::vector<RegisteredNode> &nodes)`: Recovers network database on boot.
  - `void logEvent(uint8_t category, uint8_t code, const String &msg)`: Appends to 64-entry circular audit log.
- **Inputs**: Application configuration and state updates.
- **Outputs**: ESP32 NVS flash read/write operations.
- **Related Tests**: `tests/test_prompt19_storage.exe`.

### 2.11 Security, Cryptography & Anti-Replay
- **File Paths**:
  - `Sender/AuthSecurity.cpp` / `Sender/AuthSecurity.h`
  - `Sender/CryptoManager.cpp` / `Sender/CryptoManager.h`
  - `Sender/ReplayProtection.cpp` / `Sender/ReplayProtection.h`
- **Module Name**: Gateway Security Subsystem
- **Purpose**: AES-128-GCM packet payload encryption, 12-byte initialization vector (IV) derivation, HMAC authentication tag validation, and sliding window anti-replay protection.
- **Dependencies**: `config.h`, `Types.h`.
- **Important Functions**:
  - `bool encryptFrame(uint8_t *plain, size_t len, uint8_t *cipher, uint8_t *tag, uint32_t nonce)`: Encrypts packet payload.
  - `bool decryptAndVerify(const uint8_t *cipher, size_t len, const uint8_t *tag, uint8_t *plain, uint32_t nonce)`: Decrypts and validates authentication tag.
  - `bool checkAndRecordNonce(const String &nodeId, uint32_t nonce)`: Suppresses replayed sequence numbers.
- **Related Tests**: `tests/test_prompt14.exe`.
