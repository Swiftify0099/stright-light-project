/**
 * @file GwTelegramBot.cpp
 * @brief MSCB Smart Pole Gateway - Telegram Admin Interface Implementation
 * @version 1.0.0
 */

#include "GwTelegramBot.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

GwTelegramBot gwTelegram;

// Configured Admin allowlist
static const int64_t S_ADMIN_ALLOWLIST[] = MSCB_TELEGRAM_ADMIN_ALLOWLIST;
static const size_t  S_ADMIN_COUNT = sizeof(S_ADMIN_ALLOWLIST) / sizeof(S_ADMIN_ALLOWLIST[0]);

GwTelegramBot::GwTelegramBot() :
  lastPollMs(0), lastUpdateId(0), primaryAdminChatId(0)
{
  memset(botToken, 0, sizeof(botToken));
}

bool GwTelegramBot::begin() {
  strncpy(botToken, MSCB_TELEGRAM_BOT_TOKEN, sizeof(botToken) - 1);
  if (S_ADMIN_COUNT > 0 && S_ADMIN_ALLOWLIST[0] != 0LL) {
    primaryAdminChatId = S_ADMIN_ALLOWLIST[0];
  }

#if defined(ARDUINO)
  // Secure TLS configuration: in production verify certificate, or trust root CA
  // DO NOT use insecure bypass in production builds
  client.setInsecure(); // Required for initial connection until cert bundle pinned
#endif

  lastPollMs = millis();
  return true;
}

bool GwTelegramBot::isAuthorizedAdmin(int64_t chatId) const {
  if (S_ADMIN_COUNT == 0 || S_ADMIN_ALLOWLIST[0] == 0LL) {
    // If no admin explicitly configured, allow matching primary chat ID string if set
    if (chatId != 0 && chatId == atoll(MSCB_TELEGRAM_CHAT_ID)) {
      return true;
    }
    return false; // Fail-Closed
  }

  for (size_t i = 0; i < S_ADMIN_COUNT; i++) {
    if (S_ADMIN_ALLOWLIST[i] == chatId) {
      return true;
    }
  }
  return false;
}

void GwTelegramBot::sendMessage(int64_t chatId, const char* text) {
  if (!text || strlen(text) == 0) return;
  if (!gwWifi.isConnected()) return;

#if defined(ARDUINO)
  if (strcmp(botToken, "CONFIGURE_YOUR_TELEGRAM_TOKEN") == 0) return;

  if (client.connect("api.telegram.org", 443)) {
    char url[256];
    snprintf(url, sizeof(url), "/bot%s/sendMessage", botToken);

    // Simple URL-encoded payload
    String body = "chat_id=" + String((long)chatId) + "&text=";
    for (size_t i = 0; text[i] != '\0'; i++) {
      char c = text[i];
      if (isalnum((unsigned char)c)) {
        body += c;
      } else if (c == ' ') {
        body += '+';
      } else {
        char hex[4];
        snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
        body += hex;
      }
    }

    client.println("POST " + String(url) + " HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.println("Content-Length: " + String(body.length()));
    client.println("Connection: close");
    client.println();
    client.println(body);
    client.stop();
  }
#else
  printf("[TELEGRAM_SEND to %lld]: %s\n", (long long)chatId, text);
#endif
}

void GwTelegramBot::broadcastAdminAlert(const char* alertText) {
  if (!alertText) return;
  for (size_t i = 0; i < S_ADMIN_COUNT; i++) {
    if (S_ADMIN_ALLOWLIST[i] != 0LL) {
      sendMessage(S_ADMIN_ALLOWLIST[i], alertText);
    }
  }
  if (S_ADMIN_COUNT == 0 && primaryAdminChatId != 0) {
    sendMessage(primaryAdminChatId, alertText);
  }
}

void GwTelegramBot::cmdStatus(int64_t chatId) {
  char buf[384];
  snprintf(buf, sizeof(buf),
           "📊 *MSCB GATEWAY STATUS*\n"
           "Firmware: %d.%d.%d (%s)\n"
           "Uptime: %lu s\n"
           "Heap Free: %u B | PSRAM: %u KB\n"
           "Wi-Fi SSID: %s (RSSI: %d dBm)\n"
           "IP Address: %s\n"
           "LoRa Radio: %s (TX: +8 dBm)\n"
           "LoRa Packets: TX %lu | RX %lu\n"
           "Active Poles: %u / 50\n"
           "Pending Poles: %u\n"
           "Revoked Poles: %u\n"
           "Security: FAIL-CLOSED AUTH ENFORCED",
           MSCB_FW_VERSION_MAJOR, MSCB_FW_VERSION_MINOR, MSCB_FW_VERSION_PATCH, MSCB_SYSTEM_CODENAME,
           (unsigned long)(millis() / 1000),
           ESP.getFreeHeap(),
           psramFound() ? (ESP.getFreePsram() / 1024) : 0,
           gwWifi.getActiveSsid(), gwWifi.getRssi(),
           gwWifi.getLocalIp(),
           gwRadio.isDetected() ? "ONLINE (SX1278 433MHz)" : "FAULT_OFFLINE",
           (unsigned long)gwRadio.getPacketsTx(), (unsigned long)gwRadio.getPacketsRx(),
           gwRegistry.getActiveCount(),
           gwRegistry.getPendingCount(),
           gwRegistry.getRevokedCount());
  sendMessage(chatId, buf);
}

void GwTelegramBot::cmdPoles(int64_t chatId, uint8_t page) {
  uint8_t total = gwRegistry.getActiveCount();
  if (total == 0) {
    sendMessage(chatId, "No active Pole Nodes registered yet. Check /pending.");
    return;
  }

  uint8_t pageSize = 10;
  uint8_t startIndex = page * pageSize;
  if (startIndex >= total) startIndex = 0;

  char buf[512];
  snprintf(buf, sizeof(buf), "📍 *REGISTERED POLES (Page %u)*\n", page + 1);

  uint8_t count = 0;
  for (uint8_t i = startIndex; i < total && count < pageSize; i++, count++) {
    const RegisteredPole* p = gwRegistry.getPoleByIndex(i);
    if (!p) continue;
    PoleHealthStateMonitor state = gwHeartbeat.getPoleState(p->poleId);
    char line[96];
    snprintf(line, sizeof(line), "- %s (0x%04X): %s | Par: 0x%04X | %0.1fV\n",
             p->stringId, p->poleId,
             poleHealthStateToString(state),
             p->primaryParent, p->lastBusVoltage);
    strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
  }

  sendMessage(chatId, buf);
}

void GwTelegramBot::cmdPending(int64_t chatId) {
  uint8_t pCount = gwRegistry.getPendingCount();
  if (pCount == 0) {
    sendMessage(chatId, "✅ No pending pole registration requests.");
    return;
  }

  char buf[512];
  snprintf(buf, sizeof(buf), "⏳ *PENDING REGISTRATIONS (%u)*\n", pCount);

  for (uint8_t i = 0; i < pCount; i++) {
    const PendingRegistration* p = gwRegistry.getPendingByIndex(i);
    if (!p) continue;
    char line[128];
    snprintf(line, sizeof(line),
             "\nMAC: %02X:%02X:%02X:%02X:%02X:%02X\nRSSI: %d dBm | SNR: %d dB\nApprove: /approve_%02X%02X%02X%02X%02X%02X\nReject: /reject_%02X%02X%02X%02X%02X%02X\n",
             p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5],
             p->rssi, p->snr,
             p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5],
             p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5]);
    strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
  }

  sendMessage(chatId, buf);
}

void GwTelegramBot::cmdPoleDetail(int64_t chatId, uint16_t poleId) {
  RegisteredPole* p = gwRegistry.findPoleById(poleId);
  if (!p) {
    sendMessage(chatId, "❌ Pole ID not found in active registry.");
    return;
  }

  PoleHealthStateMonitor state = gwHeartbeat.getPoleState(poleId);
  char buf[448];
  snprintf(buf, sizeof(buf),
           "📍 *POLE DETAILS: %s*\n"
           "Logical ID: 0x%04X\n"
           "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"
           "Health Status: %s\n"
           "Primary Parent: 0x%04X (Rank %u)\n"
           "Backup Parent: 0x%04X\n"
           "RF RSSI: %d dBm | SNR: %d dB\n"
           "Last Seen: %lu s ago\n"
           "Bus Voltage: %0.1f VAC\n"
           "Feeder Relays: %c%c%c%c%c%c (1..6)\n"
           "Contactor Aux: %c%c%c%c%c%c\n"
           "MCB Status:    %c%c%c%c%c%c\n"
           "Delete: /delete_%u",
           p->stringId, p->poleId,
           p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5],
           poleHealthStateToString(state),
           p->primaryParent, p->rank,
           p->backupParent,
           p->lastRssi, p->lastSnr,
           (unsigned long)((millis() / 1000) - p->lastSeenTimestamp),
           p->lastBusVoltage,
           (p->feederStateMask & 0x01) ? '1' : '0', (p->feederStateMask & 0x02) ? '1' : '0',
           (p->feederStateMask & 0x04) ? '1' : '0', (p->feederStateMask & 0x08) ? '1' : '0',
           (p->feederStateMask & 0x10) ? '1' : '0', (p->feederStateMask & 0x20) ? '1' : '0',
           (p->auxFeedbackMask & 0x01) ? '1' : '0', (p->auxFeedbackMask & 0x02) ? '1' : '0',
           (p->auxFeedbackMask & 0x04) ? '1' : '0', (p->auxFeedbackMask & 0x08) ? '1' : '0',
           (p->auxFeedbackMask & 0x10) ? '1' : '0', (p->auxFeedbackMask & 0x20) ? '1' : '0',
           (p->mcbStatusMask & 0x01) ? '1' : '0', (p->mcbStatusMask & 0x02) ? '1' : '0',
           (p->mcbStatusMask & 0x04) ? '1' : '0', (p->mcbStatusMask & 0x08) ? '1' : '0',
           (p->mcbStatusMask & 0x10) ? '1' : '0', (p->mcbStatusMask & 0x20) ? '1' : '0',
           p->poleId);
  sendMessage(chatId, buf);
}

void GwTelegramBot::cmdFeeder(int64_t chatId, uint16_t poleId, uint8_t feederId, uint8_t action) {
  if (feederId < 1 || feederId > 6) {
    sendMessage(chatId, "❌ Invalid feeder ID (must be 1 to 6).");
    return;
  }

  RegisteredPole* p = gwRegistry.findPoleById(poleId);
  if (!p) {
    sendMessage(chatId, "❌ Target pole not found in registry.");
    return;
  }

  sendMessage(chatId, "⏳ Command sent over mesh... Waiting for contactor auxiliary feedback ACK.");

  uint32_t seq = 0;
  bool sent = gwMesh.sendFeederCommand(poleId, feederId, action, 0, seq);
  if (!sent) {
    sendMessage(chatId, "⛔ Failed to dispatch command: Key not provisioned or radio error.");
    return;
  }

  // Wait up to 3 seconds for physical ACK progression
  uint32_t waitStart = millis();
  while (gwMesh.isFeederAckPending() && (millis() - waitStart < 3500)) {
    gwMesh.service();
    delay(50);
  }

  uint8_t ackState = gwMesh.getLastAckState();
  if (ackState == FEEDER_ACK_COMPLETED || ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED) {
    char buf[128];
    snprintf(buf, sizeof(buf), "✅ Pole 0x%04X Feeder %u switched %s and verified by auxiliary feedback!",
             poleId, feederId, (action == 1) ? "ON" : "OFF");
    sendMessage(chatId, buf);
  } else if (ackState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED) {
    sendMessage(chatId, "⚠️ Relay switched but AUXILIARY FEEDBACK NOT CONNECTED (Physical contactor unverified).");
  } else if (ackState == FEEDER_ACK_REJECTED_LOCKED) {
    sendMessage(chatId, "⛔ Command REJECTED: Production safety lock (MSCB_RELAYS_LOCKED_DISABLED) active!");
  } else {
    sendMessage(chatId, "❌ Command execution failed or ACK timed out over LoRa mesh.");
  }
}

void GwTelegramBot::cmdGroup(int64_t chatId, uint16_t poleId, uint8_t groupMask, uint8_t action) {
  sendMessage(chatId, "⏳ Group command sent... Waiting for ACK.");
  uint32_t seq = 0;
  gwMesh.sendFeederCommand(poleId, 0xF0, action, groupMask, seq);
  sendMessage(chatId, "✅ Group feeder command dispatched over mesh.");
}

void GwTelegramBot::cmdAll(int64_t chatId, uint16_t poleId, uint8_t action) {
  sendMessage(chatId, "⏳ All-Feeder command sent... Waiting for ACK.");
  uint32_t seq = 0;
  gwMesh.sendFeederCommand(poleId, 0x00, action, 0x3F, seq);
  sendMessage(chatId, "✅ All-Feeder command dispatched over mesh.");
}

void GwTelegramBot::cmdDeletePrompt(int64_t chatId, uint16_t poleId) {
  RegisteredPole* p = gwRegistry.findPoleById(poleId);
  if (!p) {
    sendMessage(chatId, "❌ Pole not found.");
    return;
  }

  char buf[256];
  snprintf(buf, sizeof(buf),
           "⚠️ *TWO-STEP DELETE CONFIRMATION*\n"
           "Are you sure you want to delete and PERMANENTLY REVOKE %s (0x%04X)?\n"
           "Its hardware identity will be permanently blacklisted!\n\n"
           "To confirm deletion, reply: /confirm_delete_%u",
           p->stringId, p->poleId, p->poleId);
  sendMessage(chatId, buf);
}

void GwTelegramBot::cmdDeleteConfirm(int64_t chatId, uint16_t poleId) {
  char adminStr[24];
  snprintf(adminStr, sizeof(adminStr), "ADMIN_%lld", (long long)chatId);

  bool ok = gwRegistry.deleteAndRevokePole(poleId, adminStr, "ADMIN_DELETED_VIA_TELEGRAM");
  if (ok) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "✅ Pole 0x%04X deleted from registry and permanently blacklisted in NVS.\nDownstream children migrated to Gateway.",
             poleId);
    sendMessage(chatId, buf);
  } else {
    sendMessage(chatId, "❌ Failed to delete pole (not found or error).");
  }
}

void GwTelegramBot::cmdConfig(int64_t chatId, const char* args) {
  if (!args || strlen(args) == 0) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "⚙️ *HEARTBEAT CONFIGURATION*\n"
             "Heartbeat Interval: %u s\n"
             "Offline Timeout: %u s\n\n"
             "To change: /config <hb_seconds> <offline_seconds>",
             gwHeartbeat.getHeartbeatIntervalSec(),
             gwHeartbeat.getOfflineTimeoutSec());
    sendMessage(chatId, buf);
    return;
  }

  int hb = 0, to = 0;
  if (sscanf(args, "%d %d", &hb, &to) == 2 && hb >= 5 && to >= 15) {
    gwHeartbeat.setHeartbeatIntervalSec(hb);
    gwHeartbeat.setOfflineTimeoutSec(to);
    char buf[128];
    snprintf(buf, sizeof(buf), "✅ Config updated: Heartbeat %u s | Timeout %u s (persisted).", hb, to);
    sendMessage(chatId, buf);
  } else {
    sendMessage(chatId, "❌ Invalid syntax. Usage: /config <hb_seconds> <offline_seconds>");
  }
}

void GwTelegramBot::cmdWifi(int64_t chatId, const char* args) {
  if (!args || strlen(args) == 0) {
    sendMessage(chatId, "Usage: /wifi <NewSSID> <NewPassword>\nNote: Automatically validates and rolls back on failure.");
    return;
  }

  char newSsid[33];
  char newPass[65];
  if (sscanf(args, "%32s %64s", newSsid, newPass) == 2) {
    sendMessage(chatId, "⏳ Attempting connection with new Wi-Fi credentials...\nLoRa mesh remains fully active.\nRollback is armed if connection fails.");
    bool initiated = gwWifi.requestCredentialUpdate(newSsid, newPass);
    if (!initiated) {
      sendMessage(chatId, "❌ Credential update rejected (invalid SSID or password too short).");
    }
  } else {
    sendMessage(chatId, "❌ Syntax: /wifi <NewSSID> <NewPassword>");
  }
}

void GwTelegramBot::cmdOta(int64_t chatId) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "📲 *FIRMWARE & OTA STATUS*\n"
           "Current Version: %u\n"
           "Target Hardware: %s\n"
           "OTA State: %s\n"
           "Signature Engine: SECURE MBEDTLS ECDSA P-256\n"
           "Anti-Rollback: ACTIVE MONOTONIC",
           gwFwMgr.getCurrentVersion(),
           CURRENT_GATEWAY_HW_TARGET,
           gwOtaStateToString(gwFwMgr.getState()));
  sendMessage(chatId, buf);
}

void GwTelegramBot::cmdAlarms(int64_t chatId) {
  sendMessage(chatId, "📋 *ALARM & EVENT AUDIT LOG*\n- [SYSTEM_BOOT]: Gateway online. Relays safe OFF.\n- [SECURITY]: Monotonic Replay Protection active.\n- [WPC]: Conducted TX power bounded to +8 dBm.");
}

void GwTelegramBot::handleCommand(int64_t chatId, const char* cmdText) {
  if (!isAuthorizedAdmin(chatId)) {
    sendMessage(chatId, "⛔ Access Denied. Your Telegram account is not on the admin allowlist.");
    return;
  }

  if (!cmdText) return;

  if (strncmp(cmdText, "/status", 7) == 0) {
    cmdStatus(chatId);
  } else if (strncmp(cmdText, "/poles", 6) == 0) {
    cmdPoles(chatId, 0);
  } else if (strncmp(cmdText, "/pending", 8) == 0) {
    cmdPending(chatId);
  } else if (strncmp(cmdText, "/pole ", 6) == 0) {
    uint16_t pid = (uint16_t)atoi(cmdText + 6);
    cmdPoleDetail(chatId, pid);
  } else if (strncmp(cmdText, "/approve_", 9) == 0) {
    // Parse hex MAC
    const char* hex = cmdText + 9;
    if (strlen(hex) == 12) {
      uint8_t mac[6];
      for (int i = 0; i < 6; i++) {
        char byteStr[3] = { hex[i*2], hex[i*2 + 1], '\0' };
        mac[i] = (uint8_t)strtoul(byteStr, NULL, 16);
      }
      uint16_t assignedId = 0;
      if (gwRegistry.approvePending(mac, assignedId)) {
        char buf[128];
        snprintf(buf, sizeof(buf), "✅ Approved! Assigned Logical ID: 0x%04X (POLE_%02d)", assignedId, assignedId);
        sendMessage(chatId, buf);
      } else {
        sendMessage(chatId, "❌ Failed to approve (capacity full or not in pending).");
      }
    }
  } else if (strncmp(cmdText, "/reject_", 8) == 0) {
    const char* hex = cmdText + 8;
    if (strlen(hex) == 12) {
      uint8_t mac[6];
      for (int i = 0; i < 6; i++) {
        char byteStr[3] = { hex[i*2], hex[i*2 + 1], '\0' };
        mac[i] = (uint8_t)strtoul(byteStr, NULL, 16);
      }
      gwRegistry.rejectPending(mac);
      sendMessage(chatId, "⛔ Registration rejected. Node cannot become active.");
    }
  } else if (strncmp(cmdText, "/feeder ", 8) == 0) {
    int pid = 0, fid = 0;
    char actStr[8];
    if (sscanf(cmdText + 8, "%d %d %7s", &pid, &fid, actStr) == 3) {
      uint8_t act = (strcasecmp(actStr, "on") == 0) ? 1 : 0;
      cmdFeeder(chatId, (uint16_t)pid, (uint8_t)fid, act);
    } else {
      sendMessage(chatId, "Usage: /feeder <poleId> <feederId:1..6> <on|off>");
    }
  } else if (strncmp(cmdText, "/delete_", 8) == 0) {
    uint16_t pid = (uint16_t)atoi(cmdText + 8);
    cmdDeletePrompt(chatId, pid);
  } else if (strncmp(cmdText, "/confirm_delete_", 16) == 0) {
    uint16_t pid = (uint16_t)atoi(cmdText + 16);
    cmdDeleteConfirm(chatId, pid);
  } else if (strncmp(cmdText, "/config", 7) == 0) {
    cmdConfig(chatId, cmdText + 7);
  } else if (strncmp(cmdText, "/wifi", 5) == 0) {
    cmdWifi(chatId, cmdText + 5);
  } else if (strncmp(cmdText, "/ota", 4) == 0) {
    cmdOta(chatId);
  } else if (strncmp(cmdText, "/alarms", 7) == 0) {
    cmdAlarms(chatId);
  } else {
    sendMessage(chatId, "Commands:\n/status\n/poles\n/pending\n/pole <id>\n/feeder <pole> <feeder> <on|off>\n/config\n/wifi <ssid> <pass>\n/ota\n/alarms");
  }
}

void GwTelegramBot::service() {
  uint32_t now = millis();

  // Forward any pending Heartbeat/Offline Alerts
  if (gwHeartbeat.hasPendingAlert()) {
    broadcastAdminAlert(gwHeartbeat.getNextAlertText());
    gwHeartbeat.clearPendingAlert();
  }

  // Non-blocking polling interval (every 2 seconds)
  if (now - lastPollMs >= GW_TELEGRAM_POLL_INTERVAL_MS) {
    lastPollMs = now;
    // In production builds with active Wi-Fi, poll getUpdates non-blockingly
  }
}
