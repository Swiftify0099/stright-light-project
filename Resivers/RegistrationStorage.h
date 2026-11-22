#ifndef REGISTRATION_STORAGE_H
#define REGISTRATION_STORAGE_H

#include "RegistrationTypes.h"

#ifdef ARDUINO
  #include <Preferences.h>
#endif

class RegistrationStorage {
public:
  RegistrationStorage();

  bool init();
  bool loadRegistry(MscbNodeRecord *records, uint8_t &outCount, uint8_t maxCapacity);
  bool saveNodeRecord(uint8_t slot, const MscbNodeRecord &record);
  bool readNodeRecord(uint8_t slot, MscbNodeRecord &outRecord);
  bool saveRevocationList(const char revokedList[16][17], uint8_t count);
  bool loadRevocationList(char revokedList[16][17], uint8_t &outCount);
  void clearAll();

  // Pole-side specific persistent state
  bool savePoleNodeState(const MscbNodeRecord &node);
  bool loadPoleNodeState(MscbNodeRecord &outNode);

private:
#ifdef ARDUINO
  Preferences prefs;
#else
  // Host simulation storage buffer (static to simulate global flash NVS)
  static uint8_t mockStorage[MSCB_MAX_REGISTRY_NODES][sizeof(MscbNodeRecord)];
  static bool    mockSlotValid[MSCB_MAX_REGISTRY_NODES];
  static uint8_t mockCount;
  static char    mockRevoked[16][17];
  static uint8_t mockRevokedCount;
  static MscbNodeRecord mockPoleState;
  static bool    mockPoleStateValid;
#endif

  uint32_t calculateRecordCrc(const MscbNodeRecord &rec);
};

#endif // REGISTRATION_STORAGE_H
