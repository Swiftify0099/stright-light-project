/*
 * =====================================================================
 *  FEEDERMANAGER.CPP — FIELD POLE MULTI-FEEDER & MCB CONTROLLER
 * =====================================================================
 */

#include "FeederManager.h"
#include "StorageManager.h"

FeederManager feederMgr;

FeederManager::FeederManager() : lastDebounceMs(0) {
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    feeders[i].relayOn = false;
    feeders[i].mcbR = true;
    feeders[i].mcbY = true;
    feeders[i].mcbB = true;
    feeders[i].contactor = false;
    feeders[i].mcbRDebounceCount = 0;
    feeders[i].mcbYDebounceCount = 0;
    feeders[i].mcbBDebounceCount = 0;
    feeders[i].faultState = "OK";
  }
}

void FeederManager::init() {
  // Strap pin 12 safe init
  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);

  // Main relay pin (GPIO 26)
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_STATE_OFF);

  // Feeder relay output pins
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    uint8_t pin = FEEDER_RELAY_PINS[i];
    pinMode(pin, OUTPUT);
    digitalWrite(pin, RELAY_STATE_OFF);
    feeders[i].relayOn = false;
  }

  // MCB input pins with internal pullup
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    if (FEEDER_MCB_R_PINS[i] > 0) pinMode(FEEDER_MCB_R_PINS[i], INPUT_PULLUP);
    if (FEEDER_MCB_Y_PINS[i] > 0) pinMode(FEEDER_MCB_Y_PINS[i], INPUT_PULLUP);
    if (FEEDER_MCB_B_PINS[i] > 0) pinMode(FEEDER_MCB_B_PINS[i], INPUT_PULLUP);
  }

  // Restore main relay state from NVS
  String savedState = storageMgr.getPrevRelayState();
  if (savedState == "ON") {
    setMainRelay(true, "POWER_RECOVERY");
  } else {
    setMainRelay(false, "BOOT_INIT");
  }
}

void FeederManager::setFeederRelay(uint8_t feederIndex, bool on, const String &reason) {
  if (feederIndex >= MAX_FEEDERS) return;

  uint8_t pin = FEEDER_RELAY_PINS[feederIndex];
  uint8_t pinState = on ? RELAY_STATE_ON : RELAY_STATE_OFF;
  digitalWrite(pin, pinState);
  feeders[feederIndex].relayOn = on;

  if (feederIndex == 0) {
    storageMgr.saveRelayState(on ? "ON" : "OFF");
  }

  Serial.printf("[POLE][FDR] Feeder %d (Pin %d) -> %s (Reason: %s)\n",
                feederIndex + 1, pin, on ? "ON" : "OFF", reason.c_str());
}

void FeederManager::setMainRelay(bool on, const String &reason) {
  setFeederRelay(0, on, reason);
}

bool FeederManager::isMainRelayOn() const {
  return feeders[0].relayOn;
}

bool FeederManager::isFeederRelayOn(uint8_t feederIndex) const {
  if (feederIndex < MAX_FEEDERS) {
    return feeders[feederIndex].relayOn;
  }
  return false;
}

void FeederManager::updateMCBInputs() {
  unsigned long now = millis();
  if (now - lastDebounceMs < FEEDER_DEBOUNCE_MS) return;
  lastDebounceMs = now;

  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    // MCB R Phase (HIGH = Normal, LOW = Tripped)
    if (FEEDER_MCB_R_PINS[i] > 0) {
      bool rawHigh = (digitalRead(FEEDER_MCB_R_PINS[i]) == HIGH);
      if (rawHigh) {
        if (feeders[i].mcbRDebounceCount < FEEDER_DEBOUNCE_SAMPLES) feeders[i].mcbRDebounceCount++;
      } else {
        if (feeders[i].mcbRDebounceCount > 0) feeders[i].mcbRDebounceCount--;
      }
      feeders[i].mcbR = (feeders[i].mcbRDebounceCount >= (FEEDER_DEBOUNCE_SAMPLES / 2));
    } else {
      feeders[i].mcbR = true; // Pin disabled / wired directly
    }

    // MCB Y Phase
    if (FEEDER_MCB_Y_PINS[i] > 0) {
      bool rawHigh = (digitalRead(FEEDER_MCB_Y_PINS[i]) == HIGH);
      if (rawHigh) {
        if (feeders[i].mcbYDebounceCount < FEEDER_DEBOUNCE_SAMPLES) feeders[i].mcbYDebounceCount++;
      } else {
        if (feeders[i].mcbYDebounceCount > 0) feeders[i].mcbYDebounceCount--;
      }
      feeders[i].mcbY = (feeders[i].mcbYDebounceCount >= (FEEDER_DEBOUNCE_SAMPLES / 2));
    } else {
      feeders[i].mcbY = true;
    }

    // Fault state update for feeder
    if (!feeders[i].mcbR || !feeders[i].mcbY) {
      feeders[i].faultState = "MCB_TRIP";
    } else {
      feeders[i].faultState = "OK";
    }
  }
}

bool FeederManager::isMCBOk(uint8_t feederIndex) const {
  if (feederIndex < MAX_FEEDERS) {
    return (feeders[feederIndex].mcbR && feeders[feederIndex].mcbY);
  }
  return true;
}

String FeederManager::buildFeederTelemetryString(float vR, float vY) {
  String out = "";
  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    String rly = feeders[i].relayOn ? "ON" : "OFF";
    String mcb = isMCBOk(i) ? "1" : "0";
    String flt = feeders[i].faultState;

    char fBuf[48];
    snprintf(fBuf, sizeof(fBuf), "F%d:%s,%.1f,%.1f,%s,%s",
             i + 1, rly.c_str(), vR, vY, flt.c_str(), mcb.c_str());

    out += String(fBuf);
    if (i < MAX_FEEDERS - 1) out += "|";
  }
  return out;
}

FeederHardwareState* FeederManager::getFeederState(uint8_t feederIndex) {
  if (feederIndex < MAX_FEEDERS) return &feeders[feederIndex];
  return nullptr;
}
