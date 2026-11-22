#include "PoleRegistration.h"
#include <stdio.h>
#include <stdlib.h>

PoleRegistration poleReg;

PoleRegistration::PoleRegistration() :
  regState(REG_STATE_FACTORY), relayLocked(true),
  bootSessionId(1001), currentNonce(1), candidateCount(0),
  stateTimer(0), joinRetries(0)
{
  hwId[0] = '\0';
  selectedPriParent[0] = '\0';
  selectedBkpParent[0] = '\0';
  memset(&nodeRecord, 0, sizeof(MscbNodeRecord));
  for (int i = 0; i < MAX_CANDIDATES; i++) {
    candidates[i].nodeId[0] = '\0';
    candidates[i].hopCount = 255;
    candidates[i].rssi = -120;
    candidates[i].snr = 0;
    candidates[i].isGatewayRooted = false;
    candidates[i].lastSeen = 0;
  }
}

void PoleRegistration::generateHwId() {
#ifdef ARDUINO
  uint64_t mac = ESP.getEfuseMac();
  snprintf(hwId, sizeof(hwId), "%04X%04X%04X",
           (uint16_t)(mac >> 32),
           (uint16_t)((mac >> 16) & 0xFFFF),
           (uint16_t)(mac & 0xFFFF));
#else
  if (hwId[0] == '\0') {
    strncpy(hwId, "A4CF12E834B0", 16);
    hwId[16] = '\0';
  }
#endif
}

void PoleRegistration::deriveLocalDeviceKey() {
  const char *defaultMaster = "MSCB_MASTER_ROOT_KEY_2026_SEC";
  char tagHex[17];
  mscbComputeAuthTag((const uint8_t*)defaultMaster, strlen(defaultMaster),
                     (const uint8_t*)hwId, strlen(hwId),
                     tagHex);
  for (int i = 0; i < 32; i++) {
    devKey[i] = (uint8_t)(tagHex[i % 16] ^ (i * 0x5A));
  }
}

void PoleRegistration::init(const uint8_t *customDevKey, size_t keyLen) {
  generateHwId();
  if (customDevKey && keyLen > 0) {
    size_t copyLen = (keyLen > 32) ? 32 : keyLen;
    memcpy(devKey, customDevKey, copyLen);
  } else {
    deriveLocalDeviceKey();
  }

  // Generate pseudo-random boot session ID
#ifdef ARDUINO
  bootSessionId = (uint32_t)esp_random();
#else
  bootSessionId = 0x5A110001;
#endif

  storage.init();
  bool loaded = storage.loadPoleNodeState(nodeRecord);

  if (loaded && (nodeRecord.regState == REG_STATE_ACTIVE || nodeRecord.regState == REG_STATE_APPROVED)) {
    regState = REG_STATE_ACTIVE;
    relayLocked = false;
    strncpy(selectedPriParent, nodeRecord.primaryParent, 15);
    strncpy(selectedBkpParent, nodeRecord.backupParent, 15);
#ifdef ARDUINO
    Serial.printf("[POLE_REG] Resuming registered state. ID: %s, PriParent: %s\n",
                  nodeRecord.logicalId, nodeRecord.primaryParent);
#endif
  } else if (loaded && nodeRecord.regState == REG_STATE_REVOKED) {
    regState = REG_STATE_REVOKED;
    relayLocked = true;
  } else {
    regState = REG_STATE_DISCOVERING;
    relayLocked = true;
    stateTimer = 0;
    joinRetries = 0;
#ifdef ARDUINO
    Serial.printf("[POLE_REG] Starting in DISCOVERING mode. HW ID: %s\n", hwId);
#endif
  }
}

void PoleRegistration::onBeaconReceived(const char *srcId, bool isGatewayRooted,
                                       uint8_t hopCount, int16_t rssi, int8_t snr) {
  if (!srcId || strlen(srcId) == 0) return;
  if (!isGatewayRooted && strcmp(srcId, "GATEWAY") != 0) return;

  // Update existing candidate or add new
  int slot = -1;
  for (uint8_t i = 0; i < candidateCount; i++) {
    if (strcmp(candidates[i].nodeId, srcId) == 0) {
      slot = i;
      break;
    }
  }

  if (slot < 0 && candidateCount < MAX_CANDIDATES) {
    slot = candidateCount++;
  }

  if (slot >= 0) {
    strncpy(candidates[slot].nodeId, srcId, 15);
    candidates[slot].nodeId[15] = '\0';
    candidates[slot].hopCount = (strcmp(srcId, "GATEWAY") == 0) ? 0 : hopCount;
    candidates[slot].rssi = rssi;
    candidates[slot].snr = snr;
    candidates[slot].isGatewayRooted = isGatewayRooted || (strcmp(srcId, "GATEWAY") == 0);
    candidates[slot].lastSeen = 0;
  }
}

void PoleRegistration::selectBestParents() {
  selectedPriParent[0] = '\0';
  selectedBkpParent[0] = '\0';

  if (candidateCount == 0) {
    // If no neighbors observed yet, default to direct Gateway attempt
    strncpy(selectedPriParent, "GATEWAY", 15);
    strncpy(selectedBkpParent, "NONE", 15);
    return;
  }

  int bestIdx = -1;
  int bestScore = -9999;

  for (uint8_t i = 0; i < candidateCount; i++) {
    if (!candidates[i].isGatewayRooted) continue;
    // Score based on hop count and signal strength (lower hops weighted heavily)
    int score = (10 - candidates[i].hopCount) * 100 + candidates[i].rssi;
    if (score > bestScore) {
      bestScore = score;
      bestIdx = i;
    }
  }

  if (bestIdx >= 0) {
    strncpy(selectedPriParent, candidates[bestIdx].nodeId, 15);
  } else {
    strncpy(selectedPriParent, "GATEWAY", 15);
  }

  // Backup parent candidate: next best independent candidate
  int secondIdx = -1;
  int secondScore = -9999;
  for (uint8_t i = 0; i < candidateCount; i++) {
    if (i == bestIdx) continue;
    if (!candidates[i].isGatewayRooted) continue;
    int score = (10 - candidates[i].hopCount) * 100 + candidates[i].rssi;
    if (score > secondScore) {
      secondScore = score;
      secondIdx = i;
    }
  }

  if (secondIdx >= 0) {
    strncpy(selectedBkpParent, candidates[secondIdx].nodeId, 15);
  } else {
    strncpy(selectedBkpParent, "NONE", 15);
  }
}

bool PoleRegistration::buildJoinRequest(char outPacket[256], uint32_t currentMillis) {
  selectBestParents();
  currentNonce++;

  char signPayload[128];
  snprintf(signPayload, sizeof(signPayload), "%s|%u|%u|%u|%s|%s|%u|%04X",
           hwId, DEV_TYPE_POLE, 11, bootSessionId,
           selectedPriParent, selectedBkpParent, currentNonce, MSCB_DEFAULT_NETWORK_ID);

  char authTag[17];
  mscbComputeAuthTag(devKey, 32, (const uint8_t*)signPayload, strlen(signPayload), authTag);

  // Payload: HW_ID|TYPE|FW_VER|BOOT_SESSION|PRI_PARENT|BKP_PARENT|NONCE|AUTH_TAG
  snprintf(outPacket, 256, "JOIN_REQ|%s|%u|%u|%u|%s|%s|%u|%s",
           hwId, DEV_TYPE_POLE, 11, bootSessionId,
           selectedPriParent, selectedBkpParent, currentNonce, authTag);

  return true;
}

void PoleRegistration::update(uint32_t currentMillis) {
  // Enforce Relay Safety Interlock during all non-ACTIVE states
  if (regState != REG_STATE_ACTIVE) {
    relayLocked = true;
  }

  switch (regState) {
    case REG_STATE_DISCOVERING:
      if (currentMillis - stateTimer >= 5000) {
        stateTimer = currentMillis;
        regState = REG_STATE_JOIN_REQUESTED;
        joinRetries = 0;
      }
      break;

    case REG_STATE_JOIN_REQUESTED:
      // Managed by LoRa transmission loops
      break;

    case REG_STATE_PENDING_APPROVAL:
      // Awaiting admin approval from Gateway
      break;

    case REG_STATE_ACTIVE:
      relayLocked = false;
      break;

    case REG_STATE_REJECTED:
    case REG_STATE_REVOKED:
      relayLocked = true;
      break;

    default:
      break;
  }
}

void PoleRegistration::onJoinAckReceived(uint8_t rejectReason) {
  if (rejectReason == REJECT_NONE) {
    regState = REG_STATE_PENDING_APPROVAL;
    relayLocked = true;
  } else {
    regState = REG_STATE_REJECTED;
    relayLocked = true;
  }
}

bool PoleRegistration::onJoinAcceptReceived(const char *logicalId, const char *priParent,
                                          const char *bkpParent, uint16_t configVersion,
                                          uint32_t sessionToken, const MscbFeederRecord *feeders) {
  if (!logicalId || strlen(logicalId) == 0) return false;

  strncpy(nodeRecord.logicalId, logicalId, 15);
  strncpy(nodeRecord.hwId, hwId, 16);
  nodeRecord.nodeType = DEV_TYPE_POLE;
  strncpy(nodeRecord.primaryParent, priParent ? priParent : "GATEWAY", 15);
  strncpy(nodeRecord.backupParent, bkpParent ? bkpParent : "NONE", 15);
  nodeRecord.configVersion = configVersion;
  nodeRecord.bootSessionId = bootSessionId;
  nodeRecord.feederCount = MSCB_FEEDERS_PER_POLE;
  nodeRecord.regState = REG_STATE_ACTIVE;

  // Initialize 6 feeder records if provided
  if (feeders) {
    for (uint8_t f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
      nodeRecord.feeders[f] = feeders[f];
    }
  } else {
    for (uint8_t f = 0; f < MSCB_FEEDERS_PER_POLE; f++) {
      nodeRecord.feeders[f].feederId = f + 1;
      snprintf(nodeRecord.feeders[f].name, sizeof(nodeRecord.feeders[f].name), "Feeder %u", f + 1);
      nodeRecord.feeders[f].enabled = true;
      nodeRecord.feeders[f].currentRating = 16.0f;
      nodeRecord.feeders[f].relayState = false;
      nodeRecord.feeders[f].mcbR = true;
      nodeRecord.feeders[f].mcbY = true;
      nodeRecord.feeders[f].mcbB = true;
      nodeRecord.feeders[f].contactor = false;
    }
  }

  // Persist to NVS
  bool saved = storage.savePoleNodeState(nodeRecord);
  if (saved) {
    regState = REG_STATE_ACTIVE;
    relayLocked = false;
    strncpy(selectedPriParent, nodeRecord.primaryParent, 15);
    strncpy(selectedBkpParent, nodeRecord.backupParent, 15);
    return true;
  }
  return false;
}

void PoleRegistration::onJoinRejectReceived(uint8_t reason) {
  regState = REG_STATE_REJECTED;
  relayLocked = true;
  nodeRecord.regState = REG_STATE_REJECTED;
  storage.savePoleNodeState(nodeRecord);
}

void PoleRegistration::onRevocationReceived() {
  regState = REG_STATE_REVOKED;
  relayLocked = true;
  nodeRecord.regState = REG_STATE_REVOKED;
  storage.savePoleNodeState(nodeRecord);
}

uint8_t PoleRegistration::getState() const { return regState; }
bool PoleRegistration::isRelayControlPermitted() const { return (regState == REG_STATE_ACTIVE && !relayLocked); }
const char* PoleRegistration::getLogicalId() const { return nodeRecord.logicalId; }
const char* PoleRegistration::getHwId() const { return hwId; }
const char* PoleRegistration::getPrimaryParent() const { return selectedPriParent; }
const char* PoleRegistration::getBackupParent() const { return selectedBkpParent; }
uint32_t PoleRegistration::getBootSessionId() const { return bootSessionId; }

void PoleRegistration::forceFactoryReset() {
  storage.clearAll();
  regState = REG_STATE_FACTORY;
  relayLocked = true;
  memset(&nodeRecord, 0, sizeof(MscbNodeRecord));
}

void PoleRegistration::setHwId(const char *newHwId) {
  if (newHwId) {
    strncpy(hwId, newHwId, 16);
    hwId[16] = '\0';
    deriveLocalDeviceKey();
  }
}
