#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <CayenneLPP.h>

#include <helpers/BaseSerialInterface.h>
#include <helpers/ChannelDetails.h>
#include <helpers/ContactInfo.h>
#include <helpers/SensorManager.h>
#include <helpers/ui/DisplayDriver.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

class UITask : public AbstractUITask {
  static const int kMessageHistory = 32;
  static const int kMessageTextLen = 96;
  static const int kSensorLineCount = 12;
  static const int kBatterySampleCount = 24;

  enum class Screen : uint8_t {
    Splash,
    Home,
    Status,
    BatteryDiag,
    GpsStatus,
    Messages,
    Contacts,
    ContactDetail,
    Channels,
    ChannelDetail,
    Adverts,
    Radio,
    Bluetooth,
    Sound,
    GPS,
    Time,
    System,
    Tools,
    Repeaters,
    Stopwatch,
    Countdown,
    Sensors,
    Epic
  };

  enum class HomePage : uint8_t {
    Status,
    Messages,
    Contacts,
    Channels,
    Adverts,
    Radio,
    Bluetooth,
    Sound,
    GPS,
    Time,
    System,
    Tools,
    Count
  };

  enum class ContactTab : uint8_t {
    Favs,
    Contacts,
    Repeaters,
    Rooms,
    Sensors,
    Count
  };

  struct MessageEntry {
    char origin[32];
    char body[kMessageTextLen];
    uint32_t timestamp;
    uint8_t path_len;
  };

  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  NodePrefs* _node_prefs;

  unsigned long _next_refresh;
  unsigned long _auto_off;
  unsigned long _alert_expiry;
  unsigned long _ui_started_at;
  unsigned long _next_batt_check;
  unsigned long _next_sensor_refresh;
  unsigned long _countdown_target_at;
  unsigned long _stopwatch_started_at;
  unsigned long _stopwatch_elapsed_ms;
  unsigned long _epic_started_at;

  char _alert[80];
  int _msgcount;

  Screen _screen;
  Screen _battery_diag_return_screen;
  HomePage _home_page;
  ContactTab _contact_tab;

  MessageEntry _messages[kMessageHistory];
  int _message_count;
  int _message_head;
  int _message_cursor;

  int _contact_cursor;
  int _contact_action;
  ContactInfo _selected_contact;
  bool _selected_contact_valid;

  int _channel_cursor;
  int _channel_action;
  int _selected_channel_slot;
  ChannelDetails _selected_channel;
  bool _selected_channel_valid;

  int _advert_row;
  int _bluetooth_row;
  int _gps_row;
  int _radio_row;
  int _system_row;
  int _tools_row;
  int _repeater_cursor;
  int _sensor_cursor;
  int _epic_scene;
  int _epic_quote_idx;

  bool _stopwatch_running;
  bool _countdown_running;
  unsigned long _countdown_duration_ms;
  unsigned long _countdown_remaining_ms;
  unsigned long _next_battery_sample;

  CayenneLPP _sensor_lpp;
  char _sensor_lines[kSensorLineCount][28];
  int _sensor_line_count;
  uint16_t _battery_samples[kBatterySampleCount];
  int _battery_sample_count;
  int _battery_sample_head;
  uint16_t _battery_min_sample_mv;
  uint16_t _battery_max_sample_mv;

  void requestRefresh(unsigned long delay_millis = 0);
  void userLedHandler();
  void rememberMessage(uint8_t path_len, const char* from_name, const char* text);
  int getBatteryPercent(uint16_t batteryMilliVolts) const;
  uint8_t getSoundMode() const;
  const char* getSoundModeLabel() const;
  void applySoundMode();
  void cycleSoundMode(int delta);
  void previewSoundMode();
  bool readGpsFix(double& latitude, double& longitude, long& altitudeMeters, long& satellites) const;

  void drawHeader(DisplayDriver& display, const char* title, const char* right = NULL);
  void drawFooter(DisplayDriver& display, const char* hint);
  void drawListItem(DisplayDriver& display, int row, bool selected, const char* label, const char* value = NULL);
  void drawHomePageDots(DisplayDriver& display);
  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts);

  int renderSplash(DisplayDriver& display);
  int renderHome(DisplayDriver& display);
  int renderStatus(DisplayDriver& display);
  int renderBatteryDiag(DisplayDriver& display);
  int renderGpsStatus(DisplayDriver& display);
  int renderMessages(DisplayDriver& display);
  int renderContacts(DisplayDriver& display);
  int renderContactDetail(DisplayDriver& display);
  int renderChannels(DisplayDriver& display);
  int renderChannelDetail(DisplayDriver& display);
  int renderAdverts(DisplayDriver& display);
  int renderRadio(DisplayDriver& display);
  int renderBluetooth(DisplayDriver& display);
  int renderSound(DisplayDriver& display);
  int renderGps(DisplayDriver& display);
  int renderTime(DisplayDriver& display);
  int renderSystem(DisplayDriver& display);
  int renderTools(DisplayDriver& display);
  int renderRepeaters(DisplayDriver& display);
  int renderStopwatch(DisplayDriver& display);
  int renderCountdown(DisplayDriver& display);
  int renderSensors(DisplayDriver& display);
  int renderEpic(DisplayDriver& display);
  int renderCurrentScreen(DisplayDriver& display);

  bool handleHomeInput(char c);
  bool handleStatusInput(char c);
  bool handleBatteryDiagInput(char c);
  bool handleGpsStatusInput(char c);
  bool handleMessagesInput(char c);
  bool handleContactsInput(char c);
  bool handleContactDetailInput(char c);
  bool handleChannelsInput(char c);
  bool handleChannelDetailInput(char c);
  bool handleAdvertsInput(char c);
  bool handleRadioInput(char c);
  bool handleBluetoothInput(char c);
  bool handleSoundInput(char c);
  bool handleGpsInput(char c);
  bool handleTimeInput(char c);
  bool handleSystemInput(char c);
  bool handleToolsInput(char c);
  bool handleRepeatersInput(char c);
  bool handleStopwatchInput(char c);
  bool handleCountdownInput(char c);
  bool handleSensorsInput(char c);
  bool handleEpicInput(char c);
  bool handleInput(char c);

  bool isContactVisible(const ContactInfo& contact, ContactTab tab) const;
  int getVisibleContactCount(ContactTab tab) const;
  bool getVisibleContactByOrdinal(ContactTab tab, int ordinal, ContactInfo& contact) const;
  int getVisibleRepeaterCount() const;
  bool getVisibleRepeaterByOrdinal(int ordinal, ContactInfo& contact) const;
  void selectContact(const ContactInfo& contact);
  void selectChannel(int slot_idx, const ChannelDetails& channel);

  void adjustRadioSetting(int delta);
  void adjustSystemSetting(int delta);
  void adjustGpsSetting(int delta);
  void toggleBluetooth();
  void startEpicMode();
  void openBatteryDiagnostics(Screen return_screen);
  void resetBatteryDiagnostics();
  void recalculateBatteryDiagnosticsRange();
  void sampleBatteryDiagnostics();
  void refreshSensorLines();
  unsigned long getDisplayTimeoutMillis() const;

  char checkDisplayOn(char c);
  char handleLongPress(char c);

public:
  UITask(mesh::MainBoard* board, BaseSerialInterface* serial)
      : AbstractUITask(board, serial),
        _display(NULL),
        _sensors(NULL),
        _node_prefs(NULL),
        _next_refresh(0),
        _auto_off(0),
        _alert_expiry(0),
        _ui_started_at(0),
        _next_batt_check(0),
        _next_sensor_refresh(0),
        _countdown_target_at(0),
        _stopwatch_started_at(0),
        _stopwatch_elapsed_ms(0),
        _epic_started_at(0),
        _msgcount(0),
        _screen(Screen::Splash),
        _battery_diag_return_screen(Screen::Home),
        _home_page(HomePage::Messages),
        _contact_tab(ContactTab::Contacts),
        _message_count(0),
        _message_head(-1),
        _message_cursor(0),
        _contact_cursor(0),
        _contact_action(0),
        _selected_contact_valid(false),
        _channel_cursor(0),
        _channel_action(0),
        _selected_channel_slot(-1),
        _selected_channel_valid(false),
        _advert_row(0),
        _bluetooth_row(0),
        _gps_row(0),
        _radio_row(0),
        _system_row(0),
        _tools_row(0),
        _repeater_cursor(0),
        _sensor_cursor(0),
        _epic_scene(0),
        _epic_quote_idx(0),
        _stopwatch_running(false),
        _countdown_running(false),
        _countdown_duration_ms(0),
        _countdown_remaining_ms(0),
        _next_battery_sample(0),
        _sensor_lpp(220),
        _sensor_line_count(0),
        _battery_sample_count(0),
        _battery_sample_head(0),
        _battery_min_sample_mv(0),
        _battery_max_sample_mv(0) {
    _alert[0] = 0;
    memset(_messages, 0, sizeof(_messages));
    memset(&_selected_contact, 0, sizeof(_selected_contact));
    memset(&_selected_channel, 0, sizeof(_selected_channel));
    memset(_sensor_lines, 0, sizeof(_sensor_lines));
    memset(_battery_samples, 0, sizeof(_battery_samples));
  }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  void gotoHomeScreen() {
    _screen = Screen::Home;
    requestRefresh();
  }

  void showAlert(const char* text, int duration_millis);
  int getMsgCount() const { return _msgcount; }
  bool hasDisplay() const { return _display != NULL; }
  bool isButtonPressed() const;

  bool isBuzzerQuiet() {
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  bool getGPSState();
  void toggleGPS();

  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
