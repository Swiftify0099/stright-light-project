/*
 * =====================================================================
 *  CRYPTOMANAGER.H — MSCB CRYPTOGRAPHIC ENGINE & KEY MANAGEMENT
 *  Municipal Smart Control Box (MSCB) Smart Pole System
 *  ESP32-S3 Gateway & ESP32 Secondary Nodes (AES-128-CCM + CRC16)
 * =====================================================================
 */

#ifndef MSCB_CRYPTO_MANAGER_H
#define MSCB_CRYPTO_MANAGER_H

#include "NetworkTypes.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

class CryptoManager {
public:
    CryptoManager();
    ~CryptoManager();

    // Initialize cryptographic subsystem and hardware RNG / entropy
    bool init();

    // AES-128-CCM Authenticated Encryption with Associated Data (AEAD)
    // Key: 16 bytes, Nonce: 13 bytes, Tag: 16 bytes
    bool encrypt(const uint8_t *key,
                 const uint8_t *nonce,
                 const uint8_t *aad, size_t aadLen,
                 const uint8_t *plaintext, size_t plainLen,
                 uint8_t *ciphertext,
                 uint8_t *tag);

    // AES-128-CCM Authenticated Decryption with Associated Data (AEAD)
    // Returns true ONLY if tag matches exactly in constant time.
    bool decrypt(const uint8_t *key,
                 const uint8_t *nonce,
                 const uint8_t *aad, size_t aadLen,
                 const uint8_t *ciphertext, size_t cipherLen,
                 const uint8_t *tag,
                 uint8_t *plaintext);

    // Key Derivation Function (KDF):
    // Derives mathematically distinct 128-bit Node Key from Master Key, Node ID & HW UUID
    bool deriveNodeKey(const uint8_t *masterKey,
                       uint16_t nodeId,
                       const uint8_t *hwId, size_t hwIdLen,
                       uint8_t *outNodeKey);

    // Fast CRC16-CCITT Radio Integrity Check
    static uint16_t calculateCRC16(const uint8_t *data, size_t len);
    static bool     verifyCRC16(const uint8_t *data, size_t len, uint16_t expectedCrc);

    // Constant-time memory comparison to prevent timing attacks
    static bool constantTimeCompare(const uint8_t *a, const uint8_t *b, size_t len);

    // Secure zeroization of sensitive memory buffers
    static void zeroize(void *buf, size_t len);

private:
    bool initialized;
};

extern CryptoManager cryptoMgr;

#endif // MSCB_CRYPTO_MANAGER_H
