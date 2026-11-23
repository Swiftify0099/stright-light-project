/**
 * @file PoleHeartbeatSender.h
 * @brief MSCB Smart Pole Node - Periodic Heartbeat Uplink
 * @version 1.0.0
 */

#ifndef POLE_HEARTBEAT_SENDER_H
#define POLE_HEARTBEAT_SENDER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "PoleConfig.h"
#include "PoleStorageManager.h"
#include "PoleMeshRouter.h"
#include "PoleActuators.h"
#include "PoleMetrology.h"
#if __has_include("../MSCBShared/Protocol.h")
  #include "../MSCBShared/Protocol.h"
#elif __has_include(<Protocol.h>)
  #include <Protocol.h>
#else
  #include "Protocol.h"
#endif

class PoleHeartbeatSender {
public:
  PoleHeartbeatSender();

  bool begin();
  void service(); // Periodic heartbeat transmission

  void sendHeartbeat();
  void handleHeartbeatAck(const PktHeartbeatAckPayload &ack);

  uint32_t getHeartbeatsSent() const { return heartbeatsSent; }
  uint32_t getAcksReceived() const { return acksReceived; }

private:
  uint32_t lastHeartbeatMs;
  uint32_t heartbeatsSent;
  uint32_t acksReceived;
};

extern PoleHeartbeatSender poleHeartbeat;

#endif // POLE_HEARTBEAT_SENDER_H
