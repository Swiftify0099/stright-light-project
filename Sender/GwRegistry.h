#ifndef GW_REGISTRY_H
#define GW_REGISTRY_H

#include "RegistrationTypes.h"
#include "RegistrationStorage.h"

// Nonce & Replay Cache
struct NonceCacheEntry {
  char     hwId[17];
  uint32_t nonce;
  uint32_t bootSessionId;
  uint32_t timestamp;
};

#define NONCE_CACHE_SIZE 32

class GwRegistry {
public:
  GwRegistry();

  void init(const uint8_t *masterCommissioningKey = nullptr, size_t keyLen = 0);

  // Core Join & Authentication Processing
  uint8_t processJoinRequest(const char *hwId, uint8_t nodeType, uint16_t fwVersion,
                             uint32_t bootSessionId, const char *priCandidate,
                             const char *bkpCandidate, uint32_t nodeNonce,
                             const char *authTag, int16_t rssi, int8_t snr,
                             char outAssignedLogicalId[16], char outPriParent[16],
                             char outBkpParent[16]);

  // Admin Approval API (Future Telegram Bot Hook)
  bool approveNode(const char *hwId, const char *logicalId, const char *friendlyName,
                   const char *area, const char *installInfo, const char *primaryParent,
                   const char *backupParent, const MscbFeederRecord *feederConfigs = nullptr);

  bool rejectNode(const char *hwId, uint8_t reason);
  bool revokeNode(const char *hwId, const char *reason);

  // Node Activation Confirmation (Completes state transition APPROVED -> ACTIVE)
  bool confirmNodeActivation(const char *logicalId, uint32_t sessionToken);

  // Parent Selection & Load Balancing
  bool evaluateAndAssignParents(const char *priCand, const char *bkpCand,
                                char outPri[16], char outBkp[16]);
  uint8_t getNodeChildLoad(const char *nodeLogicalId) const;

  // Query & Lookup API
  int  findNodeByHwId(const char *hwId) const;
  int  findNodeByLogicalId(const char *logicalId) const;
  MscbNodeRecord* getNode(uint8_t index);
  uint8_t getNodeCount() const;
  bool isRegistryFull() const;

  // Pending Approval Queue
  uint8_t getPendingCount() const;
  MscbPendingRegistration* getPending(uint8_t index);
  int  findPendingByHwId(const char *hwId) const;
  void purgeExpiredPending(uint32_t currentMillis);

  // Revocation List
  bool isDeviceRevoked(const char *hwId) const;
  uint8_t getRevokedCount() const;
  const char* getRevokedHwId(uint8_t index) const;

  // Logical ID Generator (Enforces uniqueness, format "POLE-0001" to "POLE-0050")
  bool generateUniqueLogicalId(uint8_t nodeType, char outId[16]);

  // Key derivation for a device: K_dev = HMAC(MasterKey, HwId)
  bool deriveDeviceKey(const char *hwId, uint8_t outKey[32]);

  // Activity & Health
  void updateNodeLastSeen(const char *idOrHw, int16_t rssi, int8_t snr);
  void checkNodeTimeouts(uint32_t currentMillis, uint32_t timeoutMs);

private:
  RegistrationStorage storage;

  MscbNodeRecord          nodes[MSCB_MAX_REGISTRY_NODES];
  uint8_t                 nodeCount;

  MscbPendingRegistration pending[MSCB_MAX_PENDING_APPROVALS];
  uint8_t                 pendingCount;

  char                    revokedList[16][17];
  uint8_t                 revokedCount;

  NonceCacheEntry         nonceCache[NONCE_CACHE_SIZE];
  uint8_t                 nonceHead;

  uint8_t                 masterKey[32];
  size_t                  masterKeyLen;

  uint16_t                nextPoleIndex;
  uint16_t                nextFeederIndex;

  bool isNonceReplayed(const char *hwId, uint32_t nonce, uint32_t bootSessionId);
  void recordNonce(const char *hwId, uint32_t nonce, uint32_t bootSessionId);
};

extern GwRegistry gwRegistry;

#endif // GW_REGISTRY_H
