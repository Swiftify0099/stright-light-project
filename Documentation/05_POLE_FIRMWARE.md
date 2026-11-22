# 05 — MSCB Smart Pole Automation System: Field Pole Firmware Documentation

**Document ID**: MSCB-DOC-05  
**Classification**: Embedded Software Architecture & Module Reference  
**Platform**: ESP32-WROOM-32 (ESP32-D0WDQ6, 4MB Flash, No PSRAM)  
**Firmware Version**: `POLE_V10`  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Field Pole Firmware Overview & Execution Architecture

The Field Pole firmware runs autonomously on an Espressif ESP32-WROOM-32 microcontroller. Each pole manages 6 three-phase feeder relays, executes True-RMS power metrology across 21 analog channels, monitors MCB and contactor feedback, participates in multi-hop LoRa mesh forwarding, and executes local autonomous schedules and countdown timers even when disconnected from the Gateway.

### 1.1 Cooperative Task Scheduling Architecture

`Resivers.ino` executes a high-speed cooperative scheduling loop without blocking `delay()` calls:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      FIELD POLE COOPERATIVE SCHEDULER (loop)                │
├───────────────────────┬───────────────────┬─────────────────────────────────┤
│ Execution Task        │ Frequency / Cycle │ Implementation / Function       │
├───────────────────────┼───────────────────┼─────────────────────────────────┤
│ LoRa Packet & Routing │ Continuous (fast) │ loraMgr.processIncomingLoRa()   │
│ Discovery & Beaconing │ 30 seconds        │ discoveryMgr.update()           │
│ 21-Ch Sensor Update   │ 5,000 ms          │ multiAdcMgr.update()            │
│ MCB / Contactor Check │ 50 ms debounce    │ feederMgr.updateMCBInputs()     │
│ Autonomous Timers     │ 1,000 ms          │ cmdMgr.checkTimerExpiration()   │
│ Heartbeat Cadence     │ 30s + jitter      │ heartbeatMgr.checkHeartbeat()   │
│ Mesh Health Report    │ 60 seconds        │ meshMgr.sendHealthReport()      │
│ Local Schedule Audit  │ 10,000 ms         │ cmdMgr.checkAutonomousSchedule()│
│ Unregistered Retries  │ 10 seconds        │ cmdMgr.sendNewNode()            │
└───────────────────────┴───────────────────┴─────────────────────────────────┘
```

### 1.2 Hardware Relay Lockout at Boot

To ensure zero inadvertent electrical switching or contactor chatter during brownouts or microcontroller resets:
1. All 6 relay driver GPIOs (`GPIO 26, 25, 27, 16, 17, 32`) are driven to their inactive state (`RELAY_STATE_OFF = HIGH`) during the very first machine cycle of `setup()`.
2. Relay actuation is strictly locked out in firmware until the node has completed registration or loaded a verified state from persistent NVS.

---

## 2. Comprehensive Field Pole Module Reference

### 2.1 Main Orchestrator & Boot Safety
- **File Path**: `Resivers/Resivers.ino`
- **Module Name**: Field Pole Main Orchestrator
- **Purpose**: System bootstrap, relay pin locking, subsystem dependency initialization, and cooperative scheduling execution.
- **Dependencies**: `<Arduino.h>`, `config.h`, `Types.h`, all Pole subsystem managers.
- **Important Functions**:
  - `setup()`: Enforces boot safety lockout, initializes NVS storage, 21-channel ADC metrology, feeder manager, mesh router, and LoRa radio.
  - `loop()`: Executes non-blocking cooperative tasks.
- **Inputs**: Incoming LoRa packets, analog sensor signals, MCB auxiliary inputs.
- **Outputs**: Relay coil drive signals, LoRa telemetry and ACK packets, Serial logs.
- **Related Hardware**: ESP32-WROOM-32, 6 Relays, 3x ADS131M08, SX1278.
- **Related Tests**: `tests/test_prompt22_integration.exe` (Scenario A).

### 2.2 Multi-ADC Metrology Engine (3x ADS131M08)
- **File Paths**:
  - `Resivers/MultiADCManager.cpp` / `Resivers/MultiADCManager.h`
  - `Resivers/ADS131M08Driver.cpp` / `Resivers/ADS131M08Driver.h`
  - `Resivers/PowerMeasurementEngine.cpp` / `Resivers/PowerMeasurementEngine.h`
- **Module Name**: 21-Channel 24-bit Delta-Sigma Metrology Controller
- **Purpose**: Synchronizes three Texas Instruments ADS131M08 ADCs over SPI, acquires simultaneous samples across 21 active channels, and computes True-RMS voltage, True-RMS current, active power, and power factor.
- **Dependencies**: `<SPI.h>`, `config.h`, `CalibrationManager.h`.
- **Important Classes**: `MultiADCManager` (instantiated as `multiAdcMgr`), `ADS131M08`.
- **Important Functions**:
  - `bool init()`: Resets all 3 ADCs via hardware reset line, sets OSR 1024 (4 kSPS), asserts `/SYNC` on `GPIO 33` to phase-lock all 24 channels.
  - `void update()`: Samples 64 points per 20 ms AC cycle (50 Hz), computes sum of squares ($V^2, I^2$), and calculates instantaneous power ($P = V \times I$).
  - `void computeCycleRMS()`: Extracts True-RMS metrics for Phase R, Y, B bus voltages and 18 feeder phase currents.
  - `float getFeederCurrent(uint8_t fdrIdx, uint8_t phaseIdx)`: Returns calibrated RMS current in Amperes.
- **Inputs**: Analog differential voltage signals on 21 channels.
- **Outputs**: Calibrated electrical metrology dataset, overcurrent flags.
- **Related Hardware**: 3x TI ADS131M08, 18 Current CTs, 3 ZMPT101B transformers.
- **Related Tests**: `tests/test_prompt17.exe`.

### 2.3 Feeder Control & Actuator Supervision
- **File Paths**:
  - `Resivers/FeederManager.cpp` / `Resivers/FeederManager.h`
  - `Resivers/PoleActuators.cpp` / `Resivers/PoleActuators.h`
- **Module Name**: Feeder Switching & Feedback Verification Engine
- **Purpose**: Drives 6 optocoupler outputs, manages contactor auxiliary feedback, monitors MCB trip switches, and enforces 5-stage execution state verification.
- **Dependencies**: `config.h`, `Types.h`, `MultiADCManager.h`, `NodeStorage.h`.
- **Important Classes**: `FeederManager` (instantiated as `feederMgr`), `PoleActuators`.
- **Important Functions**:
  - `bool setFeederRelay(uint8_t feederIdx, bool turnOn)`: Validates interlocks and updates physical GPIO pin state.
  - `void updateMCBInputs()`: Digital debounce (5 samples @ 50 ms = 250 ms) on auxiliary feedback pins.
  - `CommandExecutionState verifyExecution(uint8_t feederIdx, bool expectedState)`: Checks contactor auxiliary contact AND verifies whether current is flowing ($I > 0.1\text{ A}$) to confirm electrical state.
- **Inputs**: Control command requests, dry contact feedback lines.
- **Outputs**: Digital switching pulses (`GPIO 26, 25, 27, 16, 17, 32`), execution status codes.
- **Related Hardware**: 6-channel optocoupler relay module, 3-pole 25A contactors, auxiliary MCBs.
- **Related Tests**: `tests/test_prompt15_feeder_control.exe`.

### 2.4 Command Execution & Local Schedule Engine
- **File Path**: `Resivers/CommandManager.cpp` / `Resivers/CommandManager.h`
- **Module Name**: Autonomous Command & Countdown Scheduler
- **Purpose**: Parses incoming Gateway commands, enforces security nonces, executes local countdown timers (e.g., auto-off after 4 hours), and runs autonomous time-based switching schedules.
- **Dependencies**: `FeederManager.h`, `LoRaManager.h`, `NodeStorage.h`.
- **Important Classes**: `CommandManager` (instantiated as `cmdMgr`).
- **Important Functions**:
  - `void handleCommand(const String &cmdPayload)`: Decodes switching action, feeder mask, and duration parameters.
  - `void checkTimerExpiration()`: Evaluates active countdown timers; safely shuts down feeders upon timer expiry without requiring Gateway communication.
  - `void checkAutonomousSchedule()`: Executes local on/off transitions based on internal RTC time.
- **Inputs**: LoRa command frames, system timestamp.
- **Outputs**: Actuation trigger requests, command ACK packets.
- **Related Tests**: `tests/test_prompt15_feeder_control.exe`, `tests/test_prompt22_integration.exe`.

### 2.5 LoRa Transceiver Driver & Queue
- **File Path**: `Resivers/LoRaManager.cpp` / `Resivers/LoRaManager.h`
- **Module Name**: Pole LoRa Radio Interface
- **Purpose**: Hardware interface to SX1278 over VSPI (`SCK=18, MISO=19, MOSI=23, CS=5, RST=14, DIO0=4`), packet reception, CRC verification, and RF transmission.
- **Dependencies**: `<SPI.h>`, `<LoRa.h>`, `config.h`, `PacketCodec.h`.
- **Important Classes**: `LoRaManager` (instantiated as `loraMgr`).
- **Important Functions**:
  - `bool init()`: Configures VSPI, sets 433.92 MHz, SF10, BW125k, CR4/5, +10 dBm TX power.
  - `void processIncomingLoRa()`: Reads FIFO, verifies destination ID, checks CRC, passes frame to mesh engine.
  - `bool sendPacket(...)`: Transmits unicast or broadcast RF packets with Carrier Sense Multiple Access (CSMA) backoff.
- **Inputs**: SX1278 SPI interrupts, application transmit requests.
- **Outputs**: SPI control pulses, 433 MHz RF modulated signal.
- **Related Hardware**: Semtech SX1278 RA-02.
- **Related Tests**: `tests/test_prompt09.exe`, `tests/test_protocol.exe`.

### 2.6 Mesh Routing & Orphan Recovery
- **File Path**: `Resivers/MeshManager.cpp` / `Resivers/MeshManager.h`
- **Module Name**: Field Pole DAG Router & Path Failover Controller
- **Purpose**: Evaluates candidate parents, tracks Link Quality metrics, selects Primary and Backup parents, forwards multi-hop transit packets, and executes orphan recovery upon link loss.
- **Dependencies**: `config.h`, `NetworkTypes.h`, `NodeStorage.h`.
- **Important Classes**: `MeshManager` (instantiated as `meshMgr`).
- **Important Functions**:
  - `void updateRouteCandidate(const String &sender, uint8_t rank, int rssi, int snr, float pdr)`: Updates neighbor quality table.
  - `void selectParents()`: Elects Primary Parent ($P_1$) and Backup Parent ($P_2$) ensuring path diversity.
  - `void handleParentFailure()`: Automatically promotes Backup Parent to Primary if 3 consecutive packet transmissions fail.
  - `void enterOrphanState()`: Triggers Route Request (RREQ) broadcasts with exponential backoff ($3\text{s} \rightarrow 6\text{s} \rightarrow 12\text{s} \dots$) upon total parent disconnection.
- **Inputs**: LoRa beacons, packet delivery acknowledgments.
- **Outputs**: Next-hop routing decisions, mesh control packets (RREQ, RREP).
- **Related Tests**: `tests/test_mesh_routing.exe`.

### 2.7 Sensor Calibration & NVS Storage
- **File Paths**:
  - `Resivers/CalibrationManager.cpp` / `Resivers/CalibrationManager.h`
  - `Resivers/NodeStorage.cpp` / `Resivers/NodeStorage.h`
- **Module Name**: Sensor Metrology Calibration & Pole NVS Manager
- **Purpose**: Persists offset and gain calibration factors for all 21 analog channels, manages node registration identity (`POLE-xxxx`), and stores circular fault event logs.
- **Dependencies**: `<Preferences.h>`, `StorageTypes.h`, `config.h`.
- **Important Classes**: `CalibrationManager` (`calibMgr`), `NodeStorage` (`storageMgr`).
- **Important Functions**:
  - `void saveChannelCalibration(uint8_t chIdx, float offset, float gain)`: Commits calibration factors to NVS.
  - `bool loadAllCalibrations()`: Retrieves calibration table on boot.
  - `bool isRegistered()`: Confirms node activation status.
- **Inputs**: Calibration CLI commands, operational state changes.
- **Outputs**: Calibrated ADC readings, persistent flash updates.
- **Related Tests**: `tests/test_prompt17.exe`, `tests/test_prompt19_storage.exe`.
