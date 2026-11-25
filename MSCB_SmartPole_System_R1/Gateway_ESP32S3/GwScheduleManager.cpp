/**
 * @file GwScheduleManager.cpp
 * @brief MSCB Smart Pole Gateway - Feeder Timer, Scheduling & Mesh Time Synchronization Implementation
 * @version 1.0.0
 */

#include "GwScheduleManager.h"
#include "GwMeshCoordinator.h"
#include "GwNodeRegistry.h"
#include <string.h>
#include <stdio.h>

GwScheduleManager gwSchedule;

GwScheduleManager::GwScheduleManager() :
  ruleCount(0),
  lastServiceMs(0),
  lastMeshSyncMs(0),
  lastNtpCheckMs(0)
{
  memset(rules, 0, sizeof(rules));
}

bool GwScheduleManager::begin() {
#if defined(ARDUINO)
  // Configure India Standard Time (UTC +05:30)
  setenv("TZ", "IST-5:30", 1);
  tzset();
  configTzTime("IST-5:30", "pool.ntp.org", "time.google.com", "time.windows.com");
#endif
  loadSchedules();
  lastServiceMs = millis();
  lastMeshSyncMs = millis();
  return true;
}

bool GwScheduleManager::isTimeSynced() const {
#if defined(ARDUINO)
  time_t now = time(nullptr);
  return (now > 1700000000UL); // Legitimate epoch check (year > 2023)
#else
  return true;
#endif
}

uint32_t GwScheduleManager::getEpoch() const {
#if defined(ARDUINO)
  return (uint32_t)time(nullptr);
#else
  return 1795435000UL;
#endif
}

void GwScheduleManager::getIstTimeStr(char* outBuf, size_t maxLen) const {
  if (!outBuf || maxLen == 0) return;
  if (!isTimeSynced()) {
    snprintf(outBuf, maxLen, "TIME_UNSYNCED (NTP pending)");
    return;
  }
  time_t now = time(nullptr);
  struct tm timeinfo;
#if defined(ARDUINO)
  localtime_r(&now, &timeinfo);
#else
  struct tm* t = localtime(&now);
  if (t) timeinfo = *t;
  else memset(&timeinfo, 0, sizeof(timeinfo));
#endif
  strftime(outBuf, maxLen, "%Y-%m-%d %H:%M:%S IST", &timeinfo);
}

void GwScheduleManager::broadcastMeshTimeSync() {
  if (!isTimeSynced()) {
    #if defined(ARDUINO)
    Serial.println(F("[SCHEDULE] Cannot broadcast time sync: Gateway NTP not synced yet."));
    #endif
    return;
  }
  uint32_t epoch = getEpoch();
  PktTimeSyncPayload syncPkt;
  if (!cmdSecurity.createTimeSyncPacket(epoch, syncPkt)) {
    #if defined(ARDUINO)
    Serial.println(F("[SCHEDULE] Failed to create authenticated time sync packet (key not provisioned)."));
    #endif
    return;
  }

  MeshWireFrame frame;
  memset(&frame, 0, sizeof(MeshWireFrame));

  frame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  frame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  frame.header.version = MSCB_PROTOCOL_VERSION;
  frame.header.type = PKT_CMD_TIME_SYNC;
  frame.header.priority = PRIO_ROUTINE;
  frame.header.hopCount = 0;
  frame.header.maxHops = GW_MESH_MAX_HOPS;
  frame.header.seqNumber = (uint16_t)(syncPkt.sequenceNumber & 0xFFFF);
  frame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
  frame.header.dstAddress = MSCB_BROADCAST_NODE_ID;
  frame.header.nextHop = MSCB_BROADCAST_NODE_ID;
  frame.header.previousHopAddress = MSCB_GATEWAY_NODE_ID;
  frame.header.payloadLen = sizeof(PktTimeSyncPayload);
  memcpy(frame.payload, &syncPkt, sizeof(PktTimeSyncPayload));
  frame.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(frame);

  gwRadio.sendPacket(frame);
  #if defined(ARDUINO)
  Serial.printf("[SCHEDULE] Broadcasted authenticated mesh time sync UTC epoch: %lu\n", (unsigned long)epoch);
  #endif
}

bool GwScheduleManager::addSchedule(uint16_t poleId, uint8_t feederId,
                                    uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM,
                                    bool recurring) {
  if (ruleCount >= GW_MAX_SCHEDULE_RULES) return false;
  if (onH > 23 || onM > 59 || offH > 23 || offM > 59) return false;

  FeederScheduleRule &r = rules[ruleCount++];
  r.enabled = true;
  r.poleId = poleId;
  r.feederId = feederId;
  r.onHour = onH;
  r.onMinute = onM;
  r.offHour = offH;
  r.offMinute = offM;
  r.recurringDaily = recurring;
  r.lastTriggeredDay = 0;
  r.lastOffTriggeredDay = 0;
  r.autoOffArmed = false;
  r.autoOffMinutes = 0;
  r.autoOffStartEpoch = 0;

  saveSchedules();
  return true;
}

bool GwScheduleManager::armAutoOffTimer(uint16_t poleId, uint8_t feederId, uint32_t minutes) {
  if (minutes == 0) return false;

  // Check if existing rule exists for this pole/feeder
  for (uint8_t i = 0; i < ruleCount; i++) {
    if (rules[i].poleId == poleId && rules[i].feederId == feederId) {
      rules[i].autoOffArmed = true;
      rules[i].autoOffMinutes = minutes;
      rules[i].autoOffStartEpoch = getEpoch();
      saveSchedules();
      return true;
    }
  }

  // Create new rule for countdown auto-off
  if (ruleCount < GW_MAX_SCHEDULE_RULES) {
    FeederScheduleRule &r = rules[ruleCount++];
    r.enabled = true;
    r.poleId = poleId;
    r.feederId = feederId;
    r.recurringDaily = false;
    r.onHour = 0; r.onMinute = 0;
    r.offHour = 0; r.offMinute = 0;
    r.lastTriggeredDay = 0;
    r.lastOffTriggeredDay = 0;
    r.autoOffArmed = true;
    r.autoOffMinutes = minutes;
    r.autoOffStartEpoch = getEpoch();
    saveSchedules();
    return true;
  }
  return false;
}

bool GwScheduleManager::cancelAutoOffTimer(uint16_t poleId, uint8_t feederId) {
  bool found = false;
  for (uint8_t i = 0; i < ruleCount; i++) {
    if (rules[i].poleId == poleId && rules[i].feederId == feederId) {
      rules[i].autoOffArmed = false;
      rules[i].autoOffMinutes = 0;
      rules[i].autoOffStartEpoch = 0;
      found = true;
    }
  }
  if (found) saveSchedules();
  return found;
}

bool GwScheduleManager::toggleSchedule(uint8_t index) {
  if (index >= ruleCount) return false;
  rules[index].enabled = !rules[index].enabled;
  saveSchedules();
  return true;
}

bool GwScheduleManager::removeSchedule(uint8_t index) {
  if (index >= ruleCount) return false;
  for (uint8_t i = index; i < ruleCount - 1; i++) {
    rules[i] = rules[i + 1];
  }
  ruleCount--;
  saveSchedules();
  return true;
}

void GwScheduleManager::clearSchedules() {
  ruleCount = 0;
  saveSchedules();
}

const FeederScheduleRule* GwScheduleManager::getSchedule(uint8_t index) const {
  if (index >= ruleCount) return NULL;
  return &rules[index];
}

void GwScheduleManager::evaluateSchedules(uint32_t nowEpoch) {
  time_t now = (time_t)nowEpoch;
  struct tm timeinfo;
#if defined(ARDUINO)
  localtime_r(&now, &timeinfo);
#else
  struct tm* t = localtime(&now);
  if (t) timeinfo = *t;
  else memset(&timeinfo, 0, sizeof(timeinfo));
#endif

  bool synced = isTimeSynced();

  for (uint8_t i = 0; i < ruleCount; i++) {
    FeederScheduleRule &r = rules[i];
    if (!r.enabled) continue;

    // 1. Auto-OFF Countdown Timer Evaluation
    if (r.autoOffArmed && r.autoOffMinutes > 0) {
      uint32_t elapsedSec = (nowEpoch >= r.autoOffStartEpoch) ? (nowEpoch - r.autoOffStartEpoch) : 0;
      if (elapsedSec >= (r.autoOffMinutes * 60)) {
        #if defined(ARDUINO)
        Serial.printf("[SCHEDULE] ⏰ Auto-OFF timer fired for Pole 0x%04X Feeder %u\n", r.poleId, r.feederId);
        #endif
        uint32_t seq = 0;
        if (r.feederId == 0) {
          gwMesh.sendFeederCommand(r.poleId, 0x00, 0, 0x3F, seq);
        } else {
          gwMesh.sendFeederCommand(r.poleId, r.feederId, 0, 0, seq);
        }
        r.autoOffArmed = false;
        r.autoOffMinutes = 0;
      }
    }

    // 2. Wall-Clock Schedule Evaluation (Strict safety: requires synchronized time)
    if (!synced) {
      continue; // Never trigger scheduled ON if time is unsynchronized!
    }

    // Scheduled ON check
    if (timeinfo.tm_hour == r.onHour && timeinfo.tm_min == r.onMinute) {
      if (r.lastTriggeredDay != timeinfo.tm_mday) {
        r.lastTriggeredDay = timeinfo.tm_mday;
        #if defined(ARDUINO)
        Serial.printf("[SCHEDULE] ⏰ Scheduled ON fired for Pole 0x%04X Feeder %u\n", r.poleId, r.feederId);
        #endif
        uint32_t seq = 0;
        if (r.feederId == 0) {
          gwMesh.sendFeederCommand(r.poleId, 0x00, 1, 0x3F, seq);
        } else {
          gwMesh.sendFeederCommand(r.poleId, r.feederId, 1, 0, seq);
        }
        if (!r.recurringDaily) {
          r.enabled = false; // Disable one-time schedule
        }
      }
    }

    // Scheduled OFF check
    if (timeinfo.tm_hour == r.offHour && timeinfo.tm_min == r.offMinute) {
      if (r.lastOffTriggeredDay != timeinfo.tm_mday) {
        r.lastOffTriggeredDay = timeinfo.tm_mday;
        #if defined(ARDUINO)
        Serial.printf("[SCHEDULE] ⏰ Scheduled OFF fired for Pole 0x%04X Feeder %u\n", r.poleId, r.feederId);
        #endif
        uint32_t seq = 0;
        if (r.feederId == 0) {
          gwMesh.sendFeederCommand(r.poleId, 0x00, 0, 0x3F, seq);
        } else {
          gwMesh.sendFeederCommand(r.poleId, r.feederId, 0, 0, seq);
        }
      }
    }
  }
}

void GwScheduleManager::service() {
  uint32_t nowMs = millis();

  // Evaluate rules every 1 second
  if (nowMs - lastServiceMs >= 1000) {
    lastServiceMs = nowMs;
    evaluateSchedules(getEpoch());
  }

  // Periodic Mesh Time Synchronization broadcast (every 10 minutes)
  if (nowMs - lastMeshSyncMs >= 600000UL) {
    lastMeshSyncMs = nowMs;
    if (isTimeSynced()) {
      broadcastMeshTimeSync();
    }
  }
}

bool GwScheduleManager::saveSchedules() {
#if defined(ARDUINO)
  if (!prefs.begin("mscb_sched", false)) return false;
  prefs.putUChar("rule_cnt", ruleCount);
  if (ruleCount > 0) {
    prefs.putBytes("rules", rules, ruleCount * sizeof(FeederScheduleRule));
  } else {
    prefs.remove("rules");
  }
  prefs.end();
#endif
  return true;
}

bool GwScheduleManager::loadSchedules() {
#if defined(ARDUINO)
  if (!prefs.begin("mscb_sched", true)) return false;
  ruleCount = prefs.getUChar("rule_cnt", 0);
  if (ruleCount > GW_MAX_SCHEDULE_RULES) ruleCount = 0;
  if (ruleCount > 0) {
    prefs.getBytes("rules", rules, ruleCount * sizeof(FeederScheduleRule));
  }
  prefs.end();
#endif
  return true;
}
