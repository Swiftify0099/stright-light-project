/*
 * =====================================================================
 *  TELEGRAMMENUS.H — TELEGRAM DASHBOARD UI & KEYBOARD BUILDERS
 *  MSCB Smart Pole Automation System — Prompt 18 Architecture
 * =====================================================================
 */

#ifndef TELEGRAM_MENUS_H
#define TELEGRAM_MENUS_H

#include <Arduino.h>
#include "config.h"
#include "Types.h"

class TelegramMenus {
public:
  TelegramMenus();

  // Helpers
  String formatUptime(unsigned long ms);
  String formatChannelCurrent(float val, MeasurementValidity valState, unsigned long updateTime);
  String formatChannelVoltage(float val, MeasurementValidity valState, unsigned long updateTime);

  // Section 2: Main Dashboard & Gateway Status
  String buildMainDashboardMessage(const String &wifiSsid);
  String buildMainDashboardKeyboard();
  String buildGatewayStatusMessage(const String &wifiSsid);
  String buildGatewayStatusKeyboard();

  // Section 3: Pole Dashboard (All, Online, Offline pagination)
  String buildAllPolesMessage(UserSession &sess, const String &filter);
  String buildAllPolesKeyboard(UserSession &sess, const String &filter);
  String buildPoleDetailMessage(UserSession &sess);
  String buildPoleDetailKeyboard(UserSession &sess);

  // Section 4: Feeder Dashboard (F1-F6)
  String buildFeederDashboardMessage(UserSession &sess);
  String buildFeederDashboardKeyboard(UserSession &sess);

  // Section 5: Voltage Dashboard
  String buildVoltageDashboardMessage(UserSession &sess);
  String buildVoltageDashboardKeyboard(UserSession &sess);
  String buildVoltageSummaryAllMessage(UserSession &sess);
  String buildVoltageSummaryAllKeyboard(UserSession &sess);
  String buildVoltageTrendMessage(UserSession &sess);
  String buildPhaseComparisonMessage(UserSession &sess);

  // Section 6: Current Dashboard (Modes A-H)
  String buildCurrentDashboardMessage(UserSession &sess);
  String buildCurrentDashboardKeyboard(UserSession &sess);

  // Section 7 & 13: Fault Alerts & Incident Management
  String buildFaultAlertsMessage();
  String buildFaultAlertsKeyboard();
  String buildIncidentDetailMessage(uint32_t eventId);
  String buildIncidentDetailKeyboard(uint32_t eventId);

  // Section 12: Event History Audit Log
  String buildEventHistoryMessage(UserSession &sess);
  String buildEventHistoryKeyboard(UserSession &sess);

  // Network Topology
  String buildTopologyMessage();
  String buildTopologyKeyboard();

  // Registration & Delete Management
  String buildRegistrationMessage(UserSession &sess);
  String buildRegistrationKeyboard(UserSession &sess);
  String buildDeleteManagementMessage(UserSession &sess);
  String buildDeleteManagementKeyboard(UserSession &sess);

  // Legacy & Utility
  String buildHomeMessage(const String &wifiSsid) { return buildMainDashboardMessage(wifiSsid); }
  String buildHomeKeyboard() { return buildMainDashboardKeyboard(); }
  String buildSettingsMessage();
  String buildSettingsKeyboard();
  String buildStatusMessage(const String &wifiSsid) { return buildMainDashboardMessage(wifiSsid); }
  String buildStatusKeyboard() { return buildMainDashboardKeyboard(); }
  String buildFeederStatusKeyboard();
  String buildNeighboursMessage(UserSession &sess);
  String buildNeighboursKeyboard(UserSession &sess);
  String buildPoleNeighboursMessage(UserSession &sess);
  String buildPoleNeighboursKeyboard(UserSession &sess);
  String buildPoleCtrlMessage(UserSession &sess);
  String buildPoleCtrlKeyboard(UserSession &sess);
  String buildFarmerListMessage(UserSession &sess);
  String buildFarmerListKeyboard(UserSession &sess);
  String buildFarmerCtrlMessage(UserSession &sess);
  String buildFarmerCtrlKeyboard(UserSession &sess);
  String buildTimerSelectMessage(UserSession &sess);
  String buildTimerSelectKeyboard(UserSession &sess);
  String buildScheduleMessage(UserSession &sess);
  String buildScheduleKeyboard(UserSession &sess);
  String buildRegTypeSelectMessage(const String &hwId);
  String buildRegTypeSelectKeyboard(const String &hwId);
  String buildResetSelectMessage();
  String buildResetSelectKeyboard();
  String buildResetPoleListKeyboard();
  String buildResetConfirmPoleMessage(const String &poleId);
  String buildResetConfirmPoleKeyboard(const String &poleId);
  String buildResetConfirmNeighMessage(const String &originPoleId, const String &neighId);
  String buildResetConfirmNeighKeyboard(const String &originId, const String &neighId);
  String buildResetConfirmAllMessage();
  String buildResetConfirmAllKeyboard();
};

extern TelegramMenus tgMenus;

#endif // TELEGRAM_MENUS_H
