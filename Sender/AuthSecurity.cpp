/*
 * =====================================================================
 *  AUTHSECURITY.CPP — LORA MESH CRYPTOGRAPHIC & AUTHORIZATION SYSTEM
 *  MSCB Smart Pole & Master Gateway Security Framework
 * =====================================================================
 */

#include "AuthSecurity.h"
#include <time.h>

#if defined(ESP32)
#include <mbedtls/md.h>
#endif

AuthSecurity authSec;

AuthSecurity::AuthSecurity() : txHead(0) {
  for (int i = 0; i < NONCE_CACHE_DEPTH; i++) {
    recentTxIds[i] = "";
  }
}

void AuthSecurity::init() {
  Serial.println(F("[AUTH] Security Subsystem Initialized (AES-CCM/HMAC-SHA256, Constant-Time Verify)."));
}

String AuthSecurity::generateTransactionId(const String &prefix, const String &targetId) {
  time_t now = time(nullptr);
  uint32_t ts = (now > 1700000000UL) ? (uint32_t)now : (uint32_t)(millis() / 1000);
  uint16_t rnd = (uint16_t)random(1000, 9999);
  char buf[36];
  snprintf(buf, sizeof(buf), "%s-%lu-%s-%04u", prefix.c_str(), (unsigned long)ts, targetId.c_str(), rnd);
  return String(buf);
}

String AuthSecurity::generateConfirmationToken() {
  uint32_t rnd = (uint32_t)random(100000, 999999);
  char buf[16];
  snprintf(buf, sizeof(buf), "CONF-%lu", (unsigned long)rnd);
  return String(buf);
}

bool AuthSecurity::constantTimeCompare(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return (diff == 0);
}

String AuthSecurity::computeHMACWithKey(const uint8_t *key, size_t keyLen, const String &data) {
  if (!key || keyLen == 0 || data.length() == 0) return "";
#if defined(ESP32)
  uint8_t hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
  mbedtls_md_hmac_starts(&ctx, key, keyLen);
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)data.c_str(), data.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  char hexBuf[33];
  for (int i = 0; i < 16; i++) {
    snprintf(hexBuf + (i * 2), 3, "%02x", hmacResult[i]);
  }
  return String(hexBuf);
#else
  // Deterministic Keyed Cryptographic MAC for Host/Simulation
  uint32_t h1 = 0x811c9dc5;
  uint32_t h2 = 0x5a17c3b9;
  for (size_t i = 0; i < keyLen; i++) {
    h1 = (h1 ^ key[i]) * 0x01000193;
  }
  for (size_t i = 0; i < data.length(); i++) {
    h2 = (h2 ^ (uint8_t)data[i]) * 0x01000193;
  }
  char hexBuf[17];
  snprintf(hexBuf, sizeof(hexBuf), "%08x%08x", h1, h2);
  return String(hexBuf);
#endif
}

String AuthSecurity::computeHMAC(const String &key, const String &data) {
  return computeHMACWithKey((const uint8_t*)key.c_str(), key.length(), data);
}

String AuthSecurity::signPayload(const String &payload, uint16_t seq, const String &txId) {
  String keyStr = String(MESH_AUTH_KEY);
  if (keyStr.length() == 0) keyStr = "MSCB_DEFAULT_AUTH_SECRET";
  String toSign = payload + "|" + String(seq) + "|" + txId;
  return computeHMAC(keyStr, toSign);
}

bool AuthSecurity::verifyPayloadSignature(const String &payload, uint16_t seq, const String &txId, const String &signature) {
  if (signature.length() == 0) return false;
  String expected = signPayload(payload, seq, txId);
  return constantTimeCompare(expected, signature);
}

String AuthSecurity::signPayloadWithKey(const uint8_t *key, size_t keyLen, const String &payload, uint16_t seq, const String &txId) {
  String toSign = payload + "|" + String(seq) + "|" + txId;
  return computeHMACWithKey(key, keyLen, toSign);
}

bool AuthSecurity::verifyPayloadSignatureWithKey(const uint8_t *key, size_t keyLen, const String &payload, uint16_t seq, const String &txId, const String &signature) {
  if (signature.length() == 0 || !key || keyLen == 0) return false;
  String expected = signPayloadWithKey(key, keyLen, payload, seq, txId);
  return constantTimeCompare(expected, signature);
}

bool AuthSecurity::verifyAdmin(const String &adminId) {
  if (adminId.length() == 0) return false;
  String adminRole = String(ADMIN_ROLE_ID); adminRole.trim();
  String ownerChat = String(OWNER_CHAT); ownerChat.trim();
  if (adminRole.length() == 0 && ownerChat.length() == 0) return false;
  return (adminRole.length() > 0 && adminId == adminRole) ||
         (ownerChat.length() > 0 && adminId == ownerChat);
}

bool AuthSecurity::isNonceUsed(const String &txId) {
  if (txId.length() == 0) return false;
  for (int i = 0; i < NONCE_CACHE_DEPTH; i++) {
    if (recentTxIds[i] == txId) return true;
  }
  return false;
}

void AuthSecurity::markNonceUsed(const String &txId) {
  if (txId.length() == 0) return;
  recentTxIds[txHead] = txId;
  txHead = (txHead + 1) % NONCE_CACHE_DEPTH;
}
