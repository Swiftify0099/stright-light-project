# 19 — MSCB Smart Pole Automation System: Diagnostic & Troubleshooting Guide

**Document ID**: MSCB-DOC-19  
**Classification**: Field Service & Diagnostic Matrix  
**Fault Scenarios**: 20 Comprehensive Failure Modes  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Safety Protocols During Hardware Troubleshooting

> [!CAUTION]
> **SAFETY FIRST**:
> Never open electrical cabinets during thunderstorms or wet conditions. Before probing any relay, contactor, or sensor wiring, use a certified non-contact voltage detector to ensure all 415 VAC / 230 VAC busbars are de-energized and locked out (LOTO).

---

## 2. Comprehensive 20-Fault Diagnostic Matrix

### Fault 1: Gateway Not Booting / Power LED Off
- **Symptoms**: No LEDs on ESP32-S3 DevKit; Serial Monitor silent at 115200 baud.
- **Possible Root Cause**: Faulty 5V power supply, blown input fuse, or reverse polarity.
- **Safe Diagnostic Method**: Measure DC voltage across `5V` and `GND` pins on the DevKit using a digital multimeter.
- **Corrective Action**: Replace external 5V/2A power supply or USB cable.
- **When to Call Technician**: If 5V is present on board pins but 3.3V rail measures < 3.0V (onboard LDO damaged).

### Fault 2: Gateway LoRa Initialization Failure
- **Symptoms**: Serial prints: `[LORA][ERROR] SX1278 init failed (-2)!`
- **Possible Root Cause**: Loose SPI jumper wire, incorrect FSPI pin mapping, or unseated SX1278 module.
- **Safe Diagnostic Method**: Inspect physical connections to `GPIO 12 (SCK), 13 (MISO), 11 (MOSI), 10 (CS), 14 (RST), 2 (DIO0)`.
- **Corrective Action**: Reseat SX1278 module in socket; verify 3.3V power to radio module.
- **When to Call Technician**: If radio fails to respond after replacing wiring (SX1278 silicon failure).

### Fault 3: Field Pole Not Discovered by Gateway
- **Symptoms**: Pole powers on, but never appears in Telegram `/registrations` menu.
- **Possible Root Cause**: Out of RF range, incorrect carrier frequency, or mismatched sync word (`0x12`).
- **Safe Diagnostic Method**: Check Pole Serial Monitor for `[POLE] Broadcasting JOIN_REQ...`; check RF signal on spectrum analyzer.
- **Corrective Action**: Re-flash approved firmware with standard RF parameters (`433.92 MHz, SF10, Sync 0x12`); improve antenna height.
- **When to Call Technician**: If RF transmitter output power is measured at 0 dBm (PA blown by transmitting without antenna).

### Fault 4: Registration Pending / Admin Approval Blocked
- **Symptoms**: Node visible in Telegram pending queue, but pressing `[Approve]` gives error or times out.
- **Possible Root Cause**: Gateway database full (50 poles reached) or invalid MAC address hash.
- **Safe Diagnostic Method**: Send Telegram command `/dashboard` to check registered node count.
- **Corrective Action**: If network is full (50/50), decommission an inactive pole before approving new hardware.
- **When to Call Technician**: Not applicable (software capacity rule).

### Fault 5: Parent Not Available / Orphan State Active
- **Symptoms**: Pole Serial logs: `[MESH][WARN] No parent available. Entering STATE_ORPHAN.`
- **Possible Root Cause**: Upstream neighbor pole lost power or antenna cable severed.
- **Safe Diagnostic Method**: Check online status of intermediate cluster head nodes in Telegram topology view.
- **Corrective Action**: Restore power to intermediate relay poles; check physical coaxial antenna connections.
- **When to Call Technician**: If physical antenna cable is damaged or water-logged.

### Fault 6: Mesh Route Failure / High Packet Loss
- **Symptoms**: Commands take multiple retries; telemetry reports PDR < 50%.
- **Possible Root Cause**: High RF interference, physical obstruction (new building/trees), or loose antenna SMA.
- **Safe Diagnostic Method**: Inspect link quality metrics: `RSSI < -115 dBm` or `SNR < -10 dB`.
- **Corrective Action**: Relocate antenna to higher bracket; check SMA center pin contact.
- **When to Call Technician**: If coaxial line exhibits high VSWR (> 2.0).

### Fault 7: Weak RF Signal / Intermittent Dropouts
- **Symptoms**: Node alternates between Online and Offline every few minutes.
- **Possible Root Cause**: Antenna mounted too close to high-voltage lines or metal pole structure without clearance.
- **Safe Diagnostic Method**: Measure RSSI at varying antenna orientations.
- **Corrective Action**: Install 1/4-wave antenna on standoff bracket with minimum 1-meter clearance from metal pole.
- **When to Call Technician**: If antenna mount requires bucket truck access.

### Fault 8: Pole Permanently Offline in Telegram
- **Symptoms**: Telegram displays `🔴 POLE-0004: OFFLINE (Last seen > 30 min)`.
- **Possible Root Cause**: Utility power cut at pole, blown auxiliary fuse, or controller brownout.
- **Safe Diagnostic Method**: Check if main pole street lights are illuminated; inspect cabinet 5V LED.
- **Corrective Action**: Check 230V auxiliary breaker and 2A glass fuse in cabinet power supply.
- **When to Call Technician**: If utility supply to pole is dead (Utility grid fault).

### Fault 9: Backup Parent Not Working / Single Point of Failure
- **Symptoms**: Telegram topology lists `Backup Parent: NONE`.
- **Possible Root Cause**: Insufficient node density; node only hears one upstream neighbor.
- **Safe Diagnostic Method**: Audit neighbor discovery table via `/pole nbrs POLE-0005`.
- **Corrective Action**: Adjust antenna angle or deploy an intermediate relay pole to establish path diversity.
- **When to Call Technician**: If network expansion requires installing a new pole node.

### Fault 10: Sensor Reading Invalid / Constant 0.0V or 0.0A
- **Symptoms**: Telegram displays `0.0V` or `0.0A` despite connected active load.
- **Possible Root Cause**: Sensor disconnected, burden resistor open-circuit, or unseated ADC chip.
- **Safe Diagnostic Method**: Measure AC millivolts across burden resistor terminals with True-RMS meter.
- **Corrective Action**: Reseat sensor screw terminals; verify CT arrow points toward load.
- **When to Call Technician**: If burden resistor measures open (> 100 $\Omega$) or ADC input channel is blown.

### Fault 11: Phase R/Y/B Current Unbalanced / Incorrect Value
- **Symptoms**: Clamp meter reads 15.0A, but Telegram reports 8.2A.
- **Possible Root Cause**: Calibration factor drift or mismatched CT burden resistor.
- **Safe Diagnostic Method**: Check current calibration table in NVS (`/calib view POLE-0001`).
- **Corrective Action**: Re-run Phase Current Calibration protocol using isolated AC test source.
- **When to Call Technician**: If recalibration fails to achieve $\pm 1\%$ linearity (defective CT core).

### Fault 12: Bus Voltage Reading Out of Range
- **Symptoms**: Bus voltage displays `340 VAC` or `45 VAC` (irrational values).
- **Possible Root Cause**: ZMPT101B primary resistor drifted or input clamping diode damaged.
- **Safe Diagnostic Method**: Measure actual mains voltage at input terminal block with calibrated DMM.
- **Corrective Action**: Verify 120 k$\Omega$ high-voltage primary resistor; re-run Voltage Calibration.
- **When to Call Technician**: If high voltage is leaking past the ZMPT101B isolation barrier.

### Fault 13: ADC Communication Failure / Degraded Subsystem
- **Symptoms**: Serial logs: `[MultiADC][ERROR] ADC 2 failed initialization!`
- **Possible Root Cause**: Hardware `/CS` line loose, missing `/SYNC` pulse, or SPI mode contention.
- **Safe Diagnostic Method**: Check continuity between ESP32 `GPIO 22` and ADS131M08 #2 pin 1 (`/CS`).
- **Corrective Action**: Reseat ADC board; check 3.3V AVDD and DVDD supply rails.
- **When to Call Technician**: If ADS131M08 Device ID register reads `0x0000` (silicon replacement needed).

### Fault 14: Feeder Relay Not Switching
- **Symptoms**: Telegram reports `Command Sent`, but contactor does not pull in.
- **Possible Root Cause**: 5V relay coil power supply dead, or JD-VCC jumper installed incorrectly.
- **Safe Diagnostic Method**: Check if optocoupler LED illuminates when command is sent; check 5V across `JD-VCC` and `GND2`.
- **Corrective Action**: Restore 5V auxiliary power to relay coil supply terminal.
- **When to Call Technician**: If coil supply is 5V and optocoupler fires, but contactor coil is burned out.

### Fault 15: Feeder MCB Tripped Frequently
- **Symptoms**: Telegram pushes repeated `MCB TRIP` alerts within seconds of turning on.
- **Possible Root Cause**: Overloaded circuit, short circuit in lamp wiring, or undersized breaker.
- **Safe Diagnostic Method**: Measure load insulation resistance with a 500V Megger (power OFF).
- **Corrective Action**: Inspect field cables for damaged insulation or water ingress inside lamp fixtures.
- **When to Call Technician**: High-voltage electrical contractor required to isolate cable fault.

### Fault 16: Contactor Feedback Mismatch / Contactor Welded
- **Symptoms**: Telegram alert: `🚨 CRITICAL: Feeder 1 Contactor Welded!`
- **Possible Root Cause**: Power contactor main contacts fused together due to high short-circuit current.
- **Safe Diagnostic Method**: Open manual MCB; inspect contactor armature visually (plunger stuck down).
- **Corrective Action**: Disconnect power; replace welded contactor assembly immediately.
- **When to Call Technician**: Immediate electrician dispatch required. Contactor replacement mandatory.

### Fault 17: Telegram Bot Disconnected / No Response to Commands
- **Symptoms**: Bot does not respond to `/start` or button clicks.
- **Possible Root Cause**: Wi-Fi internet lost, invalid Bot Token, or DNS failure.
- **Safe Diagnostic Method**: Check Gateway Serial log; ping `api.telegram.org` from mobile hotspot.
- **Corrective Action**: Power cycle Wi-Fi router; check Gateway connection status on mobile hotspot.
- **When to Call Technician**: If Gateway Wi-Fi module fails to associate with any router.

### Fault 18: Wi-Fi Credential Change Failed / Gateway Inaccessible
- **Symptoms**: Updated Wi-Fi credentials via Telegram; Gateway vanished from internet.
- **Possible Root Cause**: Typo in new Wi-Fi password.
- **Safe Diagnostic Method**: Check for broadcast of `MSCB-GW-RECOVERY` SoftAP network on smartphone.
- **Corrective Action**: Connect to `MSCB-GW-RECOVERY`, open `http://192.168.4.1`, and re-enter correct Wi-Fi details.
- **When to Call Technician**: If recovery SoftAP does not appear after 3 minutes (USB serial recovery needed).

### Fault 19: Configuration Parameters Not Saved Across Reboot
- **Symptoms**: Resetting node restores old thresholds or previous Wi-Fi SSID.
- **Possible Root Cause**: NVS sector corruption or premature power loss before NVS flush.
- **Safe Diagnostic Method**: Check Serial log for `[NVS][ERROR] CRC32 Mismatch`.
- **Corrective Action**: Issue `/reset factory` to reformat NVS partition and commit fresh configuration.
- **When to Call Technician**: If internal flash memory exhibits persistent write errors (module replacement).

### Fault 20: Over-The-Air Firmware Update Failure
- **Symptoms**: OTA transfer hangs at 45% or reports `CRC Mismatch`.
- **Possible Root Cause**: Packet loss on multi-hop LoRa link or corrupted binary file.
- **Safe Diagnostic Method**: Check Gateway OTA log for dropped chunk sequence numbers.
- **Corrective Action**: Re-attempt update with smaller patch size; flash binary directly over USB Type-C.
- **When to Call Technician**: If bootloader fails to rollback and node becomes unresponsive.
