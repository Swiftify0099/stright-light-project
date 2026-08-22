/*
 * =====================================================================
 *  TELEGRAMMENUS.H — TELEGRAM DASHBOARD UI & KEYBOARD BUILDERS
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

  // Message builders
  String buildHomeMessage(const String &wifiSsid);
  String buildSettingsMessage();
  String buildStatusMessage(const String &wifiSsid);
  String buildNeighboursMessage(UserSession &sess);
  String buildPoleNeighboursMessage(UserSession &sess);
  String buildPoleCtrlMessage(UserSession &sess);
  String buildFarmerListMessage(UserSession &sess);
  String buildFarmerCtrlMessage(UserSession &sess);
  String buildTimerSelectMessage(UserSession &sess);
  String buildScheduleMessage(UserSession &sess);
  String buildRegistrationMessage(UserSession &sess);
  String buildRegTypeSelectMessage(const String &hwId);
  String buildResetSelectMessage();
  String buildResetConfirmPoleMessage(const String &poleId);
  String buildResetConfirmNeighMessage(const String &originPoleId, const String &neighId);
  String buildResetConfirmAllMessage();

  // Keyboard builders
  String buildHomeKeyboard();
  String buildSettingsKeyboard();
  String buildStatusKeyboard();
  String buildFeederStatusKeyboard();
  String buildNeighboursKeyboard(UserSession &sess);
  String buildPoleNeighboursKeyboard(UserSession &sess);
  String buildPoleCtrlKeyboard(UserSession &sess);
  String buildFarmerListKeyboard(UserSession &sess);
  String buildFarmerCtrlKeyboard(UserSession &sess);
  String buildTimerSelectKeyboard(UserSession &sess);
  String buildScheduleKeyboard(UserSession &sess);
  String buildRegistrationKeyboard(UserSession &sess);
  String buildRegTypeSelectKeyboard(const String &hwId);
  String buildResetSelectKeyboard();
  String buildResetPoleListKeyboard();
  String buildResetConfirmPoleKeyboard(const String &poleId);
  String buildResetConfirmNeighKeyboard(const String &originId, const String &neighId);
  String buildResetConfirmAllKeyboard();

  // Helper
  String formatUptime(unsigned long ms);
};

extern TelegramMenus tgMenus;

#endif // TELEGRAM_MENUS_H
