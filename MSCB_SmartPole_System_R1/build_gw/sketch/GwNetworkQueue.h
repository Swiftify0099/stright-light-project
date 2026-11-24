#line 1 "C:\\Users\\panka\\OneDrive\\Desktop\\PS\\MSCB_SmartPole_System_R1\\Gateway_ESP32S3\\GwNetworkQueue.h"
/**
 * @file GwNetworkQueue.h
 * @brief MSCB Smart Pole Gateway - Bounded Network-Wide Command Queue & Batch Runner
 * @version 1.0.0
 * 
 * Enforces:
 * - Bounded queue for network-wide operations up to 50 active poles (300 feeders).
 * - Rate-limited, concurrency-controlled dispatch (max 3 in-flight commands).
 * - Multi-feeder ACK aggregation and per-pole timeout tracking.
 * - Truthful progress and completion reporting to Telegram admin.
 */

#ifndef GW_NETWORK_QUEUE_H
#define GW_NETWORK_QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#if defined(ARDUINO)
  #include <Arduino.h>
#else
  #include "../Tests/ArduinoMock.h"
#endif

#include "GwConfig.h"
#include "GwNodeRegistry.h"
#if __has_include("../MSCBShared/Protocol.h")
  #include "../MSCBShared/Protocol.h"
  #include "../MSCBShared/NetworkTypes.h"
#elif __has_include(<Protocol.h>)
  #include <Protocol.h>
  #include <NetworkTypes.h>
#else
  #include "Protocol.h"
  #include "NetworkTypes.h"
#endif

enum NetworkTaskState : uint8_t {
  NET_TASK_IDLE        = 0,
  NET_TASK_QUEUED      = 1,
  NET_TASK_DISPATCHED  = 2,
  NET_TASK_CONFIRMED   = 3, // All expected feeders output confirmed
  NET_TASK_PARTIAL     = 4, // Some confirmed, some timed out/faulted/rejected
  NET_TASK_FAILED      = 5, // All rejected or fault
  NET_TASK_TIMEOUT     = 6, // No response
  NET_TASK_OFFLINE     = 7  // Node was offline
};

inline const char* networkTaskStateToString(NetworkTaskState s) {
  switch (s) {
    case NET_TASK_IDLE: return "IDLE";
    case NET_TASK_QUEUED: return "QUEUED";
    case NET_TASK_DISPATCHED: return "DISPATCHED";
    case NET_TASK_CONFIRMED: return "CONFIRMED";
    case NET_TASK_PARTIAL: return "PARTIAL";
    case NET_TASK_FAILED: return "FAILED";
    case NET_TASK_TIMEOUT: return "TIMEOUT";
    case NET_TASK_OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
  }
}

struct NetworkPoleTask {
  uint16_t         poleId;
  uint8_t          action;              // 0=OFF, 1=ON
  uint8_t          feederId;            // 0=ALL, 1..6
  uint8_t          expectedMask;        // e.g. 0x3F
  uint32_t         seqNumber;
  NetworkTaskState state;
  uint32_t         dispatchTimeMs;
  uint8_t          retries;
  uint8_t          receivedAckMask;
  uint8_t          terminalAckMask;
  uint8_t          outputConfirmedMask;
  uint8_t          auxConfirmedMask;
  uint8_t          mcbFaultMask;
  uint8_t          rejectedMask;
};

class GwNetworkQueue {
public:
  GwNetworkQueue();

  bool begin();
  void service(); // Periodic dispatch & timeout manager

  // Start network-wide batch command (ALL POLES, ALL FEEDERS ON or OFF)
  bool startNetworkBatch(uint8_t action, int64_t adminChatId);

  // Incoming ACK hook from mesh
  void onFeederAck(const PktFeederAckPayload &ack);

  // Status query
  bool isInProgress() const { return inProgress; }
  uint8_t getAction() const { return batchAction; }
  uint8_t getTotalPoles() const { return totalPoles; }
  uint8_t getDispatchedCount() const { return dispatchedCount; }
  uint8_t getCompletedCount() const { return completedCount; }
  uint8_t getPartialCount() const { return partialCount; }
  uint8_t getFailedCount() const { return failedCount; }
  const NetworkPoleTask* getTask(uint8_t idx) const {
    if (idx < totalPoles) return &tasks[idx];
    return nullptr;
  }

  // Cancel in-flight batch
  void cancelBatch();

private:
  void dispatchNext();
  void checkTimeouts();
  void finalizeBatch();

  bool              inProgress;
  uint8_t           batchAction;
  int64_t           adminChatId;
  uint8_t           totalPoles;
  uint8_t           dispatchedCount;
  uint8_t           completedCount;
  uint8_t           partialCount;
  uint8_t           failedCount;
  uint32_t          startMs;
  uint32_t          lastDispatchMs;
  uint8_t           inFlightCount;

  NetworkPoleTask   tasks[GW_MAX_MANAGED_POLES];
};

extern GwNetworkQueue gwNetworkQueue;

#endif // GW_NETWORK_QUEUE_H
