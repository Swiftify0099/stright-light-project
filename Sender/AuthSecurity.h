/*
 * =====================================================================
 *  AUTHSECURITY.H — LORA MESH CRYPTOGRAPHIC & AUTHORIZATION SYSTEM
 *  MSCB Smart Pole & Master Gateway Security Framework
 * =====================================================================
 */

#ifndef AUTH_SECURITY_H
#define AUTH_SECURITY_H

#include <Arduino.h>
#include "config.h"

class AuthSecurity {
public:
  AuthSecurity();
  void init();

  // Transaction ID and confirmation tokens
  String generateTransactionId(const String &prefix, const String &targetId);
  String generateConfirmationToken();

  // HMAC-SHA256 cryptographic signing and verification (Global/Mesh Key)
  String computeHMAC(const String &key, const String &data);
  String signPayload(const String &payload, uint16_t seq, const String &txId);
  bool   verifyPayloadSignature(const String &payload, uint16_t seq, const String &txId, const String &signature);

  // HMAC-SHA256 with Independent Node Keys
  String computeHMACWithKey(const uint8_t *key, size_t keyLen, const String &data);
  String signPayloadWithKey(const uint8_t *key, size_t keyLen, const String &payload, uint16_t seq, const String &txId);
  bool   verifyPayloadSignatureWithKey(const uint8_t *key, size_t keyLen, const String &payload, uint16_t seq, const String &txId, const String &signature);

  // Constant-time memory/string comparison to mitigate timing attacks
  static bool constantTimeCompare(const String &a, const String &b);

  // Administrator authorization (Fail-Closed)
  bool   verifyAdmin(const String &adminId);

  // Replay protection tracking
  bool   isNonceUsed(const String &txId);
  void   markNonceUsed(const String &txId);

private:
  static const uint8_t NONCE_CACHE_DEPTH = 64;
  String recentTxIds[NONCE_CACHE_DEPTH];
  uint8_t txHead;
};

extern AuthSecurity authSec;

#endif // AUTH_SECURITY_H
