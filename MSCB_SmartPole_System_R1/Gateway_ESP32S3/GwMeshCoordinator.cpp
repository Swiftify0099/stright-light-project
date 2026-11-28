/**
 * @file GwMeshCoordinator.cpp
 * @brief MSCB Smart Pole Gateway - Root Mesh Coordinator & Multi-Hop Router
 * Implementation
 * @version 1.0.0
 */

#include "GwMeshCoordinator.h"
#include "GwHeartbeatMonitor.h"
#include "GwNetworkQueue.h"
#include "GwScheduleManager.h"
#include <string.h>

// Feeder ACK classification (single source of truth for the gateway).
// A success state must never be counted as a rejection: CONTACTOR_CLOSED,
// RELAY_OUTPUT_ENERGIZED and OUTPUT_CHANGED are the pole's normal ON results.
static bool isRejectedFeederAck(uint8_t s) {
  switch (s) {
    case FEEDER_ACK_REJECTED_STALE:
    case FEEDER_ACK_REJECTED_LOCKED:
    case FEEDER_ACK_REJECTED_FAULT:
    case FEEDER_ACK_REJECTED_TIME_UNSYNCED:
    case FEEDER_ACK_HARDWARE_FAULT:
    case FEEDER_ACK_MCB_TRIPPED:
    case FEEDER_ACK_COMMAND_TIMEOUT:
      return true;
    default:
      return false;
  }
}

static bool isTerminalFeederAck(uint8_t s) {
  return isRejectedFeederAck(s) || s == FEEDER_ACK_COMPLETED ||
         s == FEEDER_ACK_CONTACTOR_CLOSED || s == FEEDER_ACK_OUTPUT_CHANGED ||
         s == FEEDER_ACK_RELAY_OUTPUT_ENERGIZED ||
         s == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
         s == FEEDER_ACK_FEEDBACK_NOT_CONNECTED ||
         s == FEEDER_ACK_FEEDBACK_UNAVAILABLE;
}

GwMeshCoordinator gwMesh;

GwMeshCoordinator::GwMeshCoordinator()
    : seqCounter(1), lastBeaconMs(0), packetsForwarded(0),
      duplicatesSuppressed(0), hopExceededCount(0), dupCacheIdx(0),
      feederAckPending(false), pendingAckSeq(0), pendingAckPole(0),
      pendingAckFeeder(0), pendingAckStartMs(0), lastAckState(0),
      lastAckPole(0) {
  memset(dupCache, 0, sizeof(dupCache));
  memset(trackedCommands, 0, sizeof(trackedCommands));
  memset(pendingRejections, 0, sizeof(pendingRejections));
}

bool GwMeshCoordinator::begin() {
  lastBeaconMs = millis();
  static PersistentNonceRecord gwNvsBank0;
  static PersistentNonceRecord gwNvsBank1;
  if (!replayProtector.isTransmitterStorageReady()) {
    if (!replayProtector.initTransmitterStorage(&gwNvsBank0, &gwNvsBank1)) {
      replayProtector.reprovisionStorage(
          cmdSecurity.getActiveKeyEpoch() ? cmdSecurity.getActiveKeyEpoch() : 1,
          &gwNvsBank0, &gwNvsBank1);
    }
  }
  return true;
}

bool GwMeshCoordinator::isDuplicate(uint16_t src, uint16_t seq) {
  for (uint8_t i = 0; i < GW_DUP_CACHE_SIZE; i++) {
    if (dupCache[i].srcAddress == src && dupCache[i].seqNumber == seq) {
      return true;
    }
  }
  return false;
}

void GwMeshCoordinator::addDuplicate(uint16_t src, uint16_t seq) {
  dupCache[dupCacheIdx].srcAddress = src;
  dupCache[dupCacheIdx].seqNumber = seq;
  dupCache[dupCacheIdx].timestampMs = millis();
  dupCacheIdx = (dupCacheIdx + 1) % GW_DUP_CACHE_SIZE;
}

void GwMeshCoordinator::broadcastBeacon() {
  MeshWireFrame frame;
  memset(&frame, 0, sizeof(MeshWireFrame));

  frame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  frame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  frame.header.version = MSCB_PROTOCOL_VERSION;
  frame.header.type = PKT_BEACON_ANNOUNCE;
  frame.header.priority = PRIO_ROUTINE;
  frame.header.hopCount = 0;
  frame.header.maxHops = GW_MESH_MAX_HOPS;
  frame.header.seqNumber = seqCounter++;
  frame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
  frame.header.dstAddress = MSCB_BROADCAST_NODE_ID;
  frame.header.nextHop = MSCB_BROADCAST_NODE_ID;
  frame.header.previousHopAddress = MSCB_GATEWAY_NODE_ID;

  PktBeaconAnnouncePayload beacon;
  beacon.senderId = MSCB_GATEWAY_NODE_ID;
  beacon.rank = 0; // Root is Rank 0
  beacon.primaryParent = 0;
  beacon.capacitySlotsFree = GW_MAX_MANAGED_POLES - gwRegistry.getActiveCount();
  beacon.beaconTimestamp = millis() / 1000;

  frame.header.payloadLen = sizeof(PktBeaconAnnouncePayload);
  memcpy(frame.payload, &beacon, sizeof(PktBeaconAnnouncePayload));
  frame.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(frame);

  gwRadio.sendPacket(frame);
}

void GwMeshCoordinator::service() {
  uint32_t now = millis();

  // 1. Periodic Beacon Announce (every 30s)
  if (now - lastBeaconMs >= MSCB_BEACON_INTERVAL_MS) {
    lastBeaconMs = now;
    broadcastBeacon();
  }

  // 2. Poll Radio for incoming packets
  if (gwRadio.hasPacket()) {
    MeshWireFrame frame;
    int16_t rssi = 0;
    int8_t snr = 0;
    if (gwRadio.receivePacket(frame, rssi, snr)) {
      processIncomingWireFrame(frame, rssi, snr);
    }
  }

  // 3. Timeout check for tracked feeder commands (Non-blocking)
  for (uint8_t i = 0; i < GW_MAX_TRACKED_COMMANDS; i++) {
    if (trackedCommands[i].inUse && !trackedCommands[i].completed) {
      if (now - trackedCommands[i].sentMs >= trackedCommands[i].timeoutMs) {
        trackedCommands[i].completed = true;
        trackedCommands[i].timedOut = true;
        if ((trackedCommands[i].terminalAckMask &
             trackedCommands[i].expectedMask) !=
            trackedCommands[i].expectedMask) {
          trackedCommands[i].partialFailure = true;
        }
      }
    }
  }

  // Timeout check for legacy single-command pending Feeder ACK
  if (feederAckPending && (now - pendingAckStartMs > 5000)) {
    feederAckPending = false;
  }

  // 4. Bounded retry loop for registration approval responses awaiting
  // handshake & queued rejections
  serviceRegistrationRetries();

  // 5. Service Network-Wide Command Queue
  gwNetworkQueue.service();
}

void GwMeshCoordinator::serviceRegistrationRetries() {
  uint32_t now = millis();
  uint8_t actCount = gwRegistry.getActiveCount();
  for (uint8_t i = 0; i < actCount; i++) {
    const RegisteredPole *cp = gwRegistry.getPoleByIndex(i);
    if (!cp || cp->status != POLE_STATUS_PENDING_ACK)
      continue;
    if (cp->regResponseAttempts >= GW_REG_RESP_MAX_RETRIES)
      continue;
    if (cp->lastRegResponseMs == 0 ||
        (now - cp->lastRegResponseMs >= GW_REG_RESP_RETRY_INTERVAL_MS)) {
      sendRegisterResponse(cp->mac, cp->poleId, cp->primaryParent,
                           REG_RESP_APPROVED);
    }
  }

  for (uint8_t i = 0; i < GW_MAX_PENDING_REJECTIONS; i++) {
    if (!pendingRejections[i].inUse)
      continue;
    if (pendingRejections[i].attempts >= GW_REG_RESP_MAX_RETRIES) {
      pendingRejections[i].inUse = false;
      continue;
    }
    if (pendingRejections[i].lastAttemptMs == 0 ||
        (now - pendingRejections[i].lastAttemptMs >=
         GW_REG_RESP_RETRY_INTERVAL_MS)) {
      if (pendingRejections[i].sessionToken == 0 ||
          pendingRejections[i].nonce == 0) {
        Serial.println(F("[SECURITY] ❌ Queued rejection discarded: missing "
                         "correlation metadata"));
        pendingRejections[i].inUse = false;
        continue;
      }

      PktRegisterResponsePayload resp;
      memset(&resp, 0, sizeof(resp));
      memcpy(resp.macAddress, pendingRejections[i].mac, 6);
      resp.status = pendingRejections[i].status;
      resp.networkKeyEpoch =
          cmdSecurity.getActiveKeyEpoch() ? cmdSecurity.getActiveKeyEpoch() : 1;
      resp.sessionToken = pendingRejections[i].sessionToken;
      resp.nonce = pendingRejections[i].nonce;
      if (!cmdSecurity.isKeyProvisioned() ||
          !cmdSecurity.computeRegisterResponseAuth(resp, resp.authTag)) {
        pendingRejections[i].inUse = false;
        continue;
      }

      pendingRejections[i].attempts++;
      pendingRejections[i].lastAttemptMs = now;
      if (sendRegisterResponseFrame(resp, pendingRejections[i].firstHop,
                                    MSCB_BROADCAST_NODE_ID)) {
        pendingRejections[i].inUse = false;
      }
    }
  }
}

bool GwMeshCoordinator::queueRejectionResponse(const uint8_t mac[6],
                                               uint8_t status,
                                               uint32_t sessionToken,
                                               uint32_t nonce,
                                               uint16_t firstHop) {
  if (!mac)
    return false;
  PendingRegistration *p = gwRegistry.getPendingByMac(mac);
  uint32_t effSess =
      (sessionToken != 0) ? sessionToken : (p ? p->sessionToken : 0);
  uint32_t effNonce = (nonce != 0) ? nonce : (p ? p->lastReqNonce : 0);
  uint16_t effHop = (firstHop != 0)
                        ? firstHop
                        : (p ? p->firstHopAddress : MSCB_BROADCAST_NODE_ID);
  if (effSess == 0 || effNonce == 0) {
    Serial.println(
        F("[SECURITY] ❌ Rejection response refused: no active session/nonce"));
    return false;
  }

  for (uint8_t i = 0; i < GW_MAX_PENDING_REJECTIONS; i++) {
    if (!pendingRejections[i].inUse) {
      pendingRejections[i].inUse = true;
      memcpy(pendingRejections[i].mac, mac, 6);
      pendingRejections[i].status = status;
      pendingRejections[i].sessionToken = effSess;
      pendingRejections[i].nonce = effNonce;
      pendingRejections[i].firstHop = effHop;
      pendingRejections[i].attempts = 0;
      pendingRejections[i].lastAttemptMs = 0;
      break;
    }
  }
  return sendRegisterResponse(mac, 0, MSCB_GATEWAY_NODE_ID, status);
}

bool GwMeshCoordinator::processIncomingWireFrame(const MeshWireFrame &frame,
                                                 int16_t rssi, int8_t snr) {
  // Validate protocol magic, version & payload bounds
  if (frame.header.magic[0] != MSCB_PROTOCOL_MAGIC_0 ||
      frame.header.magic[1] != MSCB_PROTOCOL_MAGIC_1 ||
      frame.header.version != MSCB_PROTOCOL_VERSION ||
      frame.header.payloadLen > MSCB_MAX_PAYLOAD_SIZE) {
    return false;
  }

  // Validate addresses and immediate previous hop identity
  if (frame.header.srcAddress == 0 ||
      frame.header.srcAddress == MSCB_BROADCAST_NODE_ID ||
      frame.header.srcAddress == MSCB_GATEWAY_NODE_ID ||
      frame.header.dstAddress == 0 || frame.header.nextHop == 0) {
    return false;
  }
  if (frame.header.previousHopAddress == 0 ||
      frame.header.previousHopAddress == MSCB_BROADCAST_NODE_ID ||
      frame.header.previousHopAddress == MSCB_GATEWAY_NODE_ID) {
    return false;
  }

  // Strict Maximum Hop Validation (1 <= maxHops <= GW_MESH_MAX_HOPS, hopCount
  // <= maxHops)
  if (frame.header.maxHops == 0 || frame.header.maxHops > GW_MESH_MAX_HOPS ||
      frame.header.hopCount > frame.header.maxHops ||
      frame.header.hopCount > GW_MESH_MAX_HOPS) {
    hopExceededCount++;
    return false;
  }

  // Fail-closed immediate-hop and full-frame HMAC token verification
  if (!cmdSecurity.verifyFrameHopAuthToken(frame)) {
    return false;
  }

  // Check Duplicate Suppression
  if (isDuplicate(frame.header.srcAddress, frame.header.seqNumber)) {
    duplicatesSuppressed++;
    return false;
  }
  addDuplicate(frame.header.srcAddress, frame.header.seqNumber);

  // If packet is addressed to Gateway (0x0001) or Broadcast
  if (frame.header.dstAddress == MSCB_GATEWAY_NODE_ID ||
      frame.header.dstAddress == MSCB_BROADCAST_NODE_ID) {
    switch (frame.header.type) {
    case PKT_REGISTER_REQUEST: {
      if (frame.header.payloadLen >= sizeof(PktRegisterRequestPayload)) {
        if (!cmdSecurity.isKeyProvisioned()) {
          Serial.println(F("[SECURITY] ❌ Registration request dropped: "
                           "Gateway key unprovisioned (fail-closed)"));
          break;
        }

        PktRegisterRequestPayload req;
        memcpy(&req, frame.payload, sizeof(PktRegisterRequestPayload));

        if (!mscbIsValidMac(req.macAddress, false)) {
          Serial.println(F("[SECURITY] ❌ Registration request dropped: "
                           "Invalid MAC address"));
          break;
        }

        uint16_t returnHop =
            (frame.header.previousHopAddress != 0 &&
             frame.header.previousHopAddress != MSCB_GATEWAY_NODE_ID)
                ? frame.header.previousHopAddress
                : frame.header.srcAddress;
        if (returnHop == 0)
          returnHop = MSCB_BROADCAST_NODE_ID;

        PktRegisterResponsePayload resp;
        uint8_t status = gwRegistry.handleJoinRequest(req, rssi, snr, resp);
        if (status == REG_RESP_PENDING_APPROVAL ||
            status == REG_RESP_APPROVED) {
          gwRegistry.setPendingSourceRoute(req.macAddress, req.nonce, returnHop,
                                           frame.header.srcAddress);
        }

        if (status == REG_RESP_REJECTED_UNAUTHORIZED &&
            (req.sessionToken == 0 || req.nonce == 0)) {
          // Do not waste airtime or reflect unauthenticated malformed probes
          // without valid session/nonce
          break;
        }

        // R3.2.2 P0 Fix: Send response for ALL outcomes including
        // REG_RESP_PENDING_APPROVAL! Use the same authenticated frame
        // construction as approval/retry responses.
        if (resp.sessionToken == 0 || resp.nonce == 0) {
          Serial.println(F("[SECURITY] ❌ Registration response suppressed: "
                           "missing correlation metadata"));
          break;
        }
        sendRegisterResponseFrame(resp, returnHop,
                                  resp.assignedPoleId
                                      ? resp.assignedPoleId
                                      : (frame.header.srcAddress
                                             ? frame.header.srcAddress
                                             : MSCB_BROADCAST_NODE_ID));
      }
      break;
    }

    case PKT_HEARTBEAT_PING: {
      if (frame.header.payloadLen >= sizeof(PktHeartbeatPingPayload)) {
        if (!cmdSecurity.isKeyProvisioned()) {
          Serial.println(
              F("[SECURITY] ❌ Heartbeat rejected: Gateway key unprovisioned"));
          break;
        }

        PktHeartbeatPingPayload hb;
        memcpy(&hb, frame.payload, sizeof(PktHeartbeatPingPayload));

        // Verify frame source identity agrees with heartbeat payload pole ID &
        // valid ID range
        if (!mscbIsValidAssignedPoleId(hb.poleId) ||
            frame.header.srcAddress != hb.poleId) {
          Serial.printf("[SECURITY] ❌ Spoofed heartbeat dropped (hdr "
                        "src=0x%04X vs hb.poleId=0x%04X)\n",
                        frame.header.srcAddress, hb.poleId);
          break;
        }

        // Verify cryptographic HMAC authentication on heartbeat payload
        if (!cmdSecurity.verifyHeartbeatAuth(hb)) {
          Serial.printf(
              "[SECURITY] ❌ Unauthenticated heartbeat from 0x%04X rejected!\n",
              hb.poleId);
          break;
        }

        uint16_t returnHop =
            (frame.header.previousHopAddress != 0 &&
             frame.header.previousHopAddress != MSCB_GATEWAY_NODE_ID)
                ? frame.header.previousHopAddress
                : frame.header.srcAddress;
        if (returnHop == 0)
          returnHop = hb.poleId;

        // Check if pole is actively registered and not revoked
        RegisteredPole *p = gwRegistry.findPoleById(hb.poleId);
        const PermanentRevocationRecord *revByMac =
            mscbIsValidMac(hb.macAddress, false)
                ? gwRegistry.findRevokedByMac(hb.macAddress)
                : nullptr;
        const PermanentRevocationRecord *revById =
            gwRegistry.findRevokedByPoleId(hb.poleId);
        const PermanentRevocationRecord *rev = revByMac ? revByMac : revById;
        bool isRevoked = (rev != nullptr) ||
                         (p != nullptr && gwRegistry.isMacRevoked(p->mac)) ||
                         (mscbIsValidMac(hb.macAddress, false) &&
                          gwRegistry.isMacRevoked(hb.macAddress));

        if (p == nullptr || isRevoked) {
          if (isRevoked) {
            uint8_t revMac[6] = {0};
            if (mscbIsValidMac(hb.macAddress, false)) {
              memcpy(revMac, hb.macAddress, 6);
            } else if (p != nullptr) {
              memcpy(revMac, p->mac, 6);
            } else if (rev != nullptr && strlen(rev->hwId) >= 12) {
              for (int i = 0; i < 6; i++) {
                char byteStr[3] = {rev->hwId[i * 2], rev->hwId[i * 2 + 1],
                                   '\0'};
                revMac[i] = (uint8_t)strtoul(byteStr, NULL, 16);
              }
            }
            gwRegistry.setReappearedRevokedAlert(
                revMac, rev ? rev->logicalId : (p ? p->stringId : "REVOKED"),
                hb.poleId);
            Serial.printf(
                "[SECURITY] 🚨 Heartbeat from REVOKED pole 0x%04X rejected!\n",
                hb.poleId);
          } else {
            Serial.printf(
                "[MESH] Heartbeat from unregistered pole 0x%04X rejected.\n",
                hb.poleId);
          }

          // Send Heartbeat ACK instructing pole to lock or re-register
          MeshWireFrame ackFrame;
          memset(&ackFrame, 0, sizeof(MeshWireFrame));
          ackFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
          ackFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
          ackFrame.header.version = MSCB_PROTOCOL_VERSION;
          ackFrame.header.type = PKT_HEARTBEAT_ACK;
          ackFrame.header.priority = PRIO_CRITICAL;
          ackFrame.header.hopCount = 0;
          ackFrame.header.maxHops = GW_MESH_MAX_HOPS;
          ackFrame.header.seqNumber = seqCounter++;
          ackFrame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
          ackFrame.header.dstAddress = hb.poleId;
          ackFrame.header.nextHop = returnHop;
          ackFrame.header.previousHopAddress = MSCB_GATEWAY_NODE_ID;

          PktHeartbeatAckPayload ack;
          ack.poleId = hb.poleId;
          ack.gatewayTimeEpoch = gwSchedule.getEpoch();
          ack.configVersion = 0;
          ack.ackStatus =
              isRevoked ? HB_ACK_REVOKED_LOCKED : HB_ACK_RE_REGISTER_REQUIRED;

          ackFrame.header.payloadLen = sizeof(PktHeartbeatAckPayload);
          memcpy(ackFrame.payload, &ack, sizeof(PktHeartbeatAckPayload));
          ackFrame.header.hopAuthToken =
              cmdSecurity.computeFrameHopAuthToken(ackFrame);
          gwRadio.sendPacket(ackFrame);
          break;
        }

        // Verify bound hardware MAC & registration session token match
        // registered pole record!
        if (!mscbIsValidMac(hb.macAddress, false) ||
            memcmp(p->mac, hb.macAddress, 6) != 0) {
          Serial.printf("[SECURITY] ❌ Heartbeat MAC mismatch for pole 0x%04X "
                        "rejected!\n",
                        hb.poleId);
          break;
        }
        if (p->sessionToken != 0 && hb.sessionToken != p->sessionToken) {
          Serial.printf("[SECURITY] ❌ Heartbeat sessionToken mismatch for "
                        "pole 0x%04X rejected!\n",
                        hb.poleId);
          break;
        }

        // Replay protection for heartbeat sequence nonce
        if (p->lastHeartbeatSeq != 0 && hb.seqNonce == p->lastHeartbeatSeq) {
          duplicatesSuppressed++;
          break;
        }
        if (p->lastHeartbeatSeq != 0 && hb.seqNonce < p->lastHeartbeatSeq &&
            hb.uptimeSec >= p->lastUptimeSec) {
          duplicatesSuppressed++;
          break;
        }
        p->lastHeartbeatSeq = hb.seqNonce;
        p->lastUptimeSec = hb.uptimeSec;
        if (returnHop != 0 && returnHop != MSCB_GATEWAY_NODE_ID) {
          p->firstHopAddress = returnHop;
        }

        // Active registered pole: Confirm handshake & update node telemetry +
        // monitor
        if (p->status == POLE_STATUS_PENDING_ACK) {
          gwRegistry.confirmPoleHandshake(hb.poleId);
        }
        uint32_t gwEpoch = gwSchedule.getEpoch();
        if (gwEpoch == 0)
          gwEpoch = (uint32_t)(millis() / 1000UL);
        gwHeartbeat.onHeartbeatReceived(hb.poleId, gwEpoch);
        gwRegistry.updateNodeTelemetry(
            hb.poleId, hb.primaryParent, hb.backupParent, hb.rank,
            hb.feederStateMask, hb.auxFeedbackMask, hb.mcbStatusMask,
            hb.busVoltageV, hb.totalCurrentA, rssi, snr, hb.auxFeedbackValid,
            hb.mcbStatusValid);

        // Send Heartbeat ACK
        MeshWireFrame ackFrame;
        memset(&ackFrame, 0, sizeof(MeshWireFrame));
        ackFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
        ackFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
        ackFrame.header.version = MSCB_PROTOCOL_VERSION;
        ackFrame.header.type = PKT_HEARTBEAT_ACK;
        ackFrame.header.priority = PRIO_ROUTINE;
        ackFrame.header.hopCount = 0;
        ackFrame.header.maxHops = GW_MESH_MAX_HOPS;
        ackFrame.header.seqNumber = seqCounter++;
        ackFrame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
        ackFrame.header.dstAddress = hb.poleId;
        ackFrame.header.nextHop = returnHop;
        ackFrame.header.previousHopAddress = MSCB_GATEWAY_NODE_ID;

        PktHeartbeatAckPayload ack;
        ack.poleId = hb.poleId;
        ack.gatewayTimeEpoch = gwSchedule.getEpoch();
        ack.configVersion = 1;
        ack.ackStatus = HB_ACK_OK;

        ackFrame.header.payloadLen = sizeof(PktHeartbeatAckPayload);
        memcpy(ackFrame.payload, &ack, sizeof(PktHeartbeatAckPayload));
        ackFrame.header.hopAuthToken =
            cmdSecurity.computeFrameHopAuthToken(ackFrame);
        gwRadio.sendPacket(ackFrame);
      }
      break;
    }

    case PKT_ALERT_REVOKED_REAPPEAR: {
      if (frame.header.payloadLen >= sizeof(PktRevokedReappearPayload)) {
        PktRevokedReappearPayload alert;
        memcpy(&alert, frame.payload, sizeof(PktRevokedReappearPayload));
        if (!mscbIsValidMac(alert.macAddress, false))
          break;

        const PermanentRevocationRecord *rev =
            gwRegistry.findRevokedByMac(alert.macAddress);
        const char *prevId = rev ? rev->logicalId : "UNKNOWN";
        gwRegistry.setReappearedRevokedAlert(alert.macAddress, prevId,
                                             alert.previousPoleId);

        Serial.printf("[SECURITY] 🚨 PKT_ALERT_REVOKED_REAPPEAR from MAC "
                      "%02X:%02X:%02X:%02X:%02X:%02X\n",
                      alert.macAddress[0], alert.macAddress[1],
                      alert.macAddress[2], alert.macAddress[3],
                      alert.macAddress[4], alert.macAddress[5]);

        uint16_t returnHop =
            (frame.header.previousHopAddress != 0 &&
             frame.header.previousHopAddress != MSCB_GATEWAY_NODE_ID)
                ? frame.header.previousHopAddress
                : (frame.header.srcAddress ? frame.header.srcAddress
                                           : MSCB_BROADCAST_NODE_ID);

        // Reply with PKT_REGISTER_RESPONSE telling it is REJECTED_REVOKED
        // (authenticated if key provisioned)
        MeshWireFrame respFrame;
        memset(&respFrame, 0, sizeof(MeshWireFrame));
        respFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
        respFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
        respFrame.header.version = MSCB_PROTOCOL_VERSION;
        respFrame.header.type = PKT_REGISTER_RESPONSE;
        respFrame.header.priority = PRIO_CRITICAL;
        respFrame.header.hopCount = 0;
        respFrame.header.maxHops = GW_MESH_MAX_HOPS;
        respFrame.header.seqNumber = seqCounter++;
        respFrame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
        respFrame.header.dstAddress = frame.header.srcAddress
                                          ? frame.header.srcAddress
                                          : MSCB_BROADCAST_NODE_ID;
        respFrame.header.nextHop = returnHop;
        respFrame.header.previousHopAddress = MSCB_GATEWAY_NODE_ID;

        PktRegisterResponsePayload resp;
        memset(&resp, 0, sizeof(resp));
        memcpy(resp.macAddress, alert.macAddress, 6);
        resp.status = REG_RESP_REJECTED_REVOKED;
        resp.assignedPoleId = 0;
        resp.assignedParent = 0;
        resp.heartbeatIntervalSec = 0;
        resp.networkKeyEpoch = cmdSecurity.getActiveKeyEpoch()
                                   ? cmdSecurity.getActiveKeyEpoch()
                                   : 1;
        resp.sessionToken = 1;
        resp.nonce = 1;
        if (cmdSecurity.isKeyProvisioned()) {
          cmdSecurity.computeRegisterResponseAuth(resp, resp.authTag);
        }

        respFrame.header.payloadLen = sizeof(PktRegisterResponsePayload);
        memcpy(respFrame.payload, &resp, sizeof(PktRegisterResponsePayload));
        respFrame.header.hopAuthToken =
            cmdSecurity.computeFrameHopAuthToken(respFrame);
        gwRadio.sendPacket(respFrame);
      }
      break;
    }

    case PKT_ALERT_CHILD_OFFLINE: {
      if (frame.header.payloadLen >= sizeof(PktChildStatusEventPayload)) {
        PktChildStatusEventPayload ev;
        memcpy(&ev, frame.payload, sizeof(PktChildStatusEventPayload));
        if (!mscbIsValidAssignedPoleId(ev.parentPoleId) ||
            !mscbIsValidAssignedPoleId(ev.childPoleId) ||
            ev.parentPoleId == ev.childPoleId ||
            ev.parentPoleId != frame.header.srcAddress || ev.eventType != 0) {
          break;
        }
        uint32_t ts =
            (ev.eventTimestamp != 0)
                ? ev.eventTimestamp
                : (gwSchedule.getEpoch() ? gwSchedule.getEpoch()
                                         : (uint32_t)(millis() / 1000));
        char reasonBuf[32];
        snprintf(reasonBuf, sizeof(reasonBuf), "CHILD_OFFLINE(r=%u,seq=%u)",
                 (unsigned)ev.reason, (unsigned)ev.eventSeqNumber);
        Serial.printf("[MESH] 🚨 Child Offline Alert: Parent 0x%04X reports "
                      "downstream Child 0x%04X OFFLINE\n",
                      ev.parentPoleId, ev.childPoleId);
        gwHeartbeat.recordChildOfflineFromParent(
            ev.childPoleId, ev.parentPoleId, ts, reasonBuf);
      }
      break;
    }

    case PKT_ALERT_CHILD_RECOVERED: {
      if (frame.header.payloadLen >= sizeof(PktChildStatusEventPayload)) {
        PktChildStatusEventPayload ev;
        memcpy(&ev, frame.payload, sizeof(PktChildStatusEventPayload));
        if (!mscbIsValidAssignedPoleId(ev.parentPoleId) ||
            !mscbIsValidAssignedPoleId(ev.childPoleId) ||
            ev.parentPoleId == ev.childPoleId ||
            ev.parentPoleId != frame.header.srcAddress || ev.eventType != 1) {
          break;
        }
        uint32_t ts =
            (ev.eventTimestamp != 0)
                ? ev.eventTimestamp
                : (gwSchedule.getEpoch() ? gwSchedule.getEpoch()
                                         : (uint32_t)(millis() / 1000));
        Serial.printf("[MESH] ✅ Child Recovered Alert: Parent 0x%04X reports "
                      "downstream Child 0x%04X RECOVERED\n",
                      ev.parentPoleId, ev.childPoleId);
        gwHeartbeat.recordChildRecoveredFromParent(ev.childPoleId,
                                                   ev.parentPoleId, ts);
      }
      break;
    }

    case PKT_CMD_REVOKE_ACK: {
      if (frame.header.payloadLen >= sizeof(PktRevokeAckPayload)) {
        PktRevokeAckPayload ack;
        memcpy(&ack, frame.payload, sizeof(PktRevokeAckPayload));
        Serial.printf(
            "[MESH] Pole 0x%04X confirmed revocation ACK (status=%u)\n",
            ack.poleId, ack.status);
        gwRegistry.confirmRevokeDelivery(nullptr, ack.poleId);
      }
      break;
    }

    case PKT_CMD_FEEDER_ACK: {
      if (frame.header.payloadLen >= sizeof(PktFeederAckPayload)) {
        PktFeederAckPayload ack;
        memcpy(&ack, frame.payload, sizeof(PktFeederAckPayload));

        if (feederAckPending && ack.sequenceNumber == pendingAckSeq &&
            ack.poleId == pendingAckPole) {
          lastAckState = ack.ackState;
          lastAckPole = ack.poleId;
          if (ack.ackState == FEEDER_ACK_COMPLETED ||
              ack.ackState == FEEDER_ACK_CONTACTOR_CLOSED ||
              ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
              isRejectedFeederAck(ack.ackState) ||
              ack.ackState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED) {
            feederAckPending = false;
          }
        }

        // Update non-blocking tracked command record
        for (uint8_t i = 0; i < GW_MAX_TRACKED_COMMANDS; i++) {
          if (trackedCommands[i].inUse &&
              trackedCommands[i].seqNumber == ack.sequenceNumber &&
              trackedCommands[i].poleId == ack.poleId) {

            uint8_t fBit = 0;
            if (ack.feederId >= 1 && ack.feederId <= 6) {
              fBit = (1 << (ack.feederId - 1));
            } else if (ack.feederId == 0) {
              fBit = trackedCommands[i].expectedMask;
            }

            trackedCommands[i].receivedAckMask |= fBit;
            trackedCommands[i].lastAckState = ack.ackState;
            trackedCommands[i].currentRelayMask = ack.currentRelayMask;
            trackedCommands[i].currentAuxMask = ack.currentAuxMask;
            trackedCommands[i].currentMcbMask = ack.currentMcbMask;

            // Classify response
            if (ack.ackState == FEEDER_ACK_COMPLETED ||
                ack.ackState == FEEDER_ACK_CONTACTOR_CLOSED ||
                ack.ackState == FEEDER_ACK_OUTPUT_CHANGED ||
                ack.ackState == FEEDER_ACK_RELAY_OUTPUT_ENERGIZED ||
                ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
                ack.ackState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED) {
              trackedCommands[i].outputConfirmedMask |= fBit;
              if (ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
                  ack.ackState == FEEDER_ACK_CONTACTOR_CLOSED ||
                  ack.ackState == FEEDER_ACK_COMPLETED) {
                trackedCommands[i].auxConfirmedMask |= fBit;
              }
            }

            if ((ack.currentMcbMask & fBit) != 0 ||
                ack.ackState == FEEDER_ACK_MCB_TRIPPED) {
              trackedCommands[i].mcbFaultMask |= fBit;
            }
            if (isRejectedFeederAck(ack.ackState)) {
              trackedCommands[i].rejectedMask |= fBit;
            }

            // Terminal state check for this feeder
            if (isTerminalFeederAck(ack.ackState)) {
              trackedCommands[i].terminalAckMask |= fBit;
            }

            // Mark completed ONLY when ALL expected feeders have a terminal ACK
            if ((trackedCommands[i].terminalAckMask &
                 trackedCommands[i].expectedMask) ==
                trackedCommands[i].expectedMask) {
              trackedCommands[i].completed = true;
              // ON needs physical contactor/aux proof; output drive alone is not success.
              if (trackedCommands[i].rejectedMask != 0 ||
                  (trackedCommands[i].outputConfirmedMask &
                   trackedCommands[i].expectedMask) !=
                      trackedCommands[i].expectedMask ||
                  (trackedCommands[i].action != 0 &&
                   (trackedCommands[i].auxConfirmedMask &
                    trackedCommands[i].expectedMask) !=
                       trackedCommands[i].expectedMask)) {
                trackedCommands[i].partialFailure = true;
              }
            }
            break;
          }
        }

        // Forward to network queue if active batch
        gwNetworkQueue.onFeederAck(ack);

        // Also update registry relay mask
        RegisteredPole *p = gwRegistry.findPoleById(ack.poleId);
        if (p) {
          p->feederStateMask = ack.currentRelayMask;
          p->auxFeedbackMask = ack.currentAuxMask;
          p->mcbStatusMask = ack.currentMcbMask;
          p->auxFeedbackValid = ack.auxFeedbackValid;
          p->mcbStatusValid = ack.mcbStatusValid;
        }
      }
      break;
    }

    default:
      break;
    }
    return true;
  }

  // Downlink packet meant for another node (multi-hop mesh forwarding)
  if ((uint8_t)(frame.header.hopCount + 1U) <= frame.header.maxHops &&
      (uint8_t)(frame.header.hopCount + 1U) <= GW_MESH_MAX_HOPS) {
    MeshWireFrame forwardFrame = frame;
    forwardFrame.header.hopCount++;

    // Look up route for destination
    RegisteredPole *destPole = gwRegistry.findPoleById(frame.header.dstAddress);
    if (destPole) {
      forwardFrame.header.nextHop =
          (destPole->primaryParent != 0 &&
           destPole->primaryParent != MSCB_GATEWAY_NODE_ID)
              ? destPole->primaryParent
              : frame.header.dstAddress;
    } else {
      return false;
    }
    forwardFrame.header.previousHopAddress = MSCB_GATEWAY_NODE_ID;
    forwardFrame.header.hopAuthToken =
        cmdSecurity.computeFrameHopAuthToken(forwardFrame);

    gwRadio.sendPacket(forwardFrame);
    packetsForwarded++;
    return true;
  }

  return false;
}

bool GwMeshCoordinator::sendDownlinkPacket(uint16_t targetPoleId,
                                           uint8_t packetType,
                                           const uint8_t *payload,
                                           uint16_t payloadLen,
                                           uint8_t priority) {
  if (payloadLen > MSCB_MAX_PAYLOAD_SIZE)
    return false;

  MeshWireFrame frame;
  memset(&frame, 0, sizeof(MeshWireFrame));

  frame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  frame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  frame.header.version = MSCB_PROTOCOL_VERSION;
  frame.header.type = packetType;
  frame.header.priority = priority;
  frame.header.hopCount = 0;
  frame.header.maxHops = GW_MESH_MAX_HOPS;
  frame.header.seqNumber = seqCounter++;
  frame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
  frame.header.dstAddress = targetPoleId;

  // Determine next hop
  RegisteredPole *targetPole = gwRegistry.findPoleById(targetPoleId);
  if (targetPole && targetPole->primaryParent != 0 &&
      targetPole->primaryParent != MSCB_GATEWAY_NODE_ID) {
    frame.header.nextHop = targetPole->primaryParent;
  } else {
    frame.header.nextHop = targetPoleId;
  }

  frame.header.previousHopAddress = MSCB_GATEWAY_NODE_ID;
  frame.header.payloadLen = payloadLen;
  if (payload && payloadLen > 0) {
    memcpy(frame.payload, payload, payloadLen);
  }
  frame.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(frame);

  return gwRadio.sendPacket(frame);
}

bool GwMeshCoordinator::sendFeederCommand(uint16_t poleId, uint8_t feederId,
                                          uint8_t action, uint8_t groupMask,
                                          uint32_t &outSeqNumber,
                                          int64_t adminChatId) {
  uint32_t currentUtc = gwSchedule.getEpoch();
  uint32_t expiryEpoch = (currentUtc > 0)
                             ? (currentUtc + 60)
                             : ((millis() / 1000) + 60); // 60 seconds validity

  AuthenticatedCommandEnvelope env;
  bool ok = cmdSecurity.createCommand(MSCB_GATEWAY_NODE_ID, poleId, feederId,
                                      PKT_CMD_FEEDER_SWITCH, action,
                                      expiryEpoch, env);
  if (!ok) {
    return false;
  }

  PktFeederCmdPayload cmd;
  cmd.targetPoleId = poleId;
  cmd.feederId = feederId;
  cmd.commandAction = action;
  cmd.groupMask = groupMask;
  cmd.sequenceNumber = env.sequenceNumber;
  cmd.expiryEpochSec = expiryEpoch;
  memcpy(cmd.hmacTag, env.hmacTag, sizeof(cmd.hmacTag));

  outSeqNumber = env.sequenceNumber;
  feederAckPending = true;
  pendingAckSeq = env.sequenceNumber;
  pendingAckPole = poleId;
  pendingAckFeeder = feederId;
  pendingAckStartMs = millis();
  lastAckState = FEEDER_ACK_SENT;

  uint8_t expMask = 0;
  if (feederId >= 1 && feederId <= 6) {
    expMask = (1 << (feederId - 1));
  } else if (feederId == 0x00) {
    expMask = 0x3F;
  } else {
    expMask = groupMask & 0x3F;
  }
  if (expMask == 0)
    expMask = 0x3F;

  // Enqueue in non-blocking tracked commands
  int8_t slotIdx = -1;
  for (uint8_t i = 0; i < GW_MAX_TRACKED_COMMANDS; i++) {
    if (!trackedCommands[i].inUse) {
      slotIdx = i;
      trackedCommands[i].inUse = true;
      trackedCommands[i].seqNumber = env.sequenceNumber;
      trackedCommands[i].poleId = poleId;
      trackedCommands[i].feederId = feederId;
      trackedCommands[i].action = action;
      trackedCommands[i].groupMask = groupMask;
      trackedCommands[i].expectedMask = expMask;
      trackedCommands[i].receivedAckMask = 0;
      trackedCommands[i].terminalAckMask = 0;
      trackedCommands[i].outputConfirmedMask = 0;
      trackedCommands[i].auxConfirmedMask = 0;
      trackedCommands[i].mcbFaultMask = 0;
      trackedCommands[i].rejectedMask = 0;
      trackedCommands[i].adminChatId = adminChatId;
      trackedCommands[i].sentMs = millis();
      trackedCommands[i].timeoutMs =
          (expMask == 0x3F || feederId == 0x00) ? 12000 : 5000;
      trackedCommands[i].lastAckState = FEEDER_ACK_SENT;
      trackedCommands[i].currentRelayMask = 0;
      trackedCommands[i].currentAuxMask = 0;
      trackedCommands[i].currentMcbMask = 0;
      trackedCommands[i].completed = false;
      trackedCommands[i].partialFailure = false;
      trackedCommands[i].timedOut = false;
      trackedCommands[i].notified = false;
      break;
    }
  }

  if (slotIdx < 0) {
    // Never transmit a command that cannot be tracked: that would make the
    // caller report a success that no ACK can ever confirm.
    Serial.println(F("[MESH] ⛔ Feeder command refused: ACK tracker full (16 bounded slots)."));
    feederAckPending = false;
    return false;
  }

  bool sendOk =
      sendDownlinkPacket(poleId, PKT_CMD_FEEDER_SWITCH, (const uint8_t *)&cmd,
                         sizeof(PktFeederCmdPayload), PRIO_COMMAND);
  if (!sendOk) {
    if (slotIdx >= 0) {
      trackedCommands[slotIdx].inUse = false;
    }
    feederAckPending = false;
    return false;
  }
  return true;
}

bool GwMeshCoordinator::sendRegisterResponseFrame(
    const PktRegisterResponsePayload &resp, uint16_t nextHop,
    uint16_t dstAddress) {
  MeshWireFrame respFrame;
  memset(&respFrame, 0, sizeof(MeshWireFrame));
  respFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  respFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  respFrame.header.version = MSCB_PROTOCOL_VERSION;
  respFrame.header.type = PKT_REGISTER_RESPONSE;
  respFrame.header.priority = PRIO_COMMAND;
  respFrame.header.hopCount = 0;
  respFrame.header.maxHops = GW_MESH_MAX_HOPS;
  respFrame.header.seqNumber = seqCounter++;
  respFrame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
  respFrame.header.dstAddress = dstAddress;
  respFrame.header.nextHop = nextHop ? nextHop : MSCB_BROADCAST_NODE_ID;
  respFrame.header.previousHopAddress = MSCB_GATEWAY_NODE_ID;
  respFrame.header.payloadLen = sizeof(PktRegisterResponsePayload);
  memcpy(respFrame.payload, &resp, sizeof(PktRegisterResponsePayload));
  respFrame.header.hopAuthToken =
      cmdSecurity.computeFrameHopAuthToken(respFrame);
  return gwRadio.sendPacket(respFrame);
}

bool GwMeshCoordinator::sendRegisterResponse(const uint8_t mac[6],
                                             uint16_t assignedPoleId,
                                             uint16_t assignedParent,
                                             uint8_t status) {
  if (!mac || !mscbIsValidMac(mac, false))
    return false;
  if (!cmdSecurity.isKeyProvisioned())
    return false;

  // Snapshot ALL registration/session metadata BEFORE building response frame
  uint32_t sessTok = 0;
  uint32_t reqNonce = 0;
  uint16_t firstHop = 0;
  uint16_t fallbackParent = MSCB_GATEWAY_NODE_ID;

  PendingRegistration *p = gwRegistry.getPendingByMac(mac);
  if (p) {
    sessTok = p->sessionToken;
    reqNonce = p->lastReqNonce;
    firstHop = p->firstHopAddress;
    if (p->requestedParent != 0 &&
        p->requestedParent < MSCB_EPHEMERAL_JOIN_MIN) {
      fallbackParent = p->requestedParent;
    }
  } else {
    RegisteredPole *regP = gwRegistry.findPoleByMac(mac);
    if (regP) {
      sessTok = regP->sessionToken;
      reqNonce = regP->lastReqNonce;
      firstHop = regP->firstHopAddress;
      if (regP->primaryParent != 0 &&
          regP->primaryParent < MSCB_EPHEMERAL_JOIN_MIN) {
        fallbackParent = regP->primaryParent;
      }
    }
  }

  if (sessTok == 0 || reqNonce == 0) {
    Serial.println(F("[SECURITY] ❌ Registration response refused: no active "
                     "session/nonce"));
    return false;
  }

  PktRegisterResponsePayload resp;
  memset(&resp, 0, sizeof(resp));
  memcpy(resp.macAddress, mac, 6);
  resp.status = status;
  resp.assignedPoleId = assignedPoleId;
  resp.assignedParent =
      (assignedParent != 0 && assignedParent < MSCB_EPHEMERAL_JOIN_MIN &&
       assignedParent != assignedPoleId)
          ? assignedParent
          : fallbackParent;
  resp.heartbeatIntervalSec = 30;
  resp.networkKeyEpoch =
      cmdSecurity.getActiveKeyEpoch() ? cmdSecurity.getActiveKeyEpoch() : 1;
  resp.sessionToken = sessTok;
  resp.nonce = reqNonce;

  if (!cmdSecurity.computeRegisterResponseAuth(resp, resp.authTag)) {
    return false;
  }

  uint16_t resolvedNextHop = MSCB_BROADCAST_NODE_ID;
  if (firstHop != 0 && firstHop != MSCB_GATEWAY_NODE_ID) {
    resolvedNextHop = firstHop;
  } else if (resp.assignedParent != 0 &&
             resp.assignedParent != MSCB_GATEWAY_NODE_ID &&
             resp.assignedParent != assignedPoleId) {
    resolvedNextHop = resp.assignedParent;
  } else if (assignedPoleId != 0) {
    resolvedNextHop = assignedPoleId;
  }

  bool txOk = sendRegisterResponseFrame(
      resp, resolvedNextHop,
      assignedPoleId ? assignedPoleId : MSCB_BROADCAST_NODE_ID);
  gwRegistry.recordRegResponseAttempt(mac, txOk);
  return txOk;
}

bool GwMeshCoordinator::pollCompletedCommand(GwTrackedCommand &outCmd) {
  for (uint8_t i = 0; i < GW_MAX_TRACKED_COMMANDS; i++) {
    if (trackedCommands[i].inUse && trackedCommands[i].completed &&
        !trackedCommands[i].notified) {
      outCmd = trackedCommands[i];
      trackedCommands[i].notified = true;
      trackedCommands[i].inUse = false; // Mark free
      return true;
    }
  }
  return false;
}

void GwMeshCoordinator::resetFeederAckTracking() {
  feederAckPending = false;
  pendingAckSeq = 0;
  lastAckState = 0;
}

bool GwMeshCoordinator::sendRevokeCommand(uint16_t poleId, const uint8_t mac[6],
                                          uint32_t transactionId,
                                          const char *reason) {
  PktRevokeCmdPayload cmd;
  char txIdBuf[16];
  snprintf(txIdBuf, sizeof(txIdBuf), "TX_%08X", transactionId);

  uint32_t currentUtc = gwSchedule.getEpoch();
  if (currentUtc == 0)
    currentUtc = (uint32_t)(millis() / 1000);
  uint32_t expiryUtc = currentUtc + 60; // 60s validity

  bool created = cmdSecurity.createRevokeCommand(poleId, mac, txIdBuf, reason,
                                                 currentUtc, expiryUtc, cmd);
  if (!created) {
    Serial.println(
        F("[MESH] ❌ Failed to create authenticated Revoke Command envelope!"));
    return false;
  }

  return sendDownlinkPacket(poleId, PKT_CMD_REVOKE_POLE, (const uint8_t *)&cmd,
                            sizeof(PktRevokeCmdPayload), PRIO_CRITICAL);
}
