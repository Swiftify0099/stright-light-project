# 16 — MSCB Smart Pole Automation System: Arduino IDE Setup & Build Guide

**Document ID**: MSCB-DOC-16  
**Classification**: Software Toolchain & Build Verification  
**IDE Version**: Arduino IDE 2.3.x (Modern Series)  
**Core Version**: Espressif `esp32:esp32` **Version 3.3.11**  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Toolchain & Development Environment Prerequisites

To ensure 100% reproducible compilation without missing dependency errors, the engineering team must install the following exact software packages:

1. **Integrated Development Environment**: **Arduino IDE 2.3.2** or later 2.3.x series.
2. **Espressif Board Package**: `esp32` by Espressif Systems **Version 3.3.11**.
   - Board Index URL (Add to File $\rightarrow$ Preferences $\rightarrow$ Additional Board Manager URLs):
     ```text
     https://espressif.github.io/arduino-esp32/package_esp32_index.json
     ```
3. **USB-to-UART Drivers**:
   - CH343 / CH340 Driver (for Gateway ESP32-S3 UART Port 2).
   - CP210x / CH340 Driver (for Classic ESP32 Field Pole DevKit).

---

## 2. Verified External Library Inventory

Install the following exact library versions via the Arduino Library Manager:

| Library Name | Exact Verified Version | Author | Purpose / Used In Tier |
| :--- | :---: | :--- | :--- |
| **RadioLib** | **7.7.1** | Jan Gromes | SX1278 LoRa SPI Radio (Gateway & Pole) |
| **LoRa** | **0.8.0** | Sandeep Mistry | Legacy LoRa Transceiver Alternative |
| **UniversalTelegramBot**| **1.3.0** | Brian Lough | Telegram HTTPS Bot API (Gateway Tier) |
| **ArduinoJson** | **7.4.3** | Benoit Blanchon | V7 High-Performance JSON Codec |
| **WiFi** | Built-in (3.3.11) | Espressif Systems | 2.4 GHz Wi-Fi Station & SoftAP (Gateway) |
| **SPI** | Built-in (3.3.11) | Espressif Systems | High-Speed Hardware SPI (FSPI & VSPI) |
| **Preferences** | Built-in (3.3.11) | Espressif Systems | Non-Volatile Flash Storage (NVS) |
| **MSCBShared** | **1.0.0** | MSCB Team | Shared Security, Framing & Protocol (See Section 2.1) |

---

## 2.1 Shared Library Installation (`MSCBShared`)

### Root Cause of Missing-Header Compilation Errors
When compiling `Gateway_ESP32S3.ino` or `FieldPole_ESP32.ino` inside Arduino IDE 2.x, the build system copies sketch source files into an isolated temporary build sandbox (`%LOCALAPPDATA%\Temp\arduino\sketches\<hash>\`). Because the compilation occurs outside the project directory tree, relative directory references such as `../MSCBShared/` cannot be resolved by the compiler preprocessor. If `MSCBShared` is not registered in the Arduino Sketchbook `libraries/` directory, the build terminates immediately with:
```text
fatal error: CommandSecurity.h: No such file or directory
#include "CommandSecurity.h"
compilation terminated.
exit status 1
```

### Automated PowerShell Installer
An idempotent PowerShell installer is provided in `MSCB_SmartPole_System_R1/scripts/install-mscbshared.ps1` to automatically install and update the library.

#### Standard Installation (Auto-detects Sketchbook path):
Open Windows PowerShell from `MSCB_SmartPole_System_R1` directory:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-mscbshared.ps1
```

#### Custom Sketchbook / OneDrive Installation:
If your Arduino sketchbook is located inside OneDrive or a custom folder, supply the `-SketchbookPath` parameter:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-mscbshared.ps1 -SketchbookPath "C:\Users\<YourUsername>\OneDrive\Documents\Arduino"
```

#### Installed Directory Structure:
```text
<Sketchbook>/
  libraries/
    MSCBShared/
      library.properties
      BuildConfig.h
      CommandSecurity.h
      CommandSecurity.cpp
      NetworkTypes.h
      Protocol.h
      ReplayProtection.h
      ReplayProtection.cpp
      StorageTypes.h
```

After running the installer, reload Arduino IDE (`Ctrl+Shift+P` $\rightarrow$ `Reload Window` or restart Arduino IDE). All shared headers (`CommandSecurity.h`, `ReplayProtection.h`, `Protocol.h`, etc.) and implementation classes will be automatically discovered, indexed, and linked.

---

## 3. Independent Controller Build Configurations

### 3.1 Gateway Controller (ESP32-S3 N16R8)

Open sketch: `Sender/Sender.ino` (or `MSCB_SmartPole_System_R1/Gateway_ESP32S3/Gateway_ESP32S3.ino`).

#### Tools Menu Settings:
- **Board**: `ESP32S3 Dev Module`
- **Flash Size**: `16MB (128Mb)`
- **PSRAM**: `OPI PSRAM` *(CRITICAL: Activates the 8MB Octal PSRAM on N16R8)*
- **Partition Scheme**: `16M Flash (3MB APP/9.9MB FATFS)` or `Huge APP (3MB No OTA)`
- **USB CDC On Boot**: `Disabled` (if using USB-UART Type-C) or `Enabled` (if using Native USB Type-C)
- **CPU Frequency**: `240MHz (WiFi)`
- **Flash Mode**: `QIO 80MHz`
- **Upload Speed**: `921600`
- **Fully Qualified Board Name (FQBN)**:
  ```text
  esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
  ```

### 3.2 Field Pole Controller (Classic ESP-WROOM-32)

Open sketch: `Resivers/Resivers.ino` (or `MSCB_SmartPole_System_R1/FieldPole_ESP32/FieldPole_ESP32.ino`).

#### Tools Menu Settings:
- **Board**: `ESP32 Dev Module`
- **Flash Size**: `4MB (32Mb)`
- **PSRAM**: `Disabled` *(Standard ESP-WROOM-32 has no PSRAM)*
- **Partition Scheme**: `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`
- **CPU Frequency**: `240MHz (WiFi/BT)`
- **Flash Mode**: `QIO 80MHz`
- **Upload Speed**: `921600`
- **Fully Qualified Board Name (FQBN)**:
  ```text
  esp32:esp32:esp32:FlashSize=4M,PartitionScheme=default,UploadSpeed=921600
  ```

---

## 4. Compilation & Flashing Procedure

### 4.1 Step-by-Step Build Verification
1. Launch Arduino IDE 2.3.x.
2. Select the target sketch (`Sender.ino` or `Resivers.ino`).
3. Verify board and menu settings as detailed in Section 3.
4. Click **Verify** (`Ctrl+R`). The compiler must complete with code 0:
   `Sketch uses ~950,000 bytes (30%) of program storage space.`
5. Connect target hardware via USB cable.
6. Select the assigned COM port.
7. Click **Upload** (`Ctrl+U`).

### 4.2 Serial Diagnostics
- Open Serial Monitor (`Ctrl+Shift+M`).
- Set baud rate to **115200 baud**.
- Expected Gateway Boot Output:
  ```text
  ==================================================
    SMART STREET LIGHT MASTER GATEWAY GW_V10
  ==================================================
  [NVS] Storage initialized with CRC32 verification.
  [LORA] SX1278 Initialized at 433.920 MHz (SF10, BW125k, CR4/5).
  [WIFI] Connected to MSCB_GATEWAY_NET (IP: 192.168.1.105).
  [TG] Telegram Bot Online. Session Manager Active.
  ```
- Expected Pole Boot Output:
  ```text
  ==================================================
    SMART STREET LIGHT FIELD POLE FIRMWARE POLE_V10
  ==================================================
  [SAFETY] Relays locked OFF at boot (GPIO 26,25,27,16,17,32 HIGH).
  [MultiADC] All 3x ADS131M08 ADCs initialized and phase-locked!
  [LORA] SX1278 Radio Initialized.
  [MESH] Searching for Primary Parent...
  ```

---

## 5. Build Troubleshooting & Silicon Error Codes

| Compiler / Flasher Error | Root Cause | Solution |
| :--- | :--- | :--- |
| `fatal error: CommandSecurity.h: No such file` | `MSCBShared` not in Arduino libraries | Run `powershell -File .\scripts\install-mscbshared.ps1` and restart IDE |
| `fatal error: BuildConfig.h: No such file` | Path to `MSCBShared` not found | Run `powershell -File .\scripts\install-mscbshared.ps1` |
| `undefined reference to cmdSecurity` | `MSCBShared` not linked as library | Ensure `#include <CommandSecurity.h>` is present in `.ino` and library is installed |
| `A fatal error occurred: Failed to connect to ESP32` | ESP32 not in download mode | Hold `BOOT` button, tap `RST` button, then release `BOOT` |
| `Guru Meditation Error: Core 0 panic'ed (Cache disabled)`| GPIO 26-37 accessed on ESP32-S3 | Ensure no firmware pin assignment overlaps Octal PSRAM range |
| `flash read err, 1000` (ESP32 Boot Loop) | GPIO 12 pulled HIGH at reset | Ensure Feeder 4 is on GPIO 16 (NOT GPIO 12!) |
| `Sketch too big error` | Partition scheme too small | Change Partition Scheme to `Huge APP (3MB No OTA)` |
