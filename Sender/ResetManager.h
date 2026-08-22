/*
 * =====================================================================
 *  RESETMANAGER.H — SAFE REBOOT & RESET ORCHESTRATION
 * =====================================================================
 */

#ifndef RESET_MANAGER_H
#define RESET_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

class ResetManager {
public:
  ResetManager();
  void init();

  // Reset triggers (after user confirmation)
  void executeResetPole(const String &chatId, const String &nodeId);
  void executeResetNeighbour(const String &chatId, const String &originPoleId, const String &neighbourId);
  void executeResetAll(const String &chatId);

  // ACK tracking & timeout processing
  void onResetAckReceived(const String &srcNodeId, uint16_t seq);
  void processResetTracker();

  bool isResetAllInProgress() const;

private:
  BatchCmdTracker resetTracker;
};

extern ResetManager resetMgr;

#endif // RESET_MANAGER_H
