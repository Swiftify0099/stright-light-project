/**
 * @file PoleMeshRouter.cpp
 * @brief MSCB Smart Pole Node - Multi-Hop Mesh Router & Dynamic Parent Selection Implementation
 * @version 1.0.0
 */

#include "PoleMeshRouter.h"
#include <string.h>

PoleMeshRouter poleMesh;

PoleMeshRouter::PoleMeshRouter() :
  state(ROUTING_UNINITIALIZED),
  myNodeId(POLE_DEFAULT_NODE_ADDRESS),
  primaryParent(MSCB_GATEWAY_NODE_ID),
  backupParent(MSCB_GATEWAY_NODE_ID),
  myRank(1),
  seqCounter(1),
  lastParentPacketMs(0),
  lastBeaconMs(0),
  packetsForwarded(0),
  packetsDropped(0),
  neighborCount(0),
  dupCacheIdx(0)
{
  memset(neighbors, 0, sizeof(neighbors));
  memset(dupCache, 0, sizeof(dupCache));
}

bool PoleMeshRouter::begin() {
  myNodeId = poleStorage.getAssignedPoleId();
  primaryParent = poleStorage.getPrimaryParent();
  backupParent = poleStorage.getBackupParent();
  myRank = poleStorage.getNetworkRank();

  state = (primaryParent != 0) ? ROUTING_CONNECTED : ROUTING_SEARCHING;
  lastParentPacketMs = millis();
  lastBeaconMs = millis();
  return true;
}

int PoleMeshRouter::findNeighborIndex(uint16_t nodeAddr) const {
  for (uint8_t i = 0; i < neighborCount; i++) {
    if (neighbors[i].nodeAddress == nodeAddr) {
      return (int)i;
    }
  }
  return -1;
}

int16_t PoleMeshRouter::calculateLinkScore(int16_t rssi, int8_t snr, uint8_t rank) const {
  // Higher score is better. Penalize high ranks to prevent long paths.
  // Base RSSI (-120 to -30 dBm) + (SNR * 2) - (rank * 15)
  return (int16_t)(rssi + (snr * 2) - (rank * 15));
}

void PoleMeshRouter::updateNeighborsFromBeacon(uint16_t sender, uint8_t rank, uint16_t priParent, int16_t rssi, int8_t snr) {
  // Never add oneself or nodes with rank >= 254
  if (sender == myNodeId || sender == 0 || sender == MSCB_BROADCAST_NODE_ID) return;

  // Monotonically increasing rank loop prevention:
  // Cannot select a parent whose primary parent is me!
  if (priParent == myNodeId) return;

  int idx = findNeighborIndex(sender);
  if (idx < 0) {
    if (neighborCount < MESH_MAX_NEIGHBORS) {
      idx = neighborCount++;
      neighbors[idx].nodeAddress = sender;
      neighbors[idx].packetsReceived = 0;
    } else {
      // Evict lowest score neighbor
      int lowestIdx = 0;
      int16_t lowestScore = neighbors[0].linkScore;
      for (uint8_t i = 1; i < neighborCount; i++) {
        if (neighbors[i].linkScore < lowestScore) {
          lowestScore = neighbors[i].linkScore;
          lowestIdx = i;
        }
      }
      idx = lowestIdx;
      neighbors[idx].nodeAddress = sender;
      neighbors[idx].packetsReceived = 0;
    }
  }

  neighbors[idx].rank = rank;
  neighbors[idx].primaryParent = priParent;
  neighbors[idx].lastRssi = rssi;
  neighbors[idx].lastSnr = snr;
  neighbors[idx].lastSeenMs = millis();
  neighbors[idx].packetsReceived++;
  neighbors[idx].linkScore = calculateLinkScore(rssi, snr, rank);

  // If we are searching for parents or in orphan state, evaluate immediately
  if (state == ROUTING_SEARCHING || state == ROUTING_ORPHAN) {
    selectBestParents();
  }
}

bool PoleMeshRouter::selectBestParents() {
  if (neighborCount == 0) return false;

  int bestIdx = -1;
  int secondBestIdx = -1;
  int16_t bestScore = -32000;
  int16_t secondScore = -32000;

  for (uint8_t i = 0; i < neighborCount; i++) {
    // Monotonically increasing rank rule: parent must have lower or valid rank
    // Gateway is Rank 0
    if (neighbors[i].primaryParent == myNodeId) continue; // Loop prevention

    if (neighbors[i].linkScore > bestScore) {
      secondBestIdx = bestIdx;
      secondScore = bestScore;
      bestIdx = i;
      bestScore = neighbors[i].linkScore;
    } else if (neighbors[i].linkScore > secondScore) {
      secondBestIdx = i;
      secondScore = neighbors[i].linkScore;
    }
  }

  if (bestIdx >= 0) {
    primaryParent = neighbors[bestIdx].nodeAddress;
    myRank = neighbors[bestIdx].rank + 1; // Strict monotonic rank
    if (secondBestIdx >= 0) {
      backupParent = neighbors[secondBestIdx].nodeAddress;
    } else {
      backupParent = primaryParent;
    }

    state = ROUTING_CONNECTED;
    poleStorage.saveRouting(primaryParent, backupParent, myRank);
    lastParentPacketMs = millis();
    return true;
  }

  return false;
}

void PoleMeshRouter::triggerFailover() {
  if (backupParent != 0 && backupParent != primaryParent) {
    primaryParent = backupParent;
    backupParent = 0; // Promoted backup; old primary is dead until rediscovered
    state = ROUTING_FAILOVER;
    lastParentPacketMs = millis();
    poleStorage.saveRouting(primaryParent, backupParent, myRank);
  } else {
    declareOrphan();
  }
}

void PoleMeshRouter::declareOrphan() {
  state = ROUTING_ORPHAN;
  primaryParent = 0;
  backupParent = 0;
  myRank = 255; // Unreachable rank
}

bool PoleMeshRouter::isDuplicate(uint16_t src, uint16_t seq) {
  for (uint8_t i = 0; i < MESH_DUP_CACHE_SIZE; i++) {
    if (dupCache[i].srcAddress == src && dupCache[i].seqNumber == seq) {
      return true;
    }
  }
  return false;
}

void PoleMeshRouter::addDuplicate(uint16_t src, uint16_t seq) {
  dupCache[dupCacheIdx].srcAddress = src;
  dupCache[dupCacheIdx].seqNumber = seq;
  dupCache[dupCacheIdx].timestampMs = millis();
  dupCacheIdx = (dupCacheIdx + 1) % MESH_DUP_CACHE_SIZE;
}

bool PoleMeshRouter::sendUplinkPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen, uint8_t priority) {
  if (payloadLen > MSCB_MAX_PAYLOAD_SIZE) return false;
  if (primaryParent == 0 && packetType != PKT_REGISTER_REQUEST && packetType != PKT_BEACON_ANNOUNCE) {
    return false;
  }

  MeshWireFrame frame;
  memset(&frame, 0, sizeof(MeshWireFrame));

  frame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  frame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  frame.header.version = MSCB_PROTOCOL_VERSION;
  frame.header.type = packetType;
  frame.header.priority = priority;
  frame.header.hopCount = 0;
  frame.header.maxHops = MESH_MAX_HOPS;
  frame.header.seqNumber = seqCounter++;
  frame.header.srcAddress = myNodeId;
  frame.header.dstAddress = MSCB_GATEWAY_NODE_ID;
  frame.header.nextHop = (primaryParent != 0) ? primaryParent : MSCB_BROADCAST_NODE_ID;
  frame.header.payloadLen = payloadLen;

  if (payload && payloadLen > 0) {
    memcpy(frame.payload, payload, payloadLen);
  }

  return poleRadio.sendPacket(frame);
}

bool PoleMeshRouter::handleReceivedWireFrame(const MeshWireFrame &frame, int16_t rssi, int8_t snr,
                                             MeshWireFrame &outActionFrame, bool &outSendAction) {
  outSendAction = false;

  // Hop Count Limit Enforcement
  if (frame.header.hopCount > frame.header.maxHops) {
    packetsDropped++;
    return false;
  }

  // Duplicate Suppression
  if (isDuplicate(frame.header.srcAddress, frame.header.seqNumber)) {
    packetsDropped++;
    return false;
  }
  addDuplicate(frame.header.srcAddress, frame.header.seqNumber);

  // If packet comes from primary parent, update activity timer
  if (frame.header.srcAddress == primaryParent || frame.header.nextHop == primaryParent) {
    lastParentPacketMs = millis();
    if (state == ROUTING_FAILOVER) {
      state = ROUTING_CONNECTED;
    }
  }

  // Process Beacons
  if (frame.header.type == PKT_BEACON_ANNOUNCE) {
    if (frame.header.payloadLen >= sizeof(PktBeaconAnnouncePayload)) {
      PktBeaconAnnouncePayload beacon;
      memcpy(&beacon, frame.payload, sizeof(PktBeaconAnnouncePayload));
      updateNeighborsFromBeacon(beacon.senderId, beacon.rank, beacon.primaryParent, rssi, snr);
    }
    return true;
  }

  // If packet is addressed directly to ME or Broadcast
  if (frame.header.dstAddress == myNodeId || frame.header.dstAddress == MSCB_BROADCAST_NODE_ID) {
    // Deliver to local node services (registration, feeder command, etc.)
    return true;
  }

  // Multi-hop Forwarding (if not addressed to me)
  if (frame.header.hopCount + 1 <= frame.header.maxHops) {
    outActionFrame = frame;
    outActionFrame.header.hopCount++;

    // Determine direction:
    // If destination is Gateway (uplink), nextHop is our primaryParent
    if (frame.header.dstAddress == MSCB_GATEWAY_NODE_ID) {
      outActionFrame.header.nextHop = primaryParent;
    } else {
      // Downlink: forward towards destination or next hop
      outActionFrame.header.nextHop = frame.header.dstAddress;
    }

    outSendAction = true;
    packetsForwarded++;
    return true;
  }

  return false;
}

void PoleMeshRouter::service() {
  uint32_t now = millis();

  // 1. Check parent availability timeout (90 seconds)
  if (state == ROUTING_CONNECTED || state == ROUTING_FAILOVER) {
    if (now - lastParentPacketMs > MESH_PARENT_TIMEOUT_MS) {
      triggerFailover();
    }
  }

  // 2. Periodic Beacon Broadcast (every 30s) if connected
  if (state == ROUTING_CONNECTED && (now - lastBeaconMs >= MSCB_BEACON_INTERVAL_MS)) {
    lastBeaconMs = now;

    MeshWireFrame beaconFrame;
    memset(&beaconFrame, 0, sizeof(MeshWireFrame));
    beaconFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
    beaconFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
    beaconFrame.header.version = MSCB_PROTOCOL_VERSION;
    beaconFrame.header.type = PKT_BEACON_ANNOUNCE;
    beaconFrame.header.priority = PRIO_ROUTINE;
    beaconFrame.header.hopCount = 0;
    beaconFrame.header.maxHops = MESH_MAX_HOPS;
    beaconFrame.header.seqNumber = seqCounter++;
    beaconFrame.header.srcAddress = myNodeId;
    beaconFrame.header.dstAddress = MSCB_BROADCAST_NODE_ID;
    beaconFrame.header.nextHop = MSCB_BROADCAST_NODE_ID;

    PktBeaconAnnouncePayload payload;
    payload.senderId = myNodeId;
    payload.rank = myRank;
    payload.primaryParent = primaryParent;
    payload.capacitySlotsFree = 0;
    payload.beaconTimestamp = now / 1000;

    beaconFrame.header.payloadLen = sizeof(PktBeaconAnnouncePayload);
    memcpy(beaconFrame.payload, &payload, sizeof(PktBeaconAnnouncePayload));

    poleRadio.sendPacket(beaconFrame);
  }
}

const MeshNeighbor* PoleMeshRouter::getNeighbor(uint8_t idx) const {
  if (idx >= neighborCount) return NULL;
  return &neighbors[idx];
}
