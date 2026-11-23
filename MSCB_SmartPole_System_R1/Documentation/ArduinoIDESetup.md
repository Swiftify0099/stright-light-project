# Arduino IDE 2.x Configuration & Setup Guide

**Project**: MSCB Smart Pole & Feeder Automation System  
**Release**: System_R1 (Prompt 06 Modular Architecture)  
**Author**: Senior ESP32 Embedded Firmware Engineer & Arduino IDE Architect  
**Date**: November 2026  

---

## 1. Development Environment Specifications

- **Arduino IDE Version**: Arduino IDE 2.3.x (2.x Modern Series)
- **ESP32 Arduino Core**: Official Espressif `esp32:esp32` **Version 3.3.11** (or 3.x release)
- **Board Manager Index URL**:
  ```text
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
  ```

---

## 2. Controller Build Configurations & FQBN Reference

### A. Main Gateway Controller (ESP32-S3 N16R8)

#### Fully Qualified Board Name (FQBN)
```text
esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

#### Arduino IDE 2.x Tools Menu Settings
1. **Board**: `ESP32S3 Dev Module`
2. **Flash Size**: `16MB (128Mb)`
3. **Partition Scheme**: `16M Flash (3MB APP/9.9MB FATFS)` or `Huge APP (3MB No OTA)`
4. **PSRAM**: `OPI PSRAM` *(CRITICAL: Required to activate 8MB Octal PSRAM on N16R8)*
5. **USB CDC On Boot**: 
   - `Disabled` (Default: If monitoring via Type-C UART COM port)
   - `Enabled` (If monitoring directly via native USB Type-C port)
6. **CPU Frequency**: `240MHz (WiFi)`
7. **Flash Mode**: `QIO 80MHz`
8. **Upload Speed**: `921600`
9. **Port**: Select the COM port corresponding to the connected ESP32-S3

---

### B. Secondary Field Pole Node (Classic ESP-WROOM-32)

#### Fully Qualified Board Name (FQBN)
```text
esp32:esp32:esp32:FlashSize=4M,PartitionScheme=default,UploadSpeed=921600
```

#### Arduino IDE 2.x Tools Menu Settings
1. **Board**: `ESP32 Dev Module`
2. **Flash Size**: `4MB (32Mb)`
3. **Partition Scheme**: `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)`
4. **PSRAM**: `Disabled` *(Standard WROOM-32 has no PSRAM)*
5. **CPU Frequency**: `240MHz (WiFi/BT)`
6. **Flash Frequency**: `80MHz`
7. **Flash Mode**: `QIO`
8. **Upload Speed**: `921600`
9. **Port**: Select the COM port corresponding to the connected ESP32

---

## 3. Required Libraries Inventory

The following external and core libraries have been audited in this environment:

| Library Name | Installed Version | Source / Author | Compatibility Status | Used In Tier |
| :--- | :---: | :--- | :---: | :--- |
| **RadioLib** | **7.7.1** | Jan Gromes | **PASS (Production Ready)** | Gateway & Field Pole (SX1278 LoRa) |
| **LoRa** | **0.8.0** | Sandeep Mistry | **PASS (Legacy Alternative)**| Gateway & Field Pole |
| **ArduinoJson** | **7.4.3** | Benoit Blanchon | **PASS (V7 API)** | Gateway (Telegram & Mesh JSON payloads) |
| **UniversalTelegramBot**| **1.3.0** | Brian Lough | **PASS** | Gateway (Telegram Bot interface) |
| **WiFi** | Core 3.3.11 | Espressif Systems | **PASS (Built-in)** | Gateway (Wi-Fi Station / AP mode) |
| **SPI** | Core 3.3.11 | Espressif Systems | **PASS (Built-in)** | Gateway & Field Pole (LoRa & ADC SPI) |
| **Preferences** | Core 3.3.11 | Espressif Systems | **PASS (Built-in)** | Gateway & Field Pole (NVS flash storage) |
| **esp_chip_info** | ESP-IDF v5 | Espressif Systems | **PASS (Built-in)** | Gateway & Field Pole (Silicon diagnostics) |

> **IMPORTANT RULE**: Global library upgrades should not be performed arbitrarily without running regression compile tests against both Gateway and Pole sketches.

---

## 4. Shared Header Discovery Architecture (`MSCBShared/`)

In multi-project embedded workspaces developed in Arduino IDE 2.x, sketches are compiled in an isolated temporary directory (`%LOCALAPPDATA%\Temp\arduino\sketches\<hash>\`). Because the compilation occurs outside the workspace directory tree, relative directory references such as `../MSCBShared/` cannot be resolved unless the shared library is installed in the Arduino Sketchbook `libraries/` directory.

To achieve **100% compilation reliability without code duplication**, `MSCB_SmartPole_System_R1` packages `MSCBShared` as a standard Arduino library with `library.properties`, installed directly into `<Sketchbook>/libraries/MSCBShared`.

### 4.1 Automated PowerShell Installer (`install-mscbshared.ps1`)

An idempotent installer script is provided at `MSCB_SmartPole_System_R1/scripts/install-mscbshared.ps1`.

#### Standard Execution (Auto-detects Arduino Sketchbook):
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-mscbshared.ps1
```

#### Custom Sketchbook / OneDrive Execution:
If your Arduino sketchbook is located in OneDrive or a custom location:
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\install-mscbshared.ps1 -SketchbookPath "C:\Users\<YourUsername>\OneDrive\Documents\Arduino"
```

The installer verifies all 9 shared source and header files (`library.properties`, `BuildConfig.h`, `CommandSecurity.h`, `CommandSecurity.cpp`, `NetworkTypes.h`, `Protocol.h`, `ReplayProtection.h`, `ReplayProtection.cpp`, `StorageTypes.h`), copies them cleanly to `<Sketchbook>/libraries/MSCBShared`, and purges any accidental nested folders.

### 4.2 Dual-Mode Header Discovery Pattern

Headers in Gateway and Field Pole utilize automated dual-mode resolution:
```cpp
#if __has_include("../MSCBShared/BuildConfig.h")
  #include "../MSCBShared/BuildConfig.h"
  #include "../MSCBShared/NetworkTypes.h"
  #include "../MSCBShared/Protocol.h"
  #include "../MSCBShared/StorageTypes.h"
  #include "../MSCBShared/CommandSecurity.h"
  #include "../MSCBShared/ReplayProtection.h"
#elif __has_include(<BuildConfig.h>)
  #include <BuildConfig.h>
  #include <NetworkTypes.h>
  #include <Protocol.h>
  #include <StorageTypes.h>
  #include <CommandSecurity.h>
  #include <ReplayProtection.h>
#else
  #error "MSCBShared headers not found! Ensure MSCBShared directory is adjacent to sketch or installed in Arduino/libraries."
#endif
```

---

## 5. Step-by-Step Compilation Verification

1. Run the installer: `powershell -ExecutionPolicy Bypass -File .\scripts\install-mscbshared.ps1`
2. Launch Arduino IDE 2.x (or press `Ctrl+Shift+P` -> `Reload Window` if already open).
3. **Verify Gateway Target**:
   - Open `MSCB_SmartPole_System_R1/Gateway_ESP32S3/Gateway_ESP32S3.ino`.
   - Under **Tools -> Board**, select `ESP32S3 Dev Module`.
   - Ensure **PSRAM** is set to `OPI PSRAM` and **Flash Size** to `16MB`.
   - Click **Verify (Ctrl+R)**. Compilation must conclude with exit code 0 (`Sketch uses ~990KB (75%) of program storage space`).
4. **Verify Field Pole Target**:
   - Open `MSCB_SmartPole_System_R1/FieldPole_ESP32/FieldPole_ESP32.ino`.
   - Under **Tools -> Board**, select `ESP32 Dev Module`.
   - Ensure **PSRAM** is set to `Disabled` and **Flash Size** to `4MB`.
   - Click **Verify (Ctrl+R)**. Compilation must conclude with exit code 0 (`Sketch uses ~319KB (24%) of program storage space`).

---

## 6. Common Compilation Errors & Resolutions

| Compiler Error | Root Cause | Permanent Resolution |
| :--- | :--- | :--- |
| `fatal error: CommandSecurity.h: No such file or directory` | `MSCBShared` not installed in Arduino libraries | Run `.\scripts\install-mscbshared.ps1` and restart Arduino IDE |
| `undefined reference to cmdSecurity` | `#include <CommandSecurity.h>` not parsed by Arduino preprocessor | Ensure `#include <CommandSecurity.h>` is present in `.ino` and library is installed |
| `fatal error: mbedtls/sha256.h: No such file or directory` | ESP32 Core upgraded to 4.x where MbedTLS paths changed | Downgrade ESP32 Board Package to verified Core **3.3.11** |
