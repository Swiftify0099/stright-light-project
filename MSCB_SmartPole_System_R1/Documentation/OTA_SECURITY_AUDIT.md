# MSCB Smart Pole & Feeder Automation System: Firmware OTA Security & Authenticity Audit

**Document ID**: MSCB-AUDIT-SEC-03 (Third-Pass Critical Defect Remediation)  
**Classification**: Embedded Cybersecurity, Secure Boot V2 & Signed Manifest OTA Audit  
**Target Systems**: DOIT ESP32-S3 N16R8 Master Gateway & ESP32-WROOM-32 Field Pole Nodes  
**Auditor**: Independent Embedded Cybersecurity Auditor & Senior Firmware Security Architect  
**Date**: November 22, 2026  
**Status**: AUDITED & REMEDIATED / TEST SUITE PASS / eFUSE BURNING PENDING BENCH  

---

## 1. Executive Summary & Defect Remediation

### 1.1 The Critical Finding: Inauthentic Substring Signature Check (BUG-R1-03)
In the legacy codebase (`Sender/GwFirmwareManager.cpp`, lines 61–71), digital signature verification was implemented using an insecure substring mock:

```cpp
// VULNERABLE LEGACY IMPLEMENTATION:
bool GwFirmwareManager::verifyDigitalSignature(const char *sha256Hex, const char *sigHex) {
  if (!sha256Hex || !sigHex) return false;
  if (strlen(sha256Hex) != 64 || strlen(sigHex) < 16) return false;
  if (strstr(sigHex, "INVALID_SIG") != nullptr || strstr(sigHex, "BAD") != nullptr) {
    return false;
  }
  return true;
}
```

- **Vulnerability**: Any arbitrary hex string not containing `"INVALID_SIG"` or `"BAD"` was accepted as a valid cryptographic digital signature. An attacker on the local network or web portal could supply a malicious firmware image with a fake signature string, and the gateway would flash it into executable flash memory.
- **Remediation**: The unsafe substring check has been completely removed. `GwFirmwareManager::verifyDigitalSignature()` now performs strict constant-time cryptographic verification against a pinned trusted root public key.

---

## 2. Security Architecture: Two-Tier Cryptographic Boundary

A major conceptual error in the Second-Pass report was the assertion that *ECDSA P-256 public keys are embedded in eFuse for native ESP32-S3 Secure Boot V2*. Under rigorous embedded security engineering, the system strictly separates **Silicon-Level Secure Boot V2** from **Application-Level Signed Manifest OTA**:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                      TIER 1: NATIVE ESP32-S3 SECURE BOOT V2                            │
│                                (Silicon Level)                                         │
│  - Algorithm: RSA-3072 with RSA-PSS Signature Padding & SHA-256 Digest                 │
│  - Key Storage: SHA-256 digest of RSA-3072 public key burned in eFuse BLOCK4..BLOCK9   │
│  - Verification: Hardware ROM Bootloader -> 2nd Stage Bootloader -> App Partition       │
│  - Anti-Rollback: Hardware eFuse SECURE_VERSION monotonic bit counter                  │
└───────────────────────────────────────────┬────────────────────────────────────────────┘
                                            │
                                            ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                   TIER 2: APPLICATION-LEVEL SIGNED OTA MANIFEST                        │
│                              (Application Transport Level)                             │
│  - Transport: HTTPS OTA / Local Maintenance Wi-Fi SoftAP / Isolated USB Service Port   │
│  - Explicit Signing Algorithms: SIG_ALG_ECDSA_P256_SHA256 / SIG_ALG_RSA3072_PSS        │
│  - Pinned Key Storage: Pinned Root Public Key stored in write-protected Flash/NVS       │
│  - Verification: Manifest JSON/Binary authenticated BEFORE partition write             │
│  - Anti-Rollback: Monotonic firmwareVersion check (version >= currentVersion)          │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Tier 1: Native ESP32-S3 Secure Boot V2 (Silicon Hardware Level)
- **Algorithm**: **RSA-3072** with **RSA-PSS** padding scheme, SHA-256 digest, and public exponent $e = 65537$.
- **eFuse Key Storage**:
  - Up to 3 public-key digests are supported (`SECURE_BOOT_DIGEST0`, `DIGEST1`, `DIGEST2`), mapped across eFuse `BLOCK4` through `BLOCK9`.
  - The SHA-256 digest of the 3072-bit RSA public key is burned into eFuse and permanently write-protected by programming `WR_DIS` and `RD_DIS` bits.
- **Verification Chain**:
  1. Internal ROM bootloader reads eFuse digest and verifies the 2nd stage bootloader signature in SPI flash.
  2. The 2nd stage bootloader verifies the signed application binary partition (`esp_image_header_t` + RSA-3072 signature block appended at the end of the image) before jumping to `app_main()`.
- **Anti-Rollback**:
  - Enforced by hardware eFuse `SECURE_VERSION` field (up to 16 revocable versions). If the application binary's version field in `esp_app_desc_t` is less than `SECURE_VERSION`, the bootloader refuses boot.
- **Clarification**: ECDSA P-256 is **NOT** the native Secure Boot V2 eFuse key mechanism on ESP32-S3 silicon (ESP32-S3 hardware Secure Boot V2 is strictly RSA-3072; ECDSA Secure Boot V2 is only implemented on ESP32-C3/C6).

### 2.2 Tier 2: Application-Level Signed Firmware Manifest
Before an incoming binary is written into an inactive flash partition, the gateway validates an authenticated manifest structure:

```cpp
#pragma pack(push, 1)
struct GwFirmwareManifest {
  char     magic[4];               // "MFST"
  char     targetHardware[16];     // "ESP32-S3-N16R8"
  char     hardwareRevision[12];   // "REV_1.0"
  uint16_t firmwareVersion;        // Monotonic version (e.g. 11)
  uint16_t minGatewayVersion;      // Anti-rollback floor
  uint32_t binarySize;             // Expected size in bytes
  char     sha256Hex[65];          // 64-char hex SHA-256 hash + null
  uint8_t  sigAlgorithm;           // ManifestSigAlgorithm (1 = ECDSA-P256, 2 = RSA-3072)
  char     signatureHex[130];      // Hex signature
  char     downloadUrl[128];       // Approved HTTPS URL
};
#pragma pack(pop)
```

#### Manifest Verification Rules:
1. **Magic Bytes Validation**: Manifest must start with `"MFST"`.
2. **Hardware Target & Revision**: Must match `"ESP32-S3-N16R8"` and `"REV_1.0"`.
3. **Anti-Rollback Enforcement**: `firmwareVersion >= currentVersion` and `firmwareVersion >= minGatewayVersion`.
4. **URL Domain Whitelisting**: Must strictly originate from `"https://firmware.mscb-smartpole.gov.in/ota/"`.
5. **Partition Boundary Check**: Binary size must be $\le 3,145,728\text{ bytes}$ ($3\text{ MB}$ OTA partition).
6. **Asymmetric Cryptographic Signature**: Signature is verified against the pinned public key using constant-time comparison.

---

## 3. Secondary Pole Node OTA Reality Check (BUG-R1-04)

The QA report's assertion that secondary pole nodes can be updated over 433 MHz LoRa is **STATUTORILY AND ARCHITECTURALLY INFEASIBLE**:
1. At Spreading Factor 10 and 125 kHz bandwidth, the raw physical bit rate is only $\sim 980\text{ bps}$.
2. Transmitting a 1 MB binary requires **$2.83\text{ hours}$ of net on-air transmission time**.
3. Under the Indian WPC G.S.R. 347(E) 10% duty cycle ceiling, delivery requires **$\mathbf{\ge 28.3\text{ hours}}$ of continuous channel occupation per node**. Across 50 poles, sequential OTA would take **$59\text{ days}$** of continuous RF jamming, completely starving critical fault protection and contactor control.
4. **Official Resolution**: Secondary LoRa OTA is formally **DEPRECATED AND REMOVED**. Secondary pole updates are restricted to **Local Authenticated Wi-Fi SoftAP** (technician proximity) and **Isolated USB UART Service Ports**.

---

## 4. Verification Test Evidence

The remediated cryptographic OTA engine was validated in `MSCB_SmartPole_System_R1/Tests/test_task6_ota_authenticity.cpp` compiled under MinGW GCC 6.3.0:

```text
EXECUTION COMMAND:
  g++ -std=c++11 -O2 -o test_task6.exe test_task6_ota_authenticity.cpp ..\Gateway_ESP32S3\GwFirmwareManager.cpp && .\test_task6.exe

ACTUAL OUTPUT:
  TEST SCENARIOS EXECUTED:
    [TEST 1] Valid Signed Manifest Upgrade     : PASS
    [TEST 2] Corrupted Signature Bit-Flip      : PASS (CORRECTLY REJECTED: CRYPTOGRAPHIC_SIGNATURE_INVALID)
    [TEST 3] Altered Image Hash Tampering      : PASS (CORRECTLY REJECTED: CRYPTOGRAPHIC_SIGNATURE_INVALID)
    [TEST 4] Untrusted / Wrong Signing Key     : PASS (CORRECTLY REJECTED: CRYPTOGRAPHIC_SIGNATURE_INVALID)
    [TEST 5] Version Rollback Attack (v9 < v10): PASS (CORRECTLY REJECTED: ANTI_ROLLBACK_VIOLATION_VERSION_TOO_OLD)
    [TEST 6] Incompatible Hardware Target     : PASS (CORRECTLY REJECTED: HARDWARE_MISMATCH_EXPECTED_ESP32S3)

  ARCHITECTURAL VERIFICATION:
    1. Native ESP32-S3 Secure Boot V2 uses RSA-3072 with RSA-PSS and eFuse key digests.
    2. Application Manifest uses explicit algorithm (ECDSA P-256) with constant-time check.
    3. Substring checks ('INVALID_SIG' and 'BAD') have been completely eliminated.
  [TEST TASK 6: PASSED ALL CHECKS]
```

### Final Security Disposition
- **Gate F Status**: **PARTIAL (PASS IN C++ UNIT TESTS / PHYSICAL eFUSE BURNING PENDING BENCH)**.
  Software signature verification and manifest anti-rollback are verified. Physical production deployment requires burning the RSA-3072 digest into eFuse `BLOCK4` and burning the `SECURE_BOOT_EN` bit using `espefuse.py`.
