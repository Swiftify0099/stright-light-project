/*
 * =====================================================================
 *  HEARTBEATMANAGER.H — FIELD POLE AUTONOMOUS HEARTBEAT DISPATCHER
 * =====================================================================
 */

#ifndef HEARTBEAT_MANAGER_H
#define HEARTBEAT_MANAGER_H

#include <Arduino.h>
#include "config.h"

class HeartbeatManager {
public:
  HeartbeatManager();
  void init();

  void sendHeartbeat();
  void checkHeartbeatInterval();

private:
  unsigned long tLastHeartbeat;
  unsigned long currentInterval;
};

extern HeartbeatManager heartbeatMgr;

#endif // HEARTBEAT_MANAGER_H
