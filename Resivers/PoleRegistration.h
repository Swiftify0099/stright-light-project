#ifndef POLE_REGISTRATION_H
#define POLE_REGISTRATION_H

#include "RegistrationTypes.h"
#include "RegistrationStorage.h"

struct NeighborCandidate {
  char    nodeId[16];
  uint8_t hopCount;
  int16_t rssi;
  int8_t  snr;
  bool    isGatewayRooted;
  uint32_t lastSeen;
};

#define MAX_CANDIDATES 6

class PoleRegistration {
public:
  PoleRegistration();

  void init(const uint8_t *customDevKey = nullptr, size_t keyLen = 0);
  void update(uint32_t currentMillis);

  // Neighbor Beacon Processing (Discovery Mode)
  void onBeaconReceived(const char *srcId, bool isGatewayRooted, uint8_t hopCount,
                        int16_t rssi, int8_t snr);

  // Packet Handlers
  void onJoinAckReceived(uint8_t rejectReason);
  bool onJoinAcceptReceived(const char *logicalId, const char *priParent,
                            const char *bkpParent, uint16_t configVersion,
                            uint32_t sessionToken, const MscbFeederRecord *feeders);
  void onJoinRejectReceived(uint8_t reason);
  void onRevocationReceived();

  // State & Privileges
  uint8_t getState() const;
  bool isRelayControlPermitted() const;
  const char* getLogicalId() const;
  const char* getHwId() const;
  const char* getPrimaryParent() const;
  const char* getBackupParent() const;
  uint32_t getBootSessionId() const;

  // Build JOIN_REQUEST packet
  bool buildJoinRequest(char outPacket[256], uint32_t currentMillis);

  // Force trigger or test reset
  void forceFactoryReset();
  void setHwId(const char *newHwId);

private:
  RegistrationStorage storage;
  MscbNodeRecord      nodeRecord;
  uint8_t             regState;
  bool                relayLocked;

  char                hwId[17];
  uint32_t            bootSessionId;
  uint32_t            currentNonce;
  uint8_t             devKey[32];

  NeighborCandidate   candidates[MAX_CANDIDATES];
  uint8_t             candidateCount;

  char                selectedPriParent[16];
  char                selectedBkpParent[16];

  uint32_t            stateTimer;
  uint8_t             joinRetries;

  void generateHwId();
  void selectBestParents();
  void deriveLocalDeviceKey();
};

extern PoleRegistration poleReg;

#endif // POLE_REGISTRATION_H
