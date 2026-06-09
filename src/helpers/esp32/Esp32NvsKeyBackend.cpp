#if defined(ESP_PLATFORM)

#include "Esp32NvsKeyBackend.h"

int Esp32NvsKeyBackend::loadKeysFor(uint16_t id, TransportKey keys[], int max_num) {
  _prefs.begin("tks", true); // open in read-only mode

  char key_name[16];
  sprintf(key_name, "%04X", id);

  int n = 0;
  size_t size = _prefs.getBytesLength(key_name);
  if (size > 0 && (size % sizeof(TransportKey)) == 0) {
    uint8_t* buffer = (uint8_t*)malloc(size);
    if (buffer) {
      size_t read_bytes = _prefs.getBytes(key_name, buffer, size);
      if (read_bytes == size) {
        int stored_num = size / sizeof(TransportKey);
        n = stored_num < max_num ? stored_num : max_num;
        memcpy(keys, buffer, n * sizeof(TransportKey));
      }
      free(buffer);
    }
  }

  _prefs.end();
  return n;
}

bool Esp32NvsKeyBackend::saveKeysFor(uint16_t id, const TransportKey keys[], int num) {
  _prefs.begin("tks", false); // open in read-write mode

  char key_name[16];
  sprintf(key_name, "%04X", id);

  size_t bytes_written = _prefs.putBytes(key_name, keys, num * sizeof(TransportKey));

  _prefs.end();
  return bytes_written == (num * sizeof(TransportKey));
}

bool Esp32NvsKeyBackend::removeKeys(uint16_t id) {
  _prefs.begin("tks", false); // open in read-write mode

  char key_name[16];
  sprintf(key_name, "%04X", id);

  bool success = _prefs.remove(key_name);

  _prefs.end();
  return success;
}

bool Esp32NvsKeyBackend::clear() {
  _prefs.begin("tks", false); // open in read-write mode
  bool success = _prefs.clear();
  _prefs.end();
  return success;
}

#endif
