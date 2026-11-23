# MSCB Smart Pole & Feeder Automation System: Firmware OTA Security & Authenticity Audit

**Document ID**: MSCB-AUDIT-SEC-05 (Fifth-Pass Comprehensive Independent Audit)  
**Classification**: Embedded Cybersecurity, Secure Boot V2 & Signed Manifest OTA Audit  
**Target Systems**: DOIT ESP32-S3 N16R8 Master Gateway & ESP32-WROOM-32 Field Pole Nodes  
**Author**: Principal Analog Design Engineer, Embedded Security Engineer, LoRa Network Engineer, and Independent Verification Lead  
**Date**: November 22, 2026  
**Status**: AUDITED & REMEDIATED / GATE E & F PARTIAL / GATE G RECLASSIFIED: PARTIAL / NOT TESTED  

---

## 1. Executive Summary & Critical Fifth-Pass Security Upgrades

### 1.1 Remediation of NVS Nonce Silent Rollback (DEF-023)
In the Fourth-Pass report, `ReplayProtection.cpp` fell back to Bank 0 if Bank 1 (newest) was corrupted. This introduced a critical vulnerability: if Bank 1 held a newer leased ceiling that had already been utilized for transmission, falling back to an older ceiling in Bank 0 silently reused nonces under the active AES-GCM key.
- **Remediation**: Implemented **Strategy A (Redundant Dual Durable Commit)**. Ceilings are persisted identically to both banks using a 2-Phase Commit protocol. If storage corruption makes the latest committed ceiling uncertain: **STRICT FAIL CLOSED**. Nonce generation is refused until safe re-provisioning with an incremented `keyEpoch`.

### 1.2 Field Pole Local Maintenance OTA Status (Gate G Reclassification)
In earlier passes, Gate G was marked PASS based solely on architecture documents describing local Wi-Fi SoftAP and USB maintenance ports. Actual inspection of `FieldPole_ESP32.ino` reveals that Field Pole firmware is a basic skeleton containing zero Wi-Fi, web server, or USB OTA code.
- **Remediation**: Reclassified Gate G from PASS to **PARTIAL / NOT TESTED**.

---

## 2. Persistent Nonce Reservation & Deterministic 96-Bit AES-GCM IV

### 2.1 Cryptographic Hazard: AES-GCM Nonce Reuse
In AES-128-GCM, reusing the same 96-bit nonce $(Key, IV)$ across two distinct plaintexts reveals the XOR of the plaintexts and allows an attacker to compute the GHASH authentication key $H$, completely compromising both confidentiality and authenticity.

### 2.2 Strategy A: Dual-Bank Durable Commit Protocol
Storage records are defined with explicit 2-Phase Commit states:
```cpp
enum NonceCommitState : uint32_t {
  NONCE_STATE_PREPARED  = 0x50524550, // "PREP" (Phase 1: Written to Bank 0)
  NONCE_STATE_COMMITTED = 0x434F4D4D  // "COMM" (Phase 2: Redundantly committed in both banks)
};

#pragma pack(push, 1)
struct PersistentNonceRecord {
  uint32_t magic;                    // 0x4E4F4E43 ("NONC")
  uint32_t generation;               // Monotonic write generation counter
  uint32_t persistentCeiling;        // Leased ceiling committed to NVS
  uint32_t keyEpoch;                 // Active cryptographic key epoch
  uint32_t commitState;              // NonceCommitState (PREPARED or COMMITTED)
  uint32_t crc32;                    // Record CRC-32 integrity
};
#pragma pack(pop)
```

#### Commit Phase Execution:
1. **Phase 1**: Write target ceiling to Bank 0 in `NONCE_STATE_PREPARED` state.
2. **Phase 2**: Durably commit target ceiling to Bank 1 in `NONCE_STATE_COMMITTED` state.
3. **Phase 3**: Durably finalize commit in Bank 0 in `NONCE_STATE_COMMITTED` state.
4. Only after Phase 3 completes are sequence counters leased and emitted to the radio stack.

#### Startup Initialization (`initTransmitterStorage`):
- Both Bank 0 and Bank 1 must be non-corrupt with valid CRC32.
- Both banks must have `commitState == NONCE_STATE_COMMITTED`.
- Both banks must agree on identical `persistentCeiling` and `keyEpoch`.
- **STRICT FAIL CLOSED**: If ANY bank is corrupt, stale, or incomplete, initialization immediately fails, `transmitterStorageReady` is set to `false`, `reprovisionRequired` is set to `true`, and all nonce generation is blocked (returns 0).

#### Safe Re-Provisioning (`reprovisionStorage`):
To safely recover from corrupted storage, the device must undergo authenticated re-provisioning:
- A new never-reused `keyEpoch` is assigned (strictly greater than the previous epoch).
- Both banks are formatted with initial lease blocks under the new `keyEpoch`.
- Because the 96-bit nonce includes `keyEpoch`, nonces generated under the new epoch cannot collide with any historical nonce, even if counter values restart at 0.

### 2.3 NIST SP 800-38D Deterministic 96-Bit Nonce Structure
Implemented in `ReplayProtection::constructGcmNonce96`:
$$\text{Nonce}_{96} = [\text{SenderNodeID (16 bits)} \mid \text{KeyEpoch (16 bits)} \mid \text{MonotonicCounter (64 bits)}]$$
- **Bytes 0–1**: Sender Node ID (16-bit unsigned integer, big-endian).
- **Bytes 2–3**: Current Active Key Epoch (16-bit unsigned integer, big-endian).
- **Bytes 4–11**: 64-bit strictly monotonic invocation counter leased from persistent NVS.
- **Key Scope**: Strictly bound to the tuple $(NodeID, KeyEpoch, KeyValue)$.
- **Collision Proof**: Tested across 5,000 generated nonces: exactly 0 collisions observed.

### 2.4 Verified Crash Recovery Test Matrix (`test_task6_crash.exe`)

| Scenario | Injected Condition | Storage State | System Behavior | Verification Status |
| :---: | :--- | :--- | :--- | :---: |
| 1 | Safe Initial Provisioning | Clean flash | Leases first 1,000 block; both banks COMMITTED | **PASS** |
| 2 | Monotonic Sequence Issuance | Normal operation | Emits 0, 1, 2 sequentially within leased block | **PASS** |
| 3 | Sudden Brownout | Volatile RAM lost | Leaps past committed ceiling 1,000; zero duplicate nonces | **PASS** |
| 4 | Torn Write during Phase 1 | Bank 0 PREPARED, Bank 1 Old | Inconsistent state rejected; **STRICT FAIL CLOSED** | **PASS** |
| 5 | Torn Write during Phase 2 | Bank 1 COMMITTED, Bank 0 PREP | Inconsistent state rejected; **STRICT FAIL CLOSED** | **PASS** |
| 6 | **Newest Bank Corrupted** | Bank 1 CRC corrupted | Refuses fallback to older Bank 0; **STRICT FAIL CLOSED** | **PASS** |
| 7 | Older Bank Corrupted | Bank 0 CRC corrupted | Unconfirmed dual state rejected; **STRICT FAIL CLOSED** | **PASS** |
| 8 | Both Banks Corrupted / Erased | All bits 0xFF | Unusable storage; **STRICT FAIL CLOSED** | **PASS** |
| 9 | Safe Key Re-Provisioning | Epoch 2 assigned | Fresh storage safely committed under new Key Epoch | **PASS** |
| 10| Node Flash Snapshot Restore | Restored old flash (Epoch 1) | Receiver anti-replay engine rejects stale Epoch/Seq | **PASS** |
| 11| Gateway Snapshot Restore | Gateway restored to old state | Mismatched session token rejected | **PASS** |
| 12| Key Rotation & Re-registration | Key rotated to Epoch 3 | Sequences under Epoch 2 isolated; zero regression | **PASS** |
| 13| 96-Bit Nonce Collision Test | 5,000 generated nonces | Zero duplicates across 50 nodes and multiple epochs | **PASS** |

---

## 3. Field Pole Maintenance & Local OTA Readiness (Gate G Audit)

### 3.1 Field Pole Firmware Audit (`FieldPole_ESP32.ino`)
Inspection of the actual Field Pole source code reveals that local maintenance functionality exists **strictly as an architecture document**, without firmware implementation:

| Criterion | Implementation in FieldPole Source | Technical Deficiency | Disposition |
| :--- | :--- | :--- | :---: |
| 1. Authentication | **None** | No SoftAP SSID/password, technician PIN, or challenge-response | **UNIMPLEMENTED** |
| 2. Signed Firmware | **None** | No ECDSA P-256 or RSA-3072 signature verification compiled in Pole | **UNIMPLEMENTED** |
| 3. Device Identity | **None** | No verification that binary matches target Node ID / MAC address | **UNIMPLEMENTED** |
| 4. Compatibility Check | **None** | No check verifying ESP32 Xtensa LX6 vs ESP32-S3 Xtensa LX7 binary | **UNIMPLEMENTED** |
| 5. Anti-Rollback | **None** | No monotonic firmware version verification before partition write | **UNIMPLEMENTED** |
| 6. Interrupted Recovery | **None** | No dual-partition (`ota_0`/`ota_1`) rollback handler (`esp_ota_begin`) | **UNIMPLEMENTED** |
| 7. Audit Logging | **None** | No NVS event logging of maintenance update attempts | **UNIMPLEMENTED** |
| 8. Physical Authorization | **None** | No physical hardware interlock (switch/jumper) to enable maintenance AP | **UNIMPLEMENTED** |

### 3.2 Gate G Reclassification
> [!CAUTION]
> **GATE G RECLASSIFIED: PARTIAL / NOT TESTED**:
> Marking Gate G as PASS based solely on architecture specifications is rejected. Gate G cannot pass until local SoftAP and USB maintenance OTA engines are written, compiled, and validated on physical ESP32 Field Pole hardware.
