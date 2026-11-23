/**
 * @file GwMeshCoordinator.cpp
 * @brief MSCB Smart Pole Gateway - Root Mesh Coordinator & Multi-Hop Router Implementation
 * @version 1.0.0
 */

#include "GwMeshCoordinator.h"
#include "GwScheduleManager.h"
#include "GwNetworkQueue.h"
#include <string.h>

GwMeshCoordinator gwMesh;

GwMeshCoordinator::GwMeshCoordinator() :
  seqCounter(1), lastBeaconMs(0),
  packetsForwarded(0), duplicatesSuppressed(0), hopExceededCount(0),
  dupCacheIdx(0),
  feederAckPending(false), pendingAckSeq(0), pendingAckPole(0),
  pendingAckFeeder(0), pendingAckStartMs(0), lastAckState(0), lastAckPole(0)
{
  memset(dupCache, 0, sizeof(dupCache));
  memset(trackedCommands, 0, sizeof(trackedCommands));
}

bool GwMeshCoordinator::begin() {
  lastBeaconMs = millis();
  static PersistentNonceRecord gwNvsBank0;
  static PersistentNonceRecord gwNvsBank1;
  if (!replayProtector.isTransmitterStorageReady()) {
    if (!replayProtector.initTransmitterStorage(&gwNvsBank0, &gwNvsBank1)) {
      replayProtector.reprovisionStorage(cmdSecurity.getActiveKeyEpoch() ? cmdSecurity.getActiveKeyEpoch() : 1, &gwNvsBank0, &gwNvsBank1);
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

  PktBeaconAnnouncePayload beacon;
  beacon.senderId = MSCB_GATEWAY_NODE_ID;
  beacon.rank = 0; // Root is Rank 0
  beacon.primaryParent = 0;
  beacon.capacitySlotsFree = GW_MAX_MANAGED_POLES - gwRegistry.getActiveCount();
  beacon.beaconTimestamp = millis() / 1000;

  frame.header.payloadLen = sizeof(PktBeaconAnnouncePayload);
  memcpy(frame.payload, &beacon, sizeof(PktBeaconAnnouncePayload));

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
        if ((trackedCommands[i].terminalAckMask & trackedCommands[i].expectedMask) != trackedCommands[i].expectedMask) {
          trackedCommands[i].partialFailure = true;
        }
      }
    }
  }

  // Timeout check for legacy single-command pending Feeder ACK
  if (feederAckPending && (now - pendingAckStartMs > 5000)) {
    feederAckPending = false;
  }

  // 4. Service Network-Wide Command Queue
  gwNetworkQueue.service();
}

bool GwMeshCoordinator::processIncomingWireFrame(const MeshWireFrame &frame, int16_t rssi, int8_t snr) {
  // Check Hop Count limit
  if (frame.header.hopCount > frame.header.maxHops) {
    hopExceededCount++;
    return false;
  }

  // Check Duplicate Suppression
  if (isDuplicate(frame.header.srcAddress, frame.header.seqNumber)) {
    duplicatesSuppressed++;
    return false;
  }
  addDuplicate(frame.header.srcAddress, frame.header.seqNumber);

  // If packet is addressed to Gateway (0x0001) or Broadcast
  if (frame.header.dstAddress == MSCB_GATEWAY_NODE_ID || frame.header.dstAddress == MSCB_BROADCAST_NODE_ID) {
    switch (frame.header.type) {
      case PKT_REGISTER_REQUEST: {
        if (frame.header.payloadLen >= sizeof(PktRegisterRequestPayload)) {
          PktRegisterRequestPayload req;
          memcpy(&req, frame.payload, sizeof(PktRegisterRequestPayload));

          PktRegisterResponsePayload resp;
          uint8_t status = gwRegistry.handleJoinRequest(req, rssi, snr, resp);

          // If approved or rejected, send response immediately
          if (status != REG_RESP_PENDING_APPROVAL) {
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
            respFrame.header.dstAddress = resp.assignedPoleId ? resp.assignedPoleId : MSCB_BROADCAST_NODE_ID;
            respFrame.header.nextHop = frame.header.srcAddress; // send back via same path
            respFrame.header.payloadLen = sizeof(PktRegisterResponsePayload);
            memcpy(respFrame.payload, &resp, sizeof(PktRegisterResponsePayload));

            gwRadio.sendPacket(respFrame);
          }
        }
        break;
      }

      case PKT_HEARTBEAT_PING: {
        if (frame.header.payloadLen >= sizeof(PktHeartbeatPingPayload)) {
          PktHeartbeatPingPayload hb;
          memcpy(&hb, frame.payload, sizeof(PktHeartbeatPingPayload));

          // Check if pole is actively registered and not revoked
          RegisteredPole* p = gwRegistry.findPoleById(hb.poleId);
          bool isRevoked = (p != nullptr && gwRegistry.isMacRevoked(p->mac));
          if (p == nullptr || isRevoked) {
            // Check if pole was revoked
            const PermanentRevocationRecord* rev = gwRegistry.findRevokedByPoleId(hb.poleId);
            if (rev != nullptr || isRevoked) {
              // Trigger security alert for revoked pole trying to send heartbeat
              uint8_t revMac[6] = {0};
              if (p != nullptr) {
                memcpy(revMac, p->mac, 6);
              } else if (rev != nullptr && strlen(rev->hwId) >= 12) {
                for (int i = 0; i < 6; i++) {
                  char byteStr[3] = { rev->hwId[i*2], rev->hwId[i*2 + 1], '\0' };
                  revMac[i] = (uint8_t)strtoul(byteStr, NULL, 16);
                }
              }
              gwRegistry.setReappearedRevokedAlert(revMac, rev ? rev->logicalId : (p ? p->stringId : "REVOKED"), hb.poleId);
              Serial.printf("[SECURITY] 🚨 Heartbeat from REVOKED pole 0x%04X rejected!\n", hb.poleId);
            } else {
              Serial.printf("[MESH] Heartbeat from unregistered pole 0x%04X rejected.\n", hb.poleId);
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
            ackFrame.header.nextHop = frame.header.srcAddress;

            PktHeartbeatAckPayload ack;
            ack.poleId = hb.poleId;
            ack.gatewayTimeEpoch = gwSchedule.getEpoch();
            ack.configVersion = 0;
            ack.ackStatus = (rev != nullptr || isRevoked) ? HB_ACK_REVOKED_LOCKED : HB_ACK_RE_REGISTER_REQUIRED;

            ackFrame.header.payloadLen = sizeof(PktHeartbeatAckPayload);
            memcpy(ackFrame.payload, &ack, sizeof(PktHeartbeatAckPayload));
            gwRadio.sendPacket(ackFrame);
            break;
          }

          // Active registered pole: Update node telemetry
          gwRegistry.updateNodeTelemetry(hb.poleId, hb.primaryParent, hb.backupParent,
                                         hb.rank, hb.feederStateMask, hb.auxFeedbackMask, hb.mcbStatusMask,
                                         hb.busVoltageV, hb.totalCurrentA, rssi, snr);

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
          ackFrame.header.nextHop = frame.header.srcAddress;

          PktHeartbeatAckPayload ack;
          ack.poleId = hb.poleId;
          ack.gatewayTimeEpoch = gwSchedule.getEpoch();
          ack.configVersion = 1;
          ack.ackStatus = HB_ACK_OK;

          ackFrame.header.payloadLen = sizeof(PktHeartbeatAckPayload);
          memcpy(ackFrame.payload, &ack, sizeof(PktHeartbeatAckPayload));
          gwRadio.sendPacket(ackFrame);
        }
        break;
      }

      case PKT_ALERT_REVOKED_REAPPEAR: {
        if (frame.header.payloadLen >= sizeof(PktRevokedReappearPayload)) {
          PktRevokedReappearPayload alert;
          memcpy(&alert, frame.payload, sizeof(PktRevokedReappearPayload));

          const PermanentRevocationRecord* rev = gwRegistry.findRevokedByMac(alert.macAddress);
          const char* prevId = rev ? rev->logicalId : "UNKNOWN";
          gwRegistry.setReappearedRevokedAlert(alert.macAddress, prevId, alert.previousPoleId);

          Serial.printf("[SECURITY] 🚨 PKT_ALERT_REVOKED_REAPPEAR from MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                        alert.macAddress[0], alert.macAddress[1], alert.macAddress[2],
                        alert.macAddress[3], alert.macAddress[4], alert.macAddress[5]);

          // Reply with PKT_REGISTER_RESPONSE telling it is REJECTED_REVOKED
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
          respFrame.header.dstAddress = MSCB_BROADCAST_NODE_ID;
          respFrame.header.nextHop = frame.header.srcAddress;

          PktRegisterResponsePayload resp;
          memset(&resp, 0, sizeof(resp));
          memcpy(resp.macAddress, alert.macAddress, 6);
          resp.status = REG_RESP_REJECTED_REVOKED;
          resp.assignedPoleId = 0;
          resp.assignedParent = 0;
          resp.heartbeatIntervalSec = 0;
          resp.networkKeyEpoch = 0;

          respFrame.header.payloadLen = sizeof(PktRegisterResponsePayload);
          memcpy(respFrame.payload, &resp, sizeof(PktRegisterResponsePayload));
          gwRadio.sendPacket(respFrame);
        }
        break;
      }

      case PKT_CMD_REVOKE_ACK: {
        if (frame.header.payloadLen >= sizeof(PktRevokeAckPayload)) {
          PktRevokeAckPayload ack;
          memcpy(&ack, frame.payload, sizeof(PktRevokeAckPayload));
          Serial.printf("[MESH] Pole 0x%04X confirmed revocation ACK (status=%u)\n",
                        ack.poleId, ack.status);
        }
        break;
      }

      case PKT_CMD_FEEDER_ACK: {
        if (frame.header.payloadLen >= sizeof(PktFeederAckPayload)) {
          PktFeederAckPayload ack;
          memcpy(&ack, frame.payload, sizeof(PktFeederAckPayload));

          if (feederAckPending && ack.sequenceNumber == pendingAckSeq && ack.poleId == pendingAckPole) {
            lastAckState = ack.ackState;
            lastAckPole = ack.poleId;
            if (ack.ackState == FEEDER_ACK_COMPLETED ||
                ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
                ack.ackState >= FEEDER_ACK_REJECTED_STALE ||
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
                  ack.ackState == FEEDER_ACK_OUTPUT_CHANGED ||
                  ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
                  ack.ackState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED) {
                trackedCommands[i].outputConfirmedMask |= fBit;
                if (ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED) {
                  trackedCommands[i].auxConfirmedMask |= fBit;
                }
              }

              if ((ack.currentMcbMask & fBit) != 0) {
                trackedCommands[i].mcbFaultMask |= fBit;
              }
              if (ack.ackState >= FEEDER_ACK_REJECTED_STALE) {
                trackedCommands[i].rejectedMask |= fBit;
              }

              // Terminal state check for this feeder
              if (ack.ackState == FEEDER_ACK_COMPLETED ||
                  ack.ackState == FEEDER_ACK_OUTPUT_CHANGED ||
                  ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
                  ack.ackState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED ||
                  ack.ackState >= FEEDER_ACK_REJECTED_STALE) {
                trackedCommands[i].terminalAckMask |= fBit;
              }

              // Mark completed ONLY when ALL expected feeders have a terminal ACK
              if ((trackedCommands[i].terminalAckMask & trackedCommands[i].expectedMask) == trackedCommands[i].expectedMask) {
                trackedCommands[i].completed = true;
                if (trackedCommands[i].rejectedMask != 0 ||
                    (trackedCommands[i].outputConfirmedMask & trackedCommands[i].expectedMask) != trackedCommands[i].expectedMask) {
                  trackedCommands[i].partialFailure = true;
                }
              }
              break;
            }
          }

          // Forward to network queue if active batch
          gwNetworkQueue.onFeederAck(ack);

          // Also update registry relay mask
          RegisteredPole* p = gwRegistry.findPoleById(ack.poleId);
          if (p) {
            p->feederStateMask = ack.currentRelayMask;
            p->auxFeedbackMask = ack.currentAuxMask;
            p->mcbStatusMask = ack.currentMcbMask;
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
  if (frame.header.hopCount + 1 <= frame.header.maxHops) {
    MeshWireFrame forwardFrame = frame;
    forwardFrame.header.hopCount++;

    // Look up route for destination
    RegisteredPole* destPole = gwRegistry.findPoleById(frame.header.dstAddress);
    if (destPole) {
      forwardFrame.header.nextHop = destPole->primaryParent ? destPole->primaryParent : frame.header.dstAddress;
    }

    gwRadio.sendPacket(forwardFrame);
    packetsForwarded++;
    return true;
  }

  return false;
}

bool GwMeshCoordinator::sendDownlinkPacket(uint16_t targetPoleId, uint8_t packetType, const uint8_t *payload, uint16_t payloadLen, uint8_t priority) {
  if (payloadLen > MSCB_MAX_PAYLOAD_SIZE) return false;

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
  RegisteredPole* targetPole = gwRegistry.findPoleById(targetPoleId);
  if (targetPole && targetPole->primaryParent != 0 && targetPole->primaryParent != MSCB_GATEWAY_NODE_ID) {
    frame.header.nextHop = targetPole->primaryParent;
  } else {
    frame.header.nextHop = targetPoleId;
  }

  frame.header.payloadLen = payloadLen;
  if (payload && payloadLen > 0) {
    memcpy(frame.payload, payload, payloadLen);
  }

  return gwRadio.sendPacket(frame);
}

bool GwMeshCoordinator::sendFeederCommand(uint16_t poleId, uint8_t feederId, uint8_t action, uint8_t groupMask, uint32_t &outSeqNumber, int64_t adminChatId) {
  uint32_t currentUtc = gwSchedule.getEpoch();
  uint32_t expiryEpoch = (currentUtc > 0) ? (currentUtc + 60) : ((millis() / 1000) + 60); // 60 seconds validity

  AuthenticatedCommandEnvelope env;
  bool ok = cmdSecurity.createCommand(MSCB_GATEWAY_NODE_ID, poleId, feederId,
                                      PKT_CMD_FEEDER_SWITCH, action, expiryEpoch, env);
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
  if (expMask == 0) expMask = 0x3F;

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
      trackedCommands[i].timeoutMs = (expMask == 0x3F || feederId == 0x00) ? 12000 : 5000;
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

  bool sendOk = sendDownlinkPacket(poleId, PKT_CMD_FEEDER_SWITCH, (const uint8_t*)&cmd, sizeof(PktFeederCmdPayload), PRIO_COMMAND);
  if (!sendOk) {
    if (slotIdx >= 0) {
      trackedCommands[slotIdx].inUse = false;
    }
    feederAckPending = false;
    return false;
  }
  return true;
}

bool GwMeshCoordinator::sendRegisterResponse(const uint8_t mac[6], uint16_t assignedPoleId, uint16_t assignedParent, uint8_t status) {
  if (!mac) return false;
  PktRegisterResponsePayload resp;
  memcpy(resp.macAddress, mac, 6);
  resp.status = status;
  resp.assignedPoleId = assignedPoleId;
  resp.assignedParent = assignedParent ? assignedParent : MSCB_GATEWAY_NODE_ID;
  resp.heartbeatIntervalSec = 30;
  resp.networkKeyEpoch = 1;

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
  respFrame.header.dstAddress = assignedPoleId ? assignedPoleId : MSCB_BROADCAST_NODE_ID;
  respFrame.header.nextHop = MSCB_BROADCAST_NODE_ID;
  respFrame.header.payloadLen = sizeof(PktRegisterResponsePayload);
  memcpy(respFrame.payload, &resp, sizeof(PktRegisterResponsePayload));

  return gwRadio.sendPacket(respFrame);
}

bool GwMeshCoordinator::pollCompletedCommand(GwTrackedCommand &outCmd) {
  for (uint8_t i = 0; i < GW_MAX_TRACKED_COMMANDS; i++) {
    if (trackedCommands[i].inUse && trackedCommands[i].completed && !trackedCommands[i].notified) {
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

bool GwMeshCoordinator::sendRevokeCommand(uint16_t poleId, const uint8_t mac[6], uint32_t transactionId, const char* reason) {
  PktRevokeCmdPayload cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.targetPoleId = poleId;
  if (mac) memcpy(cmd.macAddress, mac, 6);
  snprintf(cmd.txId, sizeof(cmd.txId), "TX_%08X", transactionId);
  if (reason) strncpy(cmd.reason, reason, sizeof(cmd.reason) - 1);
  cmd.timestamp = millis() / 1000;
  memset(cmd.authTag, 0, sizeof(cmd.authTag));

  return sendDownlinkPacket(poleId, PKT_CMD_REVOKE_POLE, (const uint8_t*)&cmd, sizeof(PktRevokeCmdPayload), PRIO_CRITICAL);
}
