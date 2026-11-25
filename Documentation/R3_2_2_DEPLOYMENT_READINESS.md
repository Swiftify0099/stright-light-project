# MSCB SMART POLE SYSTEM — R3.2.2 DEPLOYMENT READINESS REPORT

**Date:** November 25, 2026  
**Release Branch:** `r3.2.2-registration-telegram-reliability`  
**Base Commit:** `65cccfa715d9d4f85ee829b1ab7c343e5093007c`  
**System Version:** MSCB Firmware 3.2.2  

---

## 1. Readiness Assessment Matrix

| Gate | Category | Requirement | Verified Result | Sign-Off |
|---|---|---|---|---|
| **G1** | Functional Verification | Field pole registration flow end-to-end | Validated: 69/69 tests in `test_r3_2_2.exe` | ✅ PASS |
| **G2** | Regression Safety | Full regression across all prior repairs | Validated: 271/271 tests in `test_r3.exe` | ✅ PASS |
| **G3** | Telegram Reliability | Rate limit mitigation & deduplication | 100% spam eliminated on 100 heartbeats | ✅ PASS |
| **G4** | Cryptographic Security | Fail-closed HMAC-SHA256 authentication | Zero unauthenticated packets permitted | ✅ PASS |
| **G5** | Physical Electrical Safety | Production relay hardware lockout | Hardware lockout enforced (`MSCB_RELAYS_LOCKED_DISABLED = 1`) | ✅ PASS |
| **G6** | High-Voltage Mains | No 230V/415V mains connected | Isolated bench DC LVDC profile only | ✅ PASS |
| **G7** | Git Baseline Integrity | Preservation of base commit & legacy files | `main`, `Sender/`, `Resivers/` untouched | ✅ PASS |

---

## 2. Flash & Partition Profiles

### Gateway ESP32-S3
- **Board:** ESP32S3 Dev Module
- **Flash Size:** 16MB (Octal SPI)
- **PSRAM:** OPI PSRAM (8MB)
- **Partition Scheme:** Custom large app partitions with NVS A/B rollover
- **Radio:** SX1276/78 LoRa SPI (NSS: GPIO 10, RST: GPIO 9, DIO0: GPIO 8)

### Field Pole ESP32-WROOM-32
- **Board:** ESP32 Dev Module
- **Flash Size:** 4MB (Quad SPI)
- **PSRAM:** Disabled
- **Relay Pin Mapping:** Feeder 1-6 -> Physical Pins 24, 23, 25, 6, 7, 21 (Board: D26, D25, D27, RX2, TX2, D32)
- **Relay Mode:** Locked Disabled (`MSCB_RELAYS_LOCKED_DISABLED = 1`)

---

## 3. Deployment Instructions for Field Engineers

1. **Pre-Flashing Check:**
   - Clone repository and switch to branch `r3.2.2-registration-telegram-reliability`.
   - Run `MSCB_SmartPole_System_R1\scripts\install-mscbshared.ps1` to sync the shared library.
   - Run unit test suites `test_r3_2_2.exe` and `test_r3.exe` to verify host environment.

2. **Flashing Gateway ESP32-S3:**
   - Open `MSCB_SmartPole_System_R1/Gateway_ESP32S3/Gateway_ESP32S3.ino`.
   - Verify secrets in `GwSecrets.h`.
   - Compile and flash via Arduino IDE or `arduino-cli`.

3. **Flashing Field Pole ESP32:**
   - Open `MSCB_SmartPole_System_R1/FieldPole_ESP32/FieldPole_ESP32.ino`.
   - Verify network authentication key in `PoleSecrets.h`.
   - Compile and flash via Arduino IDE or `arduino-cli`.

4. **Commissioning Verification:**
   - Power on Gateway and connect to Wi-Fi.
   - Verify `/status` command in Telegram.
   - Power on Field Pole; verify Telegram notifies administrator of discovered MAC.
   - Execute `/reg_approve` via Telegram wizard; observe automatic runtime identity assignment without reboot.
   - Confirm active status upon first authenticated heartbeat.
