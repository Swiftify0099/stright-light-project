/**
 * @file PoleHeartbeatSender.cpp
 * @brief MSCB Smart Pole Node - Periodic Heartbeat Uplink Implementation
 * @version 1.0.0
 */

#include "PoleHeartbeatSender.h"
#include <string.h>

PoleHeartbeatSender poleHeartbeat;

PoleHeartbeatSender::PoleHeartbeatSender() :
  lastHeartbeatMs(0), heartbeatsSent(0), acksReceived(0)
{
}

bool PoleHeartbeatSender::begin() {
  lastHeartbeatMs = millis();
  return true;
}

void PoleHeartbeatSender::sendHeartbeat() {
  if (!poleStorage.isRegistered() || poleStorage.isRevokedLocked()) return;

  PktHeartbeatPingPayload hb;
  hb.poleId = poleStorage.getAssignedPoleId();
  hb.primaryParent = poleMesh.getPrimaryParent();
  hb.backupParent = poleMesh.getBackupParent();
  hb.rank = poleMesh.getMyRank();
  hb.healthState = (poleActuators.getDriverState() == ACTUATOR_STATE_READY) ? NODE_HEALTH_OK : NODE_HEALTH_WARNING;
  hb.feederStateMask = poleActuators.getActiveRelayMask();
  hb.auxFeedbackMask = poleFeederMgr.readContactorAuxMask();
  hb.mcbStatusMask = poleFeederMgr.readMcbTripMask();
  hb.uptimeSec = millis() / 1000;
  hb.busVoltageV = poleMetrology.getBusVoltageR();
  hb.totalCurrentA = 0.0f;

  for (uint8_t f = 0; f < 6; f++) {
    hb.totalCurrentA += poleMetrology.getFeederCurrent(f, 0); // Phase R
  }

  poleMesh.sendUplinkPacket(PKT_HEARTBEAT_PING, (const uint8_t*)&hb, sizeof(PktHeartbeatPingPayload), PRIO_ROUTINE);
  heartbeatsSent++;
  lastHeartbeatMs = millis();
}

void PoleHeartbeatSender::handleHeartbeatAck(const PktHeartbeatAckPayload &ack) {
  if (ack.poleId == poleStorage.getAssignedPoleId()) {
    acksReceived++;
  }
}

void PoleHeartbeatSender::service() {
  uint32_t intervalMs = (uint32_t)poleStorage.getHeartbeatIntervalSec() * 1000UL;
  if (intervalMs == 0) intervalMs = 30000UL;

  if (millis() - lastHeartbeatMs >= intervalMs) {
    sendHeartbeat();
  }
}
