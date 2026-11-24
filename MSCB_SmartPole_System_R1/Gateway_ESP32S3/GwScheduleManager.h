/**
 * @file GwScheduleManager.h
 * @brief MSCB Smart Pole Gateway - Feeder Timer, Scheduling & Mesh Time Synchronization
 * @version 1.0.0
 * 
 * Enforces:
 * - Real NTP synchronization with India Standard Time (Asia/Kolkata, UTC+05:30).
 * - Safe schedule execution: If time is unsynchronized (TIME_UNSYNCED), scheduled ON commands are strictly blocked.
 * - Auto-OFF countdown timers per feeder/pole.
 * - Daily recurring schedules (ON/OFF times).
 * - Mesh time synchronization broadcast to Field Poles.
 * - NVS persistence across Gateway reboot.
 */

#ifndef GW_SCHEDULE_MANAGER_H
#define GW_SCHEDULE_MANAGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#if defined(ARDUINO)
  #include <Arduino.h>
  #include <Preferences.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "GwConfig.h"

#define GW_MAX_SCHEDULE_RULES 24

struct FeederScheduleRule {
  bool     enabled;
  uint16_t poleId;            // Target pole ID (e.g. 0x0002)
  uint8_t  feederId;          // 1..6 or 0 for All Feeders on pole
  bool     recurringDaily;    // true = Daily schedule, false = one-time
  uint8_t  onHour;            // 0..23 (IST)
  uint8_t  onMinute;          // 0..59 (IST)
  uint8_t  offHour;           // 0..23 (IST)
  uint8_t  offMinute;         // 0..59 (IST)
  uint8_t  lastTriggeredDay;  // Day of month when ON was last triggered
  uint8_t  lastOffTriggeredDay;
  bool     autoOffArmed;      // true if countdown auto-off active
  uint32_t autoOffMinutes;    // Duration in minutes
  uint32_t autoOffStartEpoch; // Epoch when timer started
};

class GwScheduleManager {
public:
  GwScheduleManager();

  bool begin();
  void service(); // Periodic schedule evaluation ticker

  // Time Synchronization
  bool isTimeSynced() const;
  uint32_t getEpoch() const;
  void getIstTimeStr(char* outBuf, size_t maxLen) const;
  void broadcastMeshTimeSync();

  // Schedule Management
  bool addSchedule(uint16_t poleId, uint8_t feederId,
                   uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM,
                   bool recurring = true);
  bool armAutoOffTimer(uint16_t poleId, uint8_t feederId, uint32_t minutes);
  bool cancelAutoOffTimer(uint16_t poleId, uint8_t feederId);
  bool toggleSchedule(uint8_t index);
  bool removeSchedule(uint8_t index);
  void clearSchedules();

  uint8_t getScheduleCount() const { return ruleCount; }
  const FeederScheduleRule* getSchedule(uint8_t index) const;

  // Persistence
  bool saveSchedules();
  bool loadSchedules();

private:
  void evaluateSchedules(uint32_t nowEpoch);

  uint8_t            ruleCount;
  FeederScheduleRule rules[GW_MAX_SCHEDULE_RULES];
  uint32_t           lastServiceMs;
  uint32_t           lastMeshSyncMs;
  uint32_t           lastNtpCheckMs;

#if defined(ARDUINO)
  Preferences        prefs;
#endif
};

extern GwScheduleManager gwSchedule;

#endif // GW_SCHEDULE_MANAGER_H
