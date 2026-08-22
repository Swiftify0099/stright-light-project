/*
 * =====================================================================
 *  COMMANDMANAGER.CPP — FIELD POLE COMMAND PROCESSOR & ACK DISPATCHER
 * =====================================================================
 */

#include "CommandManager.h"
#include "LoRaManager.h"
#include "MeshManager.h"
#include "FeederManager.h"
#include "VoltageSensor.h"
#include "CurrentSensor.h"
#include "StorageManager.h"

CommandManager cmdMgr;

CommandManager::CommandManager() :
  timerActive(false), timerEndMs(0), timerDurationMs(0),
  epochSeconds(0), epochMillis(0), hasTimeSync(false),
  lastSchedChecked(-1), newNodeRetries(0)
{}

void CommandManager::init() {}

void CommandManager::sendAck(const String &to, const String &nextHop, uint16_t seq, const String &cmd, const String &status, const String &state) {
  float vR = voltageSensor.getVoltageR();
  String vsR = voltageSensor.getVoltageStateR();
  float a = currentSensor.getCurrent();

  String ackData = cmd + "|" + status + "|STATE=" + state + "|V=" + String(vR, 1) + "|VS=" + vsR + "|A=" + String(a, 2);
  loraMgr.txMesh("ACK", to, nextHop, DEFAULT_TTL, seq, ackData);
}

void CommandManager::sendStatus(const String &replyTo, uint16_t seq) {
  voltageSensor.update();
  float vR = voltageSensor.getVoltageR();
  float vY = voltageSensor.getVoltageY();
  currentSensor.update(vR, feederMgr.isMainRelayOn());

  float a = currentSensor.getCurrent();
  float p = currentSensor.getPower();
  float kwh = currentSensor.getEnergyWh() / 1000.0f;
  String flt = currentSensor.getFaultStatus();
  String lgt = feederMgr.isMainRelayOn() ? "ON" : "OFF";
  String vsR = voltageSensor.getVoltageStateR();

  char buf[160];
  snprintf(buf, sizeof(buf), "V=%.1f|VS=%s|A=%.2f|P=%.1f|KWH=%.2f|LGT=%s|FLT=%s|UP=%lu",
           vR, vsR.c_str(), a, p, kwh, lgt.c_str(), flt.c_str(), (unsigned long)(millis() / 1000));

  String statusData = String(buf);
  String fdrData = feederMgr.buildFeederTelemetryString(vR, vY);
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

  if (c == "LIGHT_ON" || c == "PUMP_ON" || c == "ON" || c == "1") {
    if (timerActive) timerActive = false;
    feederMgr.setMainRelay(true, "TELEGRAM_CMD");
    voltageSensor.update();
    currentSensor.update(voltageSensor.getVoltageR(), true);
    sendAck(from, nh, seq, c.startsWith("PUMP") ? "PUMP_ON" : "LIGHT_ON", "OK", "ON");
  }
  else if (c == "LIGHT_OFF" || c == "PUMP_OFF" || c == "OFF" || c == "0") {
    if (timerActive) timerActive = false;
    feederMgr.setMainRelay(false, "TELEGRAM_CMD");
    voltageSensor.update();
    currentSensor.update(voltageSensor.getVoltageR(), false);
    sendAck(from, nh, seq, c.startsWith("PUMP") ? "PUMP_OFF" : "LIGHT_OFF", "OK", "OFF");
  }
  else if (c.startsWith("FDR_ON|")) {
    int fdrIdx = c.substring(7).toInt() - 1;
    if (fdrIdx >= 0 && fdrIdx < MAX_FEEDERS) {
      feederMgr.setFeederRelay(fdrIdx, true, "CMD");
      sendAck(from, nh, seq, "FDR_ON", "OK", "ON");
    }
  }
  else if (c.startsWith("FDR_OFF|")) {
    int fdrIdx = c.substring(8).toInt() - 1;
    if (fdrIdx >= 0 && fdrIdx < MAX_FEEDERS) {
      feederMgr.setFeederRelay(fdrIdx, false, "CMD");
      sendAck(from, nh, seq, "FDR_OFF", "OK", "OFF");
    }
  }
  else if (c == "GET_STATUS" || c == "STATUS" || c == "PUMP_STATUS") {
    sendStatus(from, seq);
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
  // Safe Reset / Reboot command
  else if (c == "RESET" || c == "REBOOT") {
    Serial.printf("[POLE][RESET] Received REBOOT command (Seq: %u). Sending ACK and rebooting...\n", seq);
    sendAck(from, nh, seq, "RESET", "OK", "REBOOTING");
    delay(300); // Allow LoRa TX to finish cleanly
    ESP.restart(); // Software restart; does NOT wipe NVS
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
