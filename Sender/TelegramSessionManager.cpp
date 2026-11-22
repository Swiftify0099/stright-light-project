/*
 * =====================================================================
 *  TELEGRAMSESSIONMANAGER.CPP — ADMIN AUTHENTICATION & MULTI-SESSION TRACKER
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#include "TelegramSessionManager.h"
#include <time.h>

TelegramSessionManager sessionMgr;

TelegramSessionManager::TelegramSessionManager() : adminCount(0) {
  for (int i = 0; i < MAX_ADMIN_SESSIONS; i++) {
    sessions[i] = {};
    sessions[i].currentMenu = MENU_DASHBOARD;
    sessions[i].previousMenu = MENU_DASHBOARD;
    sessions[i].lastMessageId = 0;
    sessions[i].lastActive = 0;
    sessions[i].selectedPole = "";
    sessions[i].selectedFeeder = 1;
    sessions[i].selectedNeighbour = "";
    sessions[i].selectedGroup = "";
    sessions[i].pageIndex = 0;
    sessions[i].pendingRegIdx = -1;
    sessions[i].wizardState = WIZ_NONE;
    sessions[i].tokenExpiryMs = 0;
  }
}

void TelegramSessionManager::init() {
  adminCount = 0;

  // Initialize allowlist from config macros
  String primaryOwner = String(OWNER_CHAT);
  primaryOwner.trim();
  if (primaryOwner.length() > 0) {
    adminAllowlist[adminCount++] = primaryOwner;
  }

  String adminRole = String(ADMIN_ROLE_ID);
  adminRole.trim();
  if (adminRole.length() > 0 && adminRole != primaryOwner && adminCount < MAX_ADMIN_USERS) {
    adminAllowlist[adminCount++] = adminRole;
  }

  Serial.printf("[GW][AUTH] Initialized Admin Session Manager. Authorized Admins: %d\n", adminCount);
  for (int i = 0; i < adminCount; i++) {
    Serial.printf("[GW][AUTH]   Admin [%d]: ID=%s\n", i + 1, adminAllowlist[i].c_str());
  }
}

// Fail-closed authorization check
bool TelegramSessionManager::isAuthorized(const String &chatId, const String &fromId) {
  String cId = chatId; cId.trim();
  String fId = fromId; fId.trim();

  // If no IDs provided, fail closed
  if (cId.length() == 0 && fId.length() == 0) return false;

  // Never trust user display names or usernames; compare strictly numeric Telegram ID
  for (int i = 0; i < adminCount; i++) {
    if (adminAllowlist[i].length() > 0) {
      if (cId == adminAllowlist[i] || fId == adminAllowlist[i]) {
        return true;
      }
    }
  }

  return false;
}

bool TelegramSessionManager::addAdmin(const String &adminId) {
  String id = adminId; id.trim();
  if (id.length() == 0 || adminCount >= MAX_ADMIN_USERS) return false;

  for (int i = 0; i < adminCount; i++) {
    if (adminAllowlist[i] == id) return true; // Already authorized
  }

  adminAllowlist[adminCount++] = id;
  Serial.printf("[GW][AUTH] Added Admin ID: %s (Total: %d)\n", id.c_str(), adminCount);
  return true;
}

bool TelegramSessionManager::removeAdmin(const String &adminId) {
  String id = adminId; id.trim();
  if (id.length() == 0) return false;

  int foundIdx = -1;
  for (int i = 0; i < adminCount; i++) {
    if (adminAllowlist[i] == id) {
      foundIdx = i;
      break;
    }
  }
  if (foundIdx < 0) return false;

  for (int i = foundIdx; i < adminCount - 1; i++) {
    adminAllowlist[i] = adminAllowlist[i + 1];
  }
  adminCount--;
  Serial.printf("[GW][AUTH] Removed Admin ID: %s (Remaining: %d)\n", id.c_str(), adminCount);
  return true;
}

int TelegramSessionManager::getAdminCount() const {
  return adminCount;
}

String TelegramSessionManager::getPrimaryAdminId() const {
  if (adminCount > 0) return adminAllowlist[0];
  return String(OWNER_CHAT);
}

UserSession& TelegramSessionManager::getOrCreateSession(const String &chatId, const String &userId) {
  unsigned long now = millis();

  // 1. Search for existing session by chatId
  for (int i = 0; i < MAX_ADMIN_SESSIONS; i++) {
    if (sessions[i].chatId == chatId && chatId.length() > 0) {
      sessions[i].lastActive = now;
      if (userId.length() > 0) sessions[i].userId = userId;
      return sessions[i];
    }
  }

  // 2. Find empty or oldest slot for LRU replacement
  int targetIdx = 0;
  unsigned long oldestTime = sessions[0].lastActive;

  for (int i = 0; i < MAX_ADMIN_SESSIONS; i++) {
    if (sessions[i].chatId.length() == 0) {
      targetIdx = i;
      break;
    }
    if (sessions[i].lastActive < oldestTime) {
      oldestTime = sessions[i].lastActive;
      targetIdx = i;
    }
  }

  // 3. Re-initialize slot
  sessions[targetIdx] = {};
  sessions[targetIdx].chatId = chatId;
  sessions[targetIdx].userId = userId;
  sessions[targetIdx].currentMenu = MENU_DASHBOARD;
  sessions[targetIdx].previousMenu = MENU_DASHBOARD;
  sessions[targetIdx].lastMessageId = 0;
  sessions[targetIdx].lastActive = now;
  sessions[targetIdx].selectedPole = "";
  sessions[targetIdx].selectedFeeder = 1;
  sessions[targetIdx].selectedNeighbour = "";
  sessions[targetIdx].selectedGroup = "";
  sessions[targetIdx].pageIndex = 0;
  sessions[targetIdx].pendingRegIdx = -1;
  sessions[targetIdx].wizardState = WIZ_NONE;
  sessions[targetIdx].tokenExpiryMs = 0;

  Serial.printf("[GW][SESS] Allocated session slot %d for Chat ID: %s\n", targetIdx, chatId.c_str());
  return sessions[targetIdx];
}

UserSession* TelegramSessionManager::findSession(const String &chatId) {
  for (int i = 0; i < MAX_ADMIN_SESSIONS; i++) {
    if (sessions[i].chatId == chatId && chatId.length() > 0) {
      return &sessions[i];
    }
  }
  return nullptr;
}

void TelegramSessionManager::clearSession(const String &chatId) {
  for (int i = 0; i < MAX_ADMIN_SESSIONS; i++) {
    if (sessions[i].chatId == chatId) {
      sessions[i].chatId = "";
      sessions[i].userId = "";
      sessions[i].currentMenu = MENU_DASHBOARD;
      sessions[i].wizardState = WIZ_NONE;
      sessions[i].confirmationToken = "";
      sessions[i].tokenExpiryMs = 0;
      break;
    }
  }
}

void TelegramSessionManager::sweepExpiredSessions() {
  unsigned long now = millis();
  for (int i = 0; i < MAX_ADMIN_SESSIONS; i++) {
    if (sessions[i].chatId.length() > 0) {
      if (now - sessions[i].lastActive >= SESSION_TIMEOUT_MS) {
        Serial.printf("[GW][SESS] Session for Chat %s expired due to inactivity. Resetting.\n",
                      sessions[i].chatId.c_str());
        sessions[i].currentMenu = MENU_DASHBOARD;
        sessions[i].previousMenu = MENU_DASHBOARD;
        clearWizard(sessions[i]);
      }
    }
  }
}

String TelegramSessionManager::generateRandomToken(size_t len) {
  static const char charset[] = "0123456789ABCDEF";
  String token = "";
  for (size_t i = 0; i < len; i++) {
    uint32_t r = (uint32_t)random(0, sizeof(charset) - 2);
    token += charset[r];
  }
  return token;
}

String TelegramSessionManager::generateConfirmationToken(UserSession &sess, const String &targetId, WizardState state, unsigned long ttlMs) {
  sess.wizardState = state;
  sess.wizardTargetId = targetId;
  sess.confirmationToken = generateRandomToken(6);
  sess.tokenExpiryMs = millis() + ttlMs;

  Serial.printf("[GW][AUTH] Generated verification token '%s' for target '%s' (TTL: %lu ms)\n",
                sess.confirmationToken.c_str(), targetId.c_str(), ttlMs);
  return sess.confirmationToken;
}

bool TelegramSessionManager::validateConfirmationToken(UserSession &sess, const String &token) {
  unsigned long now = millis();
  if (sess.tokenExpiryMs == 0 || now > sess.tokenExpiryMs) {
    Serial.println(F("[GW][AUTH] ⚠️ Token validation failed: Token expired."));
    clearWizard(sess);
    return false;
  }

  String t = token; t.trim();
  if (t.equalsIgnoreCase(sess.confirmationToken)) {
    return true;
  }

  Serial.printf("[GW][AUTH] ⚠️ Token mismatch: Expected '%s', got '%s'\n",
                sess.confirmationToken.c_str(), t.c_str());
  return false;
}

void TelegramSessionManager::clearWizard(UserSession &sess) {
  sess.wizardState = WIZ_NONE;
  sess.wizardTargetId = "";
  sess.confirmationToken = "";
  sess.tokenExpiryMs = 0;
  sess.pendingPoleName = "";
  sess.pendingPoleArea = "";
}
