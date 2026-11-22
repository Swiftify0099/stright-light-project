/**
 * @file ProtocolTests.ino
 * @brief MSCB Smart Pole System - Protocol Serialization & CRC16 Unit Tests
 * @version 1.0.0
 * 
 * Verifies frame packing, binary alignment, CRC-16 computation, and payload structures.
 */

#include <Arduino.h>
#if __has_include("../../MSCBShared/Protocol.h")
  #include "../../MSCBShared/Protocol.h"
#elif __has_include(<Protocol.h>)
  #include <Protocol.h>
#else
  #error "MSCBShared/Protocol.h not found!"
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n================================================"));
  Serial.println(F("     MSCB PROTOCOL & PACKET SERIALIZATION TESTS  "));
  Serial.println(F("================================================"));

  bool allPass = true;

  // Test 1: Header Size and Packing Verification
  Serial.print(F("Test 1: Header size verification (Expected: 16 bytes)... "));
  if (sizeof(MeshPacketHeader) == 16) {
    Serial.println(F("[PASS]"));
  } else {
    Serial.printf("[FAIL] Got %u bytes\n", (unsigned int)sizeof(MeshPacketHeader));
    allPass = false;
  }

  // Test 2: CRC-16 Calculation Known Vector
  // Standard test string: "123456789" -> Expected CRC16 CCITT-FALSE: 0x29B1
  const char testVector[] = "123456789";
  uint16_t calculatedCrc = ProtocolCrc::calculate((const uint8_t*)testVector, 9);
  Serial.print(F("Test 2: CRC-16 CCITT-FALSE vector '123456789' (0x29B1)... "));
  if (calculatedCrc == 0x29B1) {
    Serial.printf("[PASS] (0x%04X)\n", calculatedCrc);
  } else {
    Serial.printf("[FAIL] Expected 0x29B1, Got 0x%04X\n", calculatedCrc);
    allPass = false;
  }

  // Test 3: Packet Assembly and Verification
  Serial.print(F("Test 3: Frame assembly & CRC check... "));
  MeshWireFrame frame;
  memset(&frame, 0, sizeof(frame));
  frame.header.magic[0] = MSCB_PROTOCOL_MAGIC_0;
  frame.header.magic[1] = MSCB_PROTOCOL_MAGIC_1;
  frame.header.version  = MSCB_PROTOCOL_VERSION;
  frame.header.type     = PKT_BEACON_ANNOUNCE;
  frame.header.priority = PRIO_ROUTINE;
  frame.header.srcAddress = 0x0001;
  frame.header.dstAddress = 0xFFFF;
  frame.header.payloadLen = 4;
  frame.payload[0] = 0xDE;
  frame.payload[1] = 0xAD;
  frame.payload[2] = 0xBE;
  frame.payload[3] = 0xEF;

  size_t bytesToHash = sizeof(MeshPacketHeader) + frame.header.payloadLen;
  frame.crc16 = ProtocolCrc::calculate((const uint8_t*)&frame, bytesToHash);

  bool crcValid = ProtocolCrc::verify((const uint8_t*)&frame, bytesToHash, frame.crc16);
  if (crcValid) {
    Serial.printf("[PASS] (Payload: 4 bytes, CRC: 0x%04X)\n", frame.crc16);
  } else {
    Serial.println(F("[FAIL] Verification rejected valid frame"));
    allPass = false;
  }

  Serial.println(F("------------------------------------------------"));
  if (allPass) {
    Serial.println(F("ALL PROTOCOL TESTS PASSED SUCCESSFULLY [OK]"));
  } else {
    Serial.println(F("ONE OR MORE PROTOCOL TESTS FAILED [ERROR]"));
  }
  Serial.println(F("================================================\n"));
}

void loop() {
  delay(1000);
}
