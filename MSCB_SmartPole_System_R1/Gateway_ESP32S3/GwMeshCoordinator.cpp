/**
 * @file GwMeshCoordinator.cpp
 * @brief MSCB Smart Pole Gateway - Root Mesh Coordinator & Multi-Hop Router Implementation
 * @version 1.0.0
 */

#include "GwMeshCoordinator.h"
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
      }
    }
  }

  // Timeout check for legacy single-command pending Feeder ACK
  if (feederAckPending && (now - pendingAckStartMs > 5000)) {
    feederAckPending = false;
  }
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

          // Update registry node state
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
          ack.gatewayTimeEpoch = millis() / 1000;
          ack.configVersion = 1;
          ack.ackStatus = 0;

          ackFrame.header.payloadLen = sizeof(PktHeartbeatAckPayload);
          memcpy(ackFrame.payload, &ack, sizeof(PktHeartbeatAckPayload));
          gwRadio.sendPacket(ackFrame);
        }
        break;
      }

      case PKT_CMD_FEEDER_ACK: {
        if (frame.header.payloadLen >= sizeof(PktFeederAckPayload)) {
          PktFeederAckPayload ack;
          memcpy(&ack, frame.payload, sizeof(PktFeederAckPayload));

          if (feederAckPending && ack.sequenceNumber == pendingAckSeq) {
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
            if (trackedCommands[i].inUse && trackedCommands[i].seqNumber == ack.sequenceNumber) {
              trackedCommands[i].lastAckState = ack.ackState;
              trackedCommands[i].currentRelayMask = ack.currentRelayMask;
              trackedCommands[i].currentAuxMask = ack.currentAuxMask;
              trackedCommands[i].currentMcbMask = ack.currentMcbMask;

              if (ack.ackState == FEEDER_ACK_COMPLETED ||
                  ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
                  ack.ackState >= FEEDER_ACK_REJECTED_STALE ||
                  ack.ackState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED) {
                trackedCommands[i].completed = true;
              }
              break;
            }
          }

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
  AuthenticatedCommandEnvelope env;
  uint32_t expiryEpoch = (millis() / 1000) + 60; // 60 seconds validity
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

  // Enqueue in non-blocking tracked commands
  for (uint8_t i = 0; i < GW_MAX_TRACKED_COMMANDS; i++) {
    if (!trackedCommands[i].inUse) {
      trackedCommands[i].inUse = true;
      trackedCommands[i].seqNumber = env.sequenceNumber;
      trackedCommands[i].poleId = poleId;
      trackedCommands[i].feederId = feederId;
      trackedCommands[i].action = action;
      trackedCommands[i].groupMask = groupMask;
      trackedCommands[i].adminChatId = adminChatId;
      trackedCommands[i].sentMs = millis();
      trackedCommands[i].timeoutMs = 4500;
      trackedCommands[i].lastAckState = FEEDER_ACK_SENT;
      trackedCommands[i].currentRelayMask = 0;
      trackedCommands[i].currentAuxMask = 0;
      trackedCommands[i].currentMcbMask = 0;
      trackedCommands[i].completed = false;
      trackedCommands[i].notified = false;
      break;
    }
  }

  return sendDownlinkPacket(poleId, PKT_CMD_FEEDER_SWITCH, (const uint8_t*)&cmd, sizeof(PktFeederCmdPayload), PRIO_COMMAND);
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
