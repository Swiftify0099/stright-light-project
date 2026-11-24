#!/usr/bin/env python3
"""
verify-keys.py — Safe MSCB Network Key Provisioning Validation Checker
Validates that Gateway and Field Pole configuration files have identical 32-byte
cryptographic keys and key epochs WITHOUT EVER printing or exposing raw key bytes.
"""

import os
import re
import sys

def parse_key_and_epoch(file_path):
    if not os.path.isfile(file_path):
        return None, None
    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    # Match MSCB_PROVISIONED_KEY_EPOCH
    epoch_m = re.search(r"#define\s+MSCB_PROVISIONED_KEY_EPOCH\s+(\d+)", content)
    epoch = int(epoch_m.group(1)) if epoch_m else None

    # Match MSCB_PROVISIONED_AES_KEY block
    key_block_m = re.search(r"#define\s+MSCB_PROVISIONED_AES_KEY\s*\{([^}]+)\}", content, re.DOTALL)
    if not key_block_m:
        return None, epoch

    raw_bytes_str = key_block_m.group(1)
    hex_tokens = re.findall(r"0x([0-9a-fA-F]{2})", raw_bytes_str)
    key_bytes = [int(h, 16) for h in hex_tokens]
    return bytes(key_bytes), epoch

def compute_crc16(key_bytes):
    crc = 0xFFFF
    for b in key_bytes:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc = (crc >> 1)
    return crc & 0xFFFF

def main():
    base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    
    gw_secrets = os.path.join(base_dir, "Gateway_ESP32S3", "GwSecrets.h")
    if not os.path.isfile(gw_secrets):
        gw_secrets = os.path.join(base_dir, "Gateway_ESP32S3", "GwSecrets.h.example")

    pole_secrets = os.path.join(base_dir, "FieldPole_ESP32", "PoleSecrets.h")
    if not os.path.isfile(pole_secrets):
        pole_secrets = os.path.join(base_dir, "FieldPole_ESP32", "PoleSecrets.h.example")

    print("================================================================")
    print("  MSCB SMART POLE SYSTEM - PROVISIONED KEY INTEGRITY CHECKER   ")
    print("================================================================")
    print(f"Gateway Source   : {os.path.basename(gw_secrets)}")
    print(f"Field Pole Source: {os.path.basename(pole_secrets)}")

    gw_key, gw_epoch = parse_key_and_epoch(gw_secrets)
    pole_key, pole_epoch = parse_key_and_epoch(pole_secrets)

    if not gw_key or len(gw_key) != 32:
        print("[FAIL] Gateway key missing or invalid length (must be 32 bytes)!")
        sys.exit(1)

    if not pole_key or len(pole_key) != 32:
        print("[FAIL] Field Pole key missing or invalid length (must be 32 bytes)!")
        sys.exit(1)

    gw_fp = compute_crc16(gw_key)
    pole_fp = compute_crc16(pole_key)

    print(f"Gateway Key Fingerprint   : FP-{gw_fp:04X} (Key Epoch: {gw_epoch})")
    print(f"Field Pole Key Fingerprint: FP-{pole_fp:04X} (Key Epoch: {pole_epoch})")

    if gw_key == pole_key and gw_epoch == pole_epoch:
        print("----------------------------------------------------------------")
        print("[SUCCESS] Provisioning validated: Gateway and Field Pole keys MATCH perfectly!")
        print("No raw key bytes were printed or logged.")
        sys.exit(0)
    else:
        print("----------------------------------------------------------------")
        print("[ERROR] Key or Epoch MISMATCH detected! Nodes cannot authenticate each other.")
        sys.exit(1)

if __name__ == "__main__":
    main()
