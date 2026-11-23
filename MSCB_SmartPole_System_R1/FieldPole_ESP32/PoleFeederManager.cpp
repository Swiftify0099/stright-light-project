/**
 * @file PoleFeederManager.cpp
 * @brief MSCB Smart Pole Node - Six-Feeder Command & Auxiliary Feedback Manager Implementation
 * @version 1.0.0
 */

#include "PoleFeederManager.h"
#include <string.h>

PoleFeederManager poleFeederMgr;

// MCP23S17 SPI Register Map
#define MCP23S17_IODIRA   0x00
#define MCP23S17_IODIRB   0x01
#define MCP23S17_GPIOA    0x12
#define MCP23S17_GPIOB    0x13
#define MCP23S17_OLATA    0x14
#define MCP23S17_OLATB    0x15

PoleFeederManager::PoleFeederManager() :
  benchTestMode(false),
  auxHardwareDetected(false),
  pendingStaggerMask(0),
  pendingStaggerAction(0),
  lastStaggerStepMs(0),
  activeCommandSeq(0)
{
}

bool PoleFeederManager::detectMcp23s17() {
#if defined(ARDUINO)
  poleSelectSpiDevice(POLE_SPI_DEV_MCP23S17);
  // Read IODIRA (Control Byte: 0x41 Read, Addr 0x00)
  SPI.transfer(0x41);
  SPI.transfer(MCP23S17_IODIRA);
  uint8_t iodira = SPI.transfer(0x00);
  poleSelectSpiDevice(POLE_SPI_DEV_DESELECT);

  // If responding and not floating bus (0xFF or 0x00)
  if (iodira != 0xFF && iodira != 0x00) {
    auxHardwareDetected = true;
    return true;
  }
#endif
  auxHardwareDetected = false;
  return false;
}

bool PoleFeederManager::begin() {
  detectMcp23s17();
  return true;
}

uint8_t PoleFeederManager::readContactorAuxMask() {
  if (!auxHardwareDetected) return 0;
#if defined(ARDUINO)
  poleSelectSpiDevice(POLE_SPI_DEV_MCP23S17);
  SPI.transfer(0x41);
  SPI.transfer(MCP23S17_GPIOB);
  uint8_t portb = SPI.transfer(0x00);
  poleSelectSpiDevice(POLE_SPI_DEV_DESELECT);
  // Port B bits 0..5 are contactor aux closed contacts (Active LOW or HIGH)
  return portb & 0x3F;
#else
  return 0;
#endif
}

uint8_t PoleFeederManager::readMcbTripMask() {
  if (!auxHardwareDetected) return 0;
#if defined(ARDUINO)
  poleSelectSpiDevice(POLE_SPI_DEV_MCP23S17);
  SPI.transfer(0x41);
  SPI.transfer(MCP23S17_GPIOA);
  uint8_t porta = SPI.transfer(0x00);
  poleSelectSpiDevice(POLE_SPI_DEV_DESELECT);
  return porta & 0x3F;
#else
  return 0;
#endif
}

void PoleFeederManager::sendAckUplink(uint8_t feederId, uint32_t seqNumber, FeederCommandAckState ackState) {
  PktFeederAckPayload ack;
  ack.poleId = poleStorage.getAssignedPoleId();
  ack.feederId = feederId;
  ack.sequenceNumber = seqNumber;
  ack.ackState = ackState;
  ack.currentRelayMask = poleActuators.getActiveRelayMask();
  ack.currentAuxMask = readContactorAuxMask();
  ack.currentMcbMask = readMcbTripMask();

  poleMesh.sendUplinkPacket(PKT_CMD_FEEDER_ACK, (const uint8_t*)&ack, sizeof(PktFeederAckPayload), PRIO_COMMAND);
}

FeederCommandAckState PoleFeederManager::executeFeederSwitch(uint8_t feederId1Based, uint8_t action) {
  if (feederId1Based < 1 || feederId1Based > 6) {
    return FEEDER_ACK_REJECTED_FAULT;
  }
  uint8_t chIdx = feederId1Based - 1;

  // 0. Permanent Revocation Lock: switching strictly prohibited when revoked
  if (poleStorage.isRevokedLocked()) {
    return FEEDER_ACK_REJECTED_LOCKED;
  }

  // 1. Safety Interlock: MSCB_RELAYS_LOCKED_DISABLED
  if (poleActuators.areRelaysLockedDisabled() && !benchTestMode) {
    return FEEDER_ACK_REJECTED_LOCKED;
  }

  // 2. Drive Relay Output
  bool success = false;
  if (action == 1) {
    success = poleActuators.requestRelayOn(chIdx, "COMMAND");
  } else {
    success = poleActuators.requestRelayOff(chIdx, "COMMAND");
  }

  if (!success) {
    return FEEDER_ACK_REJECTED_FAULT;
  }

  // State: OUTPUT_CHANGED
  // 3. Verify Auxiliary Feedback Hardware
  // Strictly NEVER treat GPIO output readback as proof of contactor state!
  if (!auxHardwareDetected) {
    return FEEDER_ACK_FEEDBACK_NOT_CONNECTED;
  }

  // If auxiliary hardware is present, verify contactor closure
  uint8_t auxMask = readContactorAuxMask();
  bool auxClosed = (auxMask & (1 << chIdx)) != 0;
  bool expectedClosed = (action == 1);

  if (auxClosed == expectedClosed) {
    return FEEDER_ACK_COMPLETED;
  } else {
    return FEEDER_ACK_FEEDBACK_UNAVAILABLE;
  }
}

bool PoleFeederManager::processCommandPayload(const PktFeederCmdPayload &cmd) {
  // 1. Progression: RECEIVED
  sendAckUplink(cmd.feederId, cmd.sequenceNumber, FEEDER_ACK_RECEIVED);

  // 2. Progression: ACCEPTED & Security Envelope Verification
  // Reconstruct envelope for verification
  AuthenticatedCommandEnvelope env;
  env.networkId = MSCB_CMD_NETWORK_MAGIC;
  env.sourceAddress = MSCB_GATEWAY_NODE_ID;
  env.destAddress = cmd.targetPoleId;
  env.feederId = cmd.feederId;
  env.command = PKT_CMD_FEEDER_SWITCH;
  env.commandParam = cmd.commandAction;
  env.sequenceNumber = cmd.sequenceNumber;
  env.expiryEpochSec = cmd.expiryEpochSec;
  env.keyEpoch = 1;
  memcpy(env.hmacTag, cmd.hmacTag, sizeof(env.hmacTag));

  uint32_t nowEpoch = millis() / 1000;
  CommandAuthResult authRes = cmdSecurity.verifyCommand(env, nowEpoch, poleStorage.getAssignedPoleId());
  if (authRes != AUTH_SUCCESS) {
    if (authRes == AUTH_ERR_COMMAND_EXPIRED) {
      sendAckUplink(cmd.feederId, cmd.sequenceNumber, FEEDER_ACK_REJECTED_STALE);
    } else {
      sendAckUplink(cmd.feederId, cmd.sequenceNumber, FEEDER_ACK_REJECTED_FAULT);
    }
    return false;
  }

  sendAckUplink(cmd.feederId, cmd.sequenceNumber, FEEDER_ACK_ACCEPTED);

  // 3. Command Execution
  if (cmd.feederId >= 1 && cmd.feederId <= 6) {
    // Single Feeder Execution
    FeederCommandAckState finalState = executeFeederSwitch(cmd.feederId, cmd.commandAction);
    sendAckUplink(cmd.feederId, cmd.sequenceNumber, finalState);
    return (finalState == FEEDER_ACK_COMPLETED || finalState == FEEDER_ACK_FEEDBACK_NOT_CONNECTED);
  } else if (cmd.feederId == 0xF0) {
    // Group Staggered Execution
    executeGroupSwitch(cmd.groupMask, cmd.commandAction);
    activeCommandSeq = cmd.sequenceNumber;
    return true;
  } else if (cmd.feederId == 0x00) {
    // All Feeders Staggered Execution
    executeAllSwitch(cmd.commandAction);
    activeCommandSeq = cmd.sequenceNumber;
    return true;
  }

  return false;
}

void PoleFeederManager::executeGroupSwitch(uint8_t groupMask, uint8_t action) {
  pendingStaggerMask = groupMask & 0x3F;
  pendingStaggerAction = action;
  lastStaggerStepMs = millis() - 200; // Trigger first step immediately
}

void PoleFeederManager::executeAllSwitch(uint8_t action) {
  executeGroupSwitch(0x3F, action);
}

void PoleFeederManager::emergencyShutdownAll(const char* reason) {
  poleActuators.emergencyShutdownAll(reason);
  pendingStaggerMask = 0;
  sendAckUplink(0x00, 0, FEEDER_ACK_COMPLETED);
}

void PoleFeederManager::service() {
  if (pendingStaggerMask == 0) return;

  uint32_t now = millis();
  // 200ms stagger between sequential feeder coil activations
  if (now - lastStaggerStepMs >= 200) {
    lastStaggerStepMs = now;

    // Find next bit in pendingStaggerMask
    for (uint8_t i = 0; i < 6; i++) {
      if (pendingStaggerMask & (1 << i)) {
        pendingStaggerMask &= ~(1 << i);
        FeederCommandAckState state = executeFeederSwitch(i + 1, pendingStaggerAction);
        sendAckUplink(i + 1, activeCommandSeq, state);
        break;
      }
    }
  }
}
