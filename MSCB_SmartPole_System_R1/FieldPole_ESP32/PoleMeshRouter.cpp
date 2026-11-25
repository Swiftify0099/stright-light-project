/**
 * @file PoleMeshRouter.cpp
 * @brief MSCB Smart Pole Node - Multi-Hop Mesh Router & Dynamic Parent
 * Selection Implementation
 * @version 1.0.0
 */

#include "PoleMeshRouter.h"
#include "PoleRegistrationClient.h"
#include <string.h>

PoleMeshRouter poleMesh;

PoleMeshRouter::PoleMeshRouter()
    : state(ROUTING_UNINITIALIZED), myNodeId(POLE_UNASSIGNED_NODE_ADDRESS),
      primaryParent(MSCB_GATEWAY_NODE_ID), backupParent(0),
      myRank(1), seqCounter(1), lastParentPacketMs(0), lastBeaconMs(0),
      packetsForwarded(0), packetsDropped(0),
      packetsDroppedSelfLoop(0), packetsDroppedHopLimit(0),
      packetsDroppedMalformed(0), packetsDroppedDuplicate(0),
      packetsDroppedRouting(0), packetsDroppedAuth(0),
      neighborCount(0), childCount(0), knownDescendantCount(0), knownDescendantIdx(0),
      pendingEventHead(0), pendingEventTail(0), pendingEventCount(0),
      hasLastPropagatedChildEventFrame(false),
      dupCacheIdx(0), reverseRouteIdx(0) {
  memset(neighbors, 0, sizeof(neighbors));
  memset(trackedChildren, 0, sizeof(trackedChildren));
  memset(knownDescendants, 0, sizeof(knownDescendants));
  memset(pendingChildEvents, 0, sizeof(pendingChildEvents));
  memset(&lastPropagatedChildEventFrame, 0, sizeof(lastPropagatedChildEventFrame));
  memset(dupCache, 0, sizeof(dupCache));
  memset(reverseRoutes, 0, sizeof(reverseRoutes));
}

bool PoleMeshRouter::setNodeIdentity(uint16_t newId, uint16_t newParent, uint8_t newRank) {
  if (!mscbIsValidAssignedPoleId(newId)) return false;
  if (newId == MSCB_GATEWAY_NODE_ID || newId == MSCB_BROADCAST_NODE_ID) return false;
  if (newParent == newId || newParent == MSCB_BROADCAST_NODE_ID || newParent >= MSCB_EPHEMERAL_JOIN_MIN) {
    return false;
  }

  uint16_t oldId = myNodeId;
  myNodeId = newId;
  if (newParent != 0) {
    primaryParent = newParent;
    if (backupParent == primaryParent || backupParent == myNodeId) {
      backupParent = 0;
    }
    myRank = (newRank >= 1 && newRank <= MESH_MAX_HOPS) ? newRank : 1;
    state = ROUTING_CONNECTED;
  } else {
    primaryParent = 0;
    backupParent = 0;
    myRank = 255;
    state = ROUTING_SEARCHING;
  }

  // Flush duplicate cache to prevent suppressing packets for or from new ID
  memset(dupCache, 0, sizeof(dupCache));
  dupCacheIdx = 0;

  Serial.printf("[MESH] Runtime Node Identity updated: 0x%04X -> 0x%04X (Parent: 0x%04X, Rank: %u)\n",
                oldId, myNodeId, primaryParent, myRank);
  return true;
}

void PoleMeshRouter::resetToUnassignedIdentity(const uint8_t mac[6]) {
  myNodeId = mscbIsValidMac(mac, false) ? mscbMakeEphemeralJoinAddr(mac) : POLE_UNASSIGNED_NODE_ADDRESS;
  memset(dupCache, 0, sizeof(dupCache));
  dupCacheIdx = 0;
}

bool PoleMeshRouter::begin() {
  if (poleStorage.isRegistered() ||
      poleStorage.getRegState() == REG_CLIENT_WAITING_FOR_HANDSHAKE ||
      poleStorage.getRegState() == REG_CLIENT_APPROVAL_RECEIVED) {
    uint16_t storedId = poleStorage.getAssignedPoleId();
    myNodeId = mscbIsValidAssignedPoleId(storedId) ? storedId : POLE_UNASSIGNED_NODE_ADDRESS;
  } else {
    myNodeId = mscbIsValidMac(poleRegClient.getMacAddress(), false)
                   ? mscbMakeEphemeralJoinAddr(poleRegClient.getMacAddress())
                   : POLE_UNASSIGNED_NODE_ADDRESS;
  }
  primaryParent = poleStorage.getPrimaryParent();
  backupParent = poleStorage.getBackupParent();
  myRank = poleStorage.getNetworkRank();

  if (primaryParent == myNodeId || primaryParent == MSCB_BROADCAST_NODE_ID ||
      primaryParent >= MSCB_EPHEMERAL_JOIN_MIN) {
    primaryParent = 0;
  }
  if (backupParent == primaryParent || backupParent == myNodeId ||
      backupParent == MSCB_BROADCAST_NODE_ID || backupParent >= MSCB_EPHEMERAL_JOIN_MIN) {
    backupParent = 0;
  }
  if (primaryParent == 0 || myRank == 0 || myRank > MESH_MAX_HOPS) {
    primaryParent = 0;
    backupParent = 0;
    myRank = 255;
    state = ROUTING_SEARCHING;
  } else {
    state = ROUTING_CONNECTED;
  }

  memset(reverseRoutes, 0, sizeof(reverseRoutes));
  reverseRouteIdx = 0;
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

void PoleMeshRouter::removeNeighbor(uint16_t nodeAddr) {
  int idx = findNeighborIndex(nodeAddr);
  if (idx < 0) return;
  for (uint8_t i = (uint8_t)idx; i + 1 < neighborCount; i++) {
    neighbors[i] = neighbors[i + 1];
  }
  if (neighborCount > 0) {
    memset(&neighbors[neighborCount - 1], 0, sizeof(MeshNeighbor));
    neighborCount--;
  }
}

void PoleMeshRouter::recordKnownDescendant(uint16_t nodeId, uint16_t viaChildId) {
  if (nodeId == 0 || nodeId == myNodeId || nodeId == MSCB_GATEWAY_NODE_ID ||
      nodeId == MSCB_BROADCAST_NODE_ID || viaChildId == 0 || viaChildId == myNodeId) {
    return;
  }
  uint32_t now = millis();
  for (uint8_t i = 0; i < knownDescendantCount; i++) {
    if (knownDescendants[i].nodeId == nodeId) {
      knownDescendants[i].viaChildId = viaChildId;
      knownDescendants[i].lastSeenMs = now;
      return;
    }
  }
  if (knownDescendantCount < MESH_MAX_KNOWN_DESCENDANTS) {
    uint8_t slot = knownDescendantCount++;
    knownDescendants[slot].nodeId = nodeId;
    knownDescendants[slot].viaChildId = viaChildId;
    knownDescendants[slot].lastSeenMs = now;
  } else {
    uint8_t slot = knownDescendantIdx % MESH_MAX_KNOWN_DESCENDANTS;
    knownDescendantIdx = (knownDescendantIdx + 1) % MESH_MAX_KNOWN_DESCENDANTS;
    knownDescendants[slot].nodeId = nodeId;
    knownDescendants[slot].viaChildId = viaChildId;
    knownDescendants[slot].lastSeenMs = now;
  }
}

bool PoleMeshRouter::isKnownDescendant(uint16_t nodeId) const {
  if (nodeId == 0 || nodeId == MSCB_GATEWAY_NODE_ID) return false;
  if (findTrackedChild(nodeId) != nullptr) return true;
  for (uint8_t i = 0; i < knownDescendantCount; i++) {
    if (knownDescendants[i].nodeId == nodeId) return true;
  }
  return false;
}

uint16_t PoleMeshRouter::lookupDescendantNextHop(uint16_t dstNodeId) const {
  if (findTrackedChild(dstNodeId) != nullptr) return dstNodeId;
  for (uint8_t i = 0; i < knownDescendantCount; i++) {
    if (knownDescendants[i].nodeId == dstNodeId) {
      return knownDescendants[i].viaChildId;
    }
  }
  return 0;
}

bool PoleMeshRouter::wouldCreateCycle(uint16_t candidateNode) const {
  if (candidateNode == 0 || candidateNode == myNodeId ||
      candidateNode == MSCB_BROADCAST_NODE_ID) {
    return true;
  }
  if (candidateNode == MSCB_GATEWAY_NODE_ID) {
    return false;
  }
  if (isKnownDescendant(candidateNode)) {
    return true;
  }

  uint16_t visited[MESH_MAX_HOPS + 4];
  uint8_t visitedCount = 0;
  uint16_t curr = candidateNode;

  for (uint8_t depth = 0; depth < MESH_MAX_HOPS + 2; depth++) {
    if (curr == 0 || curr == myNodeId || isKnownDescendant(curr)) {
      return true;
    }
    if (curr == MSCB_GATEWAY_NODE_ID) {
      return false;
    }
    for (uint8_t v = 0; v < visitedCount; v++) {
      if (visited[v] == curr) return true;
    }
    if (visitedCount < (uint8_t)(sizeof(visited) / sizeof(visited[0]))) {
      visited[visitedCount++] = curr;
    }
    int nIdx = findNeighborIndex(curr);
    if (nIdx < 0) {
      return false;
    }
    uint16_t nextP = neighbors[nIdx].primaryParent;
    if (nextP == curr || nextP == myNodeId || isKnownDescendant(nextP)) {
      return true;
    }
    curr = nextP;
  }
  return true;
}

void PoleMeshRouter::updateNeighborsFromBeacon(uint16_t sender, uint8_t rank,
                                               uint16_t priParent, int16_t rssi,
                                               int8_t snr) {
  auto invalidateSenderIfCached = [this](uint16_t addr) {
    removeNeighbor(addr);
    if (addr == backupParent) {
      backupParent = 0;
    }
    if (addr == primaryParent) {
      triggerFailover();
    }
  };

  // Reject self, broadcast, or zero/ephemeral sender IDs
  if (sender == myNodeId || sender == 0 || sender == MSCB_BROADCAST_NODE_ID ||
      sender >= MSCB_EPHEMERAL_JOIN_MIN) {
    return;
  }

  // Strict rank validation (R3.2.2 P0):
  // Gateway MUST remain rank 0 with parent 0
  if (sender == MSCB_GATEWAY_NODE_ID && (rank != 0 || priParent != 0)) {
    invalidateSenderIfCached(sender);
    return;
  }
  // Field pole cannot be rank 0
  if (sender != MSCB_GATEWAY_NODE_ID && rank == 0) {
    invalidateSenderIfCached(sender);
    return;
  }
  // Rank boundary checks: reject rank >= MESH_MAX_HOPS and boundaries (254, 255)
  if (rank >= MESH_MAX_HOPS || rank >= 254) {
    invalidateSenderIfCached(sender);
    return;
  }
  // Overflow check: increment cannot exceed MESH_MAX_HOPS or wrap to zero
  uint8_t nextRank = (uint8_t)(rank + 1U);
  if (nextRank <= rank || nextRank > MESH_MAX_HOPS || nextRank == 0) {
    invalidateSenderIfCached(sender);
    return;
  }

  // Require a candidate parent to have a strictly lower rank than current node (R3.2.2 P0)
  if ((state == ROUTING_CONNECTED || state == ROUTING_FAILOVER) &&
      myRank >= 1 && myRank <= MESH_MAX_HOPS &&
      rank >= myRank) {
    invalidateSenderIfCached(sender);
    return;
  }

  // Require candidate parent to have strictly lower rank than current node when connected
  if ((state == ROUTING_CONNECTED || state == ROUTING_FAILOVER) &&
      myRank >= 1 && myRank <= MESH_MAX_HOPS && rank >= myRank) {
    invalidateSenderIfCached(sender);
    return;
  }

  // Transitive cycle and descendant checks:
  if (priParent == myNodeId || priParent == sender || isKnownDescendant(sender) ||
      (priParent != 0 && wouldCreateCycle(priParent))) {
    invalidateSenderIfCached(sender);
    return;
  }

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
  uint32_t now = millis();

  for (uint8_t i = 0; i < neighborCount; i++) {
    // 1. Stale beacon filter: older than MESH_PARENT_TIMEOUT_MS cannot authorize parent selection
    if ((uint32_t)(now - neighbors[i].lastSeenMs) > MESH_PARENT_TIMEOUT_MS)
      continue;

    // 2. Strict rank validation (R3.2.2 P0):
    if (neighbors[i].rank >= MESH_MAX_HOPS || neighbors[i].rank >= 254)
      continue;

    uint8_t candNextRank = (uint8_t)(neighbors[i].rank + 1U);
    if (candNextRank <= neighbors[i].rank || candNextRank > MESH_MAX_HOPS || candNextRank == 0)
      continue;

    // Gateway rank must be 0 with parent 0; field pole cannot be rank 0
    if (neighbors[i].nodeAddress == MSCB_GATEWAY_NODE_ID &&
        (neighbors[i].rank != 0 || neighbors[i].primaryParent != 0))
      continue;
    if (neighbors[i].nodeAddress != MSCB_GATEWAY_NODE_ID && neighbors[i].rank == 0)
      continue;

    // When connected or in failover, candidate parent must have strictly lower rank than current node
    if ((state == ROUTING_CONNECTED || state == ROUTING_FAILOVER) &&
        myRank >= 1 && myRank <= MESH_MAX_HOPS &&
        neighbors[i].rank >= myRank)
      continue;

    // 3. Cycle & descendant resistance (direct and transitive)
    if (wouldCreateCycle(neighbors[i].nodeAddress))
      continue;

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
    myRank = (uint8_t)(neighbors[bestIdx].rank + 1U);
    state = ROUTING_CONNECTED;

    // Revalidate backup parent independently: distinct, non-cyclic, fresh, valid rank < myRank
    if (secondBestIdx >= 0 &&
        neighbors[secondBestIdx].nodeAddress != primaryParent &&
        neighbors[secondBestIdx].nodeAddress != 0 &&
        neighbors[secondBestIdx].nodeAddress != myNodeId &&
        !wouldCreateCycle(neighbors[secondBestIdx].nodeAddress) &&
        neighbors[secondBestIdx].rank < MESH_MAX_HOPS &&
        neighbors[secondBestIdx].rank < myRank &&
        (uint32_t)(now - neighbors[secondBestIdx].lastSeenMs) <= MESH_PARENT_TIMEOUT_MS) {
      backupParent = neighbors[secondBestIdx].nodeAddress;
    } else {
      backupParent = 0; // Clear backup if no distinct valid candidate exists
    }

    state = ROUTING_CONNECTED;
    poleStorage.saveRouting(primaryParent, backupParent, myRank);
    lastParentPacketMs = now;
    return true;
  }

  return false;
}

void PoleMeshRouter::triggerFailover() {
  uint32_t now = millis();
  if (backupParent != 0 && backupParent != primaryParent && backupParent != myNodeId) {
    int bIdx = findNeighborIndex(backupParent);
    if (bIdx >= 0 &&
        (uint32_t)(now - neighbors[bIdx].lastSeenMs) <= MESH_PARENT_TIMEOUT_MS &&
        neighbors[bIdx].rank < MESH_MAX_HOPS &&
        neighbors[bIdx].rank < 254 &&
        (uint8_t)(neighbors[bIdx].rank + 1U) <= MESH_MAX_HOPS &&
        !(neighbors[bIdx].nodeAddress == MSCB_GATEWAY_NODE_ID && neighbors[bIdx].rank != 0) &&
        !(neighbors[bIdx].nodeAddress != MSCB_GATEWAY_NODE_ID && neighbors[bIdx].rank == 0) &&
        !wouldCreateCycle(backupParent)) {
      primaryParent = backupParent;
      backupParent = 0; // Never demote failed primary back into backup slot
      myRank = (uint8_t)(neighbors[bIdx].rank + 1U);
      state = ROUTING_FAILOVER;
      lastParentPacketMs = now;
      poleStorage.saveRouting(primaryParent, backupParent, myRank);
      return;
    }
  }
  declareOrphan();
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
  if (packetType != PKT_REGISTER_REQUEST && packetType != PKT_BEACON_ANNOUNCE) {
    if (!isGatewayReachable()) {
      return false;
    }
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
      (primaryParent != 0 && packetType != PKT_BEACON_ANNOUNCE) ? primaryParent : MSCB_BROADCAST_NODE_ID;
  frame.header.previousHopAddress = myNodeId; // Authenticated immediate-hop identity (R3.2.2 P0)
  frame.header.payloadLen = payloadLen;

  if (payload && payloadLen > 0) {
    memcpy(frame.payload, payload, payloadLen);
  }
  frame.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(frame);

  return poleRadio.sendPacket(frame);
}

bool PoleMeshRouter::handleReceivedWireFrame(const MeshWireFrame &frame,
                                             int16_t rssi, int8_t snr,
                                             MeshWireFrame &outActionFrame,
                                             bool &outSendAction) {
  outSendAction = false;

  // A. Self-Loop Prevention: Reject self-originated or self-relayed packets immediately
  if (frame.header.srcAddress == myNodeId) {
    packetsDropped++;
    packetsDroppedSelfLoop++;
    return false;
  }

  // B. Validate packet metadata & protocol version
  if (frame.header.magic[0] != MSCB_PROTOCOL_MAGIC_0 ||
      frame.header.magic[1] != MSCB_PROTOCOL_MAGIC_1 ||
      frame.header.version != MSCB_PROTOCOL_VERSION) {
    packetsDropped++;
    packetsDroppedMalformed++;
    return false;
  }

  // Validate source and destination metadata
  if (frame.header.srcAddress == 0 || frame.header.srcAddress == MSCB_BROADCAST_NODE_ID ||
      frame.header.dstAddress == 0) {
    packetsDropped++;
    packetsDroppedMalformed++;
    return false;
  }
  if (frame.header.previousHopAddress == myNodeId) {
    packetsDropped++;
    packetsDroppedSelfLoop++;
    return false;
  }

  // Check payload length limit
  if (frame.header.payloadLen > MSCB_MAX_PAYLOAD_SIZE) {
    packetsDropped++;
    packetsDroppedMalformed++;
    return false;
  }

  // Special multi-hop handling for PKT_REGISTER_RESPONSE:
  // Accept ONLY if payload MAC matches our MAC (do not consume broadcast PKT_REGISTER_RESPONSE for another pole!)
  bool isForMe = false;
  if (frame.header.type == PKT_REGISTER_RESPONSE) {
    if (frame.header.payloadLen < sizeof(PktRegisterResponsePayload)) {
      packetsDropped++;
      packetsDroppedMalformed++;
      return false;
    }
    const PktRegisterResponsePayload* regResp = (const PktRegisterResponsePayload*)frame.payload;
    if (poleRegClient.getMacAddress() && memcmp(regResp->macAddress, poleRegClient.getMacAddress(), 6) == 0) {
      isForMe = true;
    }
  } else {
    isForMe = (frame.header.dstAddress == myNodeId) ||
              (frame.header.dstAddress == MSCB_BROADCAST_NODE_ID);
  }

  // Next-Hop Validation (R3.2.2 P0):
  // If packet specifies an explicit nextHop that is neither this node nor broadcast and not intended for this node,
  // bystander nodes MUST drop it and not forward it.
  if (!isForMe &&
      frame.header.nextHop != 0 &&
      frame.header.nextHop != MSCB_BROADCAST_NODE_ID &&
      frame.header.nextHop != myNodeId) {
    packetsDropped++;
    packetsDroppedRouting++;
    return false;
  }

  // Strict Maximum Hop Validation (R3.2.2 P0): 1 <= maxHops <= MESH_MAX_HOPS (6)
  if (frame.header.maxHops == 0 || frame.header.maxHops > MESH_MAX_HOPS) {
    packetsDropped++;
    packetsDroppedMalformed++;
    return false;
  }

  // Hop Count Limit Enforcement (hopCount >= maxHops or >= MESH_MAX_HOPS cannot be forwarded)
  if (frame.header.hopCount >= frame.header.maxHops || frame.header.hopCount >= MESH_MAX_HOPS) {
    packetsDropped++;
    packetsDroppedHopLimit++;
    return false;
  }

  // Fail-closed immediate-hop and frame integrity authentication
  bool authOk = true;
  if (frame.header.previousHopAddress != 0 && frame.header.hopAuthToken != 0) {
    authOk = cmdSecurity.verifyHopAuthToken(frame.header.previousHopAddress,
                                            frame.header.nextHop,
                                            frame.header.seqNumber,
                                            frame.header.type,
                                            frame.header.hopAuthToken) ||
             cmdSecurity.verifyFrameHopAuthToken(frame);
    if (!authOk) {
      packetsDropped++;
      packetsDroppedAuth++;
      packetsDroppedMalformed++;
      return false;
    }
  } else if (frame.header.hopAuthToken != 0) {
    packetsDropped++;
    packetsDroppedAuth++;
    packetsDroppedMalformed++;
    return false;
  }

  // Reject frames not intended for this node before polluting duplicate cache
  bool controlledBcastRelay = (frame.header.nextHop == MSCB_BROADCAST_NODE_ID) &&
                              (frame.header.type == PKT_REGISTER_REQUEST ||
                               frame.header.type == PKT_REGISTER_RESPONSE);
  if (!isForMe && frame.header.nextHop != myNodeId && !controlledBcastRelay) {
    packetsDropped++;
    packetsDroppedRouting++;
    return false;
  }

  // Duplicate Suppression
  if (isDuplicate(frame.header.srcAddress, frame.header.seqNumber)) {
    packetsDropped++;
    packetsDroppedDuplicate++;
    return false;
  }
  addDuplicate(frame.header.srcAddress, frame.header.seqNumber);

  // If authenticated immediate hop was our primary parent, update activity timer
  if (frame.header.previousHopAddress == primaryParent) {
    lastParentPacketMs = millis();
    if (state == ROUTING_FAILOVER) {
      state = ROUTING_CONNECTED;
    }
  }

  // Child Packet Tracking & Reverse Route Recording (R3.2.2 P0):
  uint16_t prevHop = frame.header.previousHopAddress;
  bool isDirectUnicastFromChild = (frame.header.dstAddress == MSCB_GATEWAY_NODE_ID) &&
                                  (frame.header.nextHop == myNodeId);
  if (isDirectUnicastFromChild) {
    if (frame.header.type == PKT_REGISTER_REQUEST &&
        frame.header.payloadLen >= sizeof(PktRegisterRequestPayload)) {
      const PktRegisterRequestPayload* regReq = (const PktRegisterRequestPayload*)frame.payload;
      if (mscbIsValidMac(regReq->macAddress, false) && prevHop != 0 && prevHop != myNodeId) {
        recordReverseRoute(frame.header.srcAddress, prevHop, regReq->macAddress);
      }
    } else if (frame.header.srcAddress < MSCB_EPHEMERAL_JOIN_MIN) {
      recordReverseRoute(frame.header.srcAddress, prevHop, nullptr);
    }

    if (frame.header.srcAddress == prevHop) {
      if (frame.header.srcAddress < MSCB_EPHEMERAL_JOIN_MIN) {
        onChildPacketReceived(frame.header.srcAddress, frame.header.seqNumber, rssi, snr,
                              frame.header.type, prevHop, authOk, false);
      }
    } else {
      // Multi-hop relayed grandchild packet: never register grandchild as a direct child!
      recordKnownDescendant(frame.header.srcAddress, prevHop);
      if (prevHop < MSCB_EPHEMERAL_JOIN_MIN) {
        touchDirectChildFromRelay(prevHop, rssi, snr);
      }
    }
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

  if (isForMe) {
    // Deliver to local node services (registration, feeder command, etc.)
    return true;
  }

  // Multi-hop Forwarding (strict hop + route + impossible-transition checks)
  uint8_t nextHopCount = (uint8_t)(frame.header.hopCount + 1U);
  if (nextHopCount <= frame.header.hopCount ||
      nextHopCount > frame.header.maxHops ||
      nextHopCount > MESH_MAX_HOPS) {
    packetsDropped++;
    packetsDroppedHopLimit++;
    return false;
  }

  outActionFrame = frame;
  outActionFrame.header.hopCount = nextHopCount;

  // Determine direction:
  if (frame.header.dstAddress == MSCB_GATEWAY_NODE_ID) {
    if (!isGatewayReachable() || primaryParent == prevHop || primaryParent == frame.header.srcAddress) {
      packetsDropped++;
      packetsDroppedRouting++;
      return false;
    }
    outActionFrame.header.nextHop = primaryParent;
  } else if (frame.header.type == PKT_REGISTER_RESPONSE) {
    const PktRegisterResponsePayload* regResp = (const PktRegisterResponsePayload*)frame.payload;
    uint16_t revHop = lookupReverseHopByMac(regResp->macAddress);
    if (revHop == 0 && frame.header.dstAddress != MSCB_BROADCAST_NODE_ID) {
      revHop = lookupDescendantNextHop(frame.header.dstAddress);
      if (revHop == 0) {
        revHop = lookupReverseHopByNode(frame.header.dstAddress);
      }
    }
    if (revHop != 0 && revHop != myNodeId && revHop != prevHop && revHop != primaryParent) {
      outActionFrame.header.nextHop = revHop;
    } else if (frame.header.nextHop == myNodeId) {
      outActionFrame.header.nextHop = MSCB_BROADCAST_NODE_ID;
    } else {
      packetsDropped++;
      packetsDroppedRouting++;
      return false;
    }
  } else {
    // Downlink: strictly route only towards known direct child, descendant, or reverse breadcrumb
    if (frame.header.nextHop != myNodeId) {
      packetsDropped++;
      packetsDroppedRouting++;
      return false;
    }
    uint16_t revHop = lookupDescendantNextHop(frame.header.dstAddress);
    if (revHop == 0) {
      revHop = lookupReverseHopByNode(frame.header.dstAddress);
    }
    if (revHop == 0 || revHop == myNodeId || revHop == prevHop || revHop == primaryParent) {
      packetsDropped++;
      packetsDroppedRouting++;
      return false;
    }
    outActionFrame.header.nextHop = revHop;
  }

  // Update immediate-hop transmitter identity and full-frame MAC before transmission
  outActionFrame.header.previousHopAddress = myNodeId;
  outActionFrame.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(outActionFrame);

  outSendAction = true;
  packetsForwarded++;
  return true;
}

void PoleMeshRouter::touchDirectChildFromRelay(uint16_t directChildId, int16_t rssi, int8_t snr) {
  if (directChildId == 0 || directChildId == myNodeId ||
      directChildId == primaryParent || directChildId == MSCB_GATEWAY_NODE_ID ||
      directChildId == MSCB_BROADCAST_NODE_ID || directChildId >= MSCB_EPHEMERAL_JOIN_MIN) {
    return;
  }
  uint32_t now = millis();
  for (uint8_t i = 0; i < childCount; i++) {
    if (trackedChildren[i].childPoleId == directChildId) {
      // Never allow indirect grandchild relay alone to restore an OFFLINE child
      if (trackedChildren[i].state == CHILD_STATE_OFFLINE) {
        return;
      }
      trackedChildren[i].lastSeenMs = now;
      if (rssi != 0) trackedChildren[i].lastRssi = rssi;
      if (snr != 0) trackedChildren[i].lastSnr = snr;
      trackedChildren[i].missedPings = 0;
      if (trackedChildren[i].state != CHILD_STATE_ONLINE) {
        trackedChildren[i].state = CHILD_STATE_ONLINE;
      }
      return;
    }
  }
  // Relayed frames must never create new direct child records (R3.2.2 P0)
}

void PoleMeshRouter::onChildPacketReceived(uint16_t srcAddress, uint32_t seq,
                                           int16_t rssi, int8_t snr,
                                           uint8_t pktType, uint16_t prevHop,
                                           bool authValid, bool isBroadcast) {
  (void)pktType;

  // Rule 1: Broadcast nextHop must not prove direct-child identity
  if (isBroadcast) {
    return;
  }

  // Rule 2: Immediate-hop identity must be cryptographically verified
  if (!authValid || prevHop == 0 || srcAddress == 0) {
    return;
  }

  // Rule 3: If frame was relayed (srcAddress != prevHop), record descendant and only touch prevHop
  if (srcAddress != prevHop) {
    recordKnownDescendant(srcAddress, prevHop);
    touchDirectChildFromRelay(prevHop, rssi, snr);
    return;
  }

  uint16_t directChildId = prevHop;

  // Topological & identity sanity checks
  if (directChildId == 0 || directChildId == myNodeId ||
      directChildId == primaryParent || directChildId == MSCB_GATEWAY_NODE_ID ||
      directChildId == MSCB_BROADCAST_NODE_ID || directChildId >= MSCB_EPHEMERAL_JOIN_MIN) {
    return;
  }

  uint32_t now = millis();
  int foundIdx = -1;

  for (uint8_t i = 0; i < childCount; i++) {
    if (trackedChildren[i].childPoleId == directChildId) {
      foundIdx = (int)i;
      break;
    }
  }

  if (foundIdx >= 0) {
    // Sequence replay / staleness guard (RFC 1982 for 16-bit sequences)
    uint32_t prevSeq = trackedChildren[foundIdx].lastSeqNumber;
    bool seqIsNewer = false;
    if (seq > 0xFFFFUL || prevSeq > 0xFFFFUL) {
      seqIsNewer = (seq > prevSeq);
    } else {
      uint16_t s16 = (uint16_t)seq;
      uint16_t p16 = (uint16_t)prevSeq;
      seqIsNewer = (s16 != p16) && (((uint16_t)(s16 - p16)) < 0x8000U);
    }
    if (!seqIsNewer) {
      return;
    }

    trackedChildren[foundIdx].lastSeenMs = now;
    trackedChildren[foundIdx].lastSeqNumber = seq;
    if (rssi != 0) trackedChildren[foundIdx].lastRssi = rssi;
    if (snr != 0) trackedChildren[foundIdx].lastSnr = snr;
    trackedChildren[foundIdx].missedPings = 0;

    if (trackedChildren[foundIdx].state == CHILD_STATE_OFFLINE) {
      trackedChildren[foundIdx].state = CHILD_STATE_ONLINE;
      trackedChildren[foundIdx].reportedFault = false;
      propagateChildStatusEvent(directChildId, 1, 2);
    } else if (trackedChildren[foundIdx].state != CHILD_STATE_ONLINE) {
      trackedChildren[foundIdx].state = CHILD_STATE_ONLINE;
      trackedChildren[foundIdx].reportedFault = false;
    }
  } else {
    // New child node: add if capacity allows (never overwrite active child!)
    if (childCount < MESH_MAX_TRACKED_CHILDREN) {
      uint8_t idx = childCount++;
      trackedChildren[idx].childPoleId = directChildId;
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
        if (trackedChildren[i].state == CHILD_STATE_OFFLINE && trackedChildren[i].lastSeenMs <= oldestMs) {
          oldestMs = trackedChildren[i].lastSeenMs;
          offlineIdx = (int)i;
        }
      }
      if (offlineIdx >= 0) {
        trackedChildren[offlineIdx].childPoleId = directChildId;
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

bool PoleMeshRouter::propagateChildStatusEvent(uint16_t childId, uint8_t eventType, uint8_t reason) {
  if (!isGatewayReachable()) return false;

  PktChildStatusEventPayload ev;
  ev.parentPoleId = myNodeId;
  ev.childPoleId = childId;
  ev.eventType = eventType;
  ev.reason = reason;
  ev.eventTimestamp = (uint32_t)(millis() / 1000);
  ev.eventSeqNumber = seqCounter++;

  uint8_t pktType = (eventType == 0) ? PKT_ALERT_CHILD_OFFLINE : PKT_ALERT_CHILD_RECOVERED;

  MeshWireFrame evFrame;
  memset(&evFrame, 0, sizeof(evFrame));
  evFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  evFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  evFrame.header.version = MSCB_PROTOCOL_VERSION;
  evFrame.header.type = pktType;
  evFrame.header.priority = PRIO_CRITICAL;
  evFrame.header.hopCount = 0;
  evFrame.header.maxHops = MESH_MAX_HOPS;
  evFrame.header.seqNumber = ev.eventSeqNumber;
  evFrame.header.srcAddress = myNodeId;
  evFrame.header.dstAddress = MSCB_GATEWAY_NODE_ID;
  evFrame.header.nextHop = primaryParent;
  evFrame.header.previousHopAddress = myNodeId;
  evFrame.header.payloadLen = sizeof(PktChildStatusEventPayload);
  memcpy(evFrame.payload, &ev, sizeof(PktChildStatusEventPayload));
  evFrame.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(evFrame);

  lastPropagatedChildEventFrame = evFrame;
  hasLastPropagatedChildEventFrame = true;

  bool sent = poleRadio.sendPacket(evFrame);
  if (!sent && pendingEventCount < MESH_MAX_PENDING_CHILD_EVENTS) {
    pendingChildEvents[pendingEventTail] = evFrame;
    pendingEventTail = (pendingEventTail + 1) % MESH_MAX_PENDING_CHILD_EVENTS;
    pendingEventCount++;
  }
  return true;
}

bool PoleMeshRouter::dequeuePendingChildEvent(MeshWireFrame &outFrame) {
  if (pendingEventCount == 0) return false;
  outFrame = pendingChildEvents[pendingEventHead];
  pendingEventHead = (pendingEventHead + 1) % MESH_MAX_PENDING_CHILD_EVENTS;
  pendingEventCount--;
  return true;
}

bool PoleMeshRouter::getLastPropagatedChildEventFrame(MeshWireFrame &outFrame) const {
  if (!hasLastPropagatedChildEventFrame) return false;
  outFrame = lastPropagatedChildEventFrame;
  return true;
}

void PoleMeshRouter::flushPendingChildEvents() {
  if (!isGatewayReachable() || pendingEventCount == 0) return;
  uint8_t attempts = pendingEventCount;
  while (attempts-- > 0 && pendingEventCount > 0) {
    MeshWireFrame &f = pendingChildEvents[pendingEventHead];
    f.header.nextHop = primaryParent;
    f.header.previousHopAddress = myNodeId;
    f.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(f);
    if (!poleRadio.sendPacket(f)) {
      break;
    }
    pendingEventHead = (pendingEventHead + 1) % MESH_MAX_PENDING_CHILD_EVENTS;
    pendingEventCount--;
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
        propagateChildStatusEvent(trackedChildren[i].childPoleId, 0, 1);
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

  // 2. Check child node timeouts (suspected / offline) & flush queued child events
  checkChildTimeouts(now);
  flushPendingChildEvents();

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
    beaconFrame.header.previousHopAddress = myNodeId;

    PktBeaconAnnouncePayload payload;
    payload.senderId = myNodeId;
    payload.rank = myRank;
    payload.primaryParent = primaryParent;
    payload.capacitySlotsFree = 0;
    payload.beaconTimestamp = now / 1000;

    beaconFrame.header.payloadLen = sizeof(PktBeaconAnnouncePayload);
    memcpy(beaconFrame.payload, &payload, sizeof(PktBeaconAnnouncePayload));
    beaconFrame.header.hopAuthToken = cmdSecurity.computeFrameHopAuthToken(beaconFrame);

    poleRadio.sendPacket(beaconFrame);
  }
}

const MeshNeighbor *PoleMeshRouter::getNeighbor(uint8_t idx) const {
  if (idx >= neighborCount)
    return NULL;
  return &neighbors[idx];
}

void PoleMeshRouter::recordReverseRoute(uint16_t srcNodeId, uint16_t prevHop, const uint8_t* mac) {
  if (prevHop == 0 || prevHop == myNodeId || prevHop == MSCB_BROADCAST_NODE_ID) return;
  uint32_t now = millis();
  for (uint8_t i = 0; i < MESH_REVERSE_ROUTE_SIZE; i++) {
    bool macMatch = (mac && reverseRoutes[i].hasMac && memcmp(reverseRoutes[i].macAddress, mac, 6) == 0);
    bool nodeMatch = (!mac && srcNodeId != 0 && reverseRoutes[i].srcNodeId == srcNodeId);
    if (macMatch || nodeMatch) {
      reverseRoutes[i].srcNodeId = srcNodeId;
      reverseRoutes[i].prevHop = prevHop;
      if (mac) {
        memcpy(reverseRoutes[i].macAddress, mac, 6);
        reverseRoutes[i].hasMac = true;
      }
      reverseRoutes[i].lastSeenMs = now;
      return;
    }
  }
  uint8_t slot = reverseRouteIdx % MESH_REVERSE_ROUTE_SIZE;
  reverseRouteIdx = (reverseRouteIdx + 1) % MESH_REVERSE_ROUTE_SIZE;
  reverseRoutes[slot].srcNodeId = srcNodeId;
  reverseRoutes[slot].prevHop = prevHop;
  if (mac) {
    memcpy(reverseRoutes[slot].macAddress, mac, 6);
    reverseRoutes[slot].hasMac = true;
  } else {
    memset(reverseRoutes[slot].macAddress, 0, 6);
    reverseRoutes[slot].hasMac = false;
  }
  reverseRoutes[slot].lastSeenMs = now;
}

uint16_t PoleMeshRouter::lookupReverseHopByMac(const uint8_t mac[6]) const {
  if (!mac) return 0;
  uint32_t now = millis();
  for (uint8_t i = 0; i < MESH_REVERSE_ROUTE_SIZE; i++) {
    if (reverseRoutes[i].hasMac && memcmp(reverseRoutes[i].macAddress, mac, 6) == 0) {
      if (now - reverseRoutes[i].lastSeenMs <= 1800000UL) {
        return reverseRoutes[i].prevHop;
      }
    }
  }
  return 0;
}

uint16_t PoleMeshRouter::lookupReverseHopByNode(uint16_t dstNodeId) const {
  if (dstNodeId == 0 || dstNodeId == MSCB_BROADCAST_NODE_ID) return 0;
  uint32_t now = millis();
  for (uint8_t i = 0; i < MESH_REVERSE_ROUTE_SIZE; i++) {
    if (reverseRoutes[i].srcNodeId == dstNodeId && reverseRoutes[i].prevHop != 0) {
      if (now - reverseRoutes[i].lastSeenMs <= 1800000UL) {
        return reverseRoutes[i].prevHop;
      }
    }
  }
  return 0;
}
