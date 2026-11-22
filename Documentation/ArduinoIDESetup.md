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

In multi-project embedded workspaces developed in Arduino IDE, sharing common protocol definitions across multiple sketches presents a challenge because Arduino IDE restricts sketch tabs to the sketch folder.

To achieve **100% compilation reliability without brittle symbolic links or code duplication**, `MSCB_SmartPole_System_R1` implements a **Dual-Mode Discovery Pattern**:

```cpp
// GwConfig.h and PoleConfig.h automated resolution
#if __has_include("../MSCBShared/BuildConfig.h")
  #include "../MSCBShared/BuildConfig.h"
  #include "../MSCBShared/NetworkTypes.h"
  #include "../MSCBShared/Protocol.h"
#elif __has_include(<BuildConfig.h>)
  #include <BuildConfig.h>
  #include <NetworkTypes.h>
  #include <Protocol.h>
#else
  #error "MSCBShared headers not found! Ensure MSCBShared directory is adjacent to sketch or installed in Arduino/libraries."
#endif
```

### Discovery Mode 1: Relative Path (Default Workspace Mode)
When opening `Gateway_ESP32S3/Gateway_ESP32S3.ino` or `FieldPole_ESP32/FieldPole_ESP32.ino` directly from the `MSCB_SmartPole_System_R1` workspace directory, GCC automatically resolves `../MSCBShared/` relative to the sketch path.

### Discovery Mode 2: Standard Arduino Library Mode (Optional)
The `MSCBShared/` directory includes a valid `library.properties` file. If desired, developers can create a directory link (or copy) `MSCBShared` into their local Arduino libraries folder:
- **Windows**: `mklink /J "%USERPROFILE%\Documents\Arduino\libraries\MSCBShared" "C:\Users\...\PS\MSCB_SmartPole_System_R1\MSCBShared"`
The `__has_include(<BuildConfig.h>)` fallback immediately discovers the headers globally.

---

## 5. Step-by-Step Compilation Verification

1. Launch Arduino IDE 2.x.
2. Open `MSCB_SmartPole_System_R1/Gateway_ESP32S3/Gateway_ESP32S3.ino`.
3. Under **Tools -> Board**, select `ESP32S3 Dev Module`.
4. Ensure **PSRAM** is set to `OPI PSRAM` and **Flash Size** to `16MB`.
5. Click **Verify (Ctrl+R)**. Compilation must conclude with exit code 0.
6. Open `MSCB_SmartPole_System_R1/FieldPole_ESP32/FieldPole_ESP32.ino`.
7. Under **Tools -> Board**, select `ESP32 Dev Module`.
8. Ensure **PSRAM** is set to `Disabled` and **Flash Size** to `4MB`.
9. Click **Verify (Ctrl+R)**. Compilation must conclude with exit code 0.
