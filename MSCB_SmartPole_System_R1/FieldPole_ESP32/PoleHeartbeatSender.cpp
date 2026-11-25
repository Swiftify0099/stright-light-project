/**
 * @file PoleHeartbeatSender.cpp
 * @brief MSCB Smart Pole Node - Periodic Heartbeat Uplink Implementation
 * @version 1.0.0
 */

#include "PoleHeartbeatSender.h"
#include "PoleRegistrationClient.h"
#if __has_include("../MSCBShared/CommandSecurity.h")
  #include "../MSCBShared/CommandSecurity.h"
#elif __has_include(<CommandSecurity.h>)
  #include <CommandSecurity.h>
#else
  #include "CommandSecurity.h"
#endif
#include <string.h>

PoleHeartbeatSender poleHeartbeat;

PoleHeartbeatSender::PoleHeartbeatSender() :
  lastHeartbeatMs(0), heartbeatsSent(0), acksReceived(0), hbSeqNonce(1)
{
}

bool PoleHeartbeatSender::begin() {
  lastHeartbeatMs = millis();
  hbSeqNonce = poleStorage.nextBootHeartbeatSeqBase() + 1;
  if (hbSeqNonce == 0) hbSeqNonce = 1;
  return true;
}

void PoleHeartbeatSender::sendHeartbeat() {
  if (poleStorage.isRevokedLocked()) return;
  NodeRegClientState regSt = poleRegClient.getState();
  bool handshakePending = (regSt == REG_CLIENT_WAITING_FOR_HANDSHAKE ||
                           poleStorage.getRegState() == REG_CLIENT_WAITING_FOR_HANDSHAKE);
  if (!poleStorage.isRegistered() && !handshakePending) return;

  uint16_t poleId = poleStorage.getAssignedPoleId();
  if (!mscbIsValidAssignedPoleId(poleId)) return;
  if (!cmdSecurity.isKeyProvisioned()) return;

  PktHeartbeatPingPayload hb;
  memset(&hb, 0, sizeof(hb));
  hb.poleId = poleId;
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

  if (poleRegClient.getMacAddress()) {
    memcpy(hb.macAddress, poleRegClient.getMacAddress(), 6);
  }
  hb.sessionToken = (poleRegClient.getSessionToken() != 0) ? poleRegClient.getSessionToken() : poleStorage.getSessionToken();
  hbSeqNonce++;
  if (hbSeqNonce == 0) hbSeqNonce = 1;
  hb.seqNonce = hbSeqNonce;

  if (!cmdSecurity.computeHeartbeatAuth(hb, hb.authTag)) return;

  lastHeartbeatMs = millis();
  if (poleMesh.sendUplinkPacket(PKT_HEARTBEAT_PING, (const uint8_t*)&hb, sizeof(PktHeartbeatPingPayload), PRIO_ROUTINE)) {
    heartbeatsSent++;
  }
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
      poleMesh.resetToUnassignedIdentity(poleRegClient.getMacAddress());
    } else if (ack.ackStatus == HB_ACK_RE_REGISTER_REQUIRED) {
      Serial.println(F("[REGISTRATION] ⚠️ Gateway requested re-registration. Clearing local registration."));
      poleRegClient.clearRegistration();
      poleMesh.resetToUnassignedIdentity(poleRegClient.getMacAddress());
    } else if (ack.ackStatus == HB_ACK_UNKNOWN_NODE) {
      Serial.println(F("[REGISTRATION] ⚠️ Gateway reports unknown node. Resetting local registration."));
      poleRegClient.clearRegistration();
      poleMesh.resetToUnassignedIdentity(poleRegClient.getMacAddress());
    } else if (ack.ackStatus == HB_ACK_OK || ack.ackStatus == HB_ACK_CONFIG_OUTDATED) {
      if (poleRegClient.getState() == REG_CLIENT_WAITING_FOR_HANDSHAKE ||
          poleRegClient.getState() == REG_CLIENT_APPROVAL_RECEIVED ||
          poleStorage.getRegState() == REG_CLIENT_WAITING_FOR_HANDSHAKE) {
        poleRegClient.confirmHandshakeComplete();
        Serial.println(F("[REGISTRATION] ✅ Gateway confirmed first authenticated heartbeat! Pole state is now REGISTERED."));
      }
    }
  }
}

void PoleHeartbeatSender::service() {
  if (poleStorage.isRevokedLocked()) return;
  bool handshakePending = (poleRegClient.getState() == REG_CLIENT_WAITING_FOR_HANDSHAKE ||
                           poleStorage.getRegState() == REG_CLIENT_WAITING_FOR_HANDSHAKE);
  if (!poleStorage.isRegistered() && !handshakePending) return;

  uint32_t intervalMs = handshakePending ? 5000UL : ((uint32_t)poleStorage.getHeartbeatIntervalSec() * 1000UL);
  if (intervalMs == 0) intervalMs = 30000UL;

  if (millis() - lastHeartbeatMs >= intervalMs) {
    sendHeartbeat();
  }
}
