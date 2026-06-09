#pragma once

#if defined(ESP_PLATFORM)

#include "TransportKeyStore.h"
#include <Preferences.h>

class Esp32NvsKeyBackend : public TransportKeyBackend {
  Preferences _prefs;

public:
  Esp32NvsKeyBackend() {}

  void begin() {}

  int loadKeysFor(uint16_t id, TransportKey keys[], int max_num) override;
  bool saveKeysFor(uint16_t id, const TransportKey keys[], int num) override;
  bool removeKeys(uint16_t id) override;
  bool clear() override;
};

#endif
