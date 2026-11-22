/**
 * @file test_task7_nonce_replay.cpp
 * @brief Independent Technical Verification: Persistent Nonce Reservation & Replay Defense
 */

#include "ArduinoMock.h"
#include "../MSCBShared/ReplayProtection.h"

int main() {
  std::cout << "======================================================================" << std::endl;
  std::cout << " TEST TASK 7: PERSISTENT NONCE RESERVATION & REPLAY DEFENSE AUDIT" << std::endl;
  std::cout << "======================================================================" << std::endl;

  // 1. Setup Persistent Flash Banks (Simulation of NVS ping-pong partition)
  PersistentNonceRecord flashBank0;
  PersistentNonceRecord flashBank1;
  memset(&flashBank0, 0xFF, sizeof(flashBank0)); // Blank flash
  memset(&flashBank1, 0xFF, sizeof(flashBank1));

  ReplayProtection transmitterNode;
  transmitterNode.initTransmitterStorage(&flashBank0, &flashBank1);

  std::cout << "--- TEST 1: INITIAL BOOT & PERSISTENT LEASE ALLOCATION ---" << std::endl;
  std::cout << "  Initial Leased Ceiling in NVS: " << transmitterNode.getCurrentCeiling() << std::endl;
  assert(transmitterNode.getCurrentCeiling() == 1000);

  // Consume some sequence numbers
  uint32_t seq1 = transmitterNode.getNextMonotonicSeq();
  uint32_t seq2 = transmitterNode.getNextMonotonicSeq();
  uint32_t seq3 = transmitterNode.getNextMonotonicSeq();
  std::cout << "  Issued Nonces: " << seq1 << ", " << seq2 << ", " << seq3 << std::endl;
  assert(seq1 == 0 && seq2 == 1 && seq3 == 2);

  // 2. Simulate Sudden Power Interruption / Brownout
  std::cout << "\n--- TEST 2: SUDDEN POWER INTERRUPTION / CRASH RECOVERY ---" << std::endl;
  transmitterNode.simulatePowerLoss();
  std::cout << "  [SIMULATION] Device power lost at sequence 2. Rebooting..." << std::endl;
  
  // Node reboots, calls initTransmitterStorage()
  transmitterNode.initTransmitterStorage(&flashBank0, &flashBank1);
  std::cout << "  Post-Reboot Leased Ceiling in NVS: " << transmitterNode.getCurrentCeiling() << std::endl;
  uint32_t postRebootSeq1 = transmitterNode.getNextMonotonicSeq();
  std::cout << "  First Nonce Issued After Reboot  : " << postRebootSeq1 << std::endl;

  // Crucial invariant: postRebootSeq1 MUST be >= previous ceiling (1000)
  // Guarantees ZERO duplicate nonces across power failure!
  assert(postRebootSeq1 >= 1000);
  assert(postRebootSeq1 > seq3);
  std::cout << "  VERIFICATION: Nonce uniqueness preserved across crash! (Old: 2, New: " << postRebootSeq1 << ")" << std::endl;

  // 3. Simulate NVS Bank Corruption (Power cut during write)
  std::cout << "\n--- TEST 3: NVS SINGLE-BANK CORRUPTION RECOVERY (PING-PONG) ---" << std::endl;
  // Corrupt Bank 0
  transmitterNode.simulateNvsCorruption(true, false);
  ReplayProtection corruptedRecoverNode;
  corruptedRecoverNode.initTransmitterStorage(&flashBank0, &flashBank1);
  uint32_t recoveredSeq = corruptedRecoverNode.getNextMonotonicSeq();
  std::cout << "  Bank 0 CRC corrupted. Recovery selected Bank 1 successfully." << std::endl;
  std::cout << "  Next Guaranteed Unique Sequence: " << recoveredSeq << std::endl;
  assert(recoveredSeq >= 2000);

  // 4. Receiver Anti-Replay Sliding Window Tests
  std::cout << "\n--- TEST 4: RECEIVER ANTI-REPLAY SLIDING WINDOW & RANDOM SESSION ---" << std::endl;
  ReplayProtection receiverGateway;
  receiverGateway.init();

  const uint16_t testNodeId = 0x0002;
  const uint32_t randomSession1 = 0x98A1B2C3; // Random 32-bit token
  const uint32_t keyEpoch1 = 1;

  receiverGateway.registerNewSession(testNodeId, randomSession1, keyEpoch1, 100);

  // Frame arriving in order
  assert(receiverGateway.checkPacket(testNodeId, randomSession1, keyEpoch1, 101) == REPLAY_OK_NEW_PACKET);
  receiverGateway.commitPacket(testNodeId, randomSession1, keyEpoch1, 101);

  // Duplicate frame arriving
  assert(receiverGateway.checkPacket(testNodeId, randomSession1, keyEpoch1, 101) == REPLAY_ERR_DUPLICATE_PACKET);
  std::cout << "  Duplicate Packet (seq 101): CORRECTLY REJECTED" << std::endl;

  // In-window out-of-order packet
  assert(receiverGateway.checkPacket(testNodeId, randomSession1, keyEpoch1, 105) == REPLAY_OK_NEW_PACKET);
  receiverGateway.commitPacket(testNodeId, randomSession1, keyEpoch1, 105);

  assert(receiverGateway.checkPacket(testNodeId, randomSession1, keyEpoch1, 103) == REPLAY_OK_NEW_PACKET);
  receiverGateway.commitPacket(testNodeId, randomSession1, keyEpoch1, 103);

  // Duplicate of out-of-order packet
  assert(receiverGateway.checkPacket(testNodeId, randomSession1, keyEpoch1, 103) == REPLAY_ERR_DUPLICATE_PACKET);
  std::cout << "  Duplicate of Out-of-Order Packet (seq 103): CORRECTLY REJECTED" << std::endl;

  // Delayed packet exceeding 64-frame window
  assert(receiverGateway.checkPacket(testNodeId, randomSession1, keyEpoch1, 20) == REPLAY_ERR_TOO_OLD_WINDOW_EXCEEDED);
  std::cout << "  Delayed Packet (seq 20 << window): CORRECTLY REJECTED" << std::endl;

  // 5. Random bootSessionId: Numerically Smaller Random Value Verification
  std::cout << "\n--- TEST 5: RANDOM SESSION TRANSITION (NUMERICALLY SMALLER SESSION ID) ---" << std::endl;
  // Node reboots and TRNG yields a numerically LOWER random number
  const uint32_t randomSession2 = 0x12345678; // 0x12345678 < 0x98A1B2C3
  std::cout << "  Old Session Token: 0x" << std::hex << randomSession1 << std::dec << std::endl;
  std::cout << "  New Session Token: 0x" << std::hex << randomSession2 << std::dec << " (Numerically Smaller!)" << std::endl;

  // Stale packet from old session arrives: MUST BE REJECTED
  assert(receiverGateway.checkPacket(testNodeId, randomSession1, keyEpoch1, 106) == REPLAY_OK_NEW_PACKET); // before new session registered
  receiverGateway.registerNewSession(testNodeId, randomSession2, keyEpoch1, 1);
  
  // Now packet with old session MUST be rejected as stale
  assert(receiverGateway.checkPacket(testNodeId, randomSession1, keyEpoch1, 107) == REPLAY_ERR_STALE_SESSION);
  std::cout << "  Stale Packet from Prior Session: CORRECTLY REJECTED" << std::endl;

  // Packet with new session is accepted
  assert(receiverGateway.checkPacket(testNodeId, randomSession2, keyEpoch1, 2) == REPLAY_OK_NEW_PACKET);
  std::cout << "  Packet from New Session (with smaller random ID): ACCEPTED WITHOUT LOCKOUT!" << std::endl;

  // 6. Key Rotation Test
  std::cout << "\n--- TEST 6: KEY ROTATION & REPLAY DEFENSE ---" << std::endl;
  const uint32_t keyEpoch2 = 2;
  receiverGateway.handleKeyRotation(testNodeId, keyEpoch2);

  // Packet encrypted under old key epoch (epoch 1): MUST BE REJECTED
  assert(receiverGateway.checkPacket(testNodeId, randomSession2, keyEpoch1, 10) == REPLAY_ERR_KEY_EPOCH_MISMATCH);
  std::cout << "  Packet with Obsolete Key Epoch 1: CORRECTLY REJECTED" << std::endl;

  // Packet encrypted under new key epoch (epoch 2): ACCEPTED
  assert(receiverGateway.checkPacket(testNodeId, randomSession2, keyEpoch2, 1) == REPLAY_OK_NEW_PACKET);
  std::cout << "  Packet with New Key Epoch 2: ACCEPTED" << std::endl;

  std::cout << "\nDISPOSITION: LOGICAL & STORAGE SIMULATION PASSED." << std::endl;
  std::cout << "PHYSICAL POWER-LOSS VALIDATION: CLASSIFIED AS 'NOT TESTED' UNTIL BENCH EXECUTED ON ESP32." << std::endl;

  std::cout << "\n[TEST TASK 7: PASSED ALL CHECKS]" << std::endl;
  return 0;
}
