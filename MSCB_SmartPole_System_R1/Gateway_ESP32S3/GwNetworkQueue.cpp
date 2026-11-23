/**
 * @file GwNetworkQueue.cpp
 * @brief MSCB Smart Pole Gateway - Bounded Network-Wide Command Queue & Batch Runner Implementation
 * @version 1.0.0
 */

#include "GwNetworkQueue.h"
#include "GwMeshCoordinator.h"
#include "GwTelegramBot.h"
#include <string.h>
#include <stdio.h>

GwNetworkQueue gwNetworkQueue;

GwNetworkQueue::GwNetworkQueue() :
  inProgress(false),
  batchAction(0),
  adminChatId(0),
  totalPoles(0),
  dispatchedCount(0),
  completedCount(0),
  partialCount(0),
  failedCount(0),
  startMs(0),
  lastDispatchMs(0),
  inFlightCount(0)
{
  memset(tasks, 0, sizeof(tasks));
}

bool GwNetworkQueue::begin() {
  inProgress = false;
  return true;
}

bool GwNetworkQueue::startNetworkBatch(uint8_t action, int64_t adminChatId) {
  if (inProgress) {
    return false; // Batch already running
  }

  uint8_t count = gwRegistry.getActiveCount();
  if (count == 0) {
    return false;
  }

  this->inProgress = true;
  this->batchAction = action;
  this->adminChatId = adminChatId;
  this->totalPoles = (count <= GW_MAX_MANAGED_POLES) ? count : GW_MAX_MANAGED_POLES;
  this->dispatchedCount = 0;
  this->completedCount = 0;
  this->partialCount = 0;
  this->failedCount = 0;
  this->inFlightCount = 0;
  this->startMs = millis();
  this->lastDispatchMs = 0;

  memset(tasks, 0, sizeof(tasks));

  for (uint8_t i = 0; i < this->totalPoles; i++) {
    const RegisteredPole* p = gwRegistry.getPoleByIndex(i);
    if (!p) continue;
    tasks[i].poleId = p->poleId;
    tasks[i].action = action;
    tasks[i].feederId = 0x00; // ALL feeders
    tasks[i].expectedMask = 0x3F; // Feeders 1..6
    tasks[i].seqNumber = 0;
    tasks[i].state = NET_TASK_QUEUED;
    tasks[i].dispatchTimeMs = 0;
    tasks[i].retries = 0;
    tasks[i].receivedAckMask = 0;
    tasks[i].terminalAckMask = 0;
    tasks[i].outputConfirmedMask = 0;
    tasks[i].auxConfirmedMask = 0;
    tasks[i].mcbFaultMask = 0;
    tasks[i].rejectedMask = 0;
  }

  // Initial dispatch
  dispatchNext();
  return true;
}

void GwNetworkQueue::dispatchNext() {
  if (!inProgress) return;

  uint32_t now = millis();
  // Rate limiting: at least 400ms between mesh dispatches, max 3 in flight
  while (inFlightCount < 3 && dispatchedCount < totalPoles) {
    if (lastDispatchMs != 0 && (now - lastDispatchMs < 400)) {
      break;
    }

    uint8_t nextIdx = dispatchedCount;
    if (tasks[nextIdx].state != NET_TASK_QUEUED) {
      dispatchedCount++;
      continue;
    }

    uint32_t seq = 0;
    bool sent = gwMesh.sendFeederCommand(tasks[nextIdx].poleId,
                                         tasks[nextIdx].feederId,
                                         tasks[nextIdx].action,
                                         tasks[nextIdx].expectedMask,
                                         seq,
                                         adminChatId);
    if (sent) {
      tasks[nextIdx].seqNumber = seq;
      tasks[nextIdx].state = NET_TASK_DISPATCHED;
      tasks[nextIdx].dispatchTimeMs = now;
      inFlightCount++;
      dispatchedCount++;
      lastDispatchMs = now;
      Serial.printf("[NET_QUEUE] Dispatched batch pole 0x%04X (Seq %lu) [%u/%u]\n",
                    tasks[nextIdx].poleId, (unsigned long)seq, dispatchedCount, totalPoles);
    } else {
      // Failed to send downlink packet
      tasks[nextIdx].state = NET_TASK_FAILED;
      dispatchedCount++;
      failedCount++;
      Serial.printf("[NET_QUEUE] Failed to dispatch batch pole 0x%04X\n", tasks[nextIdx].poleId);
    }
  }
}

void GwNetworkQueue::checkTimeouts() {
  if (!inProgress) return;

  uint32_t now = millis();
  for (uint8_t i = 0; i < totalPoles; i++) {
    if (tasks[i].state == NET_TASK_DISPATCHED) {
      // 12 second timeout for staggered 6-feeder sequence
      if (now - tasks[i].dispatchTimeMs >= 12000) {
        if (tasks[i].outputConfirmedMask > 0) {
          tasks[i].state = NET_TASK_PARTIAL;
          partialCount++;
          if (inFlightCount > 0) inFlightCount--;
          Serial.printf("[NET_QUEUE] Pole 0x%04X partial timeout (confirmed mask 0x%02X)\n",
                        tasks[i].poleId, tasks[i].outputConfirmedMask);
        } else if (tasks[i].retries < 1) {
          // Retry once
          tasks[i].retries++;
          uint32_t seq = 0;
          bool sent = gwMesh.sendFeederCommand(tasks[i].poleId,
                                               tasks[i].feederId,
                                               tasks[i].action,
                                               tasks[i].expectedMask,
                                               seq,
                                               adminChatId);
          if (sent) {
            tasks[i].seqNumber = seq;
            tasks[i].dispatchTimeMs = now;
            Serial.printf("[NET_QUEUE] Retrying pole 0x%04X (Seq %lu)\n",
                          tasks[i].poleId, (unsigned long)seq);
          } else {
            tasks[i].state = NET_TASK_TIMEOUT;
            failedCount++;
            if (inFlightCount > 0) inFlightCount--;
          }
        } else {
          tasks[i].state = NET_TASK_TIMEOUT;
          failedCount++;
          if (inFlightCount > 0) inFlightCount--;
          Serial.printf("[NET_QUEUE] Pole 0x%04X timed out completely\n", tasks[i].poleId);
        }
      }
    }
  }
}

void GwNetworkQueue::onFeederAck(const PktFeederAckPayload &ack) {
  if (!inProgress) return;

  for (uint8_t i = 0; i < totalPoles; i++) {
    if (tasks[i].state == NET_TASK_DISPATCHED &&
        tasks[i].poleId == ack.poleId &&
        tasks[i].seqNumber == ack.sequenceNumber) {

      uint8_t fBit = 0;
      if (ack.feederId >= 1 && ack.feederId <= 6) {
        fBit = (1 << (ack.feederId - 1));
      } else if (ack.feederId == 0) {
        fBit = tasks[i].expectedMask;
      }

      tasks[i].receivedAckMask |= fBit;

      if (ack.ackState == FEEDER_ACK_COMPLETED ||
          ack.ackState == FEEDER_ACK_OUTPUT_CHANGED ||
          ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
          ack.ackState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED) {
        tasks[i].outputConfirmedMask |= fBit;
        if (ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED) {
          tasks[i].auxConfirmedMask |= fBit;
        }
      }

      if ((ack.currentMcbMask & fBit) != 0) {
        tasks[i].mcbFaultMask |= fBit;
      }
      if (ack.ackState >= FEEDER_ACK_REJECTED_STALE) {
        tasks[i].rejectedMask |= fBit;
      }

      if (ack.ackState == FEEDER_ACK_COMPLETED ||
          ack.ackState == FEEDER_ACK_OUTPUT_CHANGED ||
          ack.ackState == FEEDER_ACK_AUXILIARY_FEEDBACK_VERIFIED ||
          ack.ackState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED ||
          ack.ackState >= FEEDER_ACK_REJECTED_STALE) {
        tasks[i].terminalAckMask |= fBit;
      }

      // Check if all expected feeders have terminal responses
      if ((tasks[i].terminalAckMask & tasks[i].expectedMask) == tasks[i].expectedMask) {
        if (inFlightCount > 0) inFlightCount--;

        if ((tasks[i].outputConfirmedMask & tasks[i].expectedMask) == tasks[i].expectedMask &&
            tasks[i].rejectedMask == 0) {
          tasks[i].state = NET_TASK_CONFIRMED;
          completedCount++;
        } else if (tasks[i].outputConfirmedMask > 0) {
          tasks[i].state = NET_TASK_PARTIAL;
          partialCount++;
        } else {
          tasks[i].state = NET_TASK_FAILED;
          failedCount++;
        }
        Serial.printf("[NET_QUEUE] Pole 0x%04X completed batch task -> %s\n",
                      tasks[i].poleId, networkTaskStateToString(tasks[i].state));
      }
      break;
    }
  }
}

void GwNetworkQueue::service() {
  if (!inProgress) return;

  checkTimeouts();
  dispatchNext();

  // Check if batch is completely finished
  if (completedCount + partialCount + failedCount >= totalPoles) {
    finalizeBatch();
  }
}

void GwNetworkQueue::finalizeBatch() {
  inProgress = false;
  uint32_t durationSec = (millis() - startMs) / 1000UL;

  Serial.printf("[NET_QUEUE] Batch finished in %lu s: %u confirmed, %u partial, %u failed of %u poles\n",
                (unsigned long)durationSec, completedCount, partialCount, failedCount, totalPoles);

  if (adminChatId != 0) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "📊 *NETWORK-WIDE ALL FEEDERS %s REPORT*\n"
             "━━━━━━━━━━━━━━━━━━━━━━\n"
             "• Total Poles Targeted: *%u*\n"
             "• Confirmed Successful: *%u*\n"
             "• Partial Success: *%u*\n"
             "• Failed / Timed Out: *%u*\n"
             "• Duration: *%lu seconds*\n"
             "━━━━━━━━━━━━━━━━━━━━━━\n",
             (batchAction == 1) ? "ON" : "OFF",
             totalPoles, completedCount, partialCount, failedCount,
             (unsigned long)durationSec);

    if (failedCount > 0 || partialCount > 0) {
      strncat(buf, "⚠️ *Incomplete Poles:*\n", sizeof(buf) - strlen(buf) - 1);
      for (uint8_t i = 0; i < totalPoles; i++) {
        if (tasks[i].state != NET_TASK_CONFIRMED) {
          char line[64];
          snprintf(line, sizeof(line), "• Pole 0x%04X: %s\n",
                   tasks[i].poleId, networkTaskStateToString(tasks[i].state));
          strncat(buf, line, sizeof(buf) - strlen(buf) - 1);
        }
      }
    } else {
      strncat(buf, "✅ All registered poles switched successfully.\n", sizeof(buf) - strlen(buf) - 1);
    }

    gwTelegram.sendMessage(adminChatId, buf);
  }
}

void GwNetworkQueue::cancelBatch() {
  if (!inProgress) return;
  inProgress = false;
  Serial.println(F("[NET_QUEUE] Batch operation cancelled by user."));
  if (adminChatId != 0) {
    gwTelegram.sendMessage(adminChatId, "⛔ Network-wide batch operation cancelled.");
  }
}
