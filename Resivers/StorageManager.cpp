/*
 * =====================================================================
 *  STORAGEMANAGER.CPP — FIELD POLE NVS PERSISTENCE & SETTINGS
 * =====================================================================
 */

#include "StorageManager.h"

StorageManager storageMgr;

StorageManager::StorageManager() :
  hwNodeId(""), nodeId(""), nodeName("Field Pole"), nodeType("STREET"), nodeLoc("Field Area"),
  registered(false), prevRelayState("OFF"), schedEnabled(false), schedOnMin(-1), schedOffMin(-1)
{}

void StorageManager::generateHwNodeId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[16];
  snprintf(buf, sizeof(buf), "%04X%04X", (uint16_t)(mac >> 32), (uint16_t)(mac & 0xFFFF));
  hwNodeId = String(buf);
}

void StorageManager::init() {
  generateHwNodeId();
  loadPoleStateFromNVS();
  Serial.printf("[POLE] Hardware Chip ID: %s, Logical ID: '%s', Reg: %d\n",
                hwNodeId.c_str(), nodeId.c_str(), registered ? 1 : 0);
}

void StorageManager::loadPoleStateFromNVS() {
  prefs.begin(NVS_NS_POLE, true);
  nodeId         = prefs.getString("nodeId", "");
  nodeName       = prefs.getString("name", "Field Pole");
  nodeType       = prefs.getString("type", "STREET");
  registered     = prefs.getBool("reg", false);
  prevRelayState = prefs.getString("relay", "OFF");
  schedEnabled   = prefs.getBool("schedEn", false);
  schedOnMin     = prefs.getInt("schedOn", -1);
  schedOffMin    = prefs.getInt("schedOff", -1);
  prefs.end();
}

void StorageManager::savePoleStateToNVS() {
  prefs.begin(NVS_NS_POLE, false);
  prefs.putString("nodeId", nodeId);
  prefs.putString("name", nodeName);
  prefs.putString("type", nodeType);
  prefs.putBool("reg", registered);
  prefs.putString("relay", prevRelayState);
  prefs.putBool("schedEn", schedEnabled);
  prefs.putInt("schedOn", schedOnMin);
  prefs.putInt("schedOff", schedOffMin);
  prefs.end();
}

void StorageManager::saveRelayState(const String &state) {
  prevRelayState = state;
  prefs.begin(NVS_NS_POLE, false);
  prefs.putString("relay", state);
  prefs.end();
}

void StorageManager::saveSchedule(bool enabled, int onMin, int offMin) {
  schedEnabled = enabled;
  schedOnMin = onMin;
  schedOffMin = offMin;
  prefs.begin(NVS_NS_POLE, false);
  prefs.putBool("schedEn", schedEnabled);
  prefs.putInt("schedOn", schedOnMin);
  prefs.putInt("schedOff", schedOffMin);
  prefs.end();
}

void StorageManager::setRegistration(const String &logicalId, const String &name, const String &type) {
  nodeId = logicalId;
  nodeName = name;
  nodeType = type;
  registered = true;
  savePoleStateToNVS();
}

String StorageManager::getHwNodeId() const { return hwNodeId; }
String StorageManager::getNodeId() const { return nodeId; }
String StorageManager::getNodeName() const { return nodeName; }
String StorageManager::getNodeType() const { return nodeType; }
String StorageManager::getNodeLocation() const { return nodeLoc; }
bool   StorageManager::isRegistered() const { return registered; }
String StorageManager::getPrevRelayState() const { return prevRelayState; }

bool   StorageManager::isScheduleEnabled() const { return schedEnabled; }
int    StorageManager::getScheduleOnMin() const { return schedOnMin; }
int    StorageManager::getScheduleOffMin() const { return schedOffMin; }
