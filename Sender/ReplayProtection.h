/*
 * =====================================================================
 *  REPLAYPROTECTION.H — ANTI-REPLAY SLIDING WINDOW & SESSION TRACKER
 *  Municipal Smart Control Box (MSCB) Smart Pole System
 *  ESP32-S3 Gateway & ESP32 Secondary Nodes
 * =====================================================================
 */

#ifndef MSCB_REPLAY_PROTECTION_H
#define MSCB_REPLAY_PROTECTION_H

#include "NetworkTypes.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MSCB_REPLAY_WINDOW_BITS 64

typedef struct {
    uint16_t node_id;
    uint32_t last_boot_session;
    uint32_t max_seq;
    uint64_t window_mask;
    bool     initialized;
} MscbNodeReplayState;

class ReplayProtection {
public:
    ReplayProtection();
    void init();
    void reset();

    // Step 1: Pre-check if frame is valid or replayed before expensive decryption.
    // Returns true if packet sequence is acceptable, false if duplicate or old.
    bool checkReplay(uint16_t nodeId, uint32_t bootSession, uint32_t seq);

    // Step 2: Commit sequence into sliding window only after AEAD authentication succeeds.
    bool commit(uint16_t nodeId, uint32_t bootSession, uint32_t seq);

    // Access node replay state for telemetry/monitoring
    const MscbNodeReplayState* getNodeState(uint16_t nodeId) const;

private:
    MscbNodeReplayState table[MSCB_MAX_REGISTERED_POLES + 2];
    int findOrAllocateIndex(uint16_t nodeId);
};

extern ReplayProtection replayProtector;

#endif // MSCB_REPLAY_PROTECTION_H
