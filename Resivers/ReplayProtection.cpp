/*
 * =====================================================================
 *  REPLAYPROTECTION.CPP — ANTI-REPLAY SLIDING WINDOW IMPLEMENTATION
 *  Municipal Smart Control Box (MSCB) Smart Pole System
 * =====================================================================
 */

#include "ReplayProtection.h"
#include <string.h>

ReplayProtection replayProtector;

ReplayProtection::ReplayProtection() {
    reset();
}

void ReplayProtection::init() {
    reset();
}

void ReplayProtection::reset() {
    memset(table, 0, sizeof(table));
}

int ReplayProtection::findOrAllocateIndex(uint16_t nodeId) {
    if (nodeId == MSCB_NODE_ID_UNASSIGNED || nodeId == MSCB_NODE_ID_BROADCAST) {
        return -1;
    }

    // Direct mapping if nodeId is within standard range (0x0001 .. 0x0033)
    if (nodeId <= (MSCB_MAX_REGISTERED_POLES + 1)) {
        return (int)nodeId;
    }

    // Fallback linear table search for non-contiguous IDs
    int freeIdx = -1;
    for (size_t i = 0; i < (MSCB_MAX_REGISTERED_POLES + 2); i++) {
        if (table[i].initialized && table[i].node_id == nodeId) {
            return (int)i;
        }
        if (!table[i].initialized && freeIdx == -1) {
            freeIdx = (int)i;
        }
    }
    return freeIdx;
}

const MscbNodeReplayState* ReplayProtection::getNodeState(uint16_t nodeId) const {
    if (nodeId <= (MSCB_MAX_REGISTERED_POLES + 1)) {
        if (table[nodeId].initialized) {
            return &table[nodeId];
        }
    }
    for (size_t i = 0; i < (MSCB_MAX_REGISTERED_POLES + 2); i++) {
        if (table[i].initialized && table[i].node_id == nodeId) {
            return &table[i];
        }
    }
    return NULL;
}

bool ReplayProtection::checkReplay(uint16_t nodeId, uint32_t bootSession, uint32_t seq) {
    int idx = findOrAllocateIndex(nodeId);
    if (idx < 0) return false;

    MscbNodeReplayState *entry = &table[idx];

    // First packet ever received from this node
    if (!entry->initialized) {
        return true;
    }

    // Case 1: Session comparison
    if (bootSession < entry->last_boot_session) {
        // Stale packet from prior power cycle
        return false;
    }

    if (bootSession > entry->last_boot_session) {
        // Node rebooted with new session; valid new session start
        return true;
    }

    // Case 2: Same session, check sequence number
    if (seq > entry->max_seq) {
        // Packet moves sequence forward; acceptable
        return true;
    }

    // Sequence is less than or equal to max seen
    uint32_t diff = entry->max_seq - seq;
    if (diff >= MSCB_REPLAY_WINDOW_BITS) {
        // Too far behind sliding window
        return false;
    }

    // Check if bit is already set (duplicate frame)
    if (entry->window_mask & ((uint64_t)1 << diff)) {
        return false;
    }

    // In-window out-of-order frame, not yet received
    return true;
}

bool ReplayProtection::commit(uint16_t nodeId, uint32_t bootSession, uint32_t seq) {
    int idx = findOrAllocateIndex(nodeId);
    if (idx < 0) return false;

    MscbNodeReplayState *entry = &table[idx];

    if (!entry->initialized || bootSession > entry->last_boot_session) {
        entry->node_id = nodeId;
        entry->last_boot_session = bootSession;
        entry->max_seq = seq;
        entry->window_mask = 1ULL;
        entry->initialized = true;
        return true;
    }

    if (bootSession < entry->last_boot_session) {
        return false;
    }

    if (seq > entry->max_seq) {
        uint32_t shift = seq - entry->max_seq;
        if (shift >= MSCB_REPLAY_WINDOW_BITS) {
            entry->window_mask = 1ULL;
        } else {
            entry->window_mask = (entry->window_mask << shift) | 1ULL;
        }
        entry->max_seq = seq;
        return true;
    }

    uint32_t diff = entry->max_seq - seq;
    if (diff < MSCB_REPLAY_WINDOW_BITS) {
        entry->window_mask |= ((uint64_t)1 << diff);
        return true;
    }

    return false;
}
