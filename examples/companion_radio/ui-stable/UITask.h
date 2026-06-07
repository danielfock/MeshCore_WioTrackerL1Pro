#pragma once

#include <Arduino.h>
#include <MeshCore.h>

#include <helpers/BaseSerialInterface.h>
#include <helpers/SensorManager.h>
#include <helpers/ui/DisplayDriver.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

class UITask : public AbstractUITask {
  enum class Screen : uint8_t {
    Splash = 0,
    Home,
    MainMenu,
    Overview,
    GPSMenu,
    GPSDetail,
    MeshMenu,
    SettingsMenu,
    Info,
    Message
  };

  enum class MainMenuItem : uint8_t {
    Overview = 0,
    GPS,
    Mesh,
    Settings,
    Info,
    Count
  };

  enum class OverviewSection : uint8_t {
    Power = 0,
    Links,
    Tracker,
    Traffic,
    Count
  };

  enum class GPSMenuItem : uint8_t {
    Detail = 0,
    Power,
    Tracker,
    TimeSync,
    Count
  };

  enum class MeshMenuItem : uint8_t {
    Advert = 0,
    FloodAdvert,
    ClearUnread,
    Count
  };

  enum class SettingsMenuItem : uint8_t {
    Bluetooth = 0,
    Sound,
    PowerSave,
    DisplayTimeout,
    Reboot,
    Count
  };

  enum class InfoSection : uint8_t {
    Device = 0,
    Reset,
    Firmware,
    Count
  };

  DisplayDriver* _display;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
#ifdef PIN_BUZZER
  genericBuzzer _buzzer;
#endif

  Screen _screen;
  unsigned long _next_refresh;
  unsigned long _ui_started_at;
  unsigned long _display_deadline;
  unsigned long _alert_until;
  int _msgcount;
  int _main_menu_row;
  int _gps_menu_row;
  int _mesh_menu_row;
  int _settings_menu_row;
  int _overview_section;
  int _info_section;
  bool _display_soft_off;
  bool _message_active;
  bool _ignore_buttons_until_release;
  char _alert[48];
  char _msg_from[40];
  char _msg_text[96];

  void requestRefresh(unsigned long delay_millis = 0);
  void setAlert(const char* text, unsigned long duration_millis);
  void clearMessagePreview();
  void resetDisplayDeadline();
  unsigned long getDisplayTimeoutMillis() const;
  unsigned long getRefreshIntervalMillis() const;

  bool anyButtonPressed() const;
  void cancelButtonClicks();
  void wakeDisplay(bool consume_current_press);
  void openMainMenu();
  void goHome();
  void goBack();

  void renderHeader(DisplayDriver& display, const char* title) const;
  void renderMenuRow(DisplayDriver& display, int row_idx, int selected_row, const char* label, const char* value = NULL) const;
  void renderValueRow(DisplayDriver& display, int row_idx, const char* label, const char* value) const;
  void renderSectionValueRow(DisplayDriver& display, int row_idx, const char* label, const char* value) const;
  void renderSectionDots(DisplayDriver& display, int count, int active) const;
  void renderBatteryIndicator(DisplayDriver& display, uint16_t battery_mv) const;
  void drawWrappedSnippet(DisplayDriver& display, int x, int y, int w, const char* text, int max_lines) const;

  int getBatteryPercent(uint16_t battery_mv) const;
  bool isGpsEnabled() const;
  bool hasGpsFix() const;
  uint8_t getSoundMode() const;
  const char* getSoundModeLabel() const;
  void applySoundMode();
  void cycleSoundMode();
  void previewSoundMode();

  void renderSplash(DisplayDriver& display);
  void renderHome(DisplayDriver& display);
  void renderMainMenu(DisplayDriver& display);
  void renderOverview(DisplayDriver& display);
  void renderGPSMenu(DisplayDriver& display);
  void renderGPSDetail(DisplayDriver& display);
  void renderMeshMenu(DisplayDriver& display);
  void renderSettingsMenu(DisplayDriver& display);
  void renderInfo(DisplayDriver& display);
  void renderMessage(DisplayDriver& display);
  void renderCurrentScreen(DisplayDriver& display);

  void toggleGpsPower();
  void toggleTracker();
  void requestTimeSync();
  void toggleBluetooth();
  void togglePowerSave();
  void cycleDisplayTimeout();
  void performPrimaryAction();
  void performLongPressAction();

  void handleClickLeft();
  void handleClickRight();
  void handleClickUp();
  void handleClickDown();
  void handleClickEnter();
  void handleClickBack();
  void handleLongPressEnter();
  void handleLongPressBack();

public:
  UITask(mesh::MainBoard* board, BaseSerialInterface* serial)
      : AbstractUITask(board, serial),
        _display(NULL),
        _sensors(NULL),
        _node_prefs(NULL),
        _screen(Screen::Splash),
        _next_refresh(0),
        _ui_started_at(0),
        _display_deadline(0),
        _alert_until(0),
        _msgcount(0),
        _main_menu_row(0),
        _gps_menu_row(0),
        _mesh_menu_row(0),
        _settings_menu_row(0),
        _overview_section(0),
        _info_section(0),
        _display_soft_off(false),
        _message_active(false),
        _ignore_buttons_until_release(false) {
    _alert[0] = 0;
    _msg_from[0] = 0;
    _msg_text[0] = 0;
  }

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  bool hasDisplay() const { return _display != NULL; }

  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
