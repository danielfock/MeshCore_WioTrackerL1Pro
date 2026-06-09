#include "UITask.h"

#include <math.h>
#include <RTClib.h>

#include <helpers/AdvertDataHelpers.h>
#include <helpers/LocalTimeUtils.h>
#include <helpers/TxtDataHelpers.h>
#include <helpers/sensors/LPPDataHelpers.h>

#include "../MyMesh.h"
#include "target.h"

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS 15000
#endif

#define BOOT_SCREEN_DECODE_MILLIS 3200
#define BOOT_SCREEN_HOLD_MILLIS   2000
#define BOOT_SCREEN_MILLIS        (BOOT_SCREEN_DECODE_MILLIS + BOOT_SCREEN_HOLD_MILLIS)
#define LONG_PRESS_MILLIS         1200

#ifdef PIN_STATUS_LED
  #define LED_ON_MILLIS     20
  #define LED_ON_MSG_MILLIS 200
  #define LED_CYCLE_MILLIS  4000
#endif

namespace {

constexpr uint8_t kContactFavouriteBit = 0x01;
constexpr uint8_t kAutoAddOverwriteOldest = 1 << 0;
constexpr unsigned long kBatterySampleMillis = 2000UL;
constexpr int kBatteryTrendThresholdMv = 12;
constexpr uint16_t kBatteryGraphMinRangeMv = 30;
constexpr uint16_t kDisplayTimeoutChoicesSeconds[] = {0, 15, 30, 60, 120, 300};

const float kBandwidthChoices[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f};

const char* yesNo(bool value) {
  return value ? "ON" : "OFF";
}

const char* powerSaveLabel(bool value) {
  return value ? "ECO" : "FULL";
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

uint16_t cycleDisplayTimeout(uint16_t current_seconds, int delta) {
  int index = 0;
  for (int i = 0; i < (int)(sizeof(kDisplayTimeoutChoicesSeconds) / sizeof(kDisplayTimeoutChoicesSeconds[0])); ++i) {
    if (kDisplayTimeoutChoicesSeconds[i] == current_seconds) {
      index = i;
      break;
    }
  }
  index = constrain(index + (delta < 0 ? -1 : 1),
                    0,
                    (int)(sizeof(kDisplayTimeoutChoicesSeconds) / sizeof(kDisplayTimeoutChoicesSeconds[0])) - 1);
  return kDisplayTimeoutChoicesSeconds[index];
}

void copyDisplayText(DisplayDriver& display, char* dest, size_t dest_size, const char* src) {
  if (!src) {
    dest[0] = 0;
    return;
  }
  display.translateUTF8ToBlocks(dest, src, dest_size);
}

void formatDuration(char* buf, size_t buf_size, unsigned long total_ms) {
  unsigned long total_secs = total_ms / 1000UL;
  unsigned long hours = total_secs / 3600UL;
  unsigned long mins = (total_secs / 60UL) % 60UL;
  unsigned long secs = total_secs % 60UL;
  snprintf(buf, buf_size, "%02lu:%02lu:%02lu", hours, mins, secs);
}

void formatAge(char* buf, size_t buf_size, uint32_t seconds) {
  if (seconds < 60) {
    snprintf(buf, buf_size, "%lus", (unsigned long)seconds);
  } else if (seconds < 3600) {
    snprintf(buf, buf_size, "%lum", (unsigned long)(seconds / 60));
  } else if (seconds < 86400) {
    snprintf(buf, buf_size, "%luh", (unsigned long)(seconds / 3600));
  } else {
    snprintf(buf, buf_size, "%lud", (unsigned long)(seconds / 86400));
  }
}

const char* contactTypeLabel(uint8_t type) {
  switch (type) {
    case ADV_TYPE_CHAT:
      return "chat";
    case ADV_TYPE_REPEATER:
      return "repeater";
    case ADV_TYPE_ROOM:
      return "room";
    case ADV_TYPE_SENSOR:
      return "sensor";
    default:
      return "unknown";
  }
}

char bootCipherGlyphFor(unsigned long tick, int idx) {
  static const char kGlyphs[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789#@$%&*?!";
  return kGlyphs[(tick + idx * 11UL) % (sizeof(kGlyphs) - 1)];
}

void buildBootCipherText(const char* source, char* dest, size_t dest_size, unsigned long tick, int reveal_count) {
  if (!dest || dest_size == 0) {
    return;
  }

  size_t src_len = source ? strlen(source) : 0;
  if (src_len >= dest_size) {
    src_len = dest_size - 1;
  }

  for (size_t i = 0; i < src_len; ++i) {
    char ch = source[i];
    if ((int)i < reveal_count || ch == ' ') {
      dest[i] = ch;
    } else {
      dest[i] = bootCipherGlyphFor(tick, (int)i + ch);
    }
  }
  dest[src_len] = 0;
}

DisplayDriver::Color batteryColorForPercent(int battery_percent) {
  if (battery_percent <= 15) return DisplayDriver::RED;
  if (battery_percent <= 35) return DisplayDriver::YELLOW;
  return DisplayDriver::GREEN;
}

const char* batteryTrendLabel(int delta_mv, bool external_power) {
  if (delta_mv >= kBatteryTrendThresholdMv) return external_power ? "Charge" : "Rising";
  if (delta_mv <= -kBatteryTrendThresholdMv) return external_power ? "Load" : "Drain";
  return external_power ? "USB" : "Stable";
}

const char* shortResetReasonLabel(mesh::MainBoard* board) {
  if (!board) return "-";
  const char* label = board->getResetReasonString(board->getResetReason());
  if (!label || !label[0]) return "-";
  if (strcmp(label, "Watchdog") == 0) return "WDT";
  if (strcmp(label, "CPU Lockup") == 0) return "LOCK";
  if (strcmp(label, "Soft Reset") == 0) return "SOFT";
  if (strcmp(label, "Reset Pin") == 0) return "RST";
  if (strcmp(label, "Wake from GPIO") == 0) return "GPIO";
  if (strcmp(label, "Wake from VBUS") == 0) return "VBUS";
  if (strcmp(label, "Debug Interface") == 0) return "DBG";
  if (strcmp(label, "Cold Boot") == 0) return "BOOT";
  return "OTHER";
}

bool isCrashRecoveryReset(mesh::MainBoard* board) {
  if (!board) return false;
  const char* label = board->getResetReasonString(board->getResetReason());
  return label && (strcmp(label, "Watchdog") == 0 || strcmp(label, "CPU Lockup") == 0);
}

void drawBatterySparkline(DisplayDriver& display, int x, int y, int w, int h, const uint16_t* samples, int sample_count,
                          int sample_head, int sample_capacity, uint16_t min_mv, uint16_t max_mv) {
  if (!samples || sample_count <= 1) {
    return;
  }

  display.drawRect(x, y, w, h);
  int inner_w = w - 2;
  int inner_h = h - 2;
  if (inner_w <= 1 || inner_h <= 1) {
    return;
  }

  uint16_t floor_mv = min_mv;
  uint16_t ceiling_mv = max_mv;
  if (ceiling_mv < floor_mv) {
    uint16_t temp = floor_mv;
    floor_mv = ceiling_mv;
    ceiling_mv = temp;
  }
  if ((ceiling_mv - floor_mv) < kBatteryGraphMinRangeMv) {
    uint16_t padding = (kBatteryGraphMinRangeMv - (ceiling_mv - floor_mv)) / 2;
    floor_mv = floor_mv > padding ? floor_mv - padding : 0;
    ceiling_mv = floor_mv + kBatteryGraphMinRangeMv;
  }

  int prev_x = -1;
  int prev_y = -1;
  for (int i = 0; i < sample_count; ++i) {
    int idx = sample_count == sample_capacity ? (sample_head + i) % sample_capacity : i;
    uint16_t sample_mv = samples[idx];
    int px = x + 1 + (i * (inner_w - 1)) / max(1, sample_count - 1);
    int py = y + 1 + (inner_h - 1) -
             (((int)(sample_mv - floor_mv)) * (inner_h - 1)) / max(1, (int)(ceiling_mv - floor_mv));
    py = constrain(py, y + 1, y + h - 2);

    display.fillRect(px, py, 1, 1);
    if (prev_x >= 0) {
      int bridge_top = min(prev_y, py);
      int bridge_bottom = max(prev_y, py);
      display.fillRect(px, bridge_top, 1, bridge_bottom - bridge_top + 1);
    }

    prev_x = px;
    prev_y = py;
  }

  int newest_x = x + 1 + ((sample_count - 1) * (inner_w - 1)) / max(1, sample_count - 1);
  display.fillRect(newest_x - 1, y + 1, 3, 1);
}

const char* epicMottoForIndex(int idx) {
  static const char* kEpicMottos[] = {
    "LoRa legend",
    "Satellite whisperer",
    "Hero of COM9",
    "Stealth potato online",
    "Mesh wizard awake",
    "Beacon of chaos",
    "Orbit gremlins obey",
    "Packet gladiator"
  };
  const int count = sizeof(kEpicMottos) / sizeof(kEpicMottos[0]);
  idx %= count;
  if (idx < 0) idx += count;
  return kEpicMottos[idx];
}

const char* epicSceneLabel(int idx) {
  static const char* kSceneLabels[] = {
    "Core",
    "Scan",
    "Mesh"
  };
  const int count = sizeof(kSceneLabels) / sizeof(kSceneLabels[0]);
  idx %= count;
  if (idx < 0) idx += count;
  return kSceneLabels[idx];
}

void drawEpicSceneDots(DisplayDriver& display, int active_scene) {
  const int scene_count = 3;
  const int spacing = 10;
  const int y = 14;
  int x = display.width() / 2 - ((scene_count - 1) * spacing) / 2;
  for (int i = 0; i < scene_count; ++i, x += spacing) {
    bool active = i == active_scene;
    display.setColor(active ? DisplayDriver::LIGHT : DisplayDriver::GREEN);
    if (active) {
      display.fillRect(x - 2, y - 1, 5, 3);
    } else {
      display.fillRect(x, y, 1, 1);
    }
  }
}

void drawEpicMeter(DisplayDriver& display, int x, int y, int width, int value, int max_value, const char* label) {
  if (max_value <= 0) {
    max_value = 1;
  }
  value = constrain(value, 0, max_value);
  int fill = (value * (width - 2)) / max_value;
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextLeftAlign(x, y - 8, label);
  display.drawRect(x, y, width, 5);
  if (fill > 0) {
    display.fillRect(x + 1, y + 1, fill, 3);
  }
}

} // namespace

void UITask::requestRefresh(unsigned long delay_millis) {
  _next_refresh = millis() + delay_millis;
}

int UITask::getBatteryPercent(uint16_t batteryMilliVolts) const {
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
  int batteryPercentage =
      ((batteryMilliVolts - BATT_MIN_MILLIVOLTS) * 100) / (BATT_MAX_MILLIVOLTS - BATT_MIN_MILLIVOLTS);
  if (batteryPercentage < 0) batteryPercentage = 0;
  if (batteryPercentage > 100) batteryPercentage = 100;
  return batteryPercentage;
}

void UITask::drawHeader(DisplayDriver& display, const char* title, const char* right) {
  char filtered[40];
  display.setTextSize(1);
  display.setColor(DisplayDriver::GREEN);
  copyDisplayText(display, filtered, sizeof(filtered), title);
  display.drawTextLeftAlign(0, 0, filtered);
  if (right && right[0] != 0) {
    copyDisplayText(display, filtered, sizeof(filtered), right);
    display.drawTextRightAlign(display.width() - 1, 0, filtered);
  }
  display.setColor(DisplayDriver::LIGHT);
  display.fillRect(0, 10, display.width(), 1);
}

void UITask::drawFooter(DisplayDriver& display, const char* hint) {
  (void)display;
  (void)hint;
}

uint8_t UITask::getSoundMode() const {
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
  buzzer.quiet(getSoundMode() == 1);
#endif
}

void UITask::cycleSoundMode(int delta) {
  const uint8_t modes[] = {1, 0, 2};
  int idx = 1;
  uint8_t current = getSoundMode();
  for (int i = 0; i < 3; ++i) {
    if (modes[i] == current) {
      idx = i;
      break;
    }
  }

  idx = (idx + delta + 3) % 3;
  _node_prefs->buzzer_quiet = modes[idx];
  applySoundMode();
  the_mesh.savePrefs();
  showAlert(getSoundModeLabel(), 800);
}

void UITask::previewSoundMode() {
  if (getSoundMode() == 1) {
    return;
  }
  notify(UIEventType::contactMessage);
}

bool UITask::readGpsFix(double& latitude, double& longitude, long& altitudeMeters, long& satellites) const {
  latitude = 0.0;
  longitude = 0.0;
  altitudeMeters = 0;
  satellites = 0;
  if (!_sensors) {
    return false;
  }

  auto* location = _sensors->getLocationProvider();
  if (!location || !location->isEnabled() || !location->isValid()) {
    return false;
  }

  latitude = static_cast<double>(location->getLatitude()) / 1000000.0;
  longitude = static_cast<double>(location->getLongitude()) / 1000000.0;
  altitudeMeters = location->getAltitude() / 1000L;
  satellites = location->satellitesCount();
  if (satellites < 0) {
    satellites = 0;
  }
  return true;
}

void UITask::drawHomePageDots(DisplayDriver& display) {
  const int page_count = (int)HomePage::Count;
  const int spacing = 8;
  const int y = 15;
  int x = display.width() / 2 - ((page_count - 1) * spacing) / 2;
  for (int i = 0; i < page_count; i++, x += spacing) {
    bool selected = i == (int)_home_page;
    display.setColor(DisplayDriver::LIGHT);
    if (selected) {
      display.fillRect(x - 2, y - 2, 5, 5);
    } else {
      display.fillRect(x, y, 1, 1);
    }
  }
}

void UITask::drawListItem(DisplayDriver& display, int row, bool selected, const char* label, const char* value) {
  int y = 14 + row * 11;
  char filtered_label[40];
  char filtered_value[24];

  if (selected) {
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, y - 1, display.width(), 10);
    display.setColor(DisplayDriver::DARK);
  } else {
    display.setColor(DisplayDriver::LIGHT);
  }

  copyDisplayText(display, filtered_label, sizeof(filtered_label), label);
  if (value && value[0] != 0) {
    copyDisplayText(display, filtered_value, sizeof(filtered_value), value);
    int value_width = display.getTextWidth(filtered_value);
    int max_label_width = display.width() - value_width - 6;
    display.drawTextEllipsized(2, y, max_label_width, filtered_label);
    display.drawTextRightAlign(display.width() - 1, y, filtered_value);
  } else {
    display.drawTextEllipsized(2, y, display.width() - 4, filtered_label);
  }
}

void UITask::renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
  int batteryPercentage = getBatteryPercent(batteryMilliVolts);
  int iconWidth = 18;
  int iconHeight = 8;
  int iconX = display.width() - iconWidth - 4;
  int iconY = 0;
  char pct[8];
  snprintf(pct, sizeof(pct), "%d%%", batteryPercentage);
  display.setColor(DisplayDriver::GREEN);
  display.drawTextRightAlign(iconX - 3, 0, pct);
  display.drawRect(iconX, iconY, iconWidth, iconHeight);
  display.fillRect(iconX + iconWidth, iconY + 2, 2, iconHeight - 4);
  int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
  display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);
}

void UITask::resetBatteryDiagnostics() {
  memset(_battery_samples, 0, sizeof(_battery_samples));
  _battery_sample_count = 0;
  _battery_sample_head = 0;
  _battery_min_sample_mv = 0;
  _battery_max_sample_mv = 0;
  _next_battery_sample = 0;
}

void UITask::recalculateBatteryDiagnosticsRange() {
  _battery_min_sample_mv = 0;
  _battery_max_sample_mv = 0;
  for (int i = 0; i < _battery_sample_count; ++i) {
    uint16_t sample_mv = _battery_samples[i];
    if (sample_mv == 0) {
      continue;
    }
    if (_battery_min_sample_mv == 0 || sample_mv < _battery_min_sample_mv) {
      _battery_min_sample_mv = sample_mv;
    }
    if (sample_mv > _battery_max_sample_mv) {
      _battery_max_sample_mv = sample_mv;
    }
  }
}

void UITask::sampleBatteryDiagnostics() {
  uint16_t battery_mv = getBattMilliVolts();
  if (battery_mv > 0) {
    _battery_samples[_battery_sample_head] = battery_mv;
    _battery_sample_head = (_battery_sample_head + 1) % kBatterySampleCount;
    if (_battery_sample_count < kBatterySampleCount) {
      _battery_sample_count++;
    }
    recalculateBatteryDiagnosticsRange();
  }
  _next_battery_sample = millis() + kBatterySampleMillis;
}

bool UITask::isContactVisible(const ContactInfo& contact, ContactTab tab) const {
  switch (tab) {
    case ContactTab::Favs:
      return (contact.flags & kContactFavouriteBit) != 0;
    case ContactTab::Contacts:
      return contact.type == ADV_TYPE_CHAT;
    case ContactTab::Repeaters:
      return contact.type == ADV_TYPE_REPEATER;
    case ContactTab::Rooms:
      return contact.type == ADV_TYPE_ROOM;
    case ContactTab::Sensors:
      return contact.type == ADV_TYPE_SENSOR;
    default:
      return false;
  }
}

int UITask::getVisibleContactCount(ContactTab tab) const {
  int count = 0;
  ContactInfo contact;
  for (int i = 0; i < the_mesh.getNumContacts(); i++) {
    if (the_mesh.getContactByIdx(i, contact) && isContactVisible(contact, tab)) {
      count++;
    }
  }
  return count;
}

bool UITask::getVisibleContactByOrdinal(ContactTab tab, int ordinal, ContactInfo& contact) const {
  int count = 0;
  ContactInfo curr;
  for (int i = 0; i < the_mesh.getNumContacts(); i++) {
    if (!the_mesh.getContactByIdx(i, curr) || !isContactVisible(curr, tab)) {
      continue;
    }
    if (count == ordinal) {
      contact = curr;
      return true;
    }
    count++;
  }
  return false;
}

int UITask::getVisibleRepeaterCount() const {
  return getVisibleContactCount(ContactTab::Repeaters);
}

bool UITask::getVisibleRepeaterByOrdinal(int ordinal, ContactInfo& contact) const {
  return getVisibleContactByOrdinal(ContactTab::Repeaters, ordinal, contact);
}

void UITask::selectContact(const ContactInfo& contact) {
  _selected_contact = contact;
  _selected_contact_valid = true;
  _contact_action = 0;
  _screen = Screen::ContactDetail;
  requestRefresh();
}

void UITask::selectChannel(int slot_idx, const ChannelDetails& channel) {
  _selected_channel_slot = slot_idx;
  _selected_channel = channel;
  _selected_channel_valid = true;
  _channel_action = 0;
  _screen = Screen::ChannelDetail;
  requestRefresh();
}

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  {
    unsigned long timeout_ms = getDisplayTimeoutMillis();
    _auto_off = timeout_ms > 0 ? millis() + timeout_ms : 0;
  }
  _ui_started_at = millis();
  _alert_expiry = 0;
  _next_batt_check = 0;
  resetBatteryDiagnostics();
  sampleBatteryDiagnostics();
  _next_sensor_refresh = 0;
  _screen = Screen::Splash;

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
  buzzer.begin();
  applySoundMode();
  if (getSoundMode() != 1) {
    buzzer.startup();
  }
#endif
#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  if (isCrashRecoveryReset(_board)) {
    showAlert("Recovered after crash", BOOT_SCREEN_MILLIS + 2000);
  }

  requestRefresh();
}

void UITask::showAlert(const char* text, int duration_millis) {
  StrHelper::strncpy(_alert, text, sizeof(_alert));
  _alert_expiry = millis() + duration_millis;
  requestRefresh();
}

void UITask::startEpicMode() {
  _screen = Screen::Epic;
  _epic_started_at = millis();
  _epic_scene = 0;
  _epic_quote_idx = (_epic_quote_idx + 1) % 8;
  notify(UIEventType::channelMessage);
  requestRefresh();
}

void UITask::openBatteryDiagnostics(Screen return_screen) {
  _battery_diag_return_screen = return_screen;
  resetBatteryDiagnostics();
  sampleBatteryDiagnostics();
  _screen = Screen::BatteryDiag;
  requestRefresh();
}

void UITask::rememberMessage(uint8_t path_len, const char* from_name, const char* text) {
  _message_head = (_message_head + 1) % kMessageHistory;
  MessageEntry* entry = &_messages[_message_head];
  StrHelper::strncpy(entry->origin, from_name, sizeof(entry->origin));
  StrHelper::strncpy(entry->body, text, sizeof(entry->body));
  entry->timestamp = rtc_clock.getCurrentTime();
  entry->path_len = path_len;
  if (_message_count < kMessageHistory) {
    _message_count++;
  }
  _message_cursor = 0;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
  uint8_t sound_mode = getSoundMode();
  if (sound_mode == 1) {
    // silent
  } else
  switch (t) {
    case UIEventType::contactMessage:
      buzzer.play(sound_mode == 2 ? "MsgRcvL:d=4,o=6,b=220:16e,16g,16b,8c7,16b,8c7"
                                  : "MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
      break;
    case UIEventType::channelMessage:
      buzzer.play(sound_mode == 2 ? "ChanL:d=8,o=6,b=150:16g#,16c#,16g#,16c#"
                                  : "kerplop:d=16,o=6,b=120:32g#,32c#");
      break;
    case UIEventType::ack:
      buzzer.play(sound_mode == 2 ? "ackL:d=16,o=8,b=160:c,e" : "ack:d=32,o=8,b=120:c");
      break;
    default:
      break;
  }
#endif
#ifdef PIN_VIBRATION
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}

void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;
  rememberMessage(path_len, from_name, text);
  _screen = Screen::Messages;
  if (_display && !_display->isOn() && !hasConnection()) {
    _display->turnOn();
  }
  {
    unsigned long timeout_ms = getDisplayTimeoutMillis();
    _auto_off = timeout_ms > 0 ? millis() + timeout_ms : 0;
  }
  requestRefresh();
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  static int state = 0;
  static int next_change = 0;
  static int last_increment = 0;
  int cur_time = millis();
  if (cur_time > next_change) {
    if (state == 0) {
      state = 1;
      last_increment = _msgcount > 0 ? LED_ON_MSG_MILLIS : LED_ON_MILLIS;
      next_change = cur_time + last_increment;
    } else {
      state = 0;
      next_change = cur_time + LED_CYCLE_MILLIS - last_increment;
    }
    digitalWrite(PIN_STATUS_LED, state == LED_STATE_ON);
  }
#endif
}

int UITask::renderSplash(DisplayDriver& display) {
  unsigned long elapsed = millis() - _ui_started_at;
  unsigned long decode_elapsed = elapsed > BOOT_SCREEN_DECODE_MILLIS ? BOOT_SCREEN_DECODE_MILLIS : elapsed;
  unsigned long tick = elapsed / 60UL;

  char source_name[40];
  char cipher_name[40];

  copyDisplayText(display, source_name, sizeof(source_name), _node_prefs->node_name);
  if (source_name[0] == 0) {
    strcpy(source_name, "CLIENT");
  }

  int name_len = (int)strlen(source_name);
  int reveal_count = (int)((decode_elapsed * (unsigned long)(name_len + 1)) / BOOT_SCREEN_DECODE_MILLIS) - 1;
  if (reveal_count < 0) reveal_count = 0;
  if (reveal_count > name_len) reveal_count = name_len;

  buildBootCipherText(source_name, cipher_name, sizeof(cipher_name), tick, reveal_count);

  display.setColor(DisplayDriver::LIGHT);
  display.setTextSize(2);
  int text_width = display.getTextWidth(cipher_name);
  int text_height = 16;
  if (text_width > display.width() - 8) {
    display.setTextSize(1);
    text_width = display.getTextWidth(cipher_name);
    text_height = 8;
  }

  int y = (display.height() - text_height) / 2;
  display.drawTextCentered(display.width() / 2, y, cipher_name);
  return 120;
}

int UITask::renderHome(DisplayDriver& display) {
  char title[16];
  char line1[40] = "";
  char line2[40] = "";
  ChannelDetails channel;

  drawHeader(display, _node_prefs->node_name, NULL);
  renderBatteryIndicator(display, getBattMilliVolts());

  switch (_home_page) {
    case HomePage::Status:
      strcpy(title, "Status");
      snprintf(line1, sizeof(line1), "BLE %s  GPS %s", isSerialEnabled() ? "ON" : "OFF", getGPSState() ? "ON" : "OFF");
      snprintf(line2, sizeof(line2), "Batt %d%%  %s", getBatteryPercent(getBattMilliVolts()),
               _node_prefs->power_saving_mode ? "Eco on" : "Full pwr");
      break;
    case HomePage::Messages:
      strcpy(title, "Messages");
      snprintf(line1, sizeof(line1), "Unread: %d", _msgcount);
      snprintf(line2, sizeof(line2), "Saved: %d", _message_count);
      break;
    case HomePage::Contacts:
      strcpy(title, "Contacts");
      snprintf(line1, sizeof(line1), "Known: %d", the_mesh.getNumContacts());
      snprintf(line2, sizeof(line2), "Favs: %d", getVisibleContactCount(ContactTab::Favs));
      break;
    case HomePage::Channels:
      strcpy(title, "Channels");
      snprintf(line1, sizeof(line1), "Active: %d", the_mesh.getNumConfiguredChannels());
      if (the_mesh.getConfiguredChannelByOrdinal(0, _selected_channel_slot, channel)) {
        snprintf(line2, sizeof(line2), "%s", channel.name);
      }
      break;
    case HomePage::Adverts:
      strcpy(title, "Adverts");
      snprintf(line1, sizeof(line1), "Share pos: %s", _node_prefs->advert_loc_policy == ADVERT_LOC_NONE ? "OFF" : "ON");
      snprintf(line2, sizeof(line2), "Recent: %d", the_mesh.getRecentlyHeard(NULL, 0));
      break;
    case HomePage::Radio:
      strcpy(title, "Radio");
      snprintf(line1, sizeof(line1), "%.3fMHz SF%d", _node_prefs->freq, _node_prefs->sf);
      snprintf(line2, sizeof(line2), "BW %.1f CR %d", _node_prefs->bw, _node_prefs->cr);
      break;
    case HomePage::Bluetooth:
      strcpy(title, "Bluetooth");
      snprintf(line1, sizeof(line1), "BLE: %s", yesNo(isSerialEnabled()));
      snprintf(line2, sizeof(line2), "Link: %s", hasConnection() ? "connected" : "idle");
      break;
    case HomePage::Sound:
      strcpy(title, "Sound");
      snprintf(line1, sizeof(line1), "Profile: %s", getSoundModeLabel());
      snprintf(line2, sizeof(line2), "Alerts ready");
      break;
    case HomePage::GPS:
      strcpy(title, "GPS");
      snprintf(line1, sizeof(line1), "GPS: %s", getGPSState() ? "ON" : "OFF");
#if FEATURE_GPS_TRACKER
      snprintf(line2, sizeof(line2), "Tracker: %s", _node_prefs->gps_tracker_active ? "ON" : "OFF");
#else
      snprintf(line2, sizeof(line2), "Fix: %.4f", _sensors ? _sensors->node_lat : 0.0);
#endif
      break;
    case HomePage::Time:
      strcpy(title, "Time");
      {
        uint32_t now_utc = rtc_clock.getCurrentTime();
        DateTime now = mesh::localtime::europeViennaDateTime(now_utc);
        snprintf(line1, sizeof(line1), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
        snprintf(line2, sizeof(line2), "%02d.%02d.%04d %s", now.day(), now.month(), now.year(),
                 mesh::localtime::europeViennaZoneLabel(now_utc));
      }
      break;
    case HomePage::System:
      strcpy(title, "System");
      snprintf(line1, sizeof(line1), "Auto add: %s", (_node_prefs->manual_add_contacts & 1) == 0 ? "ON" : "MANUAL");
      snprintf(line2, sizeof(line2), "Hops: %u", (unsigned)_node_prefs->autoadd_max_hops);
      break;
    case HomePage::Tools:
      strcpy(title, "Tools");
      strcpy(line1, "Repeaters, timer,");
      strcpy(line2, "sensors, battery");
      break;
    default:
      strcpy(title, "MeshCore");
      break;
  }

  display.setTextSize(2);
  display.setColor(DisplayDriver::YELLOW);
  display.drawTextCentered(display.width() / 2, 24, title);
  display.setTextSize(1);
  display.setColor(DisplayDriver::LIGHT);
  drawHomePageDots(display);
  if (line1[0]) display.drawTextCentered(display.width() / 2, 42, line1);
  if (line2[0]) display.drawTextCentered(display.width() / 2, 53, line2);
  return 1000;
}

int UITask::renderStatus(DisplayDriver& display) {
  drawHeader(display, "Status", NULL);
  renderBatteryIndicator(display, getBattMilliVolts());

  char line[32];
  snprintf(line, sizeof(line), "%d%%  %umV", getBatteryPercent(getBattMilliVolts()), (unsigned)getBattMilliVolts());
  display.drawTextLeftAlign(0, 14, line);

  snprintf(line, sizeof(line), "BLE:%s Eco:%s", isSerialEnabled() ? "ON" : "OFF",
           _node_prefs->power_saving_mode ? "ON" : "OFF");
  display.drawTextLeftAlign(0, 25, line);

  snprintf(line, sizeof(line), "Link:%s GPS:%s", hasConnection() ? "ON" : "OFF", getGPSState() ? "ON" : "OFF");
  display.drawTextLeftAlign(0, 36, line);

  snprintf(line, sizeof(line), "Reset:%s Msg:%d", shortResetReasonLabel(_board), _msgcount);
  display.drawTextLeftAlign(0, 47, line);

  drawFooter(display, "Back home");
  return 1000;
}

int UITask::renderBatteryDiag(DisplayDriver& display) {
  bool external_power = _board->isExternalPowered();
  unsigned long window_secs =
      _battery_sample_count > 1 ? ((_battery_sample_count - 1) * kBatterySampleMillis) / 1000UL : 0UL;
  char right[16];
  snprintf(right, sizeof(right), "%s %lus", external_power ? "USB" : "BAT", window_secs);
  drawHeader(display, "Battery", right);

  int newest_idx = _battery_sample_count > 0 ? (_battery_sample_head + kBatterySampleCount - 1) % kBatterySampleCount : 0;
  int oldest_idx = _battery_sample_count == kBatterySampleCount ? _battery_sample_head : 0;
  uint16_t current_mv = _battery_sample_count > 0 ? _battery_samples[newest_idx] : getBattMilliVolts();
  uint16_t oldest_mv = _battery_sample_count > 0 ? _battery_samples[oldest_idx] : current_mv;
  uint16_t boot_mv = _board->getBootVoltage();
  uint16_t min_mv = _battery_min_sample_mv > 0 ? _battery_min_sample_mv : current_mv;
  uint16_t max_mv = _battery_max_sample_mv > 0 ? _battery_max_sample_mv : current_mv;
  int battery_pct = getBatteryPercent(current_mv);
  int trend_mv = (int)current_mv - (int)oldest_mv;
  uint32_t total_mv = 0;
  for (int i = 0; i < _battery_sample_count; ++i) {
    int idx = _battery_sample_count == kBatterySampleCount ? (_battery_sample_head + i) % kBatterySampleCount : i;
    total_mv += _battery_samples[idx];
  }
  uint16_t avg_mv = _battery_sample_count > 0 ? (uint16_t)(total_mv / _battery_sample_count) : current_mv;
  long rate_per_hour = window_secs > 0 ? ((long)trend_mv * 3600L) / (long)window_secs : 0L;

  char line[32];
  display.setColor(batteryColorForPercent(battery_pct));
  snprintf(line, sizeof(line), "%d%%  %umV", battery_pct, (unsigned)current_mv);
  display.drawTextLeftAlign(0, 14, line);

  display.setColor(DisplayDriver::LIGHT);
  snprintf(line, sizeof(line), "Avg:%u Boot:%u", (unsigned)avg_mv, (unsigned)(boot_mv > 0 ? boot_mv : current_mv));
  display.drawTextLeftAlign(0, 24, line);

  snprintf(line, sizeof(line), "Lo:%u Hi:%u", (unsigned)min_mv, (unsigned)max_mv);
  display.drawTextLeftAlign(0, 34, line);

  snprintf(line, sizeof(line), "%s %+ldmVh", batteryTrendLabel(trend_mv, external_power), rate_per_hour);
  display.drawTextLeftAlign(0, 44, line);

  display.setColor(DisplayDriver::GREEN);
  drawBatterySparkline(display, 0, 55, display.width(), 8, _battery_samples, _battery_sample_count, _battery_sample_head,
                       kBatterySampleCount, min_mv, max_mv);
  return 1000;
}

int UITask::renderGpsStatus(DisplayDriver& display) {
  drawHeader(display, "GPS status", NULL);
  char line[40];
  bool gps_power = getGPSState();
  bool eco_sleep = _node_prefs->gps_tracker_active && _node_prefs->power_saving_mode && the_mesh.isTrackerGpsEcoSleeping();
  double latitude = 0.0;
  double longitude = 0.0;
  long altitude_m = 0;
  long satellites = 0;
  bool has_fix = readGpsFix(latitude, longitude, altitude_m, satellites);

  snprintf(line, sizeof(line), "Power:%s Fix:%s", eco_sleep ? "ECO" : (gps_power ? "ON" : "OFF"),
           has_fix ? "YES" : (eco_sleep ? "PAUSE" : "NO"));
  display.drawTextLeftAlign(0, 14, line);

  if (has_fix) {
    snprintf(line, sizeof(line), "Sats:%ld Alt:%ldm", satellites, altitude_m);
    display.drawTextLeftAlign(0, 25, line);

    snprintf(line, sizeof(line), "Lat %.4f", latitude);
    display.drawTextLeftAlign(0, 36, line);

    snprintf(line, sizeof(line), "Lon %.4f", longitude);
    display.drawTextLeftAlign(0, 47, line);
  } else if (_sensors && (_sensors->node_lat != 0.0 || _sensors->node_lon != 0.0)) {
    snprintf(line, sizeof(line), "Last %.4f", _sensors->node_lat);
    display.drawTextLeftAlign(0, 25, line);

    snprintf(line, sizeof(line), "Lon %.4f", _sensors->node_lon);
    display.drawTextLeftAlign(0, 36, line);

    snprintf(line, sizeof(line), "Tracker:%s", _node_prefs->gps_tracker_active ? "ON" : "OFF");
    display.drawTextLeftAlign(0, 47, line);
  } else if (eco_sleep) {
    snprintf(line, sizeof(line), "Wake in:%lus", (unsigned long)the_mesh.getTrackerGpsEcoWakeSeconds());
    display.drawTextLeftAlign(0, 25, line);

    snprintf(line, sizeof(line), "Int:%lus Share:%s", (unsigned long)_node_prefs->gps_tracker_interval,
             _node_prefs->gps_tracker_position_sharing ? "ON" : "OFF");
    display.drawTextLeftAlign(0, 36, line);

    display.drawTextLeftAlign(0, 47, "Sleeping for battery");
  } else {
    display.drawTextLeftAlign(0, 25, "Waiting for valid fix");
    snprintf(line, sizeof(line), "Tracker:%s Share:%s", _node_prefs->gps_tracker_active ? "ON" : "OFF",
             _node_prefs->gps_tracker_position_sharing ? "ON" : "OFF");
    display.drawTextLeftAlign(0, 36, line);

    snprintf(line, sizeof(line), "Int:%lus Hop:%u", (unsigned long)_node_prefs->gps_tracker_interval,
             (unsigned)_node_prefs->gps_tracker_hop_limit);
    display.drawTextLeftAlign(0, 47, line);
  }

  return 1000;
}

int UITask::renderMessages(DisplayDriver& display) {
  char right[16];
  snprintf(right, sizeof(right), "%d/%d", _message_count == 0 ? 0 : _message_cursor + 1, _message_count);
  drawHeader(display, "Messages", right);

  if (_message_count == 0) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 24, "No local messages");
    display.drawTextCentered(display.width() / 2, 36, "yet");
    drawFooter(display, "Back home");
    return 1000;
  }

  int idx = (_message_head - _message_cursor + kMessageHistory) % kMessageHistory;
  MessageEntry* entry = &_messages[idx];
  char filtered[100];
  char age[16];
  uint32_t now = rtc_clock.getCurrentTime();
  formatAge(age, sizeof(age), now >= entry->timestamp ? now - entry->timestamp : 0);

  display.setColor(DisplayDriver::YELLOW);
  copyDisplayText(display, filtered, sizeof(filtered), entry->origin);
  display.drawTextEllipsized(0, 14, display.width() - 20, filtered);
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextRightAlign(display.width() - 1, 14, age);

  char meta[24];
  snprintf(meta, sizeof(meta), "path: %s", entry->path_len == 0xFF ? "direct" : "");
  if (entry->path_len != 0xFF) {
    snprintf(meta, sizeof(meta), "hops: %u", (unsigned)entry->path_len);
  }
  display.drawTextLeftAlign(0, 24, meta);

  copyDisplayText(display, filtered, sizeof(filtered), entry->body);
  display.drawTextLeftAlign(0, 36, "");
  display.setCursor(0, 36);
  display.printWordWrap(filtered, display.width());
  drawFooter(display, "U/D browse  Enter read");
  return 1000;
}

int UITask::renderContacts(DisplayDriver& display) {
  const char* tab_name = "Contacts";
  switch (_contact_tab) {
    case ContactTab::Favs: tab_name = "Favs"; break;
    case ContactTab::Contacts: tab_name = "Contacts"; break;
    case ContactTab::Repeaters: tab_name = "Repeaters"; break;
    case ContactTab::Rooms: tab_name = "Rooms"; break;
    case ContactTab::Sensors: tab_name = "Sensors"; break;
    default: break;
  }

  int count = getVisibleContactCount(_contact_tab);
  char right[16];
  snprintf(right, sizeof(right), "%d", count);
  drawHeader(display, tab_name, right);

  if (count == 0) {
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 24, "No entries");
    drawFooter(display, "L/R tabs  Back home");
    return 1000;
  }

  if (_contact_cursor >= count) _contact_cursor = count - 1;
  if (_contact_cursor < 0) _contact_cursor = 0;
  int start = _contact_cursor > 1 ? _contact_cursor - 1 : 0;
  if (start > count - 4) start = max(0, count - 4);

  for (int row = 0; row < 4; row++) {
    int ordinal = start + row;
    if (ordinal >= count) break;
    ContactInfo contact;
    if (!getVisibleContactByOrdinal(_contact_tab, ordinal, contact)) continue;
    char value[12];
    snprintf(value, sizeof(value), "%s", (contact.flags & kContactFavouriteBit) ? "*" : contactTypeLabel(contact.type));
    drawListItem(display, row, ordinal == _contact_cursor, contact.name, value);
  }
  drawFooter(display, "U/D pick  Enter open");
  return 1000;
}

int UITask::renderContactDetail(DisplayDriver& display) {
  drawHeader(display, _selected_contact.name, contactTypeLabel(_selected_contact.type));
  char line[48];
  char age[16];
  uint32_t now = rtc_clock.getCurrentTime();
  formatAge(age, sizeof(age), now >= _selected_contact.lastmod ? now - _selected_contact.lastmod : 0);

  snprintf(line, sizeof(line), "fav: %s  path: %s",
           (_selected_contact.flags & kContactFavouriteBit) ? "yes" : "no",
           _selected_contact.out_path_len == OUT_PATH_UNKNOWN ? "flood" : "direct");
  display.drawTextLeftAlign(0, 14, line);
  snprintf(line, sizeof(line), "last: %s", age);
  display.drawTextLeftAlign(0, 25, line);
  if (_selected_contact.gps_lat != 0 || _selected_contact.gps_lon != 0) {
    snprintf(line, sizeof(line), "%.4f %.4f", _selected_contact.gps_lat / 1000000.0, _selected_contact.gps_lon / 1000000.0);
  } else {
    strcpy(line, "no GPS in advert");
  }
  display.drawTextLeftAlign(0, 36, line);

  const char* actions[] = {"Fav", "Reset", "Delete"};
  int x = 0;
  for (int i = 0; i < 3; i++, x += 42) {
    bool selected = i == _contact_action;
    display.setColor(selected ? DisplayDriver::LIGHT : DisplayDriver::GREEN);
    display.drawRect(x + 1, 48, 38, 12);
    display.setColor(selected ? DisplayDriver::LIGHT : DisplayDriver::LIGHT);
    display.drawTextCentered(x + 20, 51, actions[i]);
  }
  drawFooter(display, "L/R action  Enter run");
  return 1000;
}

int UITask::renderChannels(DisplayDriver& display) {
  int count = the_mesh.getNumConfiguredChannels();
  char right[16];
  snprintf(right, sizeof(right), "%d", count);
  drawHeader(display, "Channels", right);

  if (count == 0) {
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 24, "No channels");
    drawFooter(display, "Back home");
    return 1000;
  }

  if (_channel_cursor >= count) _channel_cursor = count - 1;
  if (_channel_cursor < 0) _channel_cursor = 0;
  int start = _channel_cursor > 1 ? _channel_cursor - 1 : 0;
  if (start > count - 4) start = max(0, count - 4);

  for (int row = 0; row < 4; row++) {
    int ordinal = start + row;
    if (ordinal >= count) break;
    int slot_idx;
    ChannelDetails channel;
    if (!the_mesh.getConfiguredChannelByOrdinal(ordinal, slot_idx, channel)) continue;
    char value[8];
    snprintf(value, sizeof(value), "#%d", slot_idx + 1);
    drawListItem(display, row, ordinal == _channel_cursor, channel.name, value);
  }
  drawFooter(display, "U/D pick  Enter open");
  return 1000;
}

int UITask::renderChannelDetail(DisplayDriver& display) {
  char right[12];
  snprintf(right, sizeof(right), "slot %d", _selected_channel_slot + 1);
  drawHeader(display, _selected_channel.name, right);
  display.drawTextLeftAlign(0, 14, "Channel ready");
  char hash_str[16];
  snprintf(hash_str, sizeof(hash_str), "hash: %02X", _selected_channel.channel.hash[0]);
  display.drawTextLeftAlign(0, 25, hash_str);
  display.drawTextLeftAlign(0, 36, "Action: remove");

  bool selected = _channel_action == 0;
  display.setColor(selected ? DisplayDriver::LIGHT : DisplayDriver::GREEN);
  display.drawRect(24, 48, 80, 12);
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(display.width() / 2, 51, "Remove channel");
  drawFooter(display, "Enter remove");
  return 1000;
}

int UITask::renderAdverts(DisplayDriver& display) {
  drawHeader(display, "Adverts", NULL);
  const char* labels[] = {"Send 0-hop", "Send flood", "Share position"};
  const char* values[] = {"", "", _node_prefs->advert_loc_policy == ADVERT_LOC_NONE ? "OFF" : "ON"};
  for (int i = 0; i < 3; i++) {
    drawListItem(display, i, i == _advert_row, labels[i], values[i]);
  }
  drawFooter(display, "U/D pick  Enter act");
  return 1000;
}

int UITask::renderRadio(DisplayDriver& display) {
  drawHeader(display, "Radio", NULL);
  const int rows = 6;
  if (_radio_row < 0) _radio_row = 0;
  if (_radio_row >= rows) _radio_row = rows - 1;
  int start = _radio_row > 1 ? _radio_row - 1 : 0;
  if (start > rows - 4) start = rows - 4;

  for (int row = 0; row < 4; row++) {
    int idx = start + row;
    char value[20];
    const char* label = "";
    switch (idx) {
      case 0:
        label = "Frequency";
        snprintf(value, sizeof(value), "%.3f", _node_prefs->freq);
        break;
      case 1:
        label = "Bandwidth";
        snprintf(value, sizeof(value), "%.1f", _node_prefs->bw);
        break;
      case 2:
        label = "Spread";
        snprintf(value, sizeof(value), "SF%u", (unsigned)_node_prefs->sf);
        break;
      case 3:
        label = "Coding";
        snprintf(value, sizeof(value), "CR%u", (unsigned)_node_prefs->cr);
        break;
      case 4:
        label = "TX power";
        snprintf(value, sizeof(value), "%ddBm", (int)_node_prefs->tx_power_dbm);
        break;
      case 5:
        label = "Repeat";
        snprintf(value, sizeof(value), "%s", yesNo(_node_prefs->client_repeat != 0));
        break;
    }
    drawListItem(display, row, idx == _radio_row, label, value);
  }
  drawFooter(display, "L/R edit  U/D row");
  return 1000;
}

int UITask::renderBluetooth(DisplayDriver& display) {
  drawHeader(display, "Bluetooth", NULL);
  char pin[16];
  snprintf(pin, sizeof(pin), "%06lu", (unsigned long)the_mesh.getBLEPin());
  const char* labels[] = {"BLE serial", "Mode", "Link", "PIN"};
  const char* values[] = {isSerialEnabled() ? "ON" : "OFF", powerSaveLabel(_node_prefs->power_saving_mode != 0),
                          hasConnection() ? "linked" : "idle", pin};
  for (int i = 0; i < 4; i++) {
    drawListItem(display, i, i == _bluetooth_row, labels[i], values[i]);
  }
  drawFooter(display, "Enter toggle");
  return 1000;
}

int UITask::renderSound(DisplayDriver& display) {
  drawHeader(display, "Sound", NULL);
  drawListItem(display, 0, true, "Profile", getSoundModeLabel());
  display.drawTextLeftAlign(0, 28, "Alerts and ACK tones");
  display.drawTextLeftAlign(0, 40, "Silent, normal, loud");
  drawFooter(display, "L/R or Enter toggle");
  return 1000;
}

int UITask::renderGps(DisplayDriver& display) {
  drawHeader(display, "GPS", NULL);
  double latitude = 0.0;
  double longitude = 0.0;
  long altitude_m = 0;
  long satellites = 0;
  bool has_fix = readGpsFix(latitude, longitude, altitude_m, satellites);
#if FEATURE_GPS_TRACKER
  const int rows = 6;
#else
  const int rows = 3;
#endif
  if (_gps_row < 0) _gps_row = 0;
  if (_gps_row >= rows) _gps_row = rows - 1;
  int start = _gps_row > 1 ? _gps_row - 1 : 0;
  if (start > rows - 4) start = rows - 4;
  if (start < 0) start = 0;

  for (int row = 0; row < 4; row++) {
    int idx = start + row;
    if (idx >= rows) break;
    char value[20];
    const char* label = "";
    switch (idx) {
      case 0:
        label = "Status";
        snprintf(value, sizeof(value), "%s", has_fix ? "FIX" : "VIEW");
        break;
      case 1:
        label = "GPS power";
        snprintf(value, sizeof(value), "%s", getGPSState() ? "ON" : "OFF");
        break;
#if FEATURE_GPS_TRACKER
      case 2:
        label = "Tracker";
        snprintf(value, sizeof(value), "%s", yesNo(_node_prefs->gps_tracker_active != 0));
        break;
      case 3:
        label = "Share pos";
        snprintf(value, sizeof(value), "%s", yesNo(_node_prefs->gps_tracker_position_sharing != 0));
        break;
      case 4:
        label = "Interval";
        snprintf(value, sizeof(value), "%lus", (unsigned long)_node_prefs->gps_tracker_interval);
        break;
      case 5:
        label = "Hop limit";
        snprintf(value, sizeof(value), "%u", (unsigned)_node_prefs->gps_tracker_hop_limit);
        break;
#else
      case 2:
        label = "Interval";
        snprintf(value, sizeof(value), "%lus", (unsigned long)_node_prefs->gps_interval);
        break;
#endif
    }
    drawListItem(display, row, idx == _gps_row, label, value);
  }
  drawFooter(display, "L/R edit  U/D row");
  return 1000;
}

int UITask::renderTime(DisplayDriver& display) {
  drawHeader(display, "Time", NULL);
  uint32_t now_utc = rtc_clock.getCurrentTime();
  DateTime now = mesh::localtime::europeViennaDateTime(now_utc);
  char line[32];
  display.setTextSize(2);
  snprintf(line, sizeof(line), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  display.setColor(DisplayDriver::YELLOW);
  display.drawTextCentered(display.width() / 2, 16, line);
  display.setTextSize(1);
  display.setColor(DisplayDriver::LIGHT);
  snprintf(line, sizeof(line), "%02d.%02d.%04d %s", now.day(), now.month(), now.year(),
           mesh::localtime::europeViennaZoneLabel(now_utc));
  display.drawTextCentered(display.width() / 2, 40, line);
  formatDuration(line, sizeof(line), millis());
  display.drawTextCentered(display.width() / 2, 50, line);
  drawFooter(display, "Back home");
  return 1000;
}

int UITask::renderSystem(DisplayDriver& display) {
  drawHeader(display, "System", NULL);
  const int rows = 7;
  if (_system_row < 0) _system_row = 0;
  if (_system_row >= rows) _system_row = rows - 1;
  int start = _system_row > 1 ? _system_row - 1 : 0;
  if (start > rows - 4) start = rows - 4;

  for (int row = 0; row < 4; row++) {
    int idx = start + row;
    char value[20];
    const char* label = "";
    switch (idx) {
      case 0:
        label = "Power save";
        snprintf(value, sizeof(value), "%s", powerSaveLabel(_node_prefs->power_saving_mode != 0));
        break;
      case 1:
        label = "Disp timeout";
        snprintf(value, sizeof(value), "%s", displayTimeoutLabel(_node_prefs->display_timeout_s));
        break;
      case 2:
        label = "Auto add";
        snprintf(value, sizeof(value), "%s", (_node_prefs->manual_add_contacts & 1) == 0 ? "AUTO" : "MANUAL");
        break;
      case 3:
        label = "Overwrite";
        snprintf(value, sizeof(value), "%s", yesNo((_node_prefs->autoadd_config & kAutoAddOverwriteOldest) != 0));
        break;
      case 4:
        label = "Max hops";
        snprintf(value, sizeof(value), "%u", (unsigned)_node_prefs->autoadd_max_hops);
        break;
      case 5:
        label = "Path hash";
        snprintf(value, sizeof(value), "%u", (unsigned)_node_prefs->path_hash_mode);
        break;
      case 6:
        label = "Airtime";
        snprintf(value, sizeof(value), "%.1f", _node_prefs->airtime_factor);
        break;
    }
    drawListItem(display, row, idx == _system_row, label, value);
  }
  drawFooter(display, "L/R edit  U/D row");
  return 1000;
}

int UITask::renderTools(DisplayDriver& display) {
  drawHeader(display, "Tools", NULL);
  const char* labels[] = {"Discover repeaters", "Stopwatch", "Countdown", "Sensors", "Battery diag", "Epic mode"};
  const int item_count = sizeof(labels) / sizeof(labels[0]);
  int start = _tools_row > 1 ? _tools_row - 1 : 0;
  if (start > item_count - 4) start = max(0, item_count - 4);
  for (int row = 0; row < 4; row++) {
    int idx = start + row;
    if (idx >= item_count) break;
    drawListItem(display, row, idx == _tools_row, labels[idx], NULL);
  }
  drawFooter(display, "U/D pick  Enter open");
  return 1000;
}

int UITask::renderRepeaters(DisplayDriver& display) {
  int count = getVisibleRepeaterCount();
  char right[16];
  snprintf(right, sizeof(right), "%d", count);
  drawHeader(display, "Repeaters", right);
  if (count == 0) {
    display.drawTextCentered(display.width() / 2, 24, "No repeaters");
    drawFooter(display, "Back tools");
    return 1000;
  }

  if (_repeater_cursor >= count) _repeater_cursor = count - 1;
  if (_repeater_cursor < 0) _repeater_cursor = 0;
  int start = _repeater_cursor > 1 ? _repeater_cursor - 1 : 0;
  if (start > count - 4) start = max(0, count - 4);

  for (int row = 0; row < 4; row++) {
    int ordinal = start + row;
    if (ordinal >= count) break;
    ContactInfo contact;
    if (!getVisibleRepeaterByOrdinal(ordinal, contact)) continue;
    char value[12];
    snprintf(value, sizeof(value), "%s", contact.out_path_len == OUT_PATH_UNKNOWN ? "flood" : "direct");
    drawListItem(display, row, ordinal == _repeater_cursor, contact.name, value);
  }
  drawFooter(display, "U/D pick  Enter open");
  return 1000;
}

int UITask::renderStopwatch(DisplayDriver& display) {
  drawHeader(display, "Stopwatch", NULL);
  unsigned long elapsed = _stopwatch_running ? _stopwatch_elapsed_ms + (millis() - _stopwatch_started_at) : _stopwatch_elapsed_ms;
  char buf[20];
  formatDuration(buf, sizeof(buf), elapsed);
  display.setTextSize(2);
  display.setColor(DisplayDriver::YELLOW);
  display.drawTextCentered(display.width() / 2, 18, buf);
  display.setTextSize(1);
  display.setColor(DisplayDriver::LIGHT);
  display.drawTextCentered(display.width() / 2, 44, _stopwatch_running ? "Running" : "Stopped");
  display.drawTextCentered(display.width() / 2, 54, " ");
  return 250;
}

int UITask::renderCountdown(DisplayDriver& display) {
  drawHeader(display, "Countdown", NULL);
  unsigned long remaining = _countdown_running ? (_countdown_target_at > millis() ? _countdown_target_at - millis() : 0) : _countdown_remaining_ms;
  char buf[20];
  formatDuration(buf, sizeof(buf), remaining);
  display.setTextSize(2);
  display.setColor(DisplayDriver::YELLOW);
  display.drawTextCentered(display.width() / 2, 18, buf);
  display.setTextSize(1);
  display.setColor(DisplayDriver::LIGHT);
  if (_countdown_running) {
    display.drawTextCentered(display.width() / 2, 44, "Running");
  } else {
    display.drawTextCentered(display.width() / 2, 44, "Ready");
    display.drawTextCentered(display.width() / 2, 54, " ");
  }
  return 250;
}

void UITask::refreshSensorLines() {
  if (!_sensors) {
    _sensor_line_count = 0;
    return;
  }
  if (millis() < _next_sensor_refresh && _sensor_line_count > 0) {
    return;
  }

  _sensor_lpp.reset();
  _sensor_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)_board->getBattMilliVolts() / 1000.0f);
  _sensors->querySensors(0xFF, _sensor_lpp);

  _sensor_line_count = 0;
  LPPReader reader(_sensor_lpp.getBuffer(), _sensor_lpp.getSize());
  while (_sensor_line_count < kSensorLineCount) {
    uint8_t channel, type;
    if (!reader.readHeader(channel, type)) {
      break;
    }

    char name[16];
    char value[16];
    float v = 0.0f;
    name[0] = 0;
    value[0] = 0;

    switch (type) {
      case LPP_GPS: {
        float lat, lon, alt;
        reader.readGPS(lat, lon, alt);
        strcpy(name, "gps");
        snprintf(value, sizeof(value), "%.3f", lat);
        break;
      }
      case LPP_VOLTAGE:
        reader.readVoltage(v);
        strcpy(name, "volt");
        snprintf(value, sizeof(value), "%.2f", v);
        break;
      case LPP_CURRENT:
        reader.readCurrent(v);
        strcpy(name, "curr");
        snprintf(value, sizeof(value), "%.3f", v);
        break;
      case LPP_TEMPERATURE:
        reader.readTemperature(v);
        strcpy(name, "temp");
        snprintf(value, sizeof(value), "%.2f", v);
        break;
      case LPP_RELATIVE_HUMIDITY:
        reader.readRelativeHumidity(v);
        strcpy(name, "humid");
        snprintf(value, sizeof(value), "%.1f", v);
        break;
      case LPP_BAROMETRIC_PRESSURE:
        reader.readPressure(v);
        strcpy(name, "press");
        snprintf(value, sizeof(value), "%.1f", v);
        break;
      case LPP_ALTITUDE:
        reader.readAltitude(v);
        strcpy(name, "alt");
        snprintf(value, sizeof(value), "%.0f", v);
        break;
      case LPP_POWER:
        reader.readPower(v);
        strcpy(name, "power");
        snprintf(value, sizeof(value), "%.2f", v);
        break;
      default:
        reader.skipData(type);
        continue;
    }

    snprintf(_sensor_lines[_sensor_line_count], sizeof(_sensor_lines[_sensor_line_count]), "%s %s", name, value);
    _sensor_line_count++;
  }

  _next_sensor_refresh = millis() + 5000;
}

int UITask::renderSensors(DisplayDriver& display) {
  refreshSensorLines();
  char right[8];
  snprintf(right, sizeof(right), "%d", _sensor_line_count);
  drawHeader(display, "Sensors", right);
  if (_sensor_line_count == 0) {
    display.drawTextCentered(display.width() / 2, 24, "No sensor data");
    drawFooter(display, "Back tools");
    return 1000;
  }

  if (_sensor_cursor >= _sensor_line_count) _sensor_cursor = _sensor_line_count - 1;
  if (_sensor_cursor < 0) _sensor_cursor = 0;
  int start = _sensor_cursor > 1 ? _sensor_cursor - 1 : 0;
  if (start > _sensor_line_count - 4) start = max(0, _sensor_line_count - 4);

  for (int row = 0; row < 4; row++) {
    int idx = start + row;
    if (idx >= _sensor_line_count) break;
    drawListItem(display, row, idx == _sensor_cursor, _sensor_lines[idx], NULL);
  }
  drawFooter(display, "U/D browse  Back tools");
  return 1000;
}

int UITask::renderEpic(DisplayDriver& display) {
  char right[12];
  snprintf(right, sizeof(right), "%s", epicSceneLabel(_epic_scene));
  drawHeader(display, "Epic mode", right);
  drawEpicSceneDots(display, _epic_scene);

  unsigned long elapsed = millis() - _epic_started_at;
  bool eco_sleep = _node_prefs->gps_tracker_active && _node_prefs->power_saving_mode && the_mesh.isTrackerGpsEcoSleeping();
  bool gps_power = getGPSState();
  bool linked = hasConnection();
  double latitude = 0.0;
  double longitude = 0.0;
  long altitude_m = 0;
  long satellites = 0;
  bool has_fix = readGpsFix(latitude, longitude, altitude_m, satellites);
  int tx_total = (int)(the_mesh.getNumSentFlood() + the_mesh.getNumSentDirect());
  int rx_total = (int)(the_mesh.getNumRecvFlood() + the_mesh.getNumRecvDirect());
  int recent_nodes = the_mesh.getRecentlyHeard(NULL, 0);
  int repeater_count = getVisibleRepeaterCount();
  int battery_pct = getBatteryPercent(getBattMilliVolts());
  int signal_blocks = has_fix ? constrain((int)satellites, 1, 12) : (gps_power ? 2 : 1);

  char callsign[40];
  copyDisplayText(display, callsign, sizeof(callsign), _node_prefs->node_name);
  if (callsign[0] == 0) {
    strcpy(callsign, "CLIENT");
  }

  const char* motto = epicMottoForIndex(_epic_quote_idx + (int)((elapsed / 2200UL) % 4UL));

  if (_epic_scene == 0) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    display.drawTextCentered(display.width() / 2, 18, "CALLSIGN");

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(2);
    int callsign_width = display.getTextWidth(callsign);
    int callsign_y = 26;
    if (callsign_width > display.width() - 24) {
      display.setTextSize(1);
      callsign_width = display.getTextWidth(callsign);
      callsign_y = 28;
    }
    int callsign_mid = display.width() / 2;
    display.drawTextCentered(callsign_mid, callsign_y, callsign);

    int left = max(2, callsign_mid - callsign_width / 2 - 8);
    int right_edge = min(display.width() - 3, callsign_mid + callsign_width / 2 + 8);
    display.fillRect(left, callsign_y + 2, 5, 1);
    display.fillRect(left, callsign_y + 2, 1, 8);
    display.fillRect(right_edge - 4, callsign_y + 2, 5, 1);
    display.fillRect(right_edge, callsign_y + 2, 1, 8);

    const char* status = "Chaos in standby";
    if (eco_sleep) {
      status = "Stealth orbit engaged";
    } else if (has_fix && linked) {
      status = "Mesh lock acquired";
    } else if (has_fix) {
      status = "GPS lock. Mesh ready";
    } else if (gps_power) {
      status = "Scanning satellites";
    }

    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.drawTextCentered(display.width() / 2, 44, status);
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 54, motto);
  } else if (_epic_scene == 1) {
    char line[32];
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    display.drawTextCentered(display.width() / 2, 18, "TACTICAL SCAN");

    snprintf(line, sizeof(line), "BATT %d%%", battery_pct);
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextLeftAlign(4, 26, line);
    snprintf(line, sizeof(line), "SAT %ld", satellites);
    display.drawTextRightAlign(display.width() - 4, 26, line);
    drawEpicMeter(display, 4, 36, 54, battery_pct, 100, "");
    drawEpicMeter(display, 70, 36, 54, signal_blocks, 12, "");

    int traffic_value = min(99, tx_total + rx_total);
    int orbit_value = eco_sleep ? max(1, 12 - (int)the_mesh.getTrackerGpsEcoWakeSeconds()) : (gps_power ? 8 : 2);
    orbit_value = constrain(orbit_value, 0, 12);
    snprintf(line, sizeof(line), "TRAF %d", traffic_value);
    display.drawTextLeftAlign(4, 44, line);
    snprintf(line, sizeof(line), "%s %d", eco_sleep ? "WAKE" : "ORBT", orbit_value);
    display.drawTextRightAlign(display.width() - 4, 44, line);
    drawEpicMeter(display, 4, 54, 54, traffic_value, 99, "");
    drawEpicMeter(display, 70, 54, 54, orbit_value, 12, "");

    int scan_y = 60;
    int scan_x = 10;
    int scan_w = display.width() - 20;
    int pulse = (int)((elapsed / 110UL) % max(1, scan_w - 4));
    display.setColor(DisplayDriver::GREEN);
    display.drawRect(scan_x, scan_y, scan_w, 3);
    display.fillRect(scan_x + 1 + pulse, scan_y + 1, 4, 1);
  } else {
    char line[32];
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    display.drawTextCentered(display.width() / 2, 18, "MESH COMMAND");

    snprintf(line, sizeof(line), "CT:%d  CH:%d", the_mesh.getNumContacts(), the_mesh.getNumConfiguredChannels());
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextLeftAlign(2, 28, line);

    snprintf(line, sizeof(line), "HD:%d  RP:%d", recent_nodes, repeater_count);
    display.drawTextLeftAlign(2, 38, line);

    snprintf(line, sizeof(line), "TX:%d  RX:%d", tx_total, rx_total);
    display.drawTextLeftAlign(2, 48, line);

    const char* mesh_rank = eco_sleep ? "Ghost captain" :
                            linked ? "Link marshal" :
                            has_fix ? "Field operator" :
                            "Dormant menace";
    display.setColor(DisplayDriver::GREEN);
    display.drawTextCentered(display.width() / 2, 56, mesh_rank);
  }

  return 150;
}

int UITask::renderCurrentScreen(DisplayDriver& display) {
  switch (_screen) {
    case Screen::Splash: return renderSplash(display);
    case Screen::Home: return renderHome(display);
    case Screen::Status: return renderStatus(display);
    case Screen::BatteryDiag: return renderBatteryDiag(display);
    case Screen::GpsStatus: return renderGpsStatus(display);
    case Screen::Messages: return renderMessages(display);
    case Screen::Contacts: return renderContacts(display);
    case Screen::ContactDetail: return renderContactDetail(display);
    case Screen::Channels: return renderChannels(display);
    case Screen::ChannelDetail: return renderChannelDetail(display);
    case Screen::Adverts: return renderAdverts(display);
    case Screen::Radio: return renderRadio(display);
    case Screen::Bluetooth: return renderBluetooth(display);
    case Screen::Sound: return renderSound(display);
    case Screen::GPS: return renderGps(display);
    case Screen::Time: return renderTime(display);
    case Screen::System: return renderSystem(display);
    case Screen::Tools: return renderTools(display);
    case Screen::Repeaters: return renderRepeaters(display);
    case Screen::Stopwatch: return renderStopwatch(display);
    case Screen::Countdown: return renderCountdown(display);
    case Screen::Sensors: return renderSensors(display);
    case Screen::Epic: return renderEpic(display);
    default: return 1000;
  }
}

void UITask::adjustRadioSetting(int delta) {
  float old_freq = _node_prefs->freq;
  float old_bw = _node_prefs->bw;
  uint8_t old_sf = _node_prefs->sf;
  uint8_t old_cr = _node_prefs->cr;
  int8_t old_tx = _node_prefs->tx_power_dbm;
  uint8_t old_repeat = _node_prefs->client_repeat;

  switch (_radio_row) {
    case 0:
      _node_prefs->freq += 0.025f * delta;
      break;
    case 1: {
      int idx = 0;
      for (int i = 0; i < (int)(sizeof(kBandwidthChoices) / sizeof(kBandwidthChoices[0])); i++) {
        if (fabs(_node_prefs->bw - kBandwidthChoices[i]) < 0.2f) {
          idx = i;
          break;
        }
      }
      idx = constrain(idx + delta, 0, (int)(sizeof(kBandwidthChoices) / sizeof(kBandwidthChoices[0])) - 1);
      _node_prefs->bw = kBandwidthChoices[idx];
      break;
    }
    case 2:
      _node_prefs->sf = constrain((int)_node_prefs->sf + delta, 5, 12);
      break;
    case 3:
      _node_prefs->cr = constrain((int)_node_prefs->cr + delta, 5, 8);
      break;
    case 4:
      _node_prefs->tx_power_dbm = constrain((int)_node_prefs->tx_power_dbm + delta, -9, MAX_LORA_TX_POWER);
      break;
    case 5:
      _node_prefs->client_repeat = _node_prefs->client_repeat ? 0 : 1;
      break;
  }

  if (!the_mesh.applyCurrentRadioPrefs()) {
    _node_prefs->freq = old_freq;
    _node_prefs->bw = old_bw;
    _node_prefs->sf = old_sf;
    _node_prefs->cr = old_cr;
    _node_prefs->tx_power_dbm = old_tx;
    _node_prefs->client_repeat = old_repeat;
    showAlert("Repeat blocked here", 1200);
  }
}

void UITask::adjustSystemSetting(int delta) {
  bool needs_save = true;
  switch (_system_row) {
    case 0:
      _node_prefs->power_saving_mode = _node_prefs->power_saving_mode ? 0 : 1;
      the_mesh.applyPowerPrefs(true);
      showAlert(_node_prefs->power_saving_mode ? "Power save on" : "Power save off", 1000);
      needs_save = false;
      break;
    case 1:
      _node_prefs->display_timeout_s = cycleDisplayTimeout(_node_prefs->display_timeout_s, delta);
      showAlert(_node_prefs->display_timeout_s == 0 ? "Display always on" : displayTimeoutLabel(_node_prefs->display_timeout_s), 900);
      break;
    case 2:
      _node_prefs->manual_add_contacts ^= 0x01;
      break;
    case 3:
      _node_prefs->autoadd_config ^= kAutoAddOverwriteOldest;
      break;
    case 4:
      _node_prefs->autoadd_max_hops = constrain((int)_node_prefs->autoadd_max_hops + delta, 0, 64);
      break;
    case 5:
      _node_prefs->path_hash_mode = constrain((int)_node_prefs->path_hash_mode + delta, 0, 2);
      break;
    case 6:
      _node_prefs->airtime_factor = constrain(_node_prefs->airtime_factor + 0.1f * delta, 0.0f, 9.0f);
      break;
  }
  {
    unsigned long timeout_ms = getDisplayTimeoutMillis();
    _auto_off = timeout_ms > 0 ? millis() + timeout_ms : 0;
  }
  if (needs_save) {
    the_mesh.savePrefs();
  }
}

void UITask::adjustGpsSetting(int delta) {
  switch (_gps_row) {
    case 0:
      return;
    case 1:
      toggleGPS();
      return;
#if FEATURE_GPS_TRACKER
    case 2:
      _node_prefs->gps_tracker_active = _node_prefs->gps_tracker_active ? 0 : 1;
      if (_node_prefs->gps_tracker_active) {
        _node_prefs->gps_enabled = 1;
      }
#if ENV_INCLUDE_GPS == 1
      the_mesh.applyGpsPrefs();
#endif
      break;
    case 3:
      _node_prefs->gps_tracker_position_sharing = _node_prefs->gps_tracker_position_sharing ? 0 : 1;
      break;
    case 4:
      _node_prefs->gps_tracker_interval = constrain((int)_node_prefs->gps_tracker_interval + delta * 15, 15, 3600);
      break;
    case 5:
      _node_prefs->gps_tracker_hop_limit = constrain((int)_node_prefs->gps_tracker_hop_limit + delta, 0, 8);
      break;
#else
    case 2:
      _node_prefs->gps_interval = constrain((int)_node_prefs->gps_interval + delta * 15, 0, 3600);
      break;
#endif
  }
  the_mesh.savePrefs();
}

void UITask::toggleBluetooth() {
  if (isSerialEnabled()) {
    disableSerial();
    showAlert("BLE disabled", 800);
  } else {
    enableSerial();
    showAlert("BLE enabled", 800);
  }
}

bool UITask::handleHomeInput(char c) {
  if (c == KEY_LEFT) {
    _home_page = (HomePage)(((int)_home_page + (int)HomePage::Count - 1) % (int)HomePage::Count);
    return true;
  }
  if (c == KEY_RIGHT) {
    _home_page = (HomePage)(((int)_home_page + 1) % (int)HomePage::Count);
    return true;
  }
  if (c == KEY_ENTER) {
    switch (_home_page) {
      case HomePage::Status: _screen = Screen::Status; break;
      case HomePage::Messages: _screen = Screen::Messages; break;
      case HomePage::Contacts: _screen = Screen::Contacts; break;
      case HomePage::Channels: _screen = Screen::Channels; break;
      case HomePage::Adverts: _screen = Screen::Adverts; break;
      case HomePage::Radio: _screen = Screen::Radio; break;
      case HomePage::Bluetooth: _screen = Screen::Bluetooth; break;
      case HomePage::Sound: _screen = Screen::Sound; break;
      case HomePage::GPS: _screen = Screen::GPS; break;
      case HomePage::Time: _screen = Screen::Time; break;
      case HomePage::System: _screen = Screen::System; break;
      case HomePage::Tools: _screen = Screen::Tools; break;
      default: break;
    }
    return true;
  }
  return false;
}

bool UITask::handleStatusInput(char c) {
  if (c == KEY_ENTER) {
    openBatteryDiagnostics(Screen::Status);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleBatteryDiagInput(char c) {
  if (c == KEY_ENTER) {
    resetBatteryDiagnostics();
    sampleBatteryDiagnostics();
    showAlert("Battery reset", 700);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    _screen = _battery_diag_return_screen;
    return true;
  }
  return false;
}

bool UITask::handleGpsStatusInput(char c) {
  if (c == KEY_SELECT || c == KEY_CANCEL || c == KEY_ENTER) {
    _screen = Screen::GPS;
    return true;
  }
  return false;
}

bool UITask::handleMessagesInput(char c) {
  if (c == KEY_UP || c == KEY_LEFT) {
    if (_message_count > 0 && _message_cursor < _message_count - 1) _message_cursor++;
    return true;
  }
  if (c == KEY_DOWN || c == KEY_RIGHT) {
    if (_message_cursor > 0) _message_cursor--;
    return true;
  }
  if (c == KEY_ENTER) {
    _msgcount = 0;
    showAlert("Unread cleared", 800);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleContactsInput(char c) {
  int count = getVisibleContactCount(_contact_tab);
  if (c == KEY_LEFT) {
    _contact_tab = (ContactTab)(((int)_contact_tab + (int)ContactTab::Count - 1) % (int)ContactTab::Count);
    _contact_cursor = 0;
    return true;
  }
  if (c == KEY_RIGHT) {
    _contact_tab = (ContactTab)(((int)_contact_tab + 1) % (int)ContactTab::Count);
    _contact_cursor = 0;
    return true;
  }
  if (c == KEY_UP && count > 0) {
    _contact_cursor = max(0, _contact_cursor - 1);
    return true;
  }
  if (c == KEY_DOWN && count > 0) {
    _contact_cursor = min(count - 1, _contact_cursor + 1);
    return true;
  }
  if (c == KEY_ENTER && count > 0) {
    ContactInfo contact;
    if (getVisibleContactByOrdinal(_contact_tab, _contact_cursor, contact)) {
      selectContact(contact);
    }
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleContactDetailInput(char c) {
  if (!_selected_contact_valid) {
    _screen = Screen::Contacts;
    return true;
  }
  if (c == KEY_LEFT) {
    _contact_action = (_contact_action + 2) % 3;
    return true;
  }
  if (c == KEY_RIGHT) {
    _contact_action = (_contact_action + 1) % 3;
    return true;
  }
  if (c == KEY_ENTER) {
    bool ok = false;
    switch (_contact_action) {
      case 0:
        ok = the_mesh.toggleContactFavourite(_selected_contact.id.pub_key);
        if (ok) _selected_contact.flags ^= kContactFavouriteBit;
        showAlert(ok ? "Favourite toggled" : "Failed", 800);
        break;
      case 1:
        ok = the_mesh.resetContactPathByKey(_selected_contact.id.pub_key);
        if (ok) _selected_contact.out_path_len = OUT_PATH_UNKNOWN;
        showAlert(ok ? "Path reset" : "Failed", 800);
        break;
      case 2:
        ok = the_mesh.removeContactByKey(_selected_contact.id.pub_key);
        showAlert(ok ? "Contact removed" : "Failed", 800);
        _selected_contact_valid = false;
        _screen = Screen::Contacts;
        break;
    }
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    _screen = Screen::Contacts;
    return true;
  }
  return false;
}

bool UITask::handleChannelsInput(char c) {
  int count = the_mesh.getNumConfiguredChannels();
  if (c == KEY_UP && count > 0) {
    _channel_cursor = max(0, _channel_cursor - 1);
    return true;
  }
  if (c == KEY_DOWN && count > 0) {
    _channel_cursor = min(count - 1, _channel_cursor + 1);
    return true;
  }
  if (c == KEY_ENTER && count > 0) {
    int slot_idx;
    ChannelDetails channel;
    if (the_mesh.getConfiguredChannelByOrdinal(_channel_cursor, slot_idx, channel)) {
      selectChannel(slot_idx, channel);
    }
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleChannelDetailInput(char c) {
  if (c == KEY_ENTER) {
    bool ok = the_mesh.removeChannelByIdx(_selected_channel_slot);
    showAlert(ok ? "Channel removed" : "Failed", 800);
    _selected_channel_valid = false;
    _screen = Screen::Channels;
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    _screen = Screen::Channels;
    return true;
  }
  return false;
}

bool UITask::handleAdvertsInput(char c) {
  if (c == KEY_UP) {
    _advert_row = max(0, _advert_row - 1);
    return true;
  }
  if (c == KEY_DOWN) {
    _advert_row = min(2, _advert_row + 1);
    return true;
  }
  if (c == KEY_LEFT || c == KEY_RIGHT || c == KEY_ENTER) {
    if (_advert_row == 0 && c == KEY_ENTER) {
      bool ok = the_mesh.sendSelfAdvertFromUi(false);
      showAlert(ok ? "Advert sent" : "Send failed", 800);
    } else if (_advert_row == 1 && c == KEY_ENTER) {
      bool ok = the_mesh.sendSelfAdvertFromUi(true);
      showAlert(ok ? "Flood advert sent" : "Send failed", 800);
    } else if (_advert_row == 2) {
      _node_prefs->advert_loc_policy = _node_prefs->advert_loc_policy == ADVERT_LOC_NONE ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
      the_mesh.savePrefs();
      showAlert(_node_prefs->advert_loc_policy == ADVERT_LOC_NONE ? "Share off" : "Share on", 800);
    }
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleRadioInput(char c) {
  if (c == KEY_UP) {
    _radio_row = max(0, _radio_row - 1);
    return true;
  }
  if (c == KEY_DOWN) {
    _radio_row = min(5, _radio_row + 1);
    return true;
  }
  if (c == KEY_LEFT) {
    adjustRadioSetting(-1);
    return true;
  }
  if (c == KEY_RIGHT || c == KEY_ENTER) {
    adjustRadioSetting(1);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleBluetoothInput(char c) {
  if (c == KEY_UP) {
    _bluetooth_row = max(0, _bluetooth_row - 1);
    return true;
  }
  if (c == KEY_DOWN) {
    _bluetooth_row = min(3, _bluetooth_row + 1);
    return true;
  }
  if (c == KEY_ENTER && _bluetooth_row == 0) {
    toggleBluetooth();
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleSoundInput(char c) {
  if (c == KEY_LEFT) {
    cycleSoundMode(-1);
    return true;
  }
  if (c == KEY_RIGHT) {
    cycleSoundMode(1);
    return true;
  }
  if (c == KEY_ENTER) {
    cycleSoundMode(1);
    previewSoundMode();
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleGpsInput(char c) {
#if FEATURE_GPS_TRACKER
  const int max_row = 5;
#else
  const int max_row = 2;
#endif
  if (c == KEY_UP) {
    _gps_row = max(0, _gps_row - 1);
    return true;
  }
  if (c == KEY_DOWN) {
    _gps_row = min(max_row, _gps_row + 1);
    return true;
  }
  if (c == KEY_ENTER && _gps_row == 0) {
    _screen = Screen::GpsStatus;
    return true;
  }
  if (c == KEY_LEFT) {
    adjustGpsSetting(-1);
    return true;
  }
  if (c == KEY_RIGHT || c == KEY_ENTER) {
    adjustGpsSetting(1);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleTimeInput(char c) {
  if (c == KEY_SELECT || c == KEY_CANCEL || c == KEY_ENTER) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleSystemInput(char c) {
  if (c == KEY_UP) {
    _system_row = max(0, _system_row - 1);
    return true;
  }
  if (c == KEY_DOWN) {
    _system_row = min(6, _system_row + 1);
    return true;
  }
  if (c == KEY_LEFT) {
    adjustSystemSetting(-1);
    return true;
  }
  if (c == KEY_RIGHT || c == KEY_ENTER) {
    adjustSystemSetting(1);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleToolsInput(char c) {
  if (c == KEY_UP) {
    _tools_row = max(0, _tools_row - 1);
    return true;
  }
  if (c == KEY_DOWN) {
    _tools_row = min(5, _tools_row + 1);
    return true;
  }
  if (c == KEY_ENTER) {
    switch (_tools_row) {
      case 0: _screen = Screen::Repeaters; break;
      case 1: _screen = Screen::Stopwatch; break;
      case 2: _screen = Screen::Countdown; break;
      case 3: _screen = Screen::Sensors; break;
      case 4:
        openBatteryDiagnostics(Screen::Tools);
        break;
      case 5:
        startEpicMode();
        break;
    }
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    gotoHomeScreen();
    return true;
  }
  return false;
}

bool UITask::handleRepeatersInput(char c) {
  int count = getVisibleRepeaterCount();
  if (c == KEY_UP && count > 0) {
    _repeater_cursor = max(0, _repeater_cursor - 1);
    return true;
  }
  if (c == KEY_DOWN && count > 0) {
    _repeater_cursor = min(count - 1, _repeater_cursor + 1);
    return true;
  }
  if (c == KEY_ENTER && count > 0) {
    ContactInfo contact;
    if (getVisibleRepeaterByOrdinal(_repeater_cursor, contact)) {
      selectContact(contact);
    }
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    _screen = Screen::Tools;
    return true;
  }
  return false;
}

bool UITask::handleStopwatchInput(char c) {
  if (c == KEY_ENTER) {
    if (_stopwatch_running) {
      _stopwatch_elapsed_ms += millis() - _stopwatch_started_at;
      _stopwatch_running = false;
    } else {
      _stopwatch_started_at = millis();
      _stopwatch_running = true;
    }
    return true;
  }
  if (c == KEY_LEFT || c == KEY_RIGHT) {
    _stopwatch_running = false;
    _stopwatch_elapsed_ms = 0;
    showAlert("Stopwatch reset", 800);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    _screen = Screen::Tools;
    return true;
  }
  return false;
}

bool UITask::handleCountdownInput(char c) {
  if (!_countdown_running) {
    if (c == KEY_LEFT) {
      _countdown_remaining_ms = _countdown_remaining_ms >= 30000 ? _countdown_remaining_ms - 30000 : 0;
      _countdown_duration_ms = _countdown_remaining_ms;
      return true;
    }
    if (c == KEY_RIGHT) {
      _countdown_remaining_ms = min(_countdown_remaining_ms + 30000UL, 4UL * 3600UL * 1000UL);
      _countdown_duration_ms = _countdown_remaining_ms;
      return true;
    }
    if (c == KEY_UP) {
      _countdown_remaining_ms = min(_countdown_remaining_ms + 300000UL, 4UL * 3600UL * 1000UL);
      _countdown_duration_ms = _countdown_remaining_ms;
      return true;
    }
    if (c == KEY_DOWN) {
      _countdown_remaining_ms = _countdown_remaining_ms >= 300000 ? _countdown_remaining_ms - 300000 : 0;
      _countdown_duration_ms = _countdown_remaining_ms;
      return true;
    }
  }

  if (c == KEY_ENTER) {
    if (_countdown_running) {
      _countdown_remaining_ms = _countdown_target_at > millis() ? _countdown_target_at - millis() : 0;
      _countdown_running = false;
    } else if (_countdown_remaining_ms > 0) {
      _countdown_target_at = millis() + _countdown_remaining_ms;
      _countdown_duration_ms = _countdown_remaining_ms;
      _countdown_running = true;
    }
    return true;
  }

  if (c == KEY_SELECT || c == KEY_CANCEL) {
    _screen = Screen::Tools;
    return true;
  }
  return false;
}

bool UITask::handleSensorsInput(char c) {
  if (c == KEY_UP) {
    _sensor_cursor = max(0, _sensor_cursor - 1);
    return true;
  }
  if (c == KEY_DOWN) {
    _sensor_cursor = min(max(0, _sensor_line_count - 1), _sensor_cursor + 1);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    _screen = Screen::Tools;
    return true;
  }
  return false;
}

bool UITask::handleEpicInput(char c) {
  if (c == KEY_LEFT) {
    _epic_scene = (_epic_scene + 2) % 3;
    notify(UIEventType::ack);
    return true;
  }
  if (c == KEY_RIGHT) {
    _epic_scene = (_epic_scene + 1) % 3;
    notify(UIEventType::ack);
    return true;
  }
  if (c == KEY_UP) {
    _epic_quote_idx = (_epic_quote_idx + 7) % 8;
    notify(UIEventType::ack);
    return true;
  }
  if (c == KEY_DOWN || c == KEY_ENTER) {
    _epic_quote_idx = (_epic_quote_idx + 1) % 8;
    notify(c == KEY_ENTER ? UIEventType::channelMessage : UIEventType::ack);
    return true;
  }
  if (c == KEY_SELECT || c == KEY_CANCEL) {
    _screen = Screen::Tools;
    return true;
  }
  return false;
}

bool UITask::handleInput(char c) {
  switch (_screen) {
    case Screen::Splash: _screen = Screen::Home; return true;
    case Screen::Home: return handleHomeInput(c);
    case Screen::Status: return handleStatusInput(c);
    case Screen::BatteryDiag: return handleBatteryDiagInput(c);
    case Screen::GpsStatus: return handleGpsStatusInput(c);
    case Screen::Messages: return handleMessagesInput(c);
    case Screen::Contacts: return handleContactsInput(c);
    case Screen::ContactDetail: return handleContactDetailInput(c);
    case Screen::Channels: return handleChannelsInput(c);
    case Screen::ChannelDetail: return handleChannelDetailInput(c);
    case Screen::Adverts: return handleAdvertsInput(c);
    case Screen::Radio: return handleRadioInput(c);
    case Screen::Bluetooth: return handleBluetoothInput(c);
    case Screen::Sound: return handleSoundInput(c);
    case Screen::GPS: return handleGpsInput(c);
    case Screen::Time: return handleTimeInput(c);
    case Screen::System: return handleSystemInput(c);
    case Screen::Tools: return handleToolsInput(c);
    case Screen::Repeaters: return handleRepeatersInput(c);
    case Screen::Stopwatch: return handleStopwatchInput(c);
    case Screen::Countdown: return handleCountdownInput(c);
    case Screen::Sensors: return handleSensorsInput(c);
    case Screen::Epic: return handleEpicInput(c);
    default: return false;
  }
}

unsigned long UITask::getDisplayTimeoutMillis() const {
  uint16_t timeout_seconds = _node_prefs ? _node_prefs->display_timeout_s : 0;
  if (timeout_seconds == 0) {
    return AUTO_OFF_MILLIS;
  }
  return static_cast<unsigned long>(timeout_seconds) * 1000UL;
}

char UITask::checkDisplayOn(char c) {
  if (_display) {
    if (!_display->isOn()) {
      _display->turnOn();
      c = 0;
    }
    unsigned long timeout_ms = getDisplayTimeoutMillis();
    _auto_off = timeout_ms > 0 ? millis() + timeout_ms : 0;
    requestRefresh();
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (_display && !_display->isOn()) {
    _display->turnOn();
    unsigned long timeout_ms = getDisplayTimeoutMillis();
    _auto_off = timeout_ms > 0 ? millis() + timeout_ms : 0;
    requestRefresh();
    return 0;
  }
  if (millis() - _ui_started_at < 8000 && c == KEY_ENTER) {
    the_mesh.enterCLIRescue();
    return 0;
  }
  if (c == KEY_ENTER) {
    shutdown();
    return 0;
  }
  return c;
}

void UITask::toggleBuzzer() {
  cycleSoundMode(1);
}

bool UITask::getGPSState() {
  if (_sensors) {
    const char* value = _sensors->getSettingByKey("gps");
    return value && strcmp(value, "1") == 0;
  }
  return false;
}

void UITask::toggleGPS() {
  if (_sensors) {
    bool enabled = getGPSState();
    _node_prefs->gps_enabled = enabled ? 0 : 1;
#if ENV_INCLUDE_GPS == 1
    the_mesh.applyGpsPrefs();
#endif
    the_mesh.savePrefs();
    showAlert(_node_prefs->gps_enabled ? "GPS enabled" : "GPS disabled", 800);
  }
}

bool UITask::isButtonPressed() const {
#if UI_HAS_JOYSTICK
  return user_btn.isPressed() ||
         joystick_up.isPressed() ||
         joystick_down.isPressed() ||
         joystick_left.isPressed() ||
         joystick_right.isPressed() ||
         back_btn.isPressed();
#elif defined(PIN_USER_BTN)
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::shutdown(bool restart) {
#ifdef PIN_BUZZER
  buzzer.shutdown();
  uint32_t buzzer_timer = millis();
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer) {
    buzzer.loop();
  }
#endif

  if (restart) {
    _board->reboot();
  } else {
    if (_display) {
      _display->turnOff();
    }
    radio_driver.powerOff();
    _board->powerOff();
  }
}

void UITask::loop() {
  if (_display && !_display->isOn() && isButtonPressed()) {
    _display->turnOn();
    unsigned long timeout_ms = getDisplayTimeoutMillis();
    _auto_off = timeout_ms > 0 ? millis() + timeout_ms : 0;
    requestRefresh();
    return;
  }

  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  }

  ev = joystick_up.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_UP);
  }

  ev = joystick_down.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_DOWN);
  }

  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  }

  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  }

  ev = back_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  }
#endif

  if (c != 0) {
    handleInput(c);
    unsigned long timeout_ms = getDisplayTimeoutMillis();
    _auto_off = timeout_ms > 0 ? millis() + timeout_ms : 0;
    requestRefresh();
  }

  if (_screen == Screen::Splash && millis() - _ui_started_at >= BOOT_SCREEN_MILLIS) {
    _screen = Screen::Home;
    requestRefresh();
  }

  if (_countdown_running && millis() >= _countdown_target_at) {
    _countdown_running = false;
    _countdown_remaining_ms = 0;
    showAlert("Countdown done", 1200);
    notify(UIEventType::ack);
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying()) {
    buzzer.loop();
  }
#endif
#ifdef PIN_VIBRATION
  vibration.loop();
#endif

  if (_display && _display->isOn() && millis() >= _next_refresh) {
    _display->startFrame();
    int delay_millis = renderCurrentScreen(*_display);
    if (millis() < _alert_expiry) {
      _display->setTextSize(1);
      int y = _display->height() / 3;
      _display->setColor(DisplayDriver::DARK);
      _display->fillRect(3, y, _display->width() - 6, 14);
      _display->setColor(DisplayDriver::LIGHT);
      _display->drawRect(3, y, _display->width() - 6, 14);
      _display->drawTextCentered(_display->width() / 2, y + 3, _alert);
      _next_refresh = _alert_expiry;
    } else {
      _next_refresh = millis() + delay_millis;
    }
    _display->endFrame();
  }

  if (_display && _display->isOn()) {
    unsigned long timeout_ms = getDisplayTimeoutMillis();
    if (timeout_ms > 0 && millis() > _auto_off) {
      _display->turnOff();
    }
  }

  if (millis() > _next_battery_sample) {
    sampleBatteryDiagnostics();
  }

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > _next_batt_check) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
      shutdown();
    }
    _next_batt_check = millis() + 8000;
  }
#endif
}
