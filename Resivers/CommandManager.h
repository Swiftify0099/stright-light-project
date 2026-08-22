/*
 * =====================================================================
 *  COMMANDMANAGER.H — FIELD POLE COMMAND PROCESSOR & ACK DISPATCHER
 * =====================================================================
 */

#ifndef COMMAND_MANAGER_H
#define COMMAND_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

class CommandManager {
public:
  CommandManager();
  void init();

  void processCommand(const String &cmd, const String &from, uint16_t seq, int rssi, int snr);
  void sendAck(const String &to, const String &nextHop, uint16_t seq, const String &cmd, const String &status, const String &state);
  void sendStatus(const String &replyTo, uint16_t seq);
  void sendNewNode();

  // Autonomous local schedule & timer execution
  void checkTimerExpiration();
  void checkAutonomousSchedule();

  bool isTimerActive() const;
  unsigned long getRemainingTimerSec() const;

private:
  bool          timerActive;
  unsigned long timerEndMs;
  unsigned long timerDurationMs;

  unsigned long epochSeconds;
  unsigned long epochMillis;
  bool          hasTimeSync;
  int           lastSchedChecked;
  int           newNodeRetries;
};

extern CommandManager cmdMgr;

#endif // COMMAND_MANAGER_H
