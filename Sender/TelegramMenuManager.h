/*
 * =====================================================================
 *  TELEGRAMMENUMANAGER.H — DASHBOARD UI & INLINE KEYBOARD BUILDERS
 *  MSCB Smart Pole Central Gateway System — Prompt 16 Architecture
 * =====================================================================
 */

#ifndef TELEGRAM_MENU_MANAGER_H
#define TELEGRAM_MENU_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

#define POLES_PAGE_SIZE    5
#define FEEDERS_PAGE_SIZE  6

class TelegramMenuManager {
public:
  TelegramMenuManager();

  // Helper
  String formatUptime(unsigned long ms);
  String formatRelativeTime(unsigned long pastMs);

  // ─────────────────────────────────────────────────────────────
  //  MESSAGE BUILDERS FOR 20 DASHBOARD SCREENS
  // ─────────────────────────────────────────────────────────────
  // 1. System Dashboard
  String buildDashboardMessage(const String &wifiSsid);
  String buildDashboardKeyboard();

  // 2. Pole Management
  String buildPoleManagementMessage(UserSession &sess);
  String buildPoleManagementKeyboard(UserSession &sess);

  // 2B. Pole Detail
  String buildPoleDetailMessage(UserSession &sess);
  String buildPoleDetailKeyboard(UserSession &sess);

  // 3. Feeder Management
  String buildFeederManagementMessage(UserSession &sess);
  String buildFeederManagementKeyboard(UserSession &sess);

  // 3B. Feeder Detail
  String buildFeederDetailMessage(UserSession &sess);
  String buildFeederDetailKeyboard(UserSession &sess);

  // 4. New Node Registration
  String buildRegistrationMessage(UserSession &sess);
  String buildRegistrationKeyboard(UserSession &sess);

  // 5. Delete / De-register Pole
  String buildDeleteManagementMessage(UserSession &sess);
  String buildDeleteManagementKeyboard(UserSession &sess);

  // 5B. Delete Confirmation
  String buildDeleteConfirmMessage(UserSession &sess);
  String buildDeleteConfirmKeyboard(UserSession &sess);

  // 5C. Revoked Nodes List
  String buildRevokedListMessage(UserSession &sess);
  String buildRevokedListKeyboard(UserSession &sess);

  // 6. Individual Pole Control
  String buildPoleControlMessage(UserSession &sess);
  String buildPoleControlKeyboard(UserSession &sess);

  // 7. Individual Feeder Control
  String buildFeederControlMessage(UserSession &sess);
  String buildFeederControlKeyboard(UserSession &sess);

  // 8. Group Control
  String buildGroupControlMessage(UserSession &sess);
  String buildGroupControlKeyboard(UserSession &sess);

  // 8B. Group Confirmation
  String buildGroupConfirmMessage(UserSession &sess);
  String buildGroupConfirmKeyboard(UserSession &sess);

  // 9. All Poles Control
  String buildAllPolesControlMessage(UserSession &sess);
  String buildAllPolesControlKeyboard(UserSession &sess);

  // 9B. All Poles Confirmation
  String buildAllConfirmMessage(UserSession &sess);
  String buildAllConfirmKeyboard(UserSession &sess);

  // 10. Voltage Monitoring
  String buildVoltageMonitoringMessage(UserSession &sess);
  String buildVoltageMonitoringKeyboard(UserSession &sess);

  // 10B. Voltage Detail
  String buildVoltageDetailMessage(UserSession &sess);
  String buildVoltageDetailKeyboard(UserSession &sess);

  // 11. Current Monitoring (18 Channels per Pole, 900 across network)
  String buildCurrentMonitoringMessage(UserSession &sess);
  String buildCurrentMonitoringKeyboard(UserSession &sess);

  // 11B. Current Detail
  String buildCurrentDetailMessage(UserSession &sess);
  String buildCurrentDetailKeyboard(UserSession &sess);

  // 12. Network Topology (ASCII Tree)
  String buildTopologyMessage(UserSession &sess);
  String buildTopologyKeyboard(UserSession &sess);

  // 13. Online / Offline Nodes
  String buildNodeStatusMessage(UserSession &sess);
  String buildNodeStatusKeyboard(UserSession &sess);

  // 14. Fault Alerts
  String buildFaultAlertsMessage(UserSession &sess);
  String buildFaultAlertsKeyboard(UserSession &sess);

  // 15. Time-Based Configuration
  String buildTimeConfigMessage(UserSession &sess);
  String buildTimeConfigKeyboard(UserSession &sess);

  // 16. Wi-Fi Settings & Credential Management (Prompt 17)
  String buildWiFiSettingsMessage(const String &wifiSsid);
  String buildWiFiSettingsKeyboard();
  String buildWiFiConfirmMessage(const String &candidateSsid, size_t passLen);
  String buildWiFiConfirmKeyboard();

  // 17. Gateway Settings
  String buildGatewaySettingsMessage();
  String buildGatewaySettingsKeyboard();

  // 18. Firmware Management (View-only in this stage)
  String buildFirmwareManagementMessage();
  String buildFirmwareManagementKeyboard();

  // 19. Event History
  String buildEventHistoryMessage(UserSession &sess);
  String buildEventHistoryKeyboard(UserSession &sess);

  // 20. Help / System Information
  String buildHelpInfoMessage();
  String buildHelpInfoKeyboard();

  // Backward-compatibility wrappers for old TelegramMenus
  String buildHomeMessage(const String &wifiSsid) { return buildDashboardMessage(wifiSsid); }
  String buildHomeKeyboard() { return buildDashboardKeyboard(); }
  String buildSettingsMessage() { return buildGatewaySettingsMessage(); }
  String buildSettingsKeyboard() { return buildGatewaySettingsKeyboard(); }
  String buildStatusMessage(const String &wifiSsid) { return buildDashboardMessage(wifiSsid); }
  String buildStatusKeyboard() { return buildDashboardKeyboard(); }
  String buildFeederStatusKeyboard() { return buildDashboardKeyboard(); }
  String buildNeighboursMessage(UserSession &sess) { return buildPoleManagementMessage(sess); }
  String buildNeighboursKeyboard(UserSession &sess) { return buildPoleManagementKeyboard(sess); }
  String buildPoleCtrlMessage(UserSession &sess) { return buildPoleControlMessage(sess); }
  String buildPoleCtrlKeyboard(UserSession &sess) { return buildPoleControlKeyboard(sess); }
  String buildFarmerListMessage(UserSession &sess) { return buildPoleManagementMessage(sess); }
  String buildFarmerListKeyboard(UserSession &sess) { return buildPoleManagementKeyboard(sess); }
  String buildPoleNeighboursMessage(UserSession &sess) { return buildTopologyMessage(sess); }
  String buildPoleNeighboursKeyboard(UserSession &sess) { return buildTopologyKeyboard(sess); }
  String buildTimerSelectMessage(UserSession &sess) { return buildTimeConfigMessage(sess); }
  String buildTimerSelectKeyboard(UserSession &sess) { return buildTimeConfigKeyboard(sess); }
  String buildRegTypeSelectMessage(const String &hwId) { return "Select Type for: `" + hwId + "`"; }
  String buildRegTypeSelectKeyboard(const String &hwId) {
    return "{\"inline_keyboard\":[[{\"text\":\"💡 Street Light\",\"callback_data\":\"REG_T:SL:" + hwId + "\"},{\"text\":\"🌾 Farmer Node\",\"callback_data\":\"REG_T:FR:" + hwId + "\"}],[{\"text\":\"🔙 Back\",\"callback_data\":\"NAV:REG\"}]]}";
  }
};

extern TelegramMenuManager menuMgr;

#endif // TELEGRAM_MENU_MANAGER_H
