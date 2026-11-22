# 15 — MSCB Smart Pole Automation System: Firmware Management & OTA Update Guide

**Document ID**: MSCB-DOC-15  
**Classification**: Embedded Software Deployment & OTA Architecture  
**Firmware Baseline**: Gateway `GW_V10` / Field Pole `POLE_V10`  
**Date**: November 2026  
**Status**: Authoritative Handover Documentation  

---

## 1. Dual-Tier Firmware Update Architecture

The MSCB Smart Pole System employs two distinct firmware deployment mechanisms based on available bandwidth:
1. **High-Speed Gateway OTA (Wi-Fi / Web Portal)**: Utilizes the ESP32-S3 high-bandwidth 2.4 GHz Wi-Fi radio and standard dual-OTA partition scheme (`app0` / `app1`).
2. **Sub-GHz LoRa Mesh Binary Distribution**: A specialized chunked transport protocol designed for delivering micro-patches and configuration updates across the multi-hop 433 MHz LoRa mesh.

---

## 2. Gateway Local OTA & Partition Table Architecture

The Gateway ESP32-S3 N16R8 operates with a custom 16MB partition layout (`app3M_fat9M_16MB`):

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ESP32-S3 16MB FLASH PARTITION LAYOUT                     │
├──────────────┬────────────┬─────────────┬─────────────┬─────────────────────┤
│ Name         │ Type       │ Subtype     │ Size        │ Purpose             │
├──────────────┼────────────┼─────────────┼─────────────┼─────────────────────┤
│ nvs          │ data       │ nvs         │ 64 KB       │ NVS Configuration   │
│ otadata      │ data       │ ota         │ 8 KB        │ Active Boot Pointer │
│ app0         │ app        │ ota_0       │ 3.0 MB      │ Primary Firmware    │
│ app1         │ app        │ ota_1       │ 3.0 MB      │ OTA Rollback Bank   │
│ fatfs        │ data       │ fat         │ 9.9 MB      │ Audit Logs / Assets │
└──────────────┴────────────┴─────────────┴─────────────┴─────────────────────┘
```

### 2.1 Gateway OTA Upgrade Procedure
1. When connected to the local Wi-Fi router or `MSCB-GW-RECOVERY` SoftAP, access the OTA page at `http://<gateway_ip>/update`.
2. Authenticate using the Admin password.
3. Select the compiled binary (`Sender.ino.bin`).
4. The ESP32-S3 streams the binary into the inactive partition (`app1`), computes SHA-256 integrity, updates `otadata`, and reboots.
5. If the new firmware fails to initialize Wi-Fi or LoRa within 60 seconds, the hardware watchdog triggers automatic rollback to `app0`.

---

## 3. Sub-GHz LoRa Mesh Binary Distribution Protocol

For remote field poles where physical USB access is impractical, `GwFirmwareManager.cpp` and `PoleFirmwareManager.cpp` implement a chunked distribution protocol:

### 3.1 Binary Chunking & Framing
- **Chunk Size**: Fixed at 100 bytes (`OTA_CHUNK_SIZE = 100`).
- **Inter-Packet Staggering**: 3,000 ms interval between successive chunks (`OTA_TX_INTERVAL_MS = 3000`) to comply with legal duty cycle constraints.
- **Verification**: Each chunk contains a 32-bit chunk sequence ID and CRC-16. The complete binary image terminates with a 32-bit CRC32 and RSA/ECC digital signature.

### 3.2 Bandwidth & Airtime Reality Warning

> [!WARNING]
> **SUB-GHZ LORA BANDWIDTH CONSTRAINTS**:
> Operating at Spreading Factor 10 (SF10) and 125 kHz bandwidth under Indian WPC duty cycle limitations ($\le 10\%$), the effective net payload data rate is approximately **300 to 450 bits per second**.
>
> Transferring a full 1.2 MB ESP32 application binary over multi-hop LoRa would require over **28 to 36 hours of continuous channel occupation**, which violates duty cycle laws and risks packet drops.
>
> Therefore, **LoRa OTA is strictly reserved for**:
> 1. Differential micro-patches (< 10 KB, ~15–20 minutes transfer).
> 2. Configuration script updates (< 2 KB, ~3 minutes transfer).
> 3. Metrology coefficient updates (< 500 bytes, ~45 seconds transfer).
>
> **Full firmware re-flashing MUST be performed locally over USB Type-C / UART.**
