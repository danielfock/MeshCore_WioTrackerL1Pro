#include "UITask.h"

#include <RTClib.h>

#include <helpers/LocalTimeUtils.h>

#include "../MyMesh.h"
#include "target.h"

namespace {

constexpr unsigned long kSplashMillis = 1800UL;
constexpr unsigned long kAlertDefaultMillis = 1200UL;
constexpr int kRowY[5] = {14, 24, 34, 44, 54};
constexpr int kSectionRowY[5] = {18, 27, 36, 45, 54};

const uint16_t kDisplayTimeoutChoices[] = {0, 15, 30, 60, 120, 300};
const char* kMainMenuLabels[] = {"Status", "GPS", "Mesh", "Settings", "Device"};
const char* kOverviewLabels[] = {"Power", "Links", "Tracker", "Traffic"};
const char* kGpsMenuLabels[] = {"Detail", "GPS power", "Tracker", "Time sync"};
const char* kMeshMenuLabels[] = {"Send advert", "Flood advert", "Clear unread"};
const char* kSettingsMenuLabels[] = {"Bluetooth", "Sound", "Power save", "Display", "Reboot"};
const char* kInfoLabels[] = {"Identity", "Reset", "Firmware"};

void copyDisplayText(DisplayDriver& display, char* dest, size_t dest_size, const char* src) {
  if (!dest || dest_size == 0) {
    return;
  }
  dest[0] = 0;
  if (!src) {
    return;
  }
  display.translateUTF8ToBlocks(dest, src, dest_size);
}

const char* resetReasonLabel(mesh::MainBoard* board) {
  if (!board) return "-";
  const char* label = board->getResetReasonString(board->getResetReason());
  if (!label || !label[0]) return "-";
  if (strcmp(label, "Watchdog") == 0) return "WDT";
  if (strcmp(label, "CPU Lockup") == 0) return "LOCK";
  if (strcmp(label, "Soft Reset") == 0) return "SOFT";
  if (strcmp(label, "Reset Pin") == 0) return "RST";
  if (strcmp(label, "Wake from GPIO") == 0) return "GPIO";
  if (strcmp(label, "Wake from VBUS") == 0) return "VBUS";
  if (strcmp(label, "Cold Boot") == 0) return "BOOT";
  return "OTHER";
}

bool isCrashReset(mesh::MainBoard* board) {
  const char* label = board ? board->getResetReasonString(board->getResetReason()) : NULL;
  return label && (strcmp(label, "Watchdog") == 0 || strcmp(label, "CPU Lockup") == 0);
}

const char* onOffLabel(bool value) {
  return value ? "ON" : "OFF";
}

const char* yesNoLabel(bool value) {
  return value ? "YES" : "NO";
}

const char* displayTimeoutLabel(uint16_t seconds) {
  switch (seconds) {
    case 0: return "OFF";
    case 15: return "15s";
    case 30: return "30s";
    case 60: return "1m";
    case 120: return "2m";
    case 300: return "5m";
    default: return "?";
  }
}

uint16_t nextDisplayTimeout(uint16_t current) {
  int index = 0;
  for (int i = 0; i < (int)(sizeof(kDisplayTimeoutChoices) / sizeof(kDisplayTimeoutChoices[0])); ++i) {
    if (kDisplayTimeoutChoices[i] == current) {
      index = i;
      break;
    }
  }
  index++;
  if (index >= (int)(sizeof(kDisplayTimeoutChoices) / sizeof(kDisplayTimeoutChoices[0]))) {
    index = 0;
  }
  return kDisplayTimeoutChoices[index];
}

void formatLocalClock(char* dest, size_t dest_size, uint32_t now) {
  if (!dest || dest_size == 0) {
    return;
  }
  if (now > 1000000000UL) {
    DateTime local = mesh::localtime::europeViennaDateTime(now);
    snprintf(dest, dest_size, "%02u:%02u:%02u %s", local.hour(), local.minute(), local.second(),
             mesh::localtime::europeViennaZoneLabel(now));
  } else {
    snprintf(dest, dest_size, "Waiting for sync");
  }
}

void formatUptime(char* dest, size_t dest_size, unsigned long uptime_ms) {
  if (!dest || dest_size == 0) {
    return;
  }
  unsigned long total_seconds = uptime_ms / 1000UL;
  unsigned long hours = total_seconds / 3600UL;
  unsigned long minutes = (total_seconds / 60UL) % 60UL;
  unsigned long seconds = total_seconds % 60UL;
  if (hours < 100UL) {
    snprintf(dest, dest_size, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  } else {
    snprintf(dest, dest_size, "%luh", hours);
  }
}

}  // namespace

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  _screen = Screen::Splash;
  _msgcount = 0;
  _main_menu_row = 0;
  _gps_menu_row = 0;
  _mesh_menu_row = 0;
  _settings_menu_row = 0;
  _overview_section = 0;
  _info_section = 0;
  _display_soft_off = false;
  _message_active = false;
  _ignore_buttons_until_release = false;
  _alert_until = 0;
  _ui_started_at = millis();
  resetDisplayDeadline();

  if (_display) {
    _display->turnOn();
  }

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if UI_HAS_JOYSTICK
  joystick_up.begin();
  joystick_down.begin();
  joystick_left.begin();
  joystick_right.begin();
  back_btn.begin();
#endif

#ifdef PIN_BUZZER
  _buzzer.begin();
  applySoundMode();
#endif

  if (isCrashReset(_board)) {
    setAlert("Recovered after crash", kSplashMillis + 2000UL);
  }

  requestRefresh();
}

void UITask::requestRefresh(unsigned long delay_millis) {
  _next_refresh = millis() + delay_millis;
}

void UITask::setAlert(const char* text, unsigned long duration_millis) {
  if (!text) {
    _alert[0] = 0;
    _alert_until = 0;
    return;
  }
  strncpy(_alert, text, sizeof(_alert) - 1);
  _alert[sizeof(_alert) - 1] = 0;
  _alert_until = millis() + duration_millis;
  requestRefresh();
}

void UITask::clearMessagePreview() {
  _message_active = false;
  _msg_from[0] = 0;
  _msg_text[0] = 0;
  requestRefresh();
}

unsigned long UITask::getDisplayTimeoutMillis() const {
  if (!_node_prefs || _node_prefs->display_timeout_s == 0) {
    return 0;
  }
  return (unsigned long)_node_prefs->display_timeout_s * 1000UL;
}

unsigned long UITask::getRefreshIntervalMillis() const {
  const unsigned long now = millis();

  if (_alert[0] && _alert_until > now) {
    const unsigned long remaining = _alert_until - now;
    return remaining < 250UL ? remaining : 250UL;
  }

  if ((now - _ui_started_at) < kSplashMillis) {
    return 250UL;
  }

  if (_message_active) {
    return 0;
  }

  switch (_screen) {
    case Screen::Home:
      return 1000UL;
    case Screen::Overview:
      return 2000UL;
    case Screen::GPSDetail:
      return 1000UL;
    case Screen::Info:
      return 2000UL;
    default:
      return 0;
  }
}

void UITask::resetDisplayDeadline() {
  unsigned long timeout_ms = getDisplayTimeoutMillis();
  _display_deadline = timeout_ms > 0 ? millis() + timeout_ms : 0;
}

int UITask::getBatteryPercent(uint16_t battery_mv) const {
  const int min_mv = 3000;
  const int max_mv = 4200;
  int pct = ((int)battery_mv - min_mv) * 100 / (max_mv - min_mv);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

bool UITask::isGpsEnabled() const {
  return _node_prefs && _node_prefs->gps_enabled != 0;
}

bool UITask::hasGpsFix() const {
  return _sensors && _sensors->getLocationProvider() && _sensors->getLocationProvider()->isValid();
}

uint8_t UITask::getSoundMode() const {
  if (!_node_prefs) {
    return 0;
  }

  switch (_node_prefs->buzzer_quiet) {
    case 1:
    case 2:
      return _node_prefs->buzzer_quiet;
    default:
      return 0;
  }
}

const char* UITask::getSoundModeLabel() const {
  switch (getSoundMode()) {
    case 1:
      return "Silent";
    case 2:
      return "Loud";
    default:
      return "Normal";
  }
}

void UITask::applySoundMode() {
#ifdef PIN_BUZZER
  _buzzer.quiet(getSoundMode() == 1);
#endif
}

void UITask::cycleSoundMode() {
#ifdef PIN_BUZZER
  if (!_node_prefs) return;
  const uint8_t modes[] = {1, 0, 2};
  int idx = 1;
  const uint8_t current = getSoundMode();
  for (int i = 0; i < 3; ++i) {
    if (modes[i] == current) {
      idx = i;
      break;
    }
  }

  idx = (idx + 1) % 3;
  _node_prefs->buzzer_quiet = modes[idx];
  applySoundMode();
  the_mesh.savePrefs();
  setAlert(getSoundModeLabel(), kAlertDefaultMillis);
#endif
}

void UITask::previewSoundMode() {
#ifdef PIN_BUZZER
  if (getSoundMode() == 1) {
    return;
  }
  notify(UIEventType::ack);
#endif
}

bool UITask::anyButtonPressed() const {
  bool pressed = false;
#if defined(PIN_USER_BTN)
  pressed = pressed || user_btn.isPressed();
#endif
#if UI_HAS_JOYSTICK
  pressed = pressed || joystick_up.isPressed() || joystick_down.isPressed() ||
            joystick_left.isPressed() || joystick_right.isPressed() || back_btn.isPressed();
#endif
  return pressed;
}

void UITask::cancelButtonClicks() {
#if defined(PIN_USER_BTN)
  user_btn.cancelClick();
#endif
#if UI_HAS_JOYSTICK
  joystick_up.cancelClick();
  joystick_down.cancelClick();
  joystick_left.cancelClick();
  joystick_right.cancelClick();
  back_btn.cancelClick();
#endif
}

void UITask::wakeDisplay(bool consume_current_press) {
  if (_display) {
    if (!_display->isOn()) {
      _display->turnOn();
    }
    if (_display_soft_off) {
      _display_soft_off = false;
    }
    requestRefresh();
  }
  resetDisplayDeadline();
  if (consume_current_press) {
    cancelButtonClicks();
    _ignore_buttons_until_release = true;
  }
}

void UITask::openMainMenu() {
  _screen = Screen::MainMenu;
  requestRefresh();
}

void UITask::goHome() {
  _screen = Screen::Home;
  requestRefresh();
}

void UITask::goBack() {
  if (_message_active) {
    clearMessagePreview();
    return;
  }

  switch (_screen) {
    case Screen::MainMenu:
      goHome();
      break;
    case Screen::Overview:
    case Screen::GPSMenu:
    case Screen::MeshMenu:
    case Screen::SettingsMenu:
    case Screen::Info:
      openMainMenu();
      break;
    case Screen::GPSDetail:
      _screen = Screen::GPSMenu;
      requestRefresh();
      break;
    default:
      goHome();
      break;
  }
}

void UITask::renderHeader(DisplayDriver& display, const char* title) const {
  char name[32];
  copyDisplayText(display, name, sizeof(name), (_node_prefs && _node_prefs->node_name[0]) ? _node_prefs->node_name : "Client");
  display.setTextSize(1);
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextEllipsized(0, 0, 68, name);
  if (title) {
    display.drawTextRightAlign(display.width(), 0, title);
  }
  display.fillRect(0, 9, display.width(), 1);
}

void UITask::renderMenuRow(DisplayDriver& display, int row_idx, int selected_row, const char* label, const char* value) const {
  int y = kRowY[row_idx];
  if (row_idx == selected_row) {
    display.drawRect(0, y - 1, display.width(), 9);
  }

  char full_label[36];
  snprintf(full_label, sizeof(full_label), "%c %s", row_idx == selected_row ? '>' : ' ', label ? label : "");
  display.drawTextEllipsized(2, y, value ? 80 : display.width() - 4, full_label);
  if (value) {
    display.drawTextRightAlign(display.width() - 2, y, value);
  }
}

void UITask::renderValueRow(DisplayDriver& display, int row_idx, const char* label, const char* value) const {
  if (row_idx < 0 || row_idx >= 5) {
    return;
  }
  int y = kRowY[row_idx];
  display.drawTextEllipsized(0, y, value ? 76 : display.width(), label ? label : "");
  if (value) {
    display.drawTextRightAlign(display.width() - 1, y, value);
  }
}

void UITask::renderSectionValueRow(DisplayDriver& display, int row_idx, const char* label, const char* value) const {
  if (row_idx < 0 || row_idx >= 5) {
    return;
  }
  int y = kSectionRowY[row_idx];
  display.drawTextEllipsized(0, y, value ? 74 : display.width(), label ? label : "");
  if (value) {
    display.drawTextRightAlign(display.width() - 1, y, value);
  }
}

void UITask::renderSectionDots(DisplayDriver& display, int count, int active) const {
  if (count <= 0) {
    return;
  }
  const int dot_w = 3;
  const int gap = 4;
  const int total_w = count * dot_w + (count - 1) * gap;
  int x = (display.width() - total_w) / 2;
  int y = 12;
  for (int i = 0; i < count; ++i) {
    if (i == active) {
      display.fillRect(x, y, dot_w, dot_w);
    } else {
      display.drawRect(x, y, dot_w, dot_w);
    }
    x += dot_w + gap;
  }
}

void UITask::renderBatteryIndicator(DisplayDriver& display, uint16_t battery_mv) const {
  int pct = getBatteryPercent(battery_mv);
  int x = display.width() - 24;
  int y = 14;
  display.drawRect(x, y, 18, 8);
  display.fillRect(x + 18, y + 2, 2, 4);
  int fill_w = ((18 - 4) * pct) / 100;
  if (fill_w > 0) {
    display.fillRect(x + 2, y + 2, fill_w, 4);
  }
}

void UITask::drawWrappedSnippet(DisplayDriver& display, int x, int y, int w, const char* text, int max_lines) const {
  if (!text || !text[0] || max_lines <= 0) {
    return;
  }

  const char* remaining = text;
  for (int line = 0; line < max_lines && *remaining; ++line) {
    while (*remaining == ' ') {
      remaining++;
    }
    if (!*remaining) {
      break;
    }

    char temp[80];
    size_t src_len = strlen(remaining);
    if (src_len >= sizeof(temp)) {
      src_len = sizeof(temp) - 1;
    }

    int best = 0;
    int last_space = -1;
    for (size_t i = 0; i < src_len; ++i) {
      temp[i] = remaining[i];
      temp[i + 1] = 0;
      if (remaining[i] == ' ') {
        last_space = (int)i;
      }
      if (display.getTextWidth(temp) > w) {
        best = last_space >= 0 ? last_space : (int)i;
        break;
      }
      best = (int)i + 1;
    }

    if (best <= 0) {
      best = 1;
    }
    while (best > 0 && remaining[best - 1] == ' ') {
      best--;
    }

    memcpy(temp, remaining, best);
    temp[best] = 0;
    display.drawTextLeftAlign(x, y + line * 10, temp);
    remaining += best;
  }
}

void UITask::renderSplash(DisplayDriver& display) {
  renderHeader(display, "BOOT");
  char name[32];
  copyDisplayText(display, name, sizeof(name), (_node_prefs && _node_prefs->node_name[0]) ? _node_prefs->node_name : "Client");
  display.setTextSize(2);
  display.drawTextCentered(display.width() / 2, 18, name);
  display.setTextSize(1);
  display.drawTextCentered(display.width() / 2, 42, "WioTracker Core");
  display.drawTextCentered(display.width() / 2, 54, FIRMWARE_VERSION);
}

void UITask::renderHome(DisplayDriver& display) {
  renderHeader(display, NULL);
  char header_value[12];
  snprintf(header_value, sizeof(header_value), "%d%%", getBatteryPercent(getBattMilliVolts()));
  display.drawTextRightAlign(display.width() - 1, 0, header_value);

  char time_line[20];
  char zone_line[16];
  uint32_t now = rtc_clock.getCurrentTime();
  if (now > 1000000000UL) {
    DateTime local = mesh::localtime::europeViennaDateTime(now);
    snprintf(time_line, sizeof(time_line), "%02u:%02u:%02u", local.hour(), local.minute(), local.second());
    snprintf(zone_line, sizeof(zone_line), "%s", mesh::localtime::europeViennaZoneLabel(now));
  } else {
    snprintf(time_line, sizeof(time_line), "--:--:--");
    snprintf(zone_line, sizeof(zone_line), "WAIT");
  }

  display.setTextSize(2);
  display.drawTextCentered(display.width() / 2, 17, time_line);
  display.setTextSize(1);
  display.drawTextCentered(display.width() / 2, 36, zone_line);

  char label[32];
  char value[20];
  if (hasGpsFix() && _sensors && _sensors->getLocationProvider()) {
    snprintf(value, sizeof(value), "%ld SAT", _sensors->getLocationProvider()->satellitesCount());
    renderValueRow(display, 3, "GPS FIX", value);
  } else {
    renderValueRow(display, 3, "GPS", isGpsEnabled() ? "WAIT" : "OFF");
  }

  if (isSerialEnabled() && !hasConnection() && the_mesh.getBLEPin() != 0) {
    snprintf(value, sizeof(value), "%06lu", (unsigned long)the_mesh.getBLEPin());
    renderValueRow(display, 4, "PAIR PIN", value);
    return;
  }

  snprintf(label, sizeof(label), "Unread %d", _msgcount);
  if (hasConnection()) {
    renderValueRow(display, 4, label, "LINK");
  } else if (isSerialEnabled()) {
    renderValueRow(display, 4, label, "READY");
  } else {
    renderValueRow(display, 4, label, "BLE OFF");
  }
}

void UITask::renderMainMenu(DisplayDriver& display) {
  renderHeader(display, "MENU");

  char value[20];
  snprintf(value, sizeof(value), "%d%%", getBatteryPercent(getBattMilliVolts()));
  renderMenuRow(display, 0, _main_menu_row, kMainMenuLabels[0], value);

  snprintf(value, sizeof(value), "%s", hasGpsFix() ? "FIX" : "WAIT");
  renderMenuRow(display, 1, _main_menu_row, kMainMenuLabels[1], value);

  snprintf(value, sizeof(value), "Msg %d", _msgcount);
  renderMenuRow(display, 2, _main_menu_row, kMainMenuLabels[2], value);

  snprintf(value, sizeof(value), "%s", (_node_prefs && _node_prefs->power_saving_mode) ? "ECO" : "FULL");
  renderMenuRow(display, 3, _main_menu_row, kMainMenuLabels[3], value);

  snprintf(value, sizeof(value), "%s", resetReasonLabel(_board));
  renderMenuRow(display, 4, _main_menu_row, kMainMenuLabels[4], value);
}

void UITask::renderOverview(DisplayDriver& display) {
  renderHeader(display, NULL);
  display.drawTextLeftAlign(46, 0, "STATUS");
  char pct[12];
  snprintf(pct, sizeof(pct), "%d%%", getBatteryPercent(getBattMilliVolts()));
  display.drawTextRightAlign(display.width() - 1, 0, pct);
  renderSectionDots(display, (int)OverviewSection::Count, _overview_section);

  char value[24];
  switch (_overview_section) {
    case (int)OverviewSection::Power:
      renderSectionValueRow(display, 0, "Power source", _board->isExternalPowered() ? "USB" : "BAT");
      renderSectionValueRow(display, 1, "Display off", displayTimeoutLabel(_node_prefs ? _node_prefs->display_timeout_s : 0));
      renderSectionValueRow(display, 2, "Power save", (_node_prefs && _node_prefs->power_saving_mode) ? "ECO" : "FULL");
#ifdef PIN_BUZZER
      renderSectionValueRow(display, 3, "Sound", getSoundModeLabel());
#else
      renderSectionValueRow(display, 3, "Sound", "N/A");
#endif
#if FEATURE_GPS_TRACKER
      if (the_mesh.isTrackerGpsEcoSleeping()) {
        snprintf(value, sizeof(value), "%lus", (unsigned long)the_mesh.getTrackerGpsEcoWakeSeconds());
      } else {
        snprintf(value, sizeof(value), "%s", "awake");
      }
      renderSectionValueRow(display, 4, "GPS eco", value);
#else
      renderSectionValueRow(display, 4, "GPS eco", "N/A");
#endif
      break;

    case (int)OverviewSection::Links:
      renderSectionValueRow(display, 0, "App link", yesNoLabel(hasConnection()));
      renderSectionValueRow(display, 1, "Bluetooth", onOffLabel(isSerialEnabled()));
      renderSectionValueRow(display, 2, "GPS power", onOffLabel(isGpsEnabled()));
      renderSectionValueRow(display, 3, "GPS fix", yesNoLabel(hasGpsFix()));
      if (_sensors && _sensors->getLocationProvider()) {
        snprintf(value, sizeof(value), "%ld", _sensors->getLocationProvider()->satellitesCount());
      } else {
        snprintf(value, sizeof(value), "-");
      }
      renderSectionValueRow(display, 4, "Satellites", value);
      break;

    case (int)OverviewSection::Tracker:
#if FEATURE_GPS_TRACKER
      renderSectionValueRow(display, 0, "Tracker", onOffLabel(_node_prefs && _node_prefs->gps_tracker_active));
      renderSectionValueRow(display, 1, "Share pos", yesNoLabel(_node_prefs && _node_prefs->gps_tracker_position_sharing));
      snprintf(value, sizeof(value), "%lus", (unsigned long)(_node_prefs ? _node_prefs->gps_tracker_interval : 0UL));
      renderSectionValueRow(display, 2, "Interval", value);
      snprintf(value, sizeof(value), "%u", (unsigned)(_node_prefs ? _node_prefs->gps_tracker_hop_limit : 0));
      renderSectionValueRow(display, 3, "Hop limit", value);
      if (the_mesh.isTrackerGpsEcoSleeping()) {
        snprintf(value, sizeof(value), "%lus", (unsigned long)the_mesh.getTrackerGpsEcoWakeSeconds());
      } else {
        snprintf(value, sizeof(value), "%s", "awake");
      }
      renderSectionValueRow(display, 4, "GPS wake", value);
#else
      renderSectionValueRow(display, 0, "Tracker", "N/A");
      renderSectionValueRow(display, 1, "GPS read", onOffLabel(isGpsEnabled()));
      snprintf(value, sizeof(value), "%lus", (unsigned long)(_node_prefs ? _node_prefs->gps_interval : 0UL));
      renderSectionValueRow(display, 2, "Interval", value);
      renderSectionValueRow(display, 3, "Time sync", (_sensors && _sensors->getLocationProvider() &&
                                                      _sensors->getLocationProvider()->waitingTimeSync()) ? "PEND" : "READY");
      snprintf(value, sizeof(value), "%d", _msgcount);
      renderSectionValueRow(display, 4, "Unread", value);
#endif
      break;

    default:
      snprintf(value, sizeof(value), "%lu", (unsigned long)the_mesh.getNumSentFlood());
      renderSectionValueRow(display, 0, "Tx flood", value);
      snprintf(value, sizeof(value), "%lu", (unsigned long)the_mesh.getNumSentDirect());
      renderSectionValueRow(display, 1, "Tx direct", value);
      snprintf(value, sizeof(value), "%lu", (unsigned long)the_mesh.getNumRecvFlood());
      renderSectionValueRow(display, 2, "Rx flood", value);
      snprintf(value, sizeof(value), "%lu", (unsigned long)the_mesh.getNumRecvDirect());
      renderSectionValueRow(display, 3, "Rx direct", value);
      snprintf(value, sizeof(value), "%d", _msgcount);
      renderSectionValueRow(display, 4, "Unread", value);
      break;
  }
}

void UITask::renderGPSMenu(DisplayDriver& display) {
  renderHeader(display, "GPS");

  char value[24];
  snprintf(value, sizeof(value), "%s", hasGpsFix() ? "FIX" : "WAIT");
  renderMenuRow(display, 0, _gps_menu_row, kGpsMenuLabels[0], value);

  snprintf(value, sizeof(value), "%s", onOffLabel(isGpsEnabled()));
  renderMenuRow(display, 1, _gps_menu_row, kGpsMenuLabels[1], value);

#if FEATURE_GPS_TRACKER
  snprintf(value, sizeof(value), "%s", onOffLabel(_node_prefs && _node_prefs->gps_tracker_active));
#else
  snprintf(value, sizeof(value), "N/A");
#endif
  renderMenuRow(display, 2, _gps_menu_row, kGpsMenuLabels[2], value);

  snprintf(value, sizeof(value), "%s", _sensors && _sensors->getLocationProvider() &&
                                           _sensors->getLocationProvider()->waitingTimeSync() ? "PEND" : "READY");
  renderMenuRow(display, 3, _gps_menu_row, kGpsMenuLabels[3], value);

}

void UITask::renderGPSDetail(DisplayDriver& display) {
  renderHeader(display, "GPS INFO");

  char value[32];
  if (_sensors && _sensors->getLocationProvider()) {
    LocationProvider* gps = _sensors->getLocationProvider();
    if (gps->isValid()) {
      snprintf(value, sizeof(value), "YES");
      renderValueRow(display, 0, "Fix", value);
      snprintf(value, sizeof(value), "%ld", gps->satellitesCount());
      renderValueRow(display, 1, "Satellites", value);
      snprintf(value, sizeof(value), "%.4f", gps->getLatitude() / 1000000.0);
      renderValueRow(display, 2, "Latitude", value);
      snprintf(value, sizeof(value), "%.4f", gps->getLongitude() / 1000000.0);
      renderValueRow(display, 3, "Longitude", value);
      snprintf(value, sizeof(value), "%ldm", gps->getAltitude());
      renderValueRow(display, 4, "Altitude", value);
    } else {
      renderValueRow(display, 0, "Fix", "WAIT");
      renderValueRow(display, 1, "GPS power", onOffLabel(isGpsEnabled()));
#if FEATURE_GPS_TRACKER
      renderValueRow(display, 2, "Tracker", onOffLabel(_node_prefs && _node_prefs->gps_tracker_active));
#else
      renderValueRow(display, 2, "Tracker", "N/A");
#endif
      snprintf(value, sizeof(value), "%s", (_sensors && _sensors->getLocationProvider() &&
                                            _sensors->getLocationProvider()->waitingTimeSync()) ? "PEND" : "READY");
      renderValueRow(display, 3, "Time sync", value);
#if FEATURE_GPS_TRACKER
      if (the_mesh.isTrackerGpsEcoSleeping()) {
        snprintf(value, sizeof(value), "%lus", (unsigned long)the_mesh.getTrackerGpsEcoWakeSeconds());
        renderValueRow(display, 4, "GPS wake", value);
      } else {
        snprintf(value, sizeof(value), "%lus", (unsigned long)(_node_prefs ? _node_prefs->gps_tracker_interval : 0UL));
        renderValueRow(display, 4, "Interval", value);
      }
#else
      snprintf(value, sizeof(value), "%lus", (unsigned long)(_node_prefs ? _node_prefs->gps_interval : 0UL));
      renderValueRow(display, 4, "Interval", value);
#endif
    }
  } else {
    renderValueRow(display, 0, "GPS", "provider miss");
    renderValueRow(display, 1, "Hint", "check setup");
  }
}

void UITask::renderMeshMenu(DisplayDriver& display) {
  renderHeader(display, "MESH");

  renderMenuRow(display, 0, _mesh_menu_row, kMeshMenuLabels[0], "send");
  renderMenuRow(display, 1, _mesh_menu_row, kMeshMenuLabels[1], "flood");

  char value[20];
  snprintf(value, sizeof(value), "%d", _msgcount);
  renderMenuRow(display, 2, _mesh_menu_row, kMeshMenuLabels[2], value);

}

void UITask::renderSettingsMenu(DisplayDriver& display) {
  renderHeader(display, "SETTINGS");

  char value[24];
  snprintf(value, sizeof(value), "%s", onOffLabel(isSerialEnabled()));
  renderMenuRow(display, 0, _settings_menu_row, kSettingsMenuLabels[0], value);

#ifdef PIN_BUZZER
  snprintf(value, sizeof(value), "%s", getSoundModeLabel());
#else
  snprintf(value, sizeof(value), "N/A");
#endif
  renderMenuRow(display, 1, _settings_menu_row, kSettingsMenuLabels[1], value);

  snprintf(value, sizeof(value), "%s", (_node_prefs && _node_prefs->power_saving_mode) ? "ECO" : "FULL");
  renderMenuRow(display, 2, _settings_menu_row, kSettingsMenuLabels[2], value);

  snprintf(value, sizeof(value), "%s", displayTimeoutLabel(_node_prefs ? _node_prefs->display_timeout_s : 0));
  renderMenuRow(display, 3, _settings_menu_row, kSettingsMenuLabels[3], value);

  renderMenuRow(display, 4, _settings_menu_row, kSettingsMenuLabels[4], "hold");
}

void UITask::renderInfo(DisplayDriver& display) {
  renderHeader(display, "DEVICE");
  renderSectionDots(display, (int)InfoSection::Count, _info_section);

  char value[40];
  switch (_info_section) {
    case (int)InfoSection::Device:
      snprintf(value, sizeof(value), "%.18s", (_node_prefs && _node_prefs->node_name[0]) ? _node_prefs->node_name : "Client");
      renderSectionValueRow(display, 0, "Name", value);
      snprintf(value, sizeof(value), "%06lu", (unsigned long)the_mesh.getBLEPin());
      renderSectionValueRow(display, 1, "BLE PIN", value);
      snprintf(value, sizeof(value), "%umV", (unsigned)_board->getBootVoltage());
      renderSectionValueRow(display, 2, "Boot bat", value);
      formatUptime(value, sizeof(value), millis());
      renderSectionValueRow(display, 3, "Uptime", value);
      renderSectionValueRow(display, 4, "Power src", _board->isExternalPowered() ? "USB" : "BAT");
      break;

    case (int)InfoSection::Reset:
      renderSectionValueRow(display, 0, "Reset", resetReasonLabel(_board));
      renderSectionValueRow(display, 1, "Crash last", yesNoLabel(isCrashReset(_board)));
      formatLocalClock(value, sizeof(value), rtc_clock.getCurrentTime());
      renderSectionValueRow(display, 2, "Clock", value);
      renderSectionValueRow(display, 3, "Bluetooth", onOffLabel(isSerialEnabled()));
      renderSectionValueRow(display, 4, "App link", yesNoLabel(hasConnection()));
      break;

    default:
      renderSectionValueRow(display, 0, "Board", "WioTracker");
      renderSectionValueRow(display, 1, "UI", "stable menu");
      renderSectionValueRow(display, 2, "Display", "128x64 OLED");
      drawWrappedSnippet(display, 0, kSectionRowY[3], display.width(), FIRMWARE_VERSION, 2);
      break;
  }
}

void UITask::renderMessage(DisplayDriver& display) {
  renderHeader(display, "MESSAGE");

  char from[32];
  char body[96];
  copyDisplayText(display, from, sizeof(from), _msg_from);
  copyDisplayText(display, body, sizeof(body), _msg_text);

  display.drawTextEllipsized(0, 14, display.width(), from);
  drawWrappedSnippet(display, 0, 24, display.width(), body, 3);

  char line[32];
  snprintf(line, sizeof(line), "Unread %d  Back close", _msgcount);
  display.drawTextLeftAlign(0, 54, line);
}

void UITask::renderCurrentScreen(DisplayDriver& display) {
  if ((millis() - _ui_started_at) < kSplashMillis) {
    renderSplash(display);
    return;
  }

  if (_message_active) {
    renderMessage(display);
    return;
  }

  switch (_screen) {
    case Screen::Home:
      renderHome(display);
      break;
    case Screen::MainMenu:
      renderMainMenu(display);
      break;
    case Screen::Overview:
      renderOverview(display);
      break;
    case Screen::GPSMenu:
      renderGPSMenu(display);
      break;
    case Screen::GPSDetail:
      renderGPSDetail(display);
      break;
    case Screen::MeshMenu:
      renderMeshMenu(display);
      break;
    case Screen::SettingsMenu:
      renderSettingsMenu(display);
      break;
    case Screen::Info:
      renderInfo(display);
      break;
    default:
      renderHome(display);
      break;
  }
}

void UITask::toggleGpsPower() {
  if (!_node_prefs) return;
  _node_prefs->gps_enabled = _node_prefs->gps_enabled ? 0 : 1;
#if FEATURE_GPS_TRACKER
  if (!_node_prefs->gps_enabled) {
    _node_prefs->gps_tracker_active = 0;
  }
#endif
  the_mesh.applyGpsPrefs();
  the_mesh.savePrefs();
  setAlert(_node_prefs->gps_enabled ? "GPS enabled" : "GPS disabled", kAlertDefaultMillis);
}

void UITask::toggleTracker() {
#if FEATURE_GPS_TRACKER
  if (!_node_prefs) return;
  _node_prefs->gps_tracker_active = _node_prefs->gps_tracker_active ? 0 : 1;
  if (_node_prefs->gps_tracker_active && !_node_prefs->gps_enabled) {
    _node_prefs->gps_enabled = 1;
  }
  the_mesh.applyGpsPrefs();
  the_mesh.savePrefs();
  setAlert(_node_prefs->gps_tracker_active ? "Tracker enabled" : "Tracker disabled", kAlertDefaultMillis);
#else
  setAlert("Tracker not built", kAlertDefaultMillis);
#endif
}

void UITask::requestTimeSync() {
  if (_sensors && _sensors->getLocationProvider()) {
    _sensors->getLocationProvider()->syncTime();
    setAlert("Time sync requested", kAlertDefaultMillis);
  } else {
    setAlert("No GPS provider", kAlertDefaultMillis);
  }
}

void UITask::toggleBluetooth() {
  if (isSerialEnabled()) {
    disableSerial();
    setAlert("BLE disabled", kAlertDefaultMillis);
  } else {
    enableSerial();
    setAlert("BLE enabled", kAlertDefaultMillis);
  }
}

void UITask::togglePowerSave() {
  if (!_node_prefs) return;
  _node_prefs->power_saving_mode = _node_prefs->power_saving_mode ? 0 : 1;
  the_mesh.applyPowerPrefs(true);
  setAlert(_node_prefs->power_saving_mode ? "Power save on" : "Power save off", kAlertDefaultMillis);
}

void UITask::cycleDisplayTimeout() {
  if (!_node_prefs) return;
  _node_prefs->display_timeout_s = nextDisplayTimeout(_node_prefs->display_timeout_s);
  the_mesh.savePrefs();
  resetDisplayDeadline();
  setAlert(displayTimeoutLabel(_node_prefs->display_timeout_s), kAlertDefaultMillis);
}

void UITask::performPrimaryAction() {
  if (_message_active) {
    clearMessagePreview();
    return;
  }

  switch (_screen) {
    case Screen::Home:
      openMainMenu();
      break;

    case Screen::MainMenu:
      switch (_main_menu_row) {
        case (int)MainMenuItem::Overview:
          _screen = Screen::Overview;
          break;
        case (int)MainMenuItem::GPS:
          _screen = Screen::GPSMenu;
          break;
        case (int)MainMenuItem::Mesh:
          _screen = Screen::MeshMenu;
          break;
        case (int)MainMenuItem::Settings:
          _screen = Screen::SettingsMenu;
          break;
        case (int)MainMenuItem::Info:
          _screen = Screen::Info;
          break;
      }
      requestRefresh();
      break;

    case Screen::GPSMenu:
      switch (_gps_menu_row) {
        case (int)GPSMenuItem::Detail:
          _screen = Screen::GPSDetail;
          break;
        case (int)GPSMenuItem::Power:
          toggleGpsPower();
          break;
        case (int)GPSMenuItem::Tracker:
          toggleTracker();
          break;
        case (int)GPSMenuItem::TimeSync:
          requestTimeSync();
          break;
      }
      requestRefresh();
      break;

    case Screen::GPSDetail:
      _screen = Screen::GPSMenu;
      requestRefresh();
      break;

    case Screen::MeshMenu:
      switch (_mesh_menu_row) {
        case (int)MeshMenuItem::Advert:
          if (the_mesh.advert()) {
            setAlert("Advert sent", kAlertDefaultMillis);
          } else {
            setAlert("Advert failed", kAlertDefaultMillis);
          }
          break;
        case (int)MeshMenuItem::FloodAdvert:
          if (the_mesh.sendSelfAdvertFromUi(true)) {
            setAlert("Flood advert sent", kAlertDefaultMillis);
          } else {
            setAlert("Flood advert failed", kAlertDefaultMillis);
          }
          break;
        case (int)MeshMenuItem::ClearUnread:
          _msgcount = 0;
          clearMessagePreview();
          setAlert("Unread cleared", kAlertDefaultMillis);
          break;
      }
      break;

    case Screen::SettingsMenu:
      switch (_settings_menu_row) {
        case (int)SettingsMenuItem::Bluetooth:
          toggleBluetooth();
          break;
        case (int)SettingsMenuItem::Sound:
          cycleSoundMode();
          previewSoundMode();
          break;
        case (int)SettingsMenuItem::PowerSave:
          togglePowerSave();
          break;
        case (int)SettingsMenuItem::DisplayTimeout:
          cycleDisplayTimeout();
          break;
        case (int)SettingsMenuItem::Reboot:
          setAlert("Hold enter to reboot", 1500UL);
          break;
      }
      break;

    default:
      break;
  }
}

void UITask::performLongPressAction() {
  if (_message_active) {
    clearMessagePreview();
    return;
  }

  if (_screen == Screen::Home) {
    if (the_mesh.sendSelfAdvertFromUi(true)) {
      setAlert("Flood advert sent", kAlertDefaultMillis);
    } else {
      setAlert("Flood advert failed", kAlertDefaultMillis);
    }
    return;
  }

  if (_screen == Screen::SettingsMenu && _settings_menu_row == (int)SettingsMenuItem::Reboot) {
    shutdown(true);
  }
}

void UITask::handleClickLeft() {
  if (_message_active) {
    clearMessagePreview();
    return;
  }

  if (_screen == Screen::Overview) {
    _overview_section--;
    if (_overview_section < 0) {
      _overview_section = (int)OverviewSection::Count - 1;
    }
    requestRefresh();
    return;
  }

  if (_screen == Screen::Info) {
    _info_section--;
    if (_info_section < 0) {
      _info_section = (int)InfoSection::Count - 1;
    }
    requestRefresh();
    return;
  }

  if (_screen == Screen::Home) {
    openMainMenu();
    return;
  }

  goBack();
}

void UITask::handleClickRight() {
  if (_message_active) {
    clearMessagePreview();
    return;
  }

  if (_screen == Screen::Overview) {
    _overview_section++;
    if (_overview_section >= (int)OverviewSection::Count) {
      _overview_section = 0;
    }
    requestRefresh();
    return;
  }

  if (_screen == Screen::Info) {
    _info_section++;
    if (_info_section >= (int)InfoSection::Count) {
      _info_section = 0;
    }
    requestRefresh();
    return;
  }

  if (_screen == Screen::Home) {
    openMainMenu();
    return;
  }

  performPrimaryAction();
}

void UITask::handleClickUp() {
  if (_message_active) {
    clearMessagePreview();
    return;
  }

  switch (_screen) {
    case Screen::MainMenu:
      _main_menu_row = constrain(_main_menu_row - 1, 0, (int)MainMenuItem::Count - 1);
      break;
    case Screen::Overview:
      _overview_section--;
      if (_overview_section < 0) {
        _overview_section = (int)OverviewSection::Count - 1;
      }
      break;
    case Screen::GPSMenu:
      _gps_menu_row = constrain(_gps_menu_row - 1, 0, (int)GPSMenuItem::Count - 1);
      break;
    case Screen::MeshMenu:
      _mesh_menu_row = constrain(_mesh_menu_row - 1, 0, (int)MeshMenuItem::Count - 1);
      break;
    case Screen::SettingsMenu:
      _settings_menu_row = constrain(_settings_menu_row - 1, 0, (int)SettingsMenuItem::Count - 1);
      break;
    case Screen::Info:
      _info_section--;
      if (_info_section < 0) {
        _info_section = (int)InfoSection::Count - 1;
      }
      break;
    default:
      break;
  }
  requestRefresh();
}

void UITask::handleClickDown() {
  if (_message_active) {
    clearMessagePreview();
    return;
  }

  switch (_screen) {
    case Screen::MainMenu:
      _main_menu_row = constrain(_main_menu_row + 1, 0, (int)MainMenuItem::Count - 1);
      break;
    case Screen::Overview:
      _overview_section++;
      if (_overview_section >= (int)OverviewSection::Count) {
        _overview_section = 0;
      }
      break;
    case Screen::GPSMenu:
      _gps_menu_row = constrain(_gps_menu_row + 1, 0, (int)GPSMenuItem::Count - 1);
      break;
    case Screen::MeshMenu:
      _mesh_menu_row = constrain(_mesh_menu_row + 1, 0, (int)MeshMenuItem::Count - 1);
      break;
    case Screen::SettingsMenu:
      _settings_menu_row = constrain(_settings_menu_row + 1, 0, (int)SettingsMenuItem::Count - 1);
      break;
    case Screen::Info:
      _info_section++;
      if (_info_section >= (int)InfoSection::Count) {
        _info_section = 0;
      }
      break;
    default:
      break;
  }
  requestRefresh();
}

void UITask::handleClickEnter() {
  performPrimaryAction();
}

void UITask::handleClickBack() {
  goBack();
}

void UITask::handleLongPressEnter() {
  performLongPressAction();
}

void UITask::handleLongPressBack() {
  if (_message_active) {
    clearMessagePreview();
    return;
  }

  goHome();
}

void UITask::notify(UIEventType t) {
#ifdef PIN_BUZZER
  const uint8_t sound_mode = getSoundMode();
  if (sound_mode == 1) {
    return;
  }
  switch (t) {
    case UIEventType::contactMessage:
      _buzzer.play(sound_mode == 2 ? "MsgRcvL:d=4,o=6,b=220:16e,16g,16b,8c7,16b,8c7"
                                   : "msg:d=8,o=6,b=180:e,g,b");
      break;
    case UIEventType::channelMessage:
      _buzzer.play(sound_mode == 2 ? "ChanL:d=8,o=6,b=150:16g#,16c#,16g#,16c#"
                                   : "grp:d=16,o=6,b=160:c,e");
      break;
    case UIEventType::ack:
      _buzzer.play(sound_mode == 2 ? "ackL:d=16,o=8,b=160:c,e"
                                   : "ack:d=32,o=7,b=200:c");
      break;
    default:
      break;
  }
#else
  (void)t;
#endif
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (_msgcount == 0) {
    clearMessagePreview();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  if (path_len == 0xFF) {
    snprintf(_msg_from, sizeof(_msg_from), "Flood %s", from_name ? from_name : "?");
  } else {
    snprintf(_msg_from, sizeof(_msg_from), "%u hop %s", (unsigned)path_len, from_name ? from_name : "?");
  }
  strncpy(_msg_text, text ? text : "", sizeof(_msg_text) - 1);
  _msg_text[sizeof(_msg_text) - 1] = 0;
  _message_active = true;
  if (_display) {
    _display_soft_off = false;
    _display->turnOn();
  }
  resetDisplayDeadline();
  requestRefresh();
}

void UITask::shutdown(bool restart) {
#ifdef PIN_BUZZER
  _buzzer.shutdown();
  unsigned long deadline = millis() + 1200UL;
  while (_buzzer.isPlaying() && millis() < deadline) {
    _buzzer.loop();
  }
#endif

  if (restart) {
    _board->reboot();
  } else {
    radio_driver.powerOff();
    _board->powerOff();
  }
}

void UITask::loop() {
  if (_display && (_display_soft_off || !_display->isOn()) && anyButtonPressed()) {
    wakeDisplay(true);
  } else if (_display && !_display_soft_off && _display->isOn() && anyButtonPressed()) {
    resetDisplayDeadline();
  }

  if (_ignore_buttons_until_release) {
    if (!anyButtonPressed()) {
      _ignore_buttons_until_release = false;
    }
  } else {
#if defined(PIN_USER_BTN)
    int event = user_btn.check();
    if (event == BUTTON_EVENT_CLICK) handleClickEnter();
    else if (event == BUTTON_EVENT_LONG_PRESS) handleLongPressEnter();
#endif
#if UI_HAS_JOYSTICK
    int up = joystick_up.check();
    int down = joystick_down.check();
    int left = joystick_left.check();
    int right = joystick_right.check();
    int back = back_btn.check();

    if (left == BUTTON_EVENT_CLICK) handleClickLeft();
    if (right == BUTTON_EVENT_CLICK) handleClickRight();
    if (up == BUTTON_EVENT_CLICK) handleClickUp();
    if (down == BUTTON_EVENT_CLICK) handleClickDown();
    if (back == BUTTON_EVENT_CLICK || back == BUTTON_EVENT_DOUBLE_CLICK || back == BUTTON_EVENT_TRIPLE_CLICK) {
      handleClickBack();
    }
    if (back == BUTTON_EVENT_LONG_PRESS) handleLongPressBack();
#endif
  }

#ifdef PIN_BUZZER
  if (_buzzer.isPlaying()) {
    _buzzer.loop();
  }
#endif

  if (_display && !_display_soft_off && _display->isOn() && millis() >= _next_refresh) {
    _display->startFrame();
    renderCurrentScreen(*_display);
    if (_alert_until > millis() && _alert[0]) {
      _display->setColor(DisplayDriver::DARK);
      _display->fillRect(4, 24, _display->width() - 8, 16);
      _display->setColor(DisplayDriver::LIGHT);
      _display->drawRect(4, 24, _display->width() - 8, 16);
      _display->drawTextCentered(_display->width() / 2, 29, _alert);
    }
    _display->endFrame();
    unsigned long refresh_interval = getRefreshIntervalMillis();
    if (refresh_interval > 0) {
      requestRefresh(refresh_interval);
    } else {
      _next_refresh = 0xFFFFFFFFUL;
    }
  }

  if (_display && !_display_soft_off && _display->isOn() && _display_deadline > 0 && millis() >= _display_deadline) {
    _display->clear();
    _display_soft_off = true;
    _next_refresh = 0xFFFFFFFFUL;
  }

  if (_screen == Screen::Splash && (millis() - _ui_started_at) >= kSplashMillis) {
    _screen = Screen::Home;
    requestRefresh();
  }
}
