/**
 * @file PoleHeartbeatSender.cpp
 * @brief MSCB Smart Pole Node - Periodic Heartbeat Uplink Implementation
 * @version 1.0.0
 */

#include "PoleHeartbeatSender.h"
#include "PoleRegistrationClient.h"
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
  hb.auxFeedbackValid = poleFeederMgr.isAuxHardwareDetected() ? 1 : 0;
  hb.mcbStatusValid = poleFeederMgr.isAuxHardwareDetected() ? 1 : 0;
  hb.uptimeSec = millis() / 1000;
  hb.busVoltageV = poleMetrology.getBusVoltageR();
  float sumCurr = 0.0f;
  bool anyNan = false;

  for (uint8_t f = 0; f < 6; f++) {
    float iPhR = poleMetrology.getFeederCurrent(f, 0); // Phase R
    if (isnan(iPhR)) {
      anyNan = true;
      break;
    }
    sumCurr += iPhR;
  }
  hb.totalCurrentA = anyNan ? NAN : sumCurr;

  poleMesh.sendUplinkPacket(PKT_HEARTBEAT_PING, (const uint8_t*)&hb, sizeof(PktHeartbeatPingPayload), PRIO_ROUTINE);
  heartbeatsSent++;
  lastHeartbeatMs = millis();
}

void PoleHeartbeatSender::handleHeartbeatAck(const PktHeartbeatAckPayload &ack) {
  if (ack.poleId == poleStorage.getAssignedPoleId()) {
    acksReceived++;

    if (ack.ackStatus == HB_ACK_REVOKED_LOCKED) {
      Serial.println(F("[REVOCATION] 🚨 Field Pole locked down permanently due to Gateway revocation in Heartbeat ACK."));
      poleStorage.setRevokedLocked("GATEWAY_HB_ACK_REVOKED");
      poleActuators.emergencyShutdownAll("GATEWAY_REVOKED_HB_ACK");
      poleActuators.setRelaysLockedDisabled(true);
      poleRegClient.clearRegistration();
      poleRegClient.setRevokedLockedState();
    } else if (ack.ackStatus == HB_ACK_RE_REGISTER_REQUIRED) {
      Serial.println(F("[REGISTRATION] ⚠️ Gateway requested re-registration. Clearing local registration."));
      poleRegClient.clearRegistration();
    } else if (ack.ackStatus == HB_ACK_UNKNOWN_NODE) {
      Serial.println(F("[REGISTRATION] ⚠️ Gateway reports unknown node. Resetting local registration."));
      poleRegClient.clearRegistration();
    }
  }
}

void PoleHeartbeatSender::service() {
  uint32_t intervalMs = (uint32_t)poleStorage.getHeartbeatIntervalSec() * 1000UL;
  if (intervalMs == 0) intervalMs = 30000UL;

  if (millis() - lastHeartbeatMs >= intervalMs) {
    sendHeartbeat();
  }
}
