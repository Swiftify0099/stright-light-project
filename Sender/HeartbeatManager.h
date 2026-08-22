/*
 * =====================================================================
 *  HEARTBEATMANAGER.H — GATEWAY HEARTBEAT & TIMEOUT MONITOR
 * =====================================================================
 */

#ifndef HEARTBEAT_MANAGER_H
#define HEARTBEAT_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

class HeartbeatManager {
public:
  HeartbeatManager();
  void init();

  void checkNodeTimeouts();
  void processHeartbeatCycle();
};

extern HeartbeatManager heartbeatMgr;

#endif // HEARTBEAT_MANAGER_H
