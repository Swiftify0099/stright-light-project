/*
 * =====================================================================
 *  TELEGRAMSESSIONMANAGER.H — ADMIN AUTHENTICATION & MULTI-SESSION TRACKER
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#ifndef TELEGRAM_SESSION_MANAGER_H
#define TELEGRAM_SESSION_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

#define MAX_ADMIN_USERS     4
#define MAX_ADMIN_SESSIONS  4
#define SESSION_TIMEOUT_MS  900000UL   // 15 minutes inactivity timeout
#define TOKEN_TTL_MS        60000UL    // 60-second token expiration for 2-step confirmations

class TelegramSessionManager {
public:
  TelegramSessionManager();
  void init();

  // ─────────────────────────────────────────────────────────────
  //  ADMIN IDENTITY & AUTHENTICATION (FAIL-CLOSED)
  // ─────────────────────────────────────────────────────────────
  bool isAuthorized(const String &chatId, const String &fromId = "");
  bool addAdmin(const String &adminId);
  bool removeAdmin(const String &adminId);
  int  getAdminCount() const;
  String getPrimaryAdminId() const;

  // ─────────────────────────────────────────────────────────────
  //  SESSION LIFECYCLE MANAGEMENT
  // ─────────────────────────────────────────────────────────────
  UserSession& getOrCreateSession(const String &chatId, const String &userId = "");
  UserSession* findSession(const String &chatId);
  void clearSession(const String &chatId);
  void sweepExpiredSessions();

  // ─────────────────────────────────────────────────────────────
  //  TWO-STEP VERIFICATION & WIZARD ENGINE
  // ─────────────────────────────────────────────────────────────
  String generateConfirmationToken(UserSession &sess, const String &targetId, WizardState state, unsigned long ttlMs = TOKEN_TTL_MS);
  bool   validateConfirmationToken(UserSession &sess, const String &token);
  void   clearWizard(UserSession &sess);

private:
  String adminAllowlist[MAX_ADMIN_USERS];
  int    adminCount;

  UserSession sessions[MAX_ADMIN_SESSIONS];

  String generateRandomToken(size_t len = 6);
};

extern TelegramSessionManager sessionMgr;

#endif // TELEGRAM_SESSION_MANAGER_H
