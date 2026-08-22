/*
 * =====================================================================
 *  STORAGEMANAGER.CPP — NVS STORAGE & LOGGING MANAGER
 * =====================================================================
 */

#include "StorageManager.h"
#include "MeshManager.h"
#include <time.h>

StorageManager storageMgr;

StorageManager::StorageManager() : gwLogHead(0), gwLogCount(0) {}

void StorageManager::init() {
  loadDatabaseFromNVS();
}

void StorageManager::loadDatabaseFromNVS() {
  prefs.begin(NVS_NS_MESH, true);
  int count = prefs.getInt("count", 0);
  int nextSL = prefs.getInt("nextSLId", 1);
  int nextFR = prefs.getInt("nextFId", 1);
  meshMgr.setNextStreetLightId(nextSL);
  meshMgr.setNextFarmerId(nextFR);

  if (count > MAX_NODES) count = MAX_NODES;

  for (int i = 0; i < count; i++) {
    char pfx[8];
    snprintf(pfx, sizeof(pfx), "n%d_", i);
    String hwId    = prefs.getString((String(pfx) + "hw").c_str(), "");
    String logId   = prefs.getString((String(pfx) + "id").c_str(), "");
    String name    = prefs.getString((String(pfx) + "nm").c_str(), "");
    String loc     = prefs.getString((String(pfx) + "lc").c_str(), "");
    String type    = prefs.getString((String(pfx) + "tp").c_str(), "STREET");
    bool   reg     = prefs.getBool((String(pfx) + "rg").c_str(), false);
    bool   schedEn = prefs.getBool((String(pfx) + "se").c_str(), false);
    int    schedOn = prefs.getInt((String(pfx) + "so").c_str(), -1);
    int    schedOff= prefs.getInt((String(pfx) + "sf").c_str(), -1);

    if (hwId.length() > 0) {
      int idx = meshMgr.addOrUpdateNode(hwId, logId, type, -80, 0);
      NodeRecord *n = meshMgr.getNode(idx);
      if (n) {
        n->name = name;
        n->location = loc;
        n->registered = reg;
        n->schedEnabled = schedEn;
        n->schedOnMin = schedOn;
        n->schedOffMin = schedOff;
        n->onlineState = "OFFLINE";
      }
    }
  }
  prefs.end();
  Serial.printf("[GW][NVS] Loaded %d nodes from database.\n", meshMgr.getNodeCount());
}

void StorageManager::saveDatabaseToNVS() {
  prefs.begin(NVS_NS_MESH, false);
  int count = meshMgr.getNodeCount();
  prefs.putInt("count", count);
  prefs.putInt("nextSLId", meshMgr.getNextStreetLightId());
  prefs.putInt("nextFId", meshMgr.getNextFarmerId());

  for (int i = 0; i < count; i++) {
    NodeRecord *n = meshMgr.getNode(i);
    if (!n) continue;

    char pfx[8];
    snprintf(pfx, sizeof(pfx), "n%d_", i);
    prefs.putString((String(pfx) + "hw").c_str(), n->hwId);
    prefs.putString((String(pfx) + "id").c_str(), n->logicalId);
    prefs.putString((String(pfx) + "nm").c_str(), n->name);
    prefs.putString((String(pfx) + "lc").c_str(), n->location);
    prefs.putString((String(pfx) + "tp").c_str(), n->type);
    prefs.putBool((String(pfx) + "rg").c_str(), n->registered);
    prefs.putBool((String(pfx) + "se").c_str(), n->schedEnabled);
    prefs.putInt((String(pfx) + "so").c_str(), n->schedOnMin);
    prefs.putInt((String(pfx) + "sf").c_str(), n->schedOffMin);
  }
  prefs.end();
  Serial.printf("[GW][NVS] Database saved (%d nodes).\n", count);
}

void StorageManager::loadWiFiCredentials(String &ssid, String &pass) {
  prefs.begin(NVS_NS_WIFI, true);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();
  if (savedSsid.length() > 0) {
    ssid = savedSsid;
    pass = savedPass;
  }
}

void StorageManager::saveWiFiCredentials(const String &ssid, const String &pass) {
  prefs.begin(NVS_NS_WIFI, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void StorageManager::addGwLog(const char* action, const char* target, uint8_t res) {
  strncpy(gwLog[gwLogHead].action, action, 31);
  gwLog[gwLogHead].action[31] = '\0';
  strncpy(gwLog[gwLogHead].target, target, 15);
  gwLog[gwLogHead].target[15] = '\0';
  gwLog[gwLogHead].timestamp = (uint32_t)(time(nullptr));
  gwLog[gwLogHead].result = res;
  gwLogHead = (gwLogHead + 1) % MAX_CMD_LOG_ENTRIES;
  if (gwLogCount < MAX_CMD_LOG_ENTRIES) gwLogCount++;
}

GwLogEntry* StorageManager::getGwLog(int index) {
  if (index >= 0 && index < gwLogCount) {
    return &gwLog[index];
  }
  return nullptr;
}

uint8_t StorageManager::getGwLogCount() const {
  return gwLogCount;
}
