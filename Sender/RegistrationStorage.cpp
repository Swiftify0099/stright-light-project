#include "RegistrationStorage.h"

#define REG_NVS_NAMESPACE     "mscb_reg"
#define REG_KEY_COUNT         "cnt"
#define REG_KEY_MAGIC         "mgc"
#define REG_KEY_VER           "ver"
#define REG_KEY_REV_CNT       "rev_cnt"
#define REG_KEY_POLE_STATE    "pole_rec"

#ifndef ARDUINO
uint8_t RegistrationStorage::mockStorage[MSCB_MAX_REGISTRY_NODES][sizeof(MscbNodeRecord)] = {{0}};
bool    RegistrationStorage::mockSlotValid[MSCB_MAX_REGISTRY_NODES] = {false};
uint8_t RegistrationStorage::mockCount = 0;
char    RegistrationStorage::mockRevoked[16][17] = {{0}};
uint8_t RegistrationStorage::mockRevokedCount = 0;
MscbNodeRecord RegistrationStorage::mockPoleState = {};
bool    RegistrationStorage::mockPoleStateValid = false;
#endif

RegistrationStorage::RegistrationStorage() {}

uint32_t RegistrationStorage::calculateRecordCrc(const MscbNodeRecord &rec) {
  // Compute CRC32 over all bytes except the final crc32 field
  size_t crcLen = sizeof(MscbNodeRecord) - sizeof(uint32_t);
  return mscbCalculateCrc32((const uint8_t*)&rec, crcLen);
}

bool RegistrationStorage::init() {
#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, false);
  uint32_t magic = prefs.getUInt(REG_KEY_MAGIC, 0);
  if (magic != MSCB_REG_MAGIC) {
    prefs.putUInt(REG_KEY_MAGIC, MSCB_REG_MAGIC);
    prefs.putUShort(REG_KEY_VER, MSCB_SCHEMA_VERSION);
    prefs.putUChar(REG_KEY_COUNT, 0);
    prefs.putUChar(REG_KEY_REV_CNT, 0);
  }
  prefs.end();
#endif
  return true;
}

bool RegistrationStorage::loadRegistry(MscbNodeRecord *records, uint8_t &outCount, uint8_t maxCapacity) {
  outCount = 0;
  if (!records || maxCapacity == 0) return false;

#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, true);
  uint32_t magic = prefs.getUInt(REG_KEY_MAGIC, 0);
  uint16_t ver   = prefs.getUShort(REG_KEY_VER, 0);

  if (magic != MSCB_REG_MAGIC || ver != MSCB_SCHEMA_VERSION) {
    prefs.end();
    return false;
  }

  uint8_t count = prefs.getUChar(REG_KEY_COUNT, 0);
  if (count > maxCapacity) count = maxCapacity;

  for (uint8_t i = 0; i < count; i++) {
    char key[8];
    snprintf(key, sizeof(key), "n%u", i);
    MscbNodeRecord temp;
    size_t readBytes = prefs.getBytes(key, &temp, sizeof(MscbNodeRecord));
    if (readBytes == sizeof(MscbNodeRecord)) {
      uint32_t expectedCrc = calculateRecordCrc(temp);
      if (temp.crc32 == expectedCrc) {
        records[outCount++] = temp;
      }
    }
  }
  prefs.end();
#else
  for (uint8_t i = 0; i < mockCount && outCount < maxCapacity; i++) {
    if (mockSlotValid[i]) {
      MscbNodeRecord temp;
      memcpy(&temp, mockStorage[i], sizeof(MscbNodeRecord));
      if (temp.crc32 == calculateRecordCrc(temp)) {
        records[outCount++] = temp;
      }
    }
  }
#endif
  return true;
}

bool RegistrationStorage::saveNodeRecord(uint8_t slot, const MscbNodeRecord &record) {
  if (slot >= MSCB_MAX_REGISTRY_NODES) return false;

  MscbNodeRecord recCopy = record;
  recCopy.crc32 = calculateRecordCrc(recCopy);

#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, false);
  char key[8];
  snprintf(key, sizeof(key), "n%u", slot);

  size_t written = prefs.putBytes(key, &recCopy, sizeof(MscbNodeRecord));
  if (written != sizeof(MscbNodeRecord)) {
    prefs.end();
    return false;
  }

  // Safe readback verification to handle power fluctuation/NVS faults
  MscbNodeRecord verifyBuf;
  size_t readBack = prefs.getBytes(key, &verifyBuf, sizeof(MscbNodeRecord));
  if (readBack != sizeof(MscbNodeRecord) || verifyBuf.crc32 != recCopy.crc32) {
    prefs.end();
    return false;
  }

  uint8_t currentCount = prefs.getUChar(REG_KEY_COUNT, 0);
  if (slot >= currentCount) {
    prefs.putUChar(REG_KEY_COUNT, slot + 1);
  }
  prefs.end();
#else
  memcpy(mockStorage[slot], &recCopy, sizeof(MscbNodeRecord));
  mockSlotValid[slot] = true;
  if (slot >= mockCount) mockCount = slot + 1;
#endif
  return true;
}

bool RegistrationStorage::readNodeRecord(uint8_t slot, MscbNodeRecord &outRecord) {
  if (slot >= MSCB_MAX_REGISTRY_NODES) return false;

#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, true);
  char key[8];
  snprintf(key, sizeof(key), "n%u", slot);
  size_t readBytes = prefs.getBytes(key, &outRecord, sizeof(MscbNodeRecord));
  prefs.end();

  if (readBytes != sizeof(MscbNodeRecord)) return false;
  return (outRecord.crc32 == calculateRecordCrc(outRecord));
#else
  if (!mockSlotValid[slot]) return false;
  memcpy(&outRecord, mockStorage[slot], sizeof(MscbNodeRecord));
  return (outRecord.crc32 == calculateRecordCrc(outRecord));
#endif
}

bool RegistrationStorage::saveRevocationList(const char revokedList[16][17], uint8_t count) {
  if (count > 16) count = 16;
#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, false);
  prefs.putUChar(REG_KEY_REV_CNT, count);
  for (uint8_t i = 0; i < count; i++) {
    char key[8];
    snprintf(key, sizeof(key), "rv%u", i);
    prefs.putString(key, revokedList[i]);
  }
  prefs.end();
#else
  mockRevokedCount = count;
  for (uint8_t i = 0; i < count; i++) {
    strncpy(mockRevoked[i], revokedList[i], 16);
    mockRevoked[i][16] = '\0';
  }
#endif
  return true;
}

bool RegistrationStorage::loadRevocationList(char revokedList[16][17], uint8_t &outCount) {
  outCount = 0;
#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, true);
  uint8_t count = prefs.getUChar(REG_KEY_REV_CNT, 0);
  if (count > 16) count = 16;
  for (uint8_t i = 0; i < count; i++) {
    char key[8];
    snprintf(key, sizeof(key), "rv%u", i);
    String s = prefs.getString(key, "");
    strncpy(revokedList[outCount], s.c_str(), 16);
    revokedList[outCount][16] = '\0';
    outCount++;
  }
  prefs.end();
#else
  outCount = mockRevokedCount;
  for (uint8_t i = 0; i < mockRevokedCount; i++) {
    strncpy(revokedList[i], mockRevoked[i], 16);
    revokedList[i][16] = '\0';
  }
#endif
  return true;
}

void RegistrationStorage::clearAll() {
#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, false);
  prefs.clear();
  prefs.putUInt(REG_KEY_MAGIC, MSCB_REG_MAGIC);
  prefs.putUShort(REG_KEY_VER, MSCB_SCHEMA_VERSION);
  prefs.putUChar(REG_KEY_COUNT, 0);
  prefs.putUChar(REG_KEY_REV_CNT, 0);
  prefs.end();
#else
  mockCount = 0;
  mockRevokedCount = 0;
  mockPoleStateValid = false;
  for (int i = 0; i < MSCB_MAX_REGISTRY_NODES; i++) {
    mockSlotValid[i] = false;
  }
#endif
}

bool RegistrationStorage::savePoleNodeState(const MscbNodeRecord &node) {
  MscbNodeRecord recCopy = node;
  recCopy.crc32 = calculateRecordCrc(recCopy);

#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, false);
  size_t written = prefs.putBytes(REG_KEY_POLE_STATE, &recCopy, sizeof(MscbNodeRecord));
  prefs.end();
  return (written == sizeof(MscbNodeRecord));
#else
  mockPoleState = recCopy;
  mockPoleStateValid = true;
  return true;
#endif
}

bool RegistrationStorage::loadPoleNodeState(MscbNodeRecord &outNode) {
#ifdef ARDUINO
  prefs.begin(REG_NVS_NAMESPACE, true);
  size_t readBytes = prefs.getBytes(REG_KEY_POLE_STATE, &outNode, sizeof(MscbNodeRecord));
  prefs.end();
  if (readBytes != sizeof(MscbNodeRecord)) return false;
  return (outNode.crc32 == calculateRecordCrc(outNode));
#else
  if (!mockPoleStateValid) return false;
  outNode = mockPoleState;
  return (outNode.crc32 == calculateRecordCrc(outNode));
#endif
}
