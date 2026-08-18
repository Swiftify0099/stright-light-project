/*
 * =====================================================================
 *  SMART STREET LIGHT / FARMER MESH SYSTEM — FIELD POLE FIRMWARE
 *  Version   : POLE_V10
 *  Platform  : ESP32 Dev Module + SX1278 RA-02 433 MHz + Relay + Sensors
 *  Role      : Autonomous Field Node / Relay Controller / Mesh Router
 * =====================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>
#include "config.h"

// ─────────────────────────────────────────────────────────────
//  HARDWARE IDENTIFIERS & NODE STATE
// ─────────────────────────────────────────────────────────────
static String HW_NODE_ID     = "";      // Stable 8-char hex chip ID (e.g. "A4CF12E8")
static String NODE_ID        = "";      // Logical ID assigned by Gateway (e.g. "P1", "FR-1")
static String NODE_TYPE      = "STREET";// "STREET", "FARMER", "CUSTOM"
static String NODE_NAME      = "";
static String NODE_LOC       = "Field Area";
static bool   isRegistered   = false;
static int    newNodeRetries = 0;

static bool   lightOn        = false;
static String prevRelayState = "OFF";

static Preferences prefs;

// ─────────────────────────────────────────────────────────────
//  TIMER & SCHEDULING STATE
// ─────────────────────────────────────────────────────────────
static bool          timerActive       = false;
static unsigned long timerEndMs        = 0;
static unsigned long timerDurationMs   = 0;

// Soft RTC synced from Gateway
static unsigned long epochSeconds      = 0;
static unsigned long epochMillis       = 0;
static bool          hasTimeSync       = false;

// Autonomous Local Schedule
static bool schedEnabled     = false;
static int  schedOnMin       = -1; // Minutes from midnight (0..1439)
static int  schedOffMin      = -1;
static int  lastSchedChecked = -1;

// ─────────────────────────────────────────────────────────────
//  SENSOR READINGS & ENERGY ACCUMULATION
// ─────────────────────────────────────────────────────────────
static float voltageV = 0.0f;
static float currentA = 0.0f;
static float powerW   = 0.0f;
static float energyWh = 0.0f;
static unsigned long lastEnergyMs = 0;

static String currentFault      = "OK";
static String lastReportedFault = "OK";

// ─────────────────────────────────────────────────────────────
//  MULTI-FEEDER DATA STRUCTURES
// ─────────────────────────────────────────────────────────────
struct FeederState {
  bool relayOn;
  bool mcbR;
  bool mcbY;
  bool mcbB;
};

static FeederState feeders[MAX_FEEDERS];
static uint8_t feederCount = MAX_FEEDERS;

// ─────────────────────────────────────────────────────────────
//  NEIGHBOR TABLE (Local Mesh Discovery)
// ─────────────────────────────────────────────────────────────
struct NeighborEntry {
  String   nodeId;
  int      rssi;
  int      snr;
  unsigned long lastSeen;
  bool     active;
  bool     healthReplied;
};

static NeighborEntry neighbors[MAX_NEIGHBORS];
static int neighborCount = 0;

// ─────────────────────────────────────────────────────────────
//  POLE ROUTING TABLE
// ─────────────────────────────────────────────────────────────
struct PoleRouteEntry {
  String   destination;
  String   nextHop;
  int      hopCount;
  int      rssi;
  unsigned long lastSeen;
  bool     valid;
};

static PoleRouteEntry poleRoutes[MAX_ROUTES_POLE];
static int poleRouteCount = 0;

// ─────────────────────────────────────────────────────────────
//  PACKET DEDUPLICATION CACHE & SEQUENCE GENERATOR
// ─────────────────────────────────────────────────────────────
struct PoleSeqCacheEntry {
  String   src;
  uint16_t seq;
};

static PoleSeqCacheEntry seqCache[MAX_SEQ_CACHE];
static int seqHead = 0;
static uint16_t nodeMasterSeq = 200;

// ─────────────────────────────────────────────────────────────
//  TASK TIMING COUNTERS
// ─────────────────────────────────────────────────────────────
static unsigned long tSensorRead    = 0;
static unsigned long tBeacon        = 0;
static unsigned long tNewNodeRetry  = 0;
static unsigned long tSchedCheck    = 0;
static unsigned long tNvsFlush      = 0;
static unsigned long tTimerCheck    = 0;

// Deep Health Check state
static bool   healthCheckActive     = false;
static String currentHealthCheckId  = "";
static String healthCheckOrigin     = "";
static unsigned long healthCheckStartMs = 0;

static int nbPingsSent = 0;
static int nbPongsReceived = 0;
static bool nbResponded[MAX_NEIGHBORS];

// ─────────────────────────────────────────────────────────────
//  FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────
uint16_t nextSeq();
void   initHardware();
void   initLoRa();
void   generateHwNodeId();
void   loadPoleStateFromNVS();
void   savePoleStateToNVS();

void   setRelay(bool on, const String &reason = "MANUAL");
void   updateSensors();
void   checkFeederMCBs();
void   checkAutonomousSchedule();
void   checkTimerExpiration();

void   processIncomingLoRa();
bool   parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out);
String buildPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);
void   txMesh(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data);

void   processCommand(const String &cmd, const String &from, uint16_t seq, int rssi, int snr);
void   sendAck(const String &to, const String &nextHop, uint16_t seq, const String &cmd, const String &status, const String &state);
void   sendStatus(const String &replyTo, uint16_t seq);
void   sendNewNode();
void   sendBeacon();

void   addOrUpdateNeighbor(const String &id, int rssi, int snr);
void   expireNeighbors();
String findNextHop(const String &dst);
void   addOrUpdateRoute(const String &dst, const String &nextHop, int hops, int rssi);

void   startNeighborHealthCheck(const String &origin, const String &checkId);
void   checkHealthCheckTimeout();
void   sendHealthReport();

// ─────────────────────────────────────────────────────────────
//  SEQUENCE GENERATOR
// ─────────────────────────────────────────────────────────────
uint16_t nextSeq() {
  nodeMasterSeq++;
  if (nodeMasterSeq == 0 || nodeMasterSeq < 200) nodeMasterSeq = 200;
  return nodeMasterSeq;
}

// ─────────────────────────────────────────────────────────────
//  HARDWARE INITIALIZATION
// ─────────────────────────────────────────────────────────────
void generateHwNodeId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[16];
  snprintf(buf, sizeof(buf), "%04X%04X", (uint16_t)(mac >> 32), (uint16_t)(mac & 0xFFFF));
  HW_NODE_ID = String(buf);
}

void initHardware() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\n=================================================="));
  Serial.printf("  SMART STREET LIGHT FIELD POLE FIRMWARE %s\n", POLE_VERSION);
  Serial.println(F("=================================================="));

  generateHwNodeId();
  Serial.printf("[POLE] Hardware Chip ID: %s\n", HW_NODE_ID.c_str());

  pinMode(12, OUTPUT);
  digitalWrite(12, LOW);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_STATE_OFF);

  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    uint8_t pin = FEEDER_RELAY_PINS[i];
    pinMode(pin, OUTPUT);
    digitalWrite(pin, RELAY_STATE_OFF);
    feeders[i].relayOn = false;
  }

  for (uint8_t i = 0; i < MAX_FEEDERS; i++) {
    if (FEEDER_MCB_R_PINS[i] > 0) pinMode(FEEDER_MCB_R_PINS[i], INPUT_PULLUP);
    if (FEEDER_MCB_Y_PINS[i] > 0) pinMode(FEEDER_MCB_Y_PINS[i], INPUT_PULLUP);
    if (FEEDER_MCB_B_PINS[i] > 0) pinMode(FEEDER_MCB_B_PINS[i], INPUT_PULLUP);
  }

  pinMode(CURRENT_PIN, INPUT);
  pinMode(VOLTAGE_R_PIN, INPUT);
  pinMode(VOLTAGE_Y_PIN, INPUT);
  pinMode(VOLTAGE_B_PIN, INPUT);
  analogReadResolution(12);

  loadPoleStateFromNVS();

  if (prevRelayState == "ON") {
    setRelay(true, "POWER_RECOVERY");
  } else {
    setRelay(false, "BOOT_INIT");
  }
}

// ─────────────────────────────────────────────────────────────
//  LORA RADIO INITIALIZATION
// ─────────────────────────────────────────────────────────────
void initLoRa() {
  Serial.println(F("[LoRa] Initializing..."));
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println(F("[LoRa] FAILED! Check SPI wiring and SX1278 power."));
    while (1) { delay(1000); }
  }

  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setTxPower(LORA_TXPOW, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  if (LORA_CRC_ENABLED) LoRa.enableCrc();
  LoRa.receive();

  Serial.printf("[LoRa] Frequency = %.1f MHz\n", LORA_FREQ / 1E6);
  Serial.printf("[LoRa] SF = %d\n", LORA_SF);
  Serial.printf("[LoRa] BW = %.1f kHz\n", LORA_BW / 1E3);
  Serial.println(F("[LoRa] CR = 4/5"));
  Serial.printf("[LoRa] TX Power = %d dBm\n", LORA_TXPOW);
  Serial.printf("[LoRa] Sync Word = 0x%02X\n", LORA_SYNC_WORD);
  Serial.println(F("[LoRa] Ready"));
}

// ─────────────────────────────────────────────────────────────
//  RELAY CONTROL & HARDWARE DRIVING
// ─────────────────────────────────────────────────────────────
void setRelay(bool on, const String &reason) {
  lightOn = on;
  uint8_t pinState = on ? RELAY_STATE_ON : RELAY_STATE_OFF;
  digitalWrite(RELAY_PIN, pinState);
  feeders[0].relayOn = on;

  prevRelayState = on ? "ON" : "OFF";
  prefs.begin(NVS_NS_POLE, false);
  prefs.putString("relay", prevRelayState);
  prefs.end();

  Serial.printf("[POLE][RELAY] Physical state changed: %s (Reason: %s, Pin: %d)\n",
                on ? "ON" : "OFF", reason.c_str(), pinState);
}

// ─────────────────────────────────────────────────────────────
//  SENSOR READINGS (RMS CALCULATION)
// ─────────────────────────────────────────────────────────────
void updateSensors() {
  uint32_t sumVsq = 0;
  const int SAMPLES = 80;
  for (int i = 0; i < SAMPLES; i++) {
    int adc = analogRead(VOLTAGE_R_PIN);
    int diff = adc - 2048;
    sumVsq += (uint32_t)(diff * diff);
    delayMicroseconds(250);
  }
  float meanVsq = (float)sumVsq / SAMPLES;
  float rawVrms = sqrt(meanVsq) * (ADC_VREF / ADC_MAX);
  voltageV = rawVrms * VMPT_CALIBRATION;
  if (voltageV < 40.0f) voltageV = 0.0f;

  uint32_t sumIsq = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int adc = analogRead(CURRENT_PIN);
    int diff = adc - ACS712_ZERO_ADC;
    sumIsq += (uint32_t)(diff * diff);
    delayMicroseconds(250);
  }
  float meanIsq = (float)sumIsq / SAMPLES;
  float rawIrms = sqrt(meanIsq) * (ADC_VREF / ADC_MAX);
  currentA = rawIrms / ACS712_SENSITIVITY;
  if (currentA < 0.08f) currentA = 0.0f;

  powerW = voltageV * currentA;
  unsigned long now = millis();
  if (lastEnergyMs > 0 && now > lastEnergyMs) {
    float hours = (now - lastEnergyMs) / 3600000.0f;
    energyWh += powerW * hours;
  }
  lastEnergyMs = now;

  if (voltageV > 270.0f) {
    currentFault = "OVER_VOLT";
  } else if (voltageV > 0.0f && voltageV < 165.0f) {
    currentFault = "UNDER_VOLT";
  } else if (currentA > 16.0f) {
    currentFault = "OVER_CURR";
  } else if (lightOn && currentA < 0.05f && voltageV > 200.0f) {
    currentFault = "LAMP_FAIL";
  } else {
    currentFault = "OK";
  }
}

// ─────────────────────────────────────────────────────────────
//  AUTONOMOUS LOCAL SCHEDULE & TIMERS
// ─────────────────────────────────────────────────────────────
void checkTimerExpiration() {
  if (timerActive && millis() >= timerEndMs) {
    timerActive = false;
    setRelay(false, "TIMER_EXPIRED");
    Serial.println(F("[POLE][TIMER] 30-min timer expired. Relay turned OFF."));
  }
}

void checkAutonomousSchedule() {
  if (!schedEnabled || !hasTimeSync || schedOnMin < 0 || schedOffMin < 0) return;

  unsigned long nowSec = epochSeconds + ((millis() - epochMillis) / 1000);
  int curMin = (nowSec % 86400) / 60;

  if (curMin == lastSchedChecked) return;
  lastSchedChecked = curMin;

  if (curMin == schedOnMin && !lightOn) {
    setRelay(true, "AUTO_SCHEDULE");
  } else if (curMin == schedOffMin && lightOn) {
    setRelay(false, "AUTO_SCHEDULE");
  }
}

// ─────────────────────────────────────────────────────────────
//  NEIGHBOR & ROUTE MANAGEMENT
// ─────────────────────────────────────────────────────────────
void addOrUpdateNeighbor(const String &id, int rssi, int snr) {
  if (id.length() == 0 || id == HW_NODE_ID || (NODE_ID.length() > 0 && id == NODE_ID)) return;

  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].nodeId == id) {
      neighbors[i].rssi = rssi;
      neighbors[i].snr  = snr;
      neighbors[i].lastSeen = millis();
      neighbors[i].active = true;
      return;
    }
  }

  if (neighborCount < MAX_NEIGHBORS) {
    neighbors[neighborCount].nodeId = id;
    neighbors[neighborCount].rssi = rssi;
    neighbors[neighborCount].snr  = snr;
    neighbors[neighborCount].lastSeen = millis();
    neighbors[neighborCount].active = true;
    neighbors[neighborCount].healthReplied = false;
    neighborCount++;
  }
}

void expireNeighbors() {
  unsigned long now = millis();
  for (int i = 0; i < neighborCount; i++) {
    if (now - neighbors[i].lastSeen >= NEIGHBOR_TIMEOUT_MS) {
      neighbors[i].active = false;
    }
  }
}

String findNextHop(const String &dst) {
  if (dst == BROADCAST_ID || dst == "ALL") return BROADCAST_ID;
  if (dst == GATEWAY_ID) return GATEWAY_ID;

  for (int i = 0; i < poleRouteCount; i++) {
    if (poleRoutes[i].destination == dst && poleRoutes[i].valid) {
      return poleRoutes[i].nextHop;
    }
  }
  return dst;
}

void addOrUpdateRoute(const String &dst, const String &nextHop, int hops, int rssi) {
  if (dst.length() == 0 || dst == HW_NODE_ID || (NODE_ID.length() > 0 && dst == NODE_ID)) return;

  for (int i = 0; i < poleRouteCount; i++) {
    if (poleRoutes[i].destination == dst) {
      if (hops <= poleRoutes[i].hopCount || !poleRoutes[i].valid) {
        poleRoutes[i].nextHop = nextHop;
        poleRoutes[i].hopCount = hops;
        poleRoutes[i].rssi = rssi;
        poleRoutes[i].valid = true;
        poleRoutes[i].lastSeen = millis();
      }
      return;
    }
  }

  if (poleRouteCount < MAX_ROUTES_POLE) {
    poleRoutes[poleRouteCount].destination = dst;
    poleRoutes[poleRouteCount].nextHop = nextHop;
    poleRoutes[poleRouteCount].hopCount = hops;
    poleRoutes[poleRouteCount].rssi = rssi;
    poleRoutes[poleRouteCount].valid = true;
    poleRoutes[poleRouteCount].lastSeen = millis();
    poleRouteCount++;
  }
}

// ─────────────────────────────────────────────────────────────
//  PACKET BUILDER & TRANSMITTER
// ─────────────────────────────────────────────────────────────
String buildPacket(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String mySrc = (NODE_ID.length() > 0) ? NODE_ID : HW_NODE_ID;
  String nh = (nextHop.length() > 0) ? nextHop : BROADCAST_ID;
  return type + "|" + mySrc + "|" + dst + "|" + nh + "|" + String(ttl) + "|" + String(seq) + "|" + data;
}

bool parsePacket(const String &raw, int rssi, int snr, ParsedPacket &out) {
  out.valid = false;
  out.rssi = rssi;
  out.snr  = snr;

  String fields[7];
  int fieldCount = 0;
  int start = 0;
  for (int i = 0; i <= (int)raw.length(); i++) {
    bool isEnd = (i == (int)raw.length());
    bool isPipe = (!isEnd && raw[i] == '|' && fieldCount < 6);
    if (isEnd || isPipe) {
      fields[fieldCount++] = raw.substring(start, i);
      start = i + 1;
    }
  }

  if (fieldCount < 6) return false;

  out.type    = fields[0];
  out.src     = fields[1];
  out.dst     = fields[2];
  out.nextHop = fields[3];
  out.ttl     = fields[4].toInt();
  out.seq     = (uint16_t)fields[5].toInt();
  out.data    = (fieldCount == 7) ? fields[6] : "";
  out.valid   = true;
  return true;
}

void txMesh(const String &type, const String &dst, const String &nextHop, int ttl, uint16_t seq, const String &data) {
  String packet = buildPacket(type, dst, nextHop, ttl, seq, data);

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  LoRa.receive();

  Serial.printf("[POLE][LORA] TX: %s\n", packet.c_str());
}

// ─────────────────────────────────────────────────────────────
//  ACK & RESPONSE GENERATORS
// ─────────────────────────────────────────────────────────────
void sendAck(const String &to, const String &nextHop, uint16_t seq, const String &cmd, const String &status, const String &state) {
  String ackData = cmd + "|" + status + "|STATE=" + state + "|V=" + String(voltageV, 1) + "|A=" + String(currentA, 2);
  txMesh("ACK", to, nextHop, DEFAULT_TTL, seq, ackData);
}

void sendStatus(const String &replyTo, uint16_t seq) {
  updateSensors();
  char buf[128];
  snprintf(buf, sizeof(buf), "V=%.1f|A=%.2f|P=%.1f|KWH=%.2f|LGT=%s|FLT=%s|UP=%lu",
           voltageV, currentA, powerW, (energyWh / 1000.0f),
           lightOn ? "ON" : "OFF", currentFault.c_str(), (unsigned long)(millis() / 1000));

  String nh = findNextHop(replyTo);
  txMesh("STATUS", replyTo, nh, DEFAULT_TTL, (seq > 0 ? seq : nextSeq()), String(buf));
}

void sendNewNode() {
  char buf[96];
  snprintf(buf, sizeof(buf), "HW_ID=%s|FW=%s|TYPE=%s|PREV=%s",
           HW_NODE_ID.c_str(), POLE_VERSION, NODE_TYPE.c_str(), prevRelayState.c_str());

  txMesh("NEW_NODE", GATEWAY_ID, BROADCAST_ID, DEFAULT_TTL, nextSeq(), String(buf));
  newNodeRetries++;
  Serial.printf("[POLE][REG] Sent NEW_NODE (Attempt %d)\n", newNodeRetries);
}

void sendBeacon() {
  char bData[64];
  snprintf(bData, sizeof(bData), "HW_ID=%s|REG=%d", HW_NODE_ID.c_str(), isRegistered ? 1 : 0);
  txMesh("BEACON", BROADCAST_ID, BROADCAST_ID, 1, nextSeq(), String(bData));
}

void sendHealthReport() {
  // Format: CHK_ID|uptime|count|nodeId,resp,rssi,snr,age|...
  String rep = String(currentHealthCheckId) + "|" + String(millis() / 1000) + "|" + String(neighborCount);
  unsigned long now = millis();
  for (int i = 0; i < neighborCount; i++) {
    if (neighbors[i].active) {
      rep += "|" + neighbors[i].nodeId + "," + (nbResponded[i] ? "1" : "0") + "," +
             String(neighbors[i].rssi) + "," + String(neighbors[i].snr) + "," +
             String((now - neighbors[i].lastSeen) / 1000);
    }
  }
  txMesh("NBHEALTHREPORT", GATEWAY_ID, findNextHop(GATEWAY_ID), DEFAULT_TTL, nextSeq(), rep);
  Serial.printf("[POLE][NEIGHBOR] Health report sent: %s\n", rep.c_str());
}

// ─────────────────────────────────────────────────────────────
//  COMMAND PROCESSOR & EXECUTION
// ─────────────────────────────────────────────────────────────
void processCommand(const String &cmd, const String &from, uint16_t seq, int rssi, int snr) {
  String c = cmd;
  c.trim();
  Serial.printf("[POLE] Parsed command = %s (From: %s, Seq: %u, RSSI: %d dBm)\n", c.c_str(), from.c_str(), seq, rssi);

  String nh = findNextHop(from);

  if (c == "LIGHT_ON" || c == "ON" || c == "1") {
    Serial.println(F("[POLE] Executing LIGHT_ON"));
    if (timerActive) timerActive = false;
    setRelay(true, "TELEGRAM_CMD");
    Serial.println(F("[POLE] Relay state = ON"));
    updateSensors();
    Serial.printf("[POLE] Sending ACK seq=%u\n", seq);
    sendAck(from, nh, seq, "LIGHT_ON", "OK", "ON");
  }
  else if (c == "LIGHT_OFF" || c == "OFF" || c == "0") {
    Serial.println(F("[POLE] Executing LIGHT_OFF"));
    if (timerActive) timerActive = false;
    setRelay(false, "TELEGRAM_CMD");
    Serial.println(F("[POLE] Relay state = OFF"));
    updateSensors();
    Serial.printf("[POLE] Sending ACK seq=%u\n", seq);
    sendAck(from, nh, seq, "LIGHT_OFF", "OK", "OFF");
  }
  else if (c == "GET_STATUS" || c == "STATUS") {
    Serial.printf("[POLE] Sending STATUS telemetry (Seq: %u)\n", seq);
    sendStatus(from, seq);
  }
  else if (c.startsWith("PING")) {
    String linkData = "RSSI=" + String(rssi) + "|SNR=" + String(snr);
    txMesh("LINK_ACK", from, nh, DEFAULT_TTL, seq, linkData);
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
        setRelay(true, "TIMER");
        sendAck(from, nh, seq, "TIMER_ON", "OK", "ON|" + String(mins) + "M");
      } else {
        sendAck(from, nh, seq, "TIMER_ON", "ERR_INVALID_MINUTES", lightOn ? "ON" : "OFF");
      }
    }
  }
  else if (c == "RESET" || c == "REBOOT") {
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
    if (idxEn >= 0) schedEnabled = (c.substring(idxEn + 3, idxEn + 4).toInt() == 1);
    if (idxOn >= 0) schedOnMin = c.substring(idxOn + 3, c.indexOf('|', idxOn)).toInt();
    if (idxOff >= 0) schedOffMin = c.substring(idxOff + 4).toInt();

    prefs.begin(NVS_NS_POLE, false);
    prefs.putBool("schedEn", schedEnabled);
    prefs.putInt("schedOn", schedOnMin);
    prefs.putInt("schedOff", schedOffMin);
    prefs.end();

    sendAck(from, nh, seq, "SET_SCHED", "OK", schedEnabled ? "ENABLED" : "DISABLED");
  }
  else if (c.startsWith("REGISTERED|")) {
    int p1 = c.indexOf('|');
    int p2 = c.indexOf('|', p1 + 1);
    int p3 = c.indexOf('|', p2 + 1);
    if (p1 > 0 && p2 > 0) {
      NODE_ID   = c.substring(p1 + 1, p2);
      NODE_NAME = (p3 > 0) ? c.substring(p2 + 1, p3) : c.substring(p2 + 1);
      if (p3 > 0) NODE_TYPE = c.substring(p3 + 1);

      isRegistered = true;
      savePoleStateToNVS();

      Serial.printf("[POLE][REG] Confirmed registration: ID=%s, Name=%s, Type=%s\n",
                    NODE_ID.c_str(), NODE_NAME.c_str(), NODE_TYPE.c_str());
      
      sendStatus(from, seq);
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  INCOMING LORA PACKET PROCESSOR & MESH ROUTER
// ─────────────────────────────────────────────────────────────
void processIncomingLoRa() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;

  String raw = "";
  while (LoRa.available()) raw += (char)LoRa.read();
  raw.trim();
  if (raw.length() == 0) return;

  int rssi = LoRa.packetRssi();
  int snr  = (int)LoRa.packetSnr();

  ParsedPacket pkt;
  if (!parsePacket(raw, rssi, snr, pkt)) return;

  Serial.printf("[POLE] LoRa packet received (Type: %s, From: %s, To: %s, RSSI: %d dBm)\n",
                pkt.type.c_str(), pkt.src.c_str(), pkt.dst.c_str(), rssi);

  addOrUpdateNeighbor(pkt.src, rssi, snr);
  addOrUpdateRoute(pkt.src, pkt.src, 1, rssi);

  String myId = (NODE_ID.length() > 0) ? NODE_ID : HW_NODE_ID;
  bool isForMe = (pkt.dst == myId || pkt.dst == HW_NODE_ID || 
                  (NODE_ID.length() > 0 && pkt.dst == NODE_ID) || 
                  pkt.dst == BROADCAST_ID || pkt.dst == "ALL");

  bool isDuplicate = false;
  for (int i = 0; i < MAX_SEQ_CACHE; i++) {
    if (seqCache[i].src == pkt.src && seqCache[i].seq == pkt.seq && pkt.seq != 0) {
      isDuplicate = true;
      break;
    }
  }

  if (!isDuplicate) {
    seqCache[seqHead].src = pkt.src;
    seqCache[seqHead].seq = pkt.seq;
    seqHead = (seqHead + 1) % MAX_SEQ_CACHE;
  } else if (!isForMe) {
    LoRa.receive();
    return;
  }

  if (isForMe) {
    if (pkt.type == "CMD") {
      processCommand(pkt.data, pkt.src, pkt.seq, rssi, snr);
    }
    else if (pkt.type == "CHK") {
      Serial.printf("[POLE] Health check requested from GW\n");
      currentHealthCheckId = pkt.data;
      healthCheckActive = true;
      healthCheckStartMs = millis();
      nbPingsSent = 0;
      nbPongsReceived = 0;
      for(int i=0; i<MAX_NEIGHBORS; i++) nbResponded[i] = false;
      
      for (int i = 0; i < neighborCount; i++) {
        if (neighbors[i].active) {
          txMesh("NB_PING", neighbors[i].nodeId, neighbors[i].nodeId, 1, nextSeq(), currentHealthCheckId);
          nbPingsSent++;
          delay(50);
        }
      }
      if (nbPingsSent == 0) {
        healthCheckActive = false;
        sendHealthReport();
      }
    }
    else if (pkt.type == "NB_PING") {
      txMesh("NB_PONG", pkt.src, pkt.src, 1, nextSeq(), pkt.data);
    }
    else if (pkt.type == "NB_PONG") {
      for (int i = 0; i < neighborCount; i++) {
        if (neighbors[i].nodeId == pkt.src) {
          nbResponded[i] = true;
          nbPongsReceived++;
          break;
        }
      }
    }
  }

  if (!isForMe && pkt.ttl > 1) {
    if (pkt.nextHop == myId || pkt.nextHop == BROADCAST_ID) {
      String forwardNextHop = findNextHop(pkt.dst);
      Serial.printf("[POLE][MESH] Forwarding packet for %s via %s (TTL: %d)\n",
                    pkt.dst.c_str(), forwardNextHop.c_str(), pkt.ttl - 1);
      txMesh(pkt.type, pkt.dst, forwardNextHop, pkt.ttl - 1, pkt.seq, pkt.data);
    }
  }

  LoRa.receive();
}

// ─────────────────────────────────────────────────────────────
//  NVS PERSISTENCE
// ─────────────────────────────────────────────────────────────
void savePoleStateToNVS() {
  prefs.begin(NVS_NS_POLE, false);
  prefs.putString("nodeId", NODE_ID);
  prefs.putString("name", NODE_NAME);
  prefs.putString("type", NODE_TYPE);
  prefs.putBool("reg", isRegistered);
  prefs.putString("relay", prevRelayState);
  prefs.end();
}

void loadPoleStateFromNVS() {
  prefs.begin(NVS_NS_POLE, true);
  NODE_ID        = prefs.getString("nodeId", "");
  NODE_NAME      = prefs.getString("name", "Field Pole");
  NODE_TYPE      = prefs.getString("type", "STREET");
  isRegistered   = prefs.getBool("reg", false);
  prevRelayState = prefs.getString("relay", "OFF");
  schedEnabled   = prefs.getBool("schedEn", false);
  schedOnMin     = prefs.getInt("schedOn", -1);
  schedOffMin    = prefs.getInt("schedOff", -1);
  prefs.end();

  Serial.printf("[POLE][NVS] Loaded state: ID='%s', Reg=%d, Relay='%s'\n",
                NODE_ID.c_str(), isRegistered ? 1 : 0, prevRelayState.c_str());
}

// ─────────────────────────────────────────────────────────────
//  SETUP & MAIN LOOP
// ─────────────────────────────────────────────────────────────
void setup() {
  initHardware();
  initLoRa();

  unsigned long now = millis();
  tSensorRead   = now;
  tBeacon       = now;
  tNewNodeRetry = now;
  tSchedCheck   = now;
  tNvsFlush     = now;
  tTimerCheck   = now;

  if (!isRegistered) {
    sendNewNode();
  }
}

void loop() {
  unsigned long now = millis();

  processIncomingLoRa();

  if (now - tSensorRead >= SENSOR_INTERVAL_MS) {
    tSensorRead = now;
    updateSensors();
  }

  if (now - tTimerCheck >= 1000) {
    tTimerCheck = now;
    checkTimerExpiration();
  }

  if (healthCheckActive) {
    bool timeout = (now - healthCheckStartMs > HEALTH_CHECK_TIMEOUT_MS);
    bool allDone = (nbPongsReceived >= nbPingsSent && nbPingsSent > 0);
    if (timeout || allDone) {
      healthCheckActive = false;
      sendHealthReport();
    }
  }
  
  static unsigned long tNeighReport = 0;
  if (isRegistered && (now - tNeighReport >= NEIGH_REPORT_INTERVAL_MS)) {
    tNeighReport = now;
    sendHealthReport();
  }

  if (now - tSchedCheck >= 10000) {
    tSchedCheck = now;
    checkAutonomousSchedule();
  }

  if (now - tBeacon >= BEACON_INTERVAL_MS) {
    tBeacon = now;
    expireNeighbors();
    sendBeacon();
  }

  if (!isRegistered && (now - tNewNodeRetry >= NEW_NODE_RETRY_INTERVAL_MS)) {
    tNewNodeRetry = now;
    if (newNodeRetries < NEW_NODE_MAX_RETRIES) {
      sendNewNode();
    }
  }

  if (now - tNvsFlush >= NVS_SAVE_INTERVAL_MS) {
    tNvsFlush = now;
    savePoleStateToNVS();
  }
}
