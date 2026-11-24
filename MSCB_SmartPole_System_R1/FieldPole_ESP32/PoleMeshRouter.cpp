/**
 * @file PoleMeshRouter.cpp
 * @brief MSCB Smart Pole Node - Multi-Hop Mesh Router & Dynamic Parent
 * Selection Implementation
 * @version 1.0.0
 */

#include "PoleMeshRouter.h"
#include <string.h>

PoleMeshRouter poleMesh;

PoleMeshRouter::PoleMeshRouter()
    : state(ROUTING_UNINITIALIZED), myNodeId(POLE_DEFAULT_NODE_ADDRESS),
      primaryParent(MSCB_GATEWAY_NODE_ID), backupParent(MSCB_GATEWAY_NODE_ID),
      myRank(1), seqCounter(1), lastParentPacketMs(0), lastBeaconMs(0),
      packetsForwarded(0), packetsDropped(0),
      packetsDroppedSelfLoop(0), packetsDroppedHopLimit(0),
      packetsDroppedMalformed(0), packetsDroppedDuplicate(0),
      neighborCount(0), childCount(0), dupCacheIdx(0) {
  memset(neighbors, 0, sizeof(neighbors));
  memset(trackedChildren, 0, sizeof(trackedChildren));
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

int16_t PoleMeshRouter::calculateLinkScore(int16_t rssi, int8_t snr,
                                           uint8_t rank) const {
  // Higher score is better. Penalize high ranks to prevent long paths.
  // Base RSSI (-120 to -30 dBm) + (SNR * 2) - (rank * 15)
  return (int16_t)(rssi + (snr * 2) - (rank * 15));
}

void PoleMeshRouter::updateNeighborsFromBeacon(uint16_t sender, uint8_t rank,
                                               uint16_t priParent, int16_t rssi,
                                               int8_t snr) {
  // Never add oneself or nodes with rank >= 254
  if (sender == myNodeId || sender == 0 || sender == MSCB_BROADCAST_NODE_ID)
    return;

  // Monotonically increasing rank loop prevention:
  // Cannot select a parent whose primary parent is me or who is a tracked child!
  if (priParent == myNodeId || findTrackedChild(sender) != nullptr)
    return;

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
  if (neighborCount == 0)
    return false;

  int bestIdx = -1;
  int secondBestIdx = -1;
  int16_t bestScore = -32000;
  int16_t secondScore = -32000;

  for (uint8_t i = 0; i < neighborCount; i++) {
    // Monotonically increasing rank rule: parent must have lower or valid rank
    // Gateway is Rank 0
    if (neighbors[i].primaryParent == myNodeId || findTrackedChild(neighbors[i].nodeAddress) != nullptr)
      continue; // Loop prevention

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
    if (secondBestIdx >= 0 && neighbors[secondBestIdx].nodeAddress != primaryParent) {
      backupParent = neighbors[secondBestIdx].nodeAddress;
    } else {
      backupParent = 0; // Clear backup if no distinct candidate exists
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
    uint16_t oldPrimary = primaryParent;
    primaryParent = backupParent;
    backupParent = oldPrimary;
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

bool PoleMeshRouter::sendUplinkPacket(uint8_t packetType,
                                      const uint8_t *payload,
                                      uint16_t payloadLen, uint8_t priority) {
  if (payloadLen > MSCB_MAX_PAYLOAD_SIZE)
    return false;
  if (primaryParent == 0 && packetType != PKT_REGISTER_REQUEST &&
      packetType != PKT_BEACON_ANNOUNCE) {
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
  frame.header.nextHop =
      (primaryParent != 0) ? primaryParent : MSCB_BROADCAST_NODE_ID;
  frame.header.payloadLen = payloadLen;

  if (payload && payloadLen > 0) {
    memcpy(frame.payload, payload, payloadLen);
  }

  return poleRadio.sendPacket(frame);
}

bool PoleMeshRouter::handleReceivedWireFrame(const MeshWireFrame &frame,
                                             int16_t rssi, int8_t snr,
                                             MeshWireFrame &outActionFrame,
                                             bool &outSendAction) {
  outSendAction = false;

  // A. Self-Loop Prevention: Reject self-originated packets immediately
  if (frame.header.srcAddress == myNodeId) {
    packetsDropped++;
    packetsDroppedSelfLoop++;
    return false;
  }

  // B. Validate packet metadata
  // Check magic numbers and protocol version
  if (frame.header.magic[0] != MSCB_PROTOCOL_MAGIC_0 ||
      frame.header.magic[1] != MSCB_PROTOCOL_MAGIC_1 ||
      frame.header.version != MSCB_PROTOCOL_VERSION) {
    packetsDropped++;
    packetsDroppedMalformed++;
    return false;
  }

  // Check valid source and destination IDs
  if (frame.header.srcAddress == 0 || frame.header.srcAddress == MSCB_BROADCAST_NODE_ID ||
      frame.header.dstAddress == 0) {
    packetsDropped++;
    packetsDroppedMalformed++;
    return false;
  }

  // Check payload length limit
  if (frame.header.payloadLen > MSCB_MAX_PAYLOAD_SIZE) {
    packetsDropped++;
    packetsDroppedMalformed++;
    return false;
  }

  // Hop Count Limit Enforcement (hopCount >= maxHops cannot be forwarded further)
  if (frame.header.hopCount >= frame.header.maxHops) {
    packetsDropped++;
    packetsDroppedHopLimit++;
    return false;
  }

  // Duplicate Suppression
  if (isDuplicate(frame.header.srcAddress, frame.header.seqNumber)) {
    packetsDropped++;
    packetsDroppedDuplicate++;
    return false;
  }
  addDuplicate(frame.header.srcAddress, frame.header.seqNumber);

  // If packet comes from primary parent, update activity timer
  if (frame.header.srcAddress == primaryParent ||
      frame.header.nextHop == primaryParent) {
    lastParentPacketMs = millis();
    if (state == ROUTING_FAILOVER) {
      state = ROUTING_CONNECTED;
    }
  }

  // Child Packet Tracking:
  // If destination is Gateway (uplink) and this node is nextHop (or broadcast relay)
  if (frame.header.dstAddress == MSCB_GATEWAY_NODE_ID &&
      (frame.header.nextHop == myNodeId || frame.header.nextHop == MSCB_BROADCAST_NODE_ID)) {
    onChildPacketReceived(frame.header.srcAddress, frame.header.seqNumber, rssi, snr, frame.header.type);
  }

  // Process Beacons
  if (frame.header.type == PKT_BEACON_ANNOUNCE) {
    if (frame.header.payloadLen >= sizeof(PktBeaconAnnouncePayload)) {
      PktBeaconAnnouncePayload beacon;
      memcpy(&beacon, frame.payload, sizeof(PktBeaconAnnouncePayload));
      updateNeighborsFromBeacon(beacon.senderId, beacon.rank,
                                beacon.primaryParent, rssi, snr);
    }
    return true;
  }

  // If packet is addressed directly to ME or Broadcast
  if (frame.header.dstAddress == myNodeId ||
      frame.header.dstAddress == MSCB_BROADCAST_NODE_ID) {
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

void PoleMeshRouter::onChildPacketReceived(uint16_t childId, uint32_t seq,
                                           int16_t rssi, int8_t snr,
                                           uint8_t pktType) {
  (void)pktType;
  // Validate sender
  if (childId == 0 || childId == myNodeId || childId == MSCB_GATEWAY_NODE_ID ||
      childId == MSCB_BROADCAST_NODE_ID) {
    return;
  }

  uint32_t now = millis();
  int foundIdx = -1;

  for (uint8_t i = 0; i < childCount; i++) {
    if (trackedChildren[i].childPoleId == childId) {
      foundIdx = (int)i;
      break;
    }
  }

  if (foundIdx >= 0) {
    // Child already tracked: update record
    // Sequence check: guard against replay
    if (seq != 0 && seq <= trackedChildren[foundIdx].lastSeqNumber &&
        (trackedChildren[foundIdx].lastSeqNumber - seq < 10000)) {
      // Replayed or stale sequence, do not advance sequence or timestamp falsely
      return;
    }

    trackedChildren[foundIdx].lastSeenMs = now;
    if (seq > trackedChildren[foundIdx].lastSeqNumber || trackedChildren[foundIdx].lastSeqNumber == 0) {
      trackedChildren[foundIdx].lastSeqNumber = seq;
    }
    if (rssi != 0) trackedChildren[foundIdx].lastRssi = rssi;
    if (snr != 0) trackedChildren[foundIdx].lastSnr = snr;
    trackedChildren[foundIdx].missedPings = 0;

    if (trackedChildren[foundIdx].state != CHILD_STATE_ONLINE) {
      trackedChildren[foundIdx].state = CHILD_STATE_ONLINE;
      trackedChildren[foundIdx].reportedFault = false;
    }
  } else {
    // New child node: add if capacity allows
    if (childCount < MESH_MAX_TRACKED_CHILDREN) {
      uint8_t idx = childCount++;
      trackedChildren[idx].childPoleId = childId;
      trackedChildren[idx].lastSeenMs = now;
      trackedChildren[idx].lastHeartbeatEpoch = 0;
      trackedChildren[idx].lastSeqNumber = seq;
      trackedChildren[idx].lastRssi = rssi;
      trackedChildren[idx].lastSnr = snr;
      trackedChildren[idx].state = CHILD_STATE_ONLINE;
      trackedChildren[idx].missedPings = 0;
      trackedChildren[idx].reportedFault = false;
    } else {
      // Table full: safely evict oldest offline child if available
      int offlineIdx = -1;
      uint32_t oldestMs = now;
      for (uint8_t i = 0; i < childCount; i++) {
        if (trackedChildren[i].state == CHILD_STATE_OFFLINE && trackedChildren[i].lastSeenMs < oldestMs) {
          oldestMs = trackedChildren[i].lastSeenMs;
          offlineIdx = (int)i;
        }
      }
      if (offlineIdx >= 0) {
        trackedChildren[offlineIdx].childPoleId = childId;
        trackedChildren[offlineIdx].lastSeenMs = now;
        trackedChildren[offlineIdx].lastHeartbeatEpoch = 0;
        trackedChildren[offlineIdx].lastSeqNumber = seq;
        trackedChildren[offlineIdx].lastRssi = rssi;
        trackedChildren[offlineIdx].lastSnr = snr;
        trackedChildren[offlineIdx].state = CHILD_STATE_ONLINE;
        trackedChildren[offlineIdx].missedPings = 0;
        trackedChildren[offlineIdx].reportedFault = false;
      }
    }
  }
}

const TrackedChildNode *PoleMeshRouter::getTrackedChild(uint8_t idx) const {
  if (idx >= childCount)
    return NULL;
  return &trackedChildren[idx];
}

const TrackedChildNode *PoleMeshRouter::findTrackedChild(uint16_t childId) const {
  for (uint8_t i = 0; i < childCount; i++) {
    if (trackedChildren[i].childPoleId == childId) {
      return &trackedChildren[i];
    }
  }
  return NULL;
}

void PoleMeshRouter::checkChildTimeouts(uint32_t nowMs) {
  for (uint8_t i = 0; i < childCount; i++) {
    uint32_t elapsed = (nowMs >= trackedChildren[i].lastSeenMs) ? (nowMs - trackedChildren[i].lastSeenMs) : 0;
    if (elapsed >= MESH_CHILD_TIMEOUT_MS) {
      if (trackedChildren[i].state != CHILD_STATE_OFFLINE) {
        trackedChildren[i].state = CHILD_STATE_OFFLINE;
        trackedChildren[i].reportedFault = true;
      }
    } else if (elapsed >= MESH_CHILD_SUSPECTED_MS) {
      if (trackedChildren[i].state == CHILD_STATE_ONLINE) {
        trackedChildren[i].state = CHILD_STATE_SUSPECTED;
      }
    }
  }
}

void PoleMeshRouter::service() {
  uint32_t now = millis();

  // 1. Check parent availability timeout (90 seconds)
  if (state == ROUTING_CONNECTED || state == ROUTING_FAILOVER) {
    if (now - lastParentPacketMs > MESH_PARENT_TIMEOUT_MS) {
      triggerFailover();
    }
  }

  // 2. Check child node timeouts (suspected / offline)
  checkChildTimeouts(now);

  // 3. Periodic Beacon Broadcast (every 30s) if connected
  if (state == ROUTING_CONNECTED &&
      (now - lastBeaconMs >= MSCB_BEACON_INTERVAL_MS)) {
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

const MeshNeighbor *PoleMeshRouter::getNeighbor(uint8_t idx) const {
  if (idx >= neighborCount)
    return NULL;
  return &neighbors[idx];
}
