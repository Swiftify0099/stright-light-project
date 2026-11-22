/*
 * =====================================================================
 *  COMMANDMANAGER.CPP — FIELD POLE COMMAND PROCESSOR & ACK DISPATCHER
 *  MSCB Smart Pole Automation System — Prompt 15 Architecture
 * =====================================================================
 */

#include "CommandManager.h"
#include "LoRaManager.h"
#include "MeshManager.h"
#include "FeederManager.h"
#include "VoltageSensor.h"
#include "CurrentSensor.h"
#include "StorageManager.h"
#include "MeasurementTests.h"
#include "CalibrationManager.h"
#include "PowerMeasurementEngine.h"
#include "AuthSecurity.h"
#include "PoleConfigManager.h"
#include "PoleFirmwareManager.h"

CommandManager cmdMgr;

CommandManager::CommandManager() :
  timerActive(false), timerEndMs(0), timerDurationMs(0),
  epochSeconds(0), epochMillis(0), hasTimeSync(false),
  lastSchedChecked(-1), newNodeRetries(0)
{}

void CommandManager::init() {}

bool CommandManager::parseParameter(const String &src, const String &key, String &val) {
  String search = key + "=";
  int idx = src.indexOf(search);
  if (idx < 0) return false;
  int start = idx + search.length();
  int end = src.indexOf('|', start);
  if (end < 0) end = src.length();
  val = src.substring(start, end);
  val.trim();
  return true;
}

void CommandManager::sendAck(const String &to, const String &nextHop, uint16_t seq, const String &cmd, const String &status, const String &state) {
  float vR = metrologyEngine.getCommonVoltageR();
  String vsR = metrologyEngine.getVoltageStateR();
  float a = feederMgr.isMainRelayOn() ? metrologyEngine.getFeederCurrent(1, 'R') : 0.0f;

  String ackData = cmd + "|" + status + "|STATE=" + state + "|V=" + String(vR, 1) + "|VS=" + vsR + "|A=" + String(a, 2);
  loraMgr.txMesh("ACK", to, nextHop, DEFAULT_TTL, seq, ackData);
}

void CommandManager::sendDetailedAck(const String &to, const String &nextHop, uint16_t seq, const String &cmd,
                                     const String &status, uint8_t feederIndex, const CommandTransaction &tx) {
  float vR = metrologyEngine.getCommonVoltageR();
  float iR = (feederIndex < MAX_FEEDERS) ? metrologyEngine.getFeederCurrent(feederIndex + 1, 'R') : 0.0f;
  float iY = (feederIndex < MAX_FEEDERS) ? metrologyEngine.getFeederCurrent(feederIndex + 1, 'Y') : 0.0f;
  float iB = (feederIndex < MAX_FEEDERS) ? metrologyEngine.getFeederCurrent(feederIndex + 1, 'B') : 0.0f;

  bool rlyOn = feederMgr.isFeederRelayOn(feederIndex);
  bool contClosed = feederMgr.isContactorClosed(feederIndex);
  bool mcbOk = feederMgr.isMCBOk(feederIndex);

  String stageStr = "A";
  if (tx.stageB_Accepted) stageStr += "_B";
  if (tx.stageC_RelayChanged) stageStr += "_C";
  if (tx.stageD_FeedbackConfirmed) stageStr += "_D";
  if (tx.stageE_MeasurementVerified) stageStr += "_E";

  char buf[200];
  snprintf(buf, sizeof(buf), "%s|%s|FDR=%d|STATE=%s|STAGE=%s|RLY=%d|CONT=%d|MCB=%d|VR=%.1f|IR=%.2f|IY=%.2f|IB=%.2f|SM=%s",
           cmd.c_str(), status.c_str(), feederIndex + 1,
           rlyOn ? "ON" : "OFF", stageStr.c_str(),
           rlyOn ? 1 : 0, contClosed ? 1 : 0, mcbOk ? 1 : 0,
           vR, iR, iY, iB, commandExecutionStateToString(tx.state));

  loraMgr.txMesh("ACK", to, nextHop, DEFAULT_TTL, seq, String(buf));
  Serial.printf("[POLE][ACK] Sent Detailed ACK: %s\n", buf);
}

void CommandManager::sendStatus(const String &replyTo, uint16_t seq) {
  metrologyEngine.computeMetrologyWindow();
  feederMgr.updateMCBInputs();

  float vR = metrologyEngine.getCommonVoltageR();
  float vY = metrologyEngine.getCommonVoltageY();
  float vB = metrologyEngine.getCommonVoltageB();
  float freq = metrologyEngine.getGridFrequency();
  String vsR = metrologyEngine.getVoltageStateR();

  float a1 = metrologyEngine.getFeederCurrent(1, 'R');
  float p1 = metrologyEngine.getFeederPower(1);
  float kwh1 = metrologyEngine.getFeederEnergy(1) / 1000.0f;
  String lgt = feederMgr.isMainRelayOn() ? "ON" : "OFF";
  String flt = feederMgr.getFeederState(0) ? feederMgr.getFeederState(0)->faultState : "OK";

  char buf[240];
  snprintf(buf, sizeof(buf), "V=%.1f|VY=%.1f|VB=%.1f|F=%.2f|VS=%s|A=%.2f|P=%.1f|KWH=%.2f|LGT=%s|FLT=%s|UP=%lu|CTRL=%s",
           vR, vY, vB, freq, vsR.c_str(), a1, p1, kwh1, lgt.c_str(), flt.c_str(),
           (unsigned long)(millis() / 1000), feederMgr.buildControlStatusString().c_str());

  String statusData = String(buf);
  String fdrData = feederMgr.buildFeederTelemetryString(vR, vY, vB);
  if (fdrData.length() > 0) {
    statusData += "|" + fdrData;
  }

  String nh = meshMgr.findNextHop(replyTo);
  loraMgr.txMesh("STATUS", replyTo, nh, DEFAULT_TTL, (seq > 0 ? seq : loraMgr.nextSeq()), statusData);
}

void CommandManager::sendNewNode() {
  char buf[96];
  snprintf(buf, sizeof(buf), "HW_ID=%s|FW=%s|TYPE=%s|PREV=%s",
           storageMgr.getHwNodeId().c_str(), POLE_VERSION, storageMgr.getNodeType().c_str(), storageMgr.getPrevRelayState().c_str());

  loraMgr.txMesh("NEW_NODE", GATEWAY_ID, BROADCAST_ID, DEFAULT_TTL, loraMgr.nextSeq(), String(buf));
  newNodeRetries++;
  Serial.printf("[POLE][REG] Sent NEW_NODE (Attempt %d)\n", newNodeRetries);
}

void CommandManager::processCommand(const String &cmd, const String &from, uint16_t seq, int rssi, int snr) {
  String c = cmd;
  c.trim();
  Serial.printf("[POLE] Parsed command: %s (From: %s, Seq: %u, RSSI: %d dBm)\n", c.c_str(), from.c_str(), seq, rssi);

  String nh = meshMgr.findNextHop(from);
  String myPoleId = storageMgr.getNodeId();
  if (myPoleId.length() == 0) myPoleId = storageMgr.getHwNodeId();

  // ─────────────────────────────────────────────────────────────
  //  READ-ONLY STATUS / TELEMETRY QUERIES (Safe, no physical state change)
  // ─────────────────────────────────────────────────────────────
  if (c == "GET_STATUS" || c == "STATUS" || c == "PUMP_STATUS") {
    sendStatus(from, seq);
    return;
  }
  else if (c.startsWith("PING")) {
    loraMgr.txMesh("PONG", from, nh, DEFAULT_TTL, (seq > 0 ? seq : loraMgr.nextSeq()), "RSSI=" + String(rssi));
    return;
  }

  // ─────────────────────────────────────────────────────────────
  //  STRICT CRYPTOGRAPHIC AUTHENTICATED COMMAND PARSER (PROMPT 21)
  //  Format: CMD|NET=<netId>|POLE=<poleId>|[FDR=<1..6>|]ID=<txId>|REQ=<act>|AUTH=<hmac>|EXP=<exp>|SEQ=<seq>
  //  RULE 1: Never trust unauthenticated packets.
  //  RULE 2: Never execute commands before authentication.
  // ─────────────────────────────────────────────────────────────
  if (c.startsWith("CMD|")) {
    String netId, poleTarget, fdrStr, idStr, reqStr, authStr, expStr, seqStr;
    parseParameter(c, "NET", netId);
    parseParameter(c, "POLE", poleTarget);
    parseParameter(c, "FDR", fdrStr);
    parseParameter(c, "ID", idStr);
    parseParameter(c, "REQ", reqStr);
    parseParameter(c, "AUTH", authStr);
    parseParameter(c, "EXP", expStr);
    parseParameter(c, "SEQ", seqStr);

    // 1. Mandatory Authentication Check (No bypasses)
    if (authStr.length() == 0) {
      Serial.println(F("[POLE][SECURITY] ⛔ REJECTED: Command missing cryptographic authentication tag!"));
      sendAck(from, nh, seq, "CMD", "ERR_AUTH_REQUIRED", "REJECTED");
      return;
    }

    // 2. Cryptographic HMAC Signature Verification (Constant-Time)
    bool authOk = authSec.verifyPayloadSignature(reqStr, seq, idStr, authStr);
    if (!authOk) {
      Serial.println(F("[POLE][SECURITY] ⛔ REJECTED: Cryptographic Authentication FAILED!"));
      sendAck(from, nh, seq, "CMD", "ERR_AUTH_FAILED", "REJECTED");
      return;
    }

    // 3. Replay Protection: Check Transaction ID / Nonce in sliding window
    if (idStr.length() > 0) {
      if (authSec.isNonceUsed(idStr)) {
        Serial.printf("[POLE][SECURITY] ⛔ REJECTED: Replay detected for Transaction ID '%s'!\n", idStr.c_str());
        sendAck(from, nh, seq, "CMD", "ERR_REPLAY_DETECTED", "REJECTED");
        return;
      }
      authSec.markNonceUsed(idStr);
    }

    // 4. Message Expiration Check
    uint32_t expMs = expStr.toInt() > 0 ? (uint32_t)strtoul(expStr.c_str(), nullptr, 10) : 0;
    if (expMs > 0 && millis() > expMs) {
      Serial.printf("[POLE][SECURITY] ⛔ REJECTED: Command EXPIRED (Exp: %lu, Now: %lu)!\n", (unsigned long)expMs, millis());
      sendAck(from, nh, seq, "CMD", "ERR_EXPIRED", "REJECTED");
      return;
    }

    // 5. Network ID Validation
    if (netId.length() > 0 && netId != MESH_NET_ID && netId != "MSCB" && netId != "0x4D53") {
      Serial.printf("[POLE][CMD] ⛔ Rejected: Foreign Network ID '%s'!\n", netId.c_str());
      sendAck(from, nh, seq, "CMD", "ERR_INVALID_NETWORK", "REJECTED");
      return;
    }

    // 6. Target Pole Validation
    if (poleTarget.length() > 0 && poleTarget != myPoleId && poleTarget != storageMgr.getHwNodeId() &&
        poleTarget != BROADCAST_ID && poleTarget != "ALL") {
      Serial.printf("[POLE][CMD] ⛔ Ignored: Target Pole '%s' does not match this node ('%s').\n",
                    poleTarget.c_str(), myPoleId.c_str());
      return;
    }

    // 7. Check Device Revocation Status
    if (storageMgr.isRevoked()) {
      Serial.println(F("[POLE][SECURITY] ⛔ REJECTED: Node is permanently REVOKED. Control locked out."));
      sendAck(from, nh, seq, "CMD", "ERR_NODE_REVOKED", "LOCKED_OUT");
      return;
    }

    // 8. Emergency Stop Reset Handling
    if (reqStr == "ESTOP_RESET") {
      feederMgr.resetEmergencyStop();
      sendAck(from, nh, seq, "ESTOP_RESET", "OK", "CLEARED");
      return;
    }

    // 9. Hardware Electrical Safety Interlocks Check
    if (feederMgr.isEmergencyStopActive()) {
      Serial.println(F("[POLE][ELECTRICAL] ⛔ REJECTED: Emergency Stop is ACTIVE! Remote commands blocked."));
      sendAck(from, nh, seq, "CMD", "ERR_EMERGENCY_STOP_ACTIVE", "REJECTED");
      return;
    }

    if (feederMgr.isManualMode()) {
      Serial.println(F("[POLE][ELECTRICAL] ⛔ REJECTED: Manual Mode is active! Remote commands locked out."));
      sendAck(from, nh, seq, "CMD", "ERR_MANUAL_MODE_ACTIVE", "REJECTED");
      return;
    }

    // 10. Execute Authenticated Action
    uint8_t cmdId = idStr.toInt();

    // A. Feeder Switching Request
    if (fdrStr.length() > 0) {
      uint8_t fdrIdx = fdrStr.toInt() - 1;
      if (fdrIdx >= MAX_FEEDERS) {
        sendAck(from, nh, seq, "CMD", "ERR_INVALID_FEEDER", "REJECTED");
        return;
      }

      // Check MCB Trip
      if (feederMgr.isMCBTripped(fdrIdx)) {
        Serial.printf("[POLE][ELECTRICAL] ⛔ REJECTED: Feeder %d MCB is TRIPPED! Requires local panel reset.\n", fdrIdx + 1);
        sendAck(from, nh, seq, "CMD", "ERR_MCB_TRIPPED", "REJECTED");
        return;
      }

      bool turnOn = (reqStr == "ON" || reqStr == "1" || reqStr == "FEEDER_ON");
      bool ok = feederMgr.requestFeederState(fdrIdx, turnOn, from, cmdId, expMs, seq);
      CommandTransaction *tx = feederMgr.getActiveTransaction(fdrIdx);
      if (tx) {
        sendDetailedAck(from, nh, seq, "CMD_FEEDER_" + String(fdrIdx + 1), ok ? "OK" : "ERR", fdrIdx, *tx);
      } else {
        sendAck(from, nh, seq, "CMD_FEEDER_" + String(fdrIdx + 1), ok ? "OK" : "ERR", turnOn ? "ON" : "OFF");
      }
      return;
    }

    // B. Group Control
    if (reqStr == "ALL_ON" || reqStr == "ALL_FEEDERS_ON" || reqStr == "GROUP_ALL_ON") {
      bool ok = feederMgr.requestGroupControl(0x3F, true, DEFAULT_GROUP_STAGGER_MS, from);
      sendAck(from, nh, seq, "ALL_FEEDERS_ON", ok ? "OK" : "ERR", "STAGGERED_ON");
      return;
    }
    else if (reqStr == "ALL_OFF" || reqStr == "ALL_FEEDERS_OFF" || reqStr == "GROUP_ALL_OFF") {
      bool ok = feederMgr.requestGroupControl(0x3F, false, DEFAULT_GROUP_STAGGER_MS, from);
      sendAck(from, nh, seq, "ALL_FEEDERS_OFF", ok ? "OK" : "ERR", "STAGGERED_OFF");
      return;
    }
    else if (reqStr.startsWith("GROUP_ON")) {
      uint8_t mask = 0x3F;
      int pPipe = reqStr.indexOf('|');
      if (pPipe > 0) mask = (uint8_t)strtoul(reqStr.substring(pPipe + 1).c_str(), nullptr, 0);
      bool ok = feederMgr.requestGroupControl(mask, true, DEFAULT_GROUP_STAGGER_MS, from);
      sendAck(from, nh, seq, "GROUP_ON", ok ? "OK" : "ERR", "MASK=0x" + String(mask, HEX));
      return;
    }
    else if (reqStr.startsWith("GROUP_OFF")) {
      uint8_t mask = 0x3F;
      int pPipe = reqStr.indexOf('|');
      if (pPipe > 0) mask = (uint8_t)strtoul(reqStr.substring(pPipe + 1).c_str(), nullptr, 0);
      bool ok = feederMgr.requestGroupControl(mask, false, DEFAULT_GROUP_STAGGER_MS, from);
      sendAck(from, nh, seq, "GROUP_OFF", ok ? "OK" : "ERR", "MASK=0x" + String(mask, HEX));
      return;
    }

    // C. Emergency Stop
    else if (reqStr == "ESTOP" || reqStr == "EMERGENCY_STOP" || reqStr == "EMERGENCY_SHUTDOWN") {
      feederMgr.triggerEmergencyStop("AUTHENTICATED_REMOTE");
      sendAck(from, nh, seq, "ESTOP", "OK", "EMERGENCY_STOP_LATCHED");
      return;
    }

    // D. Feeder 0 / Main Light Control
    else if (reqStr == "LIGHT_ON" || reqStr == "PUMP_ON") {
      if (timerActive) timerActive = false;
      feederMgr.setMainRelay(true, "AUTH_TG_CMD");
      sendAck(from, nh, seq, reqStr, "OK", "ON");
      return;
    }
    else if (reqStr == "LIGHT_OFF" || reqStr == "PUMP_OFF") {
      if (timerActive) timerActive = false;
      feederMgr.setMainRelay(false, "AUTH_TG_CMD");
      sendAck(from, nh, seq, reqStr, "OK", "OFF");
      return;
    }

    // E. Timed Actuation
    else if (reqStr.startsWith("TIMER_ON|")) {
      int mins = reqStr.substring(9).toInt();
      if (mins <= 0) mins = 60;
      feederMgr.setMainRelay(true, "TIMER");
      timerDurationMs = (unsigned long)mins * 60UL * 1000UL;
      timerEndMs = millis() + timerDurationMs;
      timerActive = true;
      sendAck(from, nh, seq, "TIMER_ON", "OK", "TIMER=" + String(mins) + "M");
      return;
    }

    // F. Safe Reboot
    else if (reqStr == "RESET" || reqStr == "REBOOT") {
      sendAck(from, nh, seq, "RESET", "OK", "REBOOTING");
      delay(200);
#ifdef ARDUINO
      ESP.restart();
#endif
      return;
    }

    // G. MCB Reset
    else if (reqStr.startsWith("MCB_RESET|")) {
      uint8_t fIdx = reqStr.substring(10).toInt() - 1;
      if (fIdx < MAX_FEEDERS) {
        feederMgr.clearMcbTrip(fIdx);
        sendAck(from, nh, seq, "MCB_RESET", "OK", "CLEARED|FDR=" + String(fIdx + 1));
      }
      return;
    }
  }

  // ─────────────────────────────────────────────────────────────
  //  UNAUTHENTICATED COMMAND REJECTION (PROMPT 21 REQUIREMENT 1 & 2)
  //  Any command arriving without cryptographic authentication is REJECTED!
  // ─────────────────────────────────────────────────────────────
  Serial.printf("[POLE][SECURITY] ⛔ REJECTED: Command '%s' arrived without cryptographic authentication!\n", c.c_str());
  sendAck(from, nh, seq, c, "ERR_AUTH_REQUIRED", "REJECTED");
}
  else if (c.startsWith("PING")) {
    String linkData = "RSSI=" + String(rssi) + "|SNR=" + String(snr);
    loraMgr.txMesh("LINK_ACK", from, nh, DEFAULT_TTL, seq, linkData);
    Serial.printf("[POLE][LORA] Replied LINK_ACK to %s (Seq: %u, RSSI: %d)\n", from.c_str(), seq, rssi);
  }
  else if (c.startsWith("TIMER_ON|")) {
    int bar = c.indexOf('|');
    if (bar > 0) {
      unsigned long mins = c.substring(bar + 1).toInt();
      if (mins > 0 && mins <= 1440) {
        timerDurationMs = mins * 60000UL;
        timerEndMs = millis() + timerDurationMs;
        timerActive = true;
        feederMgr.setMainRelay(true, "TIMER");
        sendAck(from, nh, seq, "TIMER_ON", "OK", "ON|" + String(mins) + "M");
      } else {
        sendAck(from, nh, seq, "TIMER_ON", "ERR_INVALID_MINUTES", feederMgr.isMainRelayOn() ? "ON" : "OFF");
      }
    }
  }
  else if (c == "RESET" || c == "REBOOT") {
    Serial.printf("[POLE][RESET] Received REBOOT command (Seq: %u). Sending ACK and rebooting...\n", seq);
    sendAck(from, nh, seq, "RESET", "OK", "REBOOTING");
    delay(300);
    ESP.restart();
  }
  else if (c.startsWith("SET_TIME|")) {
    int bar = c.indexOf('|');
    if (bar > 0) {
      unsigned long newEpoch = strtoul(c.substring(bar + 1).c_str(), nullptr, 10);
      if (newEpoch > 1700000000UL) {
        epochSeconds = newEpoch;
        epochMillis  = millis();
        hasTimeSync  = true;
        Serial.printf("[POLE][TIME] Soft RTC synchronized to: %lu\n", epochSeconds);
        sendAck(from, nh, seq, "SET_TIME", "OK", "SYNCED");
      }
    }
  }
  else if (c.startsWith("SET_SCHED|")) {
    int idxEn = c.indexOf("EN=");
    int idxOn = c.indexOf("ON=");
    int idxOff = c.indexOf("OFF=");
    bool sEn = (idxEn >= 0) ? (c.substring(idxEn + 3, idxEn + 4).toInt() == 1) : storageMgr.isScheduleEnabled();
    int sOn = (idxOn >= 0) ? c.substring(idxOn + 3, c.indexOf('|', idxOn)).toInt() : storageMgr.getScheduleOnMin();
    int sOff = (idxOff >= 0) ? c.substring(idxOff + 4).toInt() : storageMgr.getScheduleOffMin();

    storageMgr.saveSchedule(sEn, sOn, sOff);
    sendAck(from, nh, seq, "SET_SCHED", "OK", sEn ? "ENABLED" : "DISABLED");
  }
  else if (c.startsWith("REGISTERED|")) {
    int p1 = c.indexOf('|');
    int p2 = c.indexOf('|', p1 + 1);
    int p3 = c.indexOf('|', p2 + 1);
    if (p1 > 0 && p2 > 0) {
      String newLogId = c.substring(p1 + 1, p2);
      String newName  = (p3 > 0) ? c.substring(p2 + 1, p3) : c.substring(p2 + 1);
      String newType  = (p3 > 0) ? c.substring(p3 + 1) : "STREET";

      storageMgr.setRegistration(newLogId, newName, newType);
      Serial.printf("[POLE][REG] Confirmed registration: ID=%s, Name=%s, Type=%s\n",
                    newLogId.c_str(), newName.c_str(), newType.c_str());
      sendStatus(from, seq);
    }
  }
  else if (c == "TEST_MEAS" || c == "RUN_TESTS") {
    bool allPass = MeasurementTests::runAllTests();
    sendAck(from, nh, seq, "TEST_MEAS", allPass ? "PASS" : "FAIL", allPass ? "17_OF_17" : "DEGRADED");
  }
  else if (c.startsWith("CAL_ZERO|")) {
    uint8_t ch = (uint8_t)c.substring(9).toInt();
    bool ok = calibMgr.performZeroCalibration(ch, 0.0f);
    sendAck(from, nh, seq, "CAL_ZERO", ok ? "OK" : "ERR", String(ch));
  }
  else if (c.startsWith("CAL_SPAN|")) {
    int p1 = c.indexOf('|');
    int p2 = c.indexOf('|', p1 + 1);
    if (p1 > 0 && p2 > 0) {
      uint8_t ch = (uint8_t)c.substring(p1 + 1, p2).toInt();
      float refVal = c.substring(p2 + 1).toFloat();
      float meas = (ch < 3) ? metrologyEngine.getCommonVoltageR() : metrologyEngine.getFeederCurrent((ch - 3)/3 + 1, 'R');
      bool ok = calibMgr.performSpanCalibration(ch, refVal, meas);
      sendAck(from, nh, seq, "CAL_SPAN", ok ? "OK" : "ERR", String(ch));
    }
  }
  else if (c.startsWith("CAL_RESET|")) {
    String pin = c.substring(10);
    bool ok = calibMgr.resetToFactoryDefaults(pin);
    sendAck(from, nh, seq, "CAL_RESET", ok ? "OK" : "REJECTED_UNAUTH", ok ? "FACTORY_DEFAULT" : "DENIED");
  }
  else if (c.startsWith("REVOKE|") || c.startsWith("REVOKED|")) {
    int p1 = c.indexOf('|');
    int p2 = c.indexOf('|', p1 + 1);
    String txId = (p1 > 0 && p2 > 0) ? c.substring(p1 + 1, p2) : ((p1 > 0) ? c.substring(p1 + 1) : "REVOKED");

    Serial.printf("[POLE][SECURITY] 🔒 Received authoritative REVOKE command (Tx: %s). De-registering...\n", txId.c_str());
    storageMgr.setRevoked(txId, "AUTHORITATIVE_REVOCATION");
    meshMgr.init();
    sendAck(from, nh, seq, "REVOKE", "OK", "REVOKED|" + txId);
  }
  else if (c.startsWith("REVOKE_SYNC|")) {
    int p1 = c.indexOf('|');
    int p2 = c.indexOf('|', p1 + 1);
    String revokedNode = (p1 > 0 && p2 > 0) ? c.substring(p1 + 1, p2) : ((p1 > 0) ? c.substring(p1 + 1) : "");
    if (revokedNode.length() > 0) {
      meshMgr.purgeRevokedNode(revokedNode);
    }
  }
  else if (c.startsWith("ROUTE_MIGRATE|")) {
    int p1 = c.indexOf('|');
    int p2 = c.indexOf('|', p1 + 1);
    int p3 = c.indexOf('|', p2 + 1);
    String childId = (p1 > 0 && p2 > 0) ? c.substring(p1 + 1, p2) : "";
    String newPrim = (p2 > 0 && p3 > 0) ? c.substring(p2 + 1, p3) : ((p2 > 0) ? c.substring(p2 + 1) : "");

    String myLog = storageMgr.getNodeId();
    String myHw  = storageMgr.getHwNodeId();

    if (childId == myLog || childId == myHw || childId == "ALL") {
      meshMgr.exitOrphanState(newPrim, 2, 50);
      meshMgr.addOrUpdateRoute(GATEWAY_ID, newPrim, 1, 50, -80);
      sendAck(from, nh, seq, "ROUTE_MIGRATE", "OK", "MIGRATED|" + newPrim);
    }
  }
  else if (c.startsWith("FDR_DEACTIVATE|")) {
    int p1 = c.indexOf('|');
    int p2 = c.indexOf('|', p1 + 1);
    uint8_t fdrId = (p1 > 0 && p2 > 0) ? (uint8_t)c.substring(p1 + 1, p2).toInt() : ((p1 > 0) ? (uint8_t)c.substring(p1 + 1).toInt() : 0);

    if (fdrId >= 1 && fdrId <= MAX_FEEDERS) {
      feederMgr.deactivateFeeder(fdrId - 1);
      sendAck(from, nh, seq, "FDR_DEACTIVATE", "OK", "DEACTIVATED|" + String(fdrId));
    }
  }
  // ── 5. CENTRAL CONFIGURATION & FIRMWARE LIFECYCLE (PROMPT 20) ──
  else if (c.startsWith("CFG_SET|") || c.startsWith("CFG_ROLLBACK|") || c.startsWith("CFG_GET|")) {
    String ackData;
    poleConfigMgr.processConfigPacket(c, from, seq, ackData);
    loraMgr.txMesh("CONFIG_ACK", from, nh, DEFAULT_TTL, seq, ackData);
  }
  else if (c.startsWith("START_MAINT_AP") || c == "MAINT_AP") {
    String pin = "MSCB_Admin_2026";
    int p1 = c.indexOf('|');
    if (p1 > 0) pin = c.substring(p1 + 1);
    String ssid, ip;
    bool ok = poleFwMgr.startMaintenanceAP(pin.c_str(), ssid, ip);
    String ackPayload = "STATUS=" + String(ok ? "AP_ACTIVE" : "ERR") + "|SSID=" + ssid + "|IP=" + ip;
    loraMgr.txMesh("ACK", from, nh, DEFAULT_TTL, seq, "START_MAINT_AP|" + ackPayload);
  }
  else if (c == "STOP_MAINT_AP") {
    poleFwMgr.stopMaintenanceAP();
    sendAck(from, nh, seq, "STOP_MAINT_AP", "OK", "LORA_RESUMED");
  }
}

void CommandManager::checkTimerExpiration() {
  if (timerActive && millis() >= timerEndMs) {
    timerActive = false;
    feederMgr.setMainRelay(false, "TIMER_EXPIRED");
    Serial.println(F("[POLE][TIMER] Timer expired. Relay turned OFF."));
  }
}

void CommandManager::checkAutonomousSchedule() {
  if (!storageMgr.isScheduleEnabled() || !hasTimeSync) return;
  int sOn = storageMgr.getScheduleOnMin();
  int sOff = storageMgr.getScheduleOffMin();
  if (sOn < 0 || sOff < 0) return;

  unsigned long nowSec = epochSeconds + ((millis() - epochMillis) / 1000);
  int curMin = (nowSec % 86400) / 60;

  if (curMin == lastSchedChecked) return;
  lastSchedChecked = curMin;

  if (curMin == sOn && !feederMgr.isMainRelayOn()) {
    feederMgr.setMainRelay(true, "AUTO_SCHEDULE");
  } else if (curMin == sOff && feederMgr.isMainRelayOn()) {
    feederMgr.setMainRelay(false, "AUTO_SCHEDULE");
  }
}

bool CommandManager::isTimerActive() const { return timerActive; }

unsigned long CommandManager::getRemainingTimerSec() const {
  if (!timerActive || millis() >= timerEndMs) return 0;
  return (timerEndMs - millis()) / 1000;
}
