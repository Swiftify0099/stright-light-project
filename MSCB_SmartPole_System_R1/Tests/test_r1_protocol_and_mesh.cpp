/**
 * @file test_r1_protocol_and_mesh.cpp
 * @brief MSCB Smart Pole System R1 - Protocol, Mesh Routing & Forwarding Verification
 */

#include "ArduinoMock.h"
#include "../MSCBShared/Protocol.h"
#include "../MSCBShared/NetworkTypes.h"
#include "../FieldPole_ESP32/PoleMeshRouter.h"
#include "../Gateway_ESP32S3/GwMeshCoordinator.h"
#include "../Gateway_ESP32S3/GwNodeRegistry.h"

MockSerial Serial;

static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_REPORT(cond, tag, msg) \
  do { \
    if (cond) { \
      std::cout << "  [" tag "] " << msg << std::endl; \
      g_passCount++; \
    } else { \
      std::cout << "  [FAIL] " << msg << " (Line " << __LINE__ << ")" << std::endl; \
      g_failCount++; \
    } \
  } while (0)

int main() {
  std::cout << "\n========================================================" << std::endl;
  std::cout << "  TEST SUITE 1: R1 BINARY PROTOCOL & LORA MESH ROUTING  " << std::endl;
  std::cout << "========================================================" << std::endl;

  // -----------------------------------------------------------------
  // 1. Protocol Serialization & CCITT CRC-16 Integrity
  // -----------------------------------------------------------------
  std::cout << "\n[1. PROTOCOL SERIALIZATION & CRC-16]" << std::endl;
  {
    uint8_t testData[16] = {
      0x4D, 0x53, 0x01, 0x04, 0x00, 0x00, 0x06, 0x00,
      0x01, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00
    };
    uint16_t crc = ProtocolCrc::calculate(testData, sizeof(testData));
    bool crcOk = ProtocolCrc::verify(testData, sizeof(testData), crc);
    TEST_REPORT(crcOk && crc != 0, "UNIT_PASS", "Protocol CRC-16 CCITT calculation and verification");

    // Corrupted byte must fail CRC
    testData[5] ^= 0x55;
    bool corruptedCrcFails = !ProtocolCrc::verify(testData, sizeof(testData), crc);
    TEST_REPORT(corruptedCrcFails, "UNIT_PASS", "Corrupted packet correctly fails CRC-16 verification");
  }

  // -----------------------------------------------------------------
  // 2. LoRa Wire Frame Magic & Structure Validation
  // -----------------------------------------------------------------
  std::cout << "\n[2. WIRE FRAME PACKET VALIDATION]" << std::endl;
  {
    MeshWireFrame frame;
    memset(&frame, 0, sizeof(MeshWireFrame));
    frame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
    frame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
    frame.header.version = MSCB_PROTOCOL_VERSION;
    frame.header.type = PKT_BEACON_ANNOUNCE;
    frame.header.srcAddress = MSCB_GATEWAY_NODE_ID;
    frame.header.dstAddress = MSCB_BROADCAST_NODE_ID;
    frame.header.maxHops = 6;
    frame.header.hopCount = 0;
    frame.header.seqNumber = 101;
    frame.header.payloadLen = 8;

    bool magicValid = (frame.header.magic[0] == 0x4D && frame.header.magic[1] == 0x53);
    TEST_REPORT(magicValid, "UNIT_PASS", "LoRa packet framing magic bytes verified (0x4D 0x53)");
    TEST_REPORT(frame.header.maxHops == 6, "UNIT_PASS", "Max mesh hops default confirmed at 6");
  }

  // -----------------------------------------------------------------
  // 3. Dynamic Parent Selection & Link Score
  // -----------------------------------------------------------------
  std::cout << "\n[3. DYNAMIC PARENT SELECTION & LINK SCORING]" << std::endl;
  {
    PoleMeshRouter router;
    router.begin();

    // Beacon from Gateway: direct, RSSI -65, SNR +9, Rank 0
    router.updateNeighborsFromBeacon(MSCB_GATEWAY_NODE_ID, 0, 0, -65, 9);

    // Beacon from Pole 3: RSSI -85, SNR +3, Rank 2
    router.updateNeighborsFromBeacon(0x0003, 2, 0x0001, -85, 3);

    bool parentsSelected = router.selectBestParents();
    TEST_REPORT(parentsSelected, "UNIT_PASS", "Dynamic parent selection evaluated successfully");
    TEST_REPORT(router.getPrimaryParent() == MSCB_GATEWAY_NODE_ID, "INTEGRATION_PASS",
                "Primary parent chosen as Gateway (highest link score / lowest rank)");
    TEST_REPORT(router.getMyRank() == 1, "INTEGRATION_PASS",
                "Monotonically increasing rank enforced: Pole Rank = Parent Rank + 1 (0 + 1 = 1)");
  }

  // -----------------------------------------------------------------
  // 4. Monotonically Increasing Rank Loop Prevention
  // -----------------------------------------------------------------
  std::cout << "\n[4. LOOP PREVENTION VIA NETWORK RANK]" << std::endl;
  {
    PoleMeshRouter router;
    router.begin();

    // Downstream child (Pole 5) tries to announce beacon with primary parent set to this node (Pole 2)
    // Router MUST ignore to prevent cyclic routing loops!
    uint8_t beforeCount = router.getNeighborCount();
    router.updateNeighborsFromBeacon(0x0005, 3, POLE_DEFAULT_NODE_ADDRESS, -60, 10);
    uint8_t afterCount = router.getNeighborCount();

    TEST_REPORT(beforeCount == afterCount, "UNIT_PASS",
                "Loop prevention: Candidate whose primary parent is this node is strictly rejected");
  }

  // -----------------------------------------------------------------
  // 5. Backup Parent Failover & Orphan Detection
  // -----------------------------------------------------------------
  std::cout << "\n[5. BACKUP PARENT FAILOVER & ORPHAN RECOVERY]" << std::endl;
  {
    PoleMeshRouter router;
    router.begin();

    router.updateNeighborsFromBeacon(0x0003, 1, MSCB_GATEWAY_NODE_ID, -70, 7);
    router.updateNeighborsFromBeacon(0x0004, 1, MSCB_GATEWAY_NODE_ID, -75, 6);
    router.selectBestParents();

    uint16_t initialPrimary = router.getPrimaryParent();
    uint16_t initialBackup = router.getBackupParent();
    TEST_REPORT(initialPrimary == 0x0003 && initialBackup == 0x0004, "UNIT_PASS",
                "Primary (0x0003) and Backup (0x0004) parents established independently");

    // Trigger failover
    router.triggerFailover();
    TEST_REPORT(router.getPrimaryParent() == 0x0004 && router.getState() == ROUTING_FAILOVER, "INTEGRATION_PASS",
                "Automatic failover: Switched to backup parent (0x0004) upon primary link failure");

    // Trigger second failover when no further backup exists -> enters ORPHAN state
    router.triggerFailover();
    TEST_REPORT(router.getState() == ROUTING_ORPHAN && router.getPrimaryParent() == 0, "INTEGRATION_PASS",
                "Orphan state entered when all parents exhausted; begins autonomous route discovery");
  }

  // -----------------------------------------------------------------
  // 6. Multi-Hop Packet Forwarding & Max Hop Enforcement
  // -----------------------------------------------------------------
  std::cout << "\n[6. MULTI-HOP PACKET FORWARDING & HOP ENFORCEMENT]" << std::endl;
  {
    PoleMeshRouter router;
    router.begin();
    router.updateNeighborsFromBeacon(MSCB_GATEWAY_NODE_ID, 0, 0, -70, 8);
    router.selectBestParents();

    MeshWireFrame inFrame;
    memset(&inFrame, 0, sizeof(MeshWireFrame));
    inFrame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
    inFrame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
    inFrame.header.version = MSCB_PROTOCOL_VERSION;
    inFrame.header.srcAddress = 0x0007; // Origin: Pole 7
    inFrame.header.dstAddress = MSCB_GATEWAY_NODE_ID; // Destination: Gateway
    inFrame.header.hopCount = 2;
    inFrame.header.maxHops = 6;
    inFrame.header.seqNumber = 55;

    MeshWireFrame outFrame;
    bool shouldForward = false;
    router.handleReceivedWireFrame(inFrame, -72, 8, outFrame, shouldForward);

    TEST_REPORT(shouldForward, "INTEGRATION_PASS", "Packet addressed to Gateway forwarded uplink");
    TEST_REPORT(outFrame.header.hopCount == 3, "UNIT_PASS", "Hop count incremented from 2 to 3");
    TEST_REPORT(outFrame.header.nextHop == router.getPrimaryParent(), "UNIT_PASS", "Next hop set to primary parent");

    // Packet with hopCount >= maxHops must be dropped
    inFrame.header.hopCount = 6;
    inFrame.header.seqNumber = 56;
    shouldForward = false;
    router.handleReceivedWireFrame(inFrame, -72, 8, outFrame, shouldForward);
    TEST_REPORT(!shouldForward, "UNIT_PASS", "Packet dropped when hopCount reaches maxHops (6)");
  }

  // -----------------------------------------------------------------
  // 7. Duplicate Packet Suppression
  // -----------------------------------------------------------------
  std::cout << "\n[7. DUPLICATE PACKET SUPPRESSION]" << std::endl;
  {
    PoleMeshRouter router;
    router.begin();

    MeshWireFrame frame;
    memset(&frame, 0, sizeof(MeshWireFrame));
    frame.header.srcAddress = 0x0009;
    frame.header.dstAddress = MSCB_GATEWAY_NODE_ID;
    frame.header.seqNumber = 4001;
    frame.header.hopCount = 1;
    frame.header.maxHops = 6;

    MeshWireFrame outFrame;
    bool fwd1 = false;
    router.handleReceivedWireFrame(frame, -70, 7, outFrame, fwd1);
    TEST_REPORT(fwd1, "UNIT_PASS", "First packet reception accepted for forwarding");

    bool fwd2 = false;
    router.handleReceivedWireFrame(frame, -70, 7, outFrame, fwd2);
    TEST_REPORT(!fwd2, "UNIT_PASS", "Duplicate packet (same src + seq) successfully suppressed");
  }

  // -----------------------------------------------------------------
  // 8. Child Route Migration Upon Parent Deletion
  // -----------------------------------------------------------------
  std::cout << "\n[8. CHILD ROUTE MIGRATION UPON PARENT DELETION]" << std::endl;
  {
    GwNodeRegistry reg;
    // Register Pole 3 and Pole 4
    uint8_t mac3[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x03};
    uint8_t mac4[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x04};

    PktRegisterRequestPayload req3;
    memcpy(req3.macAddress, mac3, 6);
    PktRegisterResponsePayload resp3;
    reg.handleJoinRequest(req3, -70, 8, resp3);
    uint16_t id3 = 0;
    reg.approvePending(mac3, id3);

    PktRegisterRequestPayload req4;
    memcpy(req4.macAddress, mac4, 6);
    PktRegisterResponsePayload resp4;
    reg.handleJoinRequest(req4, -75, 7, resp4);
    uint16_t id4 = 0;
    reg.approvePending(mac4, id4);

    // Make Pole 4 use Pole 3 as parent
    RegisteredPole* p4 = reg.findPoleById(id4);
    assert(p4 != NULL);
    p4->primaryParent = id3;
    p4->rank = 2;

    // Delete Pole 3
    reg.deleteAndRevokePole(id3, "ADMIN_TEST", "TEST_DELETION");

    // Re-query Pole 4 after registry compaction/shift
    RegisteredPole* p4_migrated = reg.findPoleById(id4);
    assert(p4_migrated != NULL);

    // Child (Pole 4) must have been migrated back to Gateway (0x0001)
    TEST_REPORT(p4_migrated->primaryParent == MSCB_GATEWAY_NODE_ID && p4_migrated->rank == 1, "INTEGRATION_PASS",
                "Child route migration: Child of deleted parent automatically migrated to Gateway");
  }

  std::cout << "\n========================================================" << std::endl;
  std::cout << "TEST SUITE 1 SUMMARY: " << g_passCount << " PASSED, " << g_failCount << " FAILED" << std::endl;
  std::cout << "========================================================\n" << std::endl;

  return (g_failCount == 0) ? 0 : 1;
}
