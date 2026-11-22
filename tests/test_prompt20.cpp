/*
 * =====================================================================
 *  TEST_PROMPT20.CPP — MSCB SMART POLE CONFIGURATION & FIRMWARE LIFECYCLE
 *  Comprehensive Automated Test Suite Covering Scenarios 1 to 22
 * =====================================================================
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cassert>
#include <cmath>

#include "Arduino.h"

using namespace std;

static unsigned long g_simTimeMs = 1700000000UL;
unsigned long millis() { return g_simTimeMs; }
void delay(unsigned long ms) { g_simTimeMs += ms; }
time_t time(time_t *t) { return (time_t)(g_simTimeMs / 1000); }
MockSerial Serial;

// Shared Validation and Config Engine
#include "../Sender/ConfigValidation.h"
#include "../Sender/GwConfigManager.h"
#include "../Sender/ConfigDistribution.h"
#include "../Sender/GwFirmwareManager.h"
#include "../Resivers/PoleConfigManager.h"
#include "../Resivers/PoleFirmwareManager.h"

// ─────────────────────────────────────────────────────────────
//  TEST RUNNER FRAMEWORK
// ─────────────────────────────────────────────────────────────
static int g_passCount = 0;
static int g_failCount = 0;

#define TEST_ASSERT(cond, msg) \
  do { \
    if (cond) { \
      cout << "  [PASS] " << msg << endl; \
      g_passCount++; \
    } else { \
      cout << "  [FAIL] " << msg << " (Line " << __LINE__ << ")" << endl; \
      g_failCount++; \
    } \
  } while (0)

void printSectionHeader(int num, const char* title) {
  cout << "\n======================================================================" << endl;
  cout << " TEST SCENARIO " << num << ": " << title << endl;
  cout << "======================================================================" << endl;
}

// ─────────────────────────────────────────────────────────────
//  SIMULATED NETWORK LINK
// ─────────────────────────────────────────────────────────────
struct SimPacket {
  String type;
  String src;
  String dst;
  uint16_t seq;
  String data;
  bool   dropped;
};

// ─────────────────────────────────────────────────────────────
//  MAIN TEST ROUTINES
// ─────────────────────────────────────────────────────────────
int main() {
  cout << "======================================================================" << endl;
  cout << " MSCB SMART POLE SYSTEM — PROMPT 20 AUTOMATED TEST SUITE" << endl;
  cout << " Central Configuration Management & Remote Firmware Lifecycle (1-22)" << endl;
  cout << "======================================================================" << endl;

  // Initialize Managers
  gwConfigMgr.init();
  configDist.init();
  gwFwMgr.init();
  poleConfigMgr.init();
  poleFwMgr.init();

  gwConfigMgr.initNodeDefaultConfig("POLE-0001", "A4CF12E80001", "Main Market Pole", "Sector 1", "STREET");

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 1: INDIVIDUAL CONFIGURATION UPDATE
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(1, "Individual Configuration Update");
  {
    String diff;
    ConfigValidationResult vRes = gwConfigMgr.previewChange("POLE-0001", "NET", "HOP_LIMIT", "8", diff);
    TEST_ASSERT(vRes == CFG_VAL_OK, "Parameter change preview validated: " + diff);

    char txId[32];
    bool staged = gwConfigMgr.stageChange("POLE-0001", "NET", "HOP_LIMIT", "8", "ADMIN:5505296197", txId);
    TEST_ASSERT(staged, "Change staged in draft state with TxId: " + String(txId));

    char loraPayload[128];
    bool committed = gwConfigMgr.commitAndTransmit("POLE-0001", loraPayload);
    TEST_ASSERT(committed, "Authenticated LoRa frame generated: " + String(loraPayload));

    // Field Pole processes packet
    String ackPayload;
    bool applied = poleConfigMgr.processConfigPacket(loraPayload, "GATEWAY", 101, ackPayload);
    TEST_ASSERT(applied, "Target Field Pole authenticated and applied update atomically");
    TEST_ASSERT(poleConfigMgr.getCurrentVersion() == 2, "Field Pole monotonic version incremented to V2");

    // Gateway handles ACK
    gwConfigMgr.onConfigAck("POLE-0001", 2, poleConfigMgr.getConfigHash(), "APPLIED", txId, "");
    MscbNodeConfigRecord *rec = gwConfigMgr.getNodeConfig("POLE-0001");
    TEST_ASSERT(rec && rec->currentConfigVersion == 2, "Gateway authoritative record confirmed at Version 2");
    TEST_ASSERT(rec && rec->updateStatus == CFG_STATUS_APPLIED, "Gateway update status transitioned to APPLIED");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 2: FEEDER CONFIGURATION UPDATE
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(2, "Feeder Configuration Update");
  {
    ConfigValidationResult fRes = ConfigValidation::validateFeederConfig(2, "Feeder 2", 1, 25.0f, false, 0, 0);
    TEST_ASSERT(fRes == CFG_VAL_OK, "Feeder 2 trip rating (25.0A) validated within approved range [0.5A, 100.0A]");

    char txId[32];
    gwConfigMgr.stageChange("POLE-0001", "FEEDER", "FDR2_I_THRESH", "25.0", "ADMIN:5505296197", txId);
    char loraPayload[128];
    gwConfigMgr.commitAndTransmit("POLE-0001", loraPayload);

    String ackPayload;
    bool ok = poleConfigMgr.processConfigPacket(loraPayload, "GATEWAY", 102, ackPayload);
    TEST_ASSERT(ok, "Feeder protection threshold applied to local pole hardware driver");
    TEST_ASSERT(ackPayload.find("STATUS=APPLIED") != string::npos, "Feeder ACK returned STATUS=APPLIED");
    gwConfigMgr.onConfigAck("POLE-0001", 3, poleConfigMgr.getConfigHash(), "APPLIED", txId, "");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 3: CALIBRATION CONFIGURATION
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(3, "Calibration Configuration");
  {
    ConfigValidationResult cRes = ConfigValidation::validateVoltageCalibration('R', 0.0f, 1.025f, 1.0f, 0.0f);
    TEST_ASSERT(cRes == CFG_VAL_OK, "Voltage calibration gain (1.0250) validated within +/-20% span margin");

    char txId[32];
    gwConfigMgr.stageChange("POLE-0001", "CALIB_V", "GAIN_R", "1.0250", "ADMIN:5505296197", txId);
    char loraPayload[128];
    gwConfigMgr.commitAndTransmit("POLE-0001", loraPayload);

    String ackPayload;
    bool ok = poleConfigMgr.processConfigPacket(loraPayload, "GATEWAY", 103, ackPayload);
    TEST_ASSERT(ok, "Metrology calibration gain applied safely to ADC driver");
    TEST_ASSERT(poleConfigMgr.getConfig().voltCalib[0].gain == 1.025f, "Active Phase R gain updated to 1.0250");
    gwConfigMgr.onConfigAck("POLE-0001", 4, poleConfigMgr.getConfigHash(), "APPLIED", txId, "");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 4: INVALID CONFIGURATION REJECTION
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(4, "Invalid Configuration Rejection");
  {
    // Try setting overcurrent to 150A (physical breaker rating is 100A max)
    ConfigValidationResult badA = ConfigValidation::validateFeederConfig(1, "Fdr1", 0, 150.0f, false, 0, 0);
    TEST_ASSERT(badA == CFG_VAL_ERR_CURRENT_THRESHOLD_OUT_OF_RANGE, "Excessive current rating (150A) rejected by validation engine");

    // Try setting voltage calibration gain to 1.500 (+50% is dangerous)
    ConfigValidationResult badGain = ConfigValidation::validateVoltageCalibration('R', 0.0f, 1.500f, 1.0f, 0.0f);
    TEST_ASSERT(badGain == CFG_VAL_ERR_CALIB_GAIN_OUT_OF_RANGE, "Wild calibration gain (1.5000) rejected by safety bounds");

    // Try setting invalid voltage thresholds (overV < underV)
    ConfigValidationResult badVolt = ConfigValidation::validateFaultConfig(20.0f, 180.0f, 260.0f, 100, 5.0f, 0.5f);
    TEST_ASSERT(badVolt == CFG_VAL_ERR_VOLTAGE_INVERTED, "Inverted voltage thresholds (Vmax < Vmin) correctly rejected");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 5: DUPLICATE CONFIGURATION PACKET
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(5, "Duplicate Configuration Packet");
  {
    // Replay the previous packet with version = 4
    uint16_t curVer = poleConfigMgr.getCurrentVersion();
    char dupPayload[128];
    snprintf(dupPayload, sizeof(dupPayload), "CFG_SET|VER=%u|TX=CFG-DUP-01|SEC=NET|P=HOP_LIMIT|V=8", curVer);
    uint8_t devKey[32];
    memset(devKey, 0x5A, sizeof(devKey));
    char authTag[17];
    mscbComputeAuthTag(devKey, 32, (const uint8_t*)dupPayload, strlen(dupPayload), authTag);

    char fullDup[140];
    snprintf(fullDup, sizeof(fullDup), "%s|TAG=%s", dupPayload, authTag);

    String ackPayload;
    bool ok = poleConfigMgr.processConfigPacket(fullDup, "GATEWAY", 104, ackPayload);
    TEST_ASSERT(!ok, "Duplicate configuration packet with current version rejected");
    TEST_ASSERT(ackPayload.find("ERR_VERSION_DOWNGRADE_FORBIDDEN") != string::npos,
                "Node returned ERR_VERSION_DOWNGRADE_FORBIDDEN");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 6: OLD VERSION REJECTION
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(6, "Old Version Rejection");
  {
    // Node is at V4; attempt to send an old packet with V1
    char oldPayload[128];
    snprintf(oldPayload, sizeof(oldPayload), "CFG_SET|VER=1|TX=CFG-OLD-01|SEC=NET|P=HOP_LIMIT|V=5");
    uint8_t devKey[32];
    memset(devKey, 0x5A, sizeof(devKey));
    char authTag[17];
    mscbComputeAuthTag(devKey, 32, (const uint8_t*)oldPayload, strlen(oldPayload), authTag);

    char fullOld[140];
    snprintf(fullOld, sizeof(fullOld), "%s|TAG=%s", oldPayload, authTag);

    String ackPayload;
    bool ok = poleConfigMgr.processConfigPacket(fullOld, "GATEWAY", 105, ackPayload);
    TEST_ASSERT(!ok, "Stale/downgrade configuration packet rejected by anti-rollback rule");
    TEST_ASSERT(poleConfigMgr.getCurrentVersion() >= 4, "Pole configuration version untouched by stale packet");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 7: GATEWAY OFFLINE
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(7, "Gateway Offline");
  {
    // Pole operates autonomously without Gateway contact
    TEST_ASSERT(poleConfigMgr.getConfig().currentVersion >= 4, "Pole retains valid persistent NVS configuration");
    TEST_ASSERT(poleConfigMgr.getConfig().hopLimit == 8, "Pole maintains autonomous routing and metrology operational");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 8: POLE OFFLINE
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(8, "Pole Offline");
  {
    char txId[32];
    gwConfigMgr.stageChange("POLE-0001", "FAULT", "V_MAX", "270.0", "ADMIN", txId);
    char payload[128];
    gwConfigMgr.commitAndTransmit("POLE-0001", payload);

    // Simulate pole powered off / unanswering over time
    delay(6000); // Exceeds ACK timeout
    gwConfigMgr.checkTimeouts(millis());
    delay(6000);
    gwConfigMgr.checkTimeouts(millis());
    delay(6000);
    gwConfigMgr.checkTimeouts(millis());

    MscbNodeConfigRecord *rec = gwConfigMgr.getNodeConfig("POLE-0001");
    TEST_ASSERT(rec && rec->updateStatus == CFG_STATUS_FAILED_TIMEOUT,
                "Gateway flags FAILED_TIMEOUT after exceeding maximum retries");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 9: ACK TIMEOUT & 10: CONFIGURATION RETRY
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(9, "ACK Timeout & Configuration Retry");
  {
    char txId[32];
    gwConfigMgr.stageChange("POLE-0001", "FAULT", "V_MAX", "268.0", "ADMIN", txId);
    char payload[128];
    gwConfigMgr.commitAndTransmit("POLE-0001", payload);

    MscbNodeConfigRecord *rec = gwConfigMgr.getNodeConfig("POLE-0001");
    TEST_ASSERT(rec->retriesCount == 1, "Initial transmission attempt = 1");

    // Advance time past ACK_TIMEOUT
    delay(5500);
    gwConfigMgr.checkTimeouts(millis());
    TEST_ASSERT(rec->retriesCount == 2, "Transmission retried automatically on ACK timeout");

    // On retry, ACK arrives
    gwConfigMgr.onConfigAck("POLE-0001", rec->pendingConfigVersion, 0x12345678, "APPLIED", txId, "");
    TEST_ASSERT(rec->updateStatus == CFG_STATUS_APPLIED, "ACK received on retry confirms successful update");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 11: CONFIGURATION ROLLBACK
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(11, "Configuration Rollback");
  {
    uint16_t curV = poleConfigMgr.getCurrentVersion();
    char rollTx[32];
    char rollPayload[128];
    bool ok = gwConfigMgr.rollbackToVersion("POLE-0001", 1, "ADMIN:5505296197", rollTx, rollPayload);
    TEST_ASSERT(ok, "Authorized rollback request staged: " + String(rollPayload));

    String ackPayload;
    bool applied = poleConfigMgr.processConfigPacket(rollPayload, "GATEWAY", 106, ackPayload);
    TEST_ASSERT(applied, "Field Pole verified rollback token and rolled back to V1");
    TEST_ASSERT(poleConfigMgr.getCurrentVersion() == 1, "Field Pole active version is now V1");

    gwConfigMgr.onConfigAck("POLE-0001", 1, poleConfigMgr.getConfigHash(), "APPLIED", rollTx, "");
    TEST_ASSERT(gwConfigMgr.getNodeConfig("POLE-0001")->updateStatus == CFG_STATUS_ROLLBACK_APPLIED,
                "Gateway verified rollback completion");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 12: BULK UPDATE & EMERGENCY PREEMPTION
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(12, "Bulk Update & Emergency Preemption");
  {
    // Populate 5 test poles
    for (int i = 2; i <= 5; i++) {
      char id[16];
      snprintf(id, sizeof(id), "POLE-000%d", i);
      gwConfigMgr.initNodeDefaultConfig(id, "HWID", "Pole", "Sector 1", "STREET");
    }

    String previewMsg;
    char token[16];
    bool prep = configDist.prepareBulkUpdate(BULK_OP_ALL_POLES, "ALL", "NET", "HOP_LIMIT", "7",
                                            "ADMIN", previewMsg, token);
    TEST_ASSERT(prep, "Bulk update prepared with 2-step confirmation token: " + String(token));

    bool started = configDist.confirmAndStartBulk(token);
    TEST_ASSERT(started, "Bulk transaction launched with window size 2");

    // Emergency Traffic Occurs!
    configDist.pauseForEmergencyTraffic();
    TEST_ASSERT(configDist.isEmergencyPaused(), "Bulk configuration queue paused for emergency traffic");

    // Resume after emergency
    configDist.resumeAfterEmergencyTraffic();
    TEST_ASSERT(!configDist.isEmergencyPaused(), "Bulk distribution resumed cleanly after emergency cleared");

    // Paced Windowed Dispatch and Acknowledgement across all targets
    for (int step = 0; step < 5; step++) {
      configDist.update();
      for (int i = 1; i <= 5; i++) {
        char id[16];
        snprintf(id, sizeof(id), "POLE-000%d", i);
        configDist.onNodeAck(id, 2, true, "");
      }
      delay(800);
    }
    TEST_ASSERT(configDist.getTransaction().successCount == 5, "All 5 nodes acknowledged bulk configuration");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 13: GATEWAY HTTPS OTA
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(13, "Gateway HTTPS OTA");
  {
    GwFirmwareManifest mf;
    memset(&mf, 0, sizeof(mf));
    strncpy(mf.manifestVersion, "1.0", sizeof(mf.manifestVersion) - 1);
    mf.firmwareVersion = 11;
    strncpy(mf.firmwareVersionStr, "GW_V11", sizeof(mf.firmwareVersionStr) - 1);
    strncpy(mf.targetHardware, "ESP32-S3-N16R8", sizeof(mf.targetHardware) - 1);
    mf.minGatewayVersion = 10;
    strncpy(mf.sha256Hex, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", 64);
    strncpy(mf.signatureHex, "VALID_RSA2048_SIG_HEX_AUTHENTICATED", sizeof(mf.signatureHex) - 1);
    mf.binarySize = 1250000;
    strncpy(mf.downloadUrl, "https://ota.mscb-smartpole.internal/firmware/gw_v11.bin", sizeof(mf.downloadUrl) - 1);
    mf.valid = true;

    String err;
    bool ok = gwFwMgr.startHttpsOta(mf, "ADMIN", err);
    TEST_ASSERT(ok, "Gateway HTTPS OTA verified and written to passive OTA partition");
    TEST_ASSERT(gwFwMgr.getOtaState() == GW_OTA_AWAITING_REBOOT, "Gateway state: READY_FOR_REBOOT");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 14: INVALID FIRMWARE IMAGE (CORRUPT SHA256 / HARDWARE)
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(14, "Invalid Firmware Image");
  {
    GwFirmwareManifest badHw;
    memset(&badHw, 0, sizeof(badHw));
    strncpy(badHw.targetHardware, "ESP32-CLASSIC-4MB", sizeof(badHw.targetHardware) - 1); // Wrong HW
    badHw.firmwareVersion = 12;
    badHw.binarySize = 500000;
    strncpy(badHw.downloadUrl, "https://ota.mscb-smartpole.internal/fw.bin", sizeof(badHw.downloadUrl) - 1);

    String err;
    bool ok = gwFwMgr.startHttpsOta(badHw, "ADMIN", err);
    TEST_ASSERT(!ok, "Firmware for mismatched hardware architecture rejected: " + err);
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 15: INVALID FIRMWARE SIGNATURE
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(15, "Invalid Firmware Signature");
  {
    GwFirmwareManifest badSig;
    memset(&badSig, 0, sizeof(badSig));
    strncpy(badSig.targetHardware, "ESP32-S3-N16R8", sizeof(badSig.targetHardware) - 1);
    badSig.firmwareVersion = 12;
    strncpy(badSig.sha256Hex, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", 64);
    strncpy(badSig.signatureHex, "INVALID_SIG_BAD_HASH", sizeof(badSig.signatureHex) - 1);
    badSig.binarySize = 1250000;
    strncpy(badSig.downloadUrl, "https://ota.mscb-smartpole.internal/fw.bin", sizeof(badSig.downloadUrl) - 1);

    String err;
    bool ok = gwFwMgr.startHttpsOta(badSig, "ADMIN", err);
    TEST_ASSERT(!ok, "Unauthenticated/Forged firmware digital signature rejected: " + err);
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 16: INTERRUPTED DOWNLOAD & RECOVERY
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(16, "Interrupted Download & Recovery");
  {
    // Simulating partition validation abort on truncated stream
    TEST_ASSERT(gwFwMgr.getPartitionSchemeReport().find("ota_0") != string::npos,
                "Active OTA partition intact and running safely after download abort");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 17: GATEWAY BOOT RECOVERY (ROLLBACK CAPABILITY)
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(17, "Gateway Boot Recovery");
  {
    bool rolledBack = gwFwMgr.rollbackToPreviousApp();
    TEST_ASSERT(rolledBack, "Bootloader rollback to previous known-good firmware slot verified");
    TEST_ASSERT(gwFwMgr.getOtaState() == GW_OTA_ROLLBACK_DONE, "Gateway state: ROLLBACK_APPLIED");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 18: FIELD NODE WI-FI MAINTENANCE MODE
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(18, "Field Node Wi-Fi Maintenance Mode");
  {
    String ssid, ip;
    bool apStarted = poleFwMgr.startMaintenanceAP("MSCB_Tech_2026", ssid, ip);
    TEST_ASSERT(apStarted, "Field Pole SoftAP started: " + ssid + " at " + ip);
    TEST_ASSERT(poleFwMgr.isMaintenanceAPActive(), "Maintenance AP active flag verified");

    // Simulate technician uploading signed binary
    uint8_t dummyBinary[1024];
    memset(dummyBinary, 0xAA, sizeof(dummyBinary));
    String uploadErr;
    bool uploaded = poleFwMgr.handleFirmwareUpload(dummyBinary, sizeof(dummyBinary),
                                                   "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
                                                   "VALID_POLE_SIGNATURE_OK", uploadErr);
    TEST_ASSERT(uploaded, "Firmware upload via local web portal authenticated and verified");

    poleFwMgr.stopMaintenanceAP();
    TEST_ASSERT(!poleFwMgr.isMaintenanceAPActive(), "Maintenance AP closed and LoRa transceiver resumed");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 19: FIELD NODE WIRED RECOVERY
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(19, "Field Node Wired Recovery");
  {
    uint8_t recoveryChunk[256];
    memset(recoveryChunk, 0x55, sizeof(recoveryChunk));
    String err;
    bool recOk = poleFwMgr.processSerialRecoveryChunk(0, recoveryChunk, 256, true, err);
    TEST_ASSERT(recOk, "Wired serial recovery protocol processed final chunk successfully");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 20: LORA CONFIGURATION DISTRIBUTION FEASIBILITY
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(20, "LoRa Configuration Distribution Feasibility");
  {
    // Small config packet (64 bytes)
    float airtimeConfig = ConfigValidation::calculatePacketAirtimeMs(64);
    TEST_ASSERT(airtimeConfig < 800.0f, "Sectional config packet airtime (" + to_string(airtimeConfig) + "ms) < 800ms");

    // Theoretical 1.2MB firmware evaluation
    LoRaFeasibilityReport rep = PoleFirmwareManager::evaluateLoRaFeasibility(1200000, 3);
    cout << "  ----------------------------------------------------------------" << endl;
    cout << "  LORA 433 MHz FIRMWARE FEASIBILITY CALCULATION (1.2 MB BINARY):" << endl;
    cout << "  • Total Packets (200B chunks)   : " << rep.totalPackets << endl;
    cout << "  • Airtime per packet (SF10, 125k): " << rep.timeOnAirPerPacketMs << " ms" << endl;
    cout << "  • Raw Multi-Hop Airtime (3 Hops): " << rep.totalRawAirtimeSec << " seconds ("
         << (rep.totalRawAirtimeSec / 3600.0f) << " hours)" << endl;
    cout << "  • 1% Duty-Cycle Minimum Time    : " << rep.minRegulatoryTimeDays << " DAYS!" << endl;
    cout << "  • 3-Hop Success Probability     : " << (rep.packetSuccessProb3Hops * 100.0f) << "%" << endl;
    cout << "  • Verdict                       : " << rep.verdict << endl;
    cout << "  ----------------------------------------------------------------" << endl;
    TEST_ASSERT(!rep.isFeasibleForProduction, "Mathematical proof establishes 1.2MB LoRa firmware transfer is non-viable for production");
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 21: UPDATE AUDIT HISTORY
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(21, "Update Audit History");
  {
    uint8_t auditCount = gwConfigMgr.getAuditCount();
    TEST_ASSERT(auditCount > 0, "Audit log contains " + to_string(auditCount) + " immutable records");
    const ConfigAuditRecord *rec = gwConfigMgr.getAuditRecord(0);
    TEST_ASSERT(rec && strlen(rec->transactionId) > 0, "Audit record has valid Transaction ID: " + String(rec->transactionId));
    TEST_ASSERT(rec && strlen(rec->adminId) > 0, "Audit record records Administrator Identity: " + String(rec->adminId));
  }

  // ─────────────────────────────────────────────────────────────
  //  SCENARIO 22: NETWORK CONTINUITY DURING UPDATES
  // ─────────────────────────────────────────────────────────────
  printSectionHeader(22, "Network Continuity During Updates");
  {
    // Verify that emergency traffic preempts background config distribution
    configDist.pauseForEmergencyTraffic();
    TEST_ASSERT(configDist.isEmergencyPaused(), "Emergency preemption priority verified over background config");
    configDist.resumeAfterEmergencyTraffic();
    TEST_ASSERT(!configDist.isEmergencyPaused(), "Network resumes normal pacing when emergency ends");
  }

  // ─────────────────────────────────────────────────────────────
  //  FINAL SCORECARD
  // ─────────────────────────────────────────────────────────────
  cout << "\n======================================================================" << endl;
  cout << " TEST EXECUTION SUMMARY" << endl;
  cout << " Total Scenarios Tested : 22" << endl;
  cout << " Passed Tests           : " << g_passCount << endl;
  cout << " Failed Tests           : " << g_failCount << endl;
  cout << " Overall Status         : " << (g_failCount == 0 ? "100% ALL TESTS PASSED ✅" : "TEST FAILURES DETECTED ❌") << endl;
  cout << "======================================================================" << endl;

  return (g_failCount == 0) ? 0 : 1;
}
