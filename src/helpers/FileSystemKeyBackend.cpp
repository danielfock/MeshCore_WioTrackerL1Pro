#include "FileSystemKeyBackend.h"

int FileSystemKeyBackend::loadKeysFor(uint16_t id, TransportKey keys[], int max_num) {
  char filename[64];
  snprintf(filename, sizeof(filename), "%s/%04X.tks", _dir, id);

  if (_fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
    File file = _fs->open(filename, "r");
#else
    File file = _fs->open(filename);
#endif
    if (file) {
      int num = 0;
      while (num < max_num && file.available() >= sizeof(TransportKey)) {
        if (file.read((uint8_t*)&keys[num], sizeof(TransportKey)) == sizeof(TransportKey)) {
          num++;
        } else {
          break;
        }
      }
      file.close();
      return num;
    }
  }
  return 0;
}

bool FileSystemKeyBackend::saveKeysFor(uint16_t id, const TransportKey keys[], int num) {
  char filename[64];
  snprintf(filename, sizeof(filename), "%s/%04X.tks", _dir, id);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _fs->remove(filename);
  File file = _fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  File file = _fs->open(filename, "w");
#else
  File file = _fs->open(filename, "w", true);
#endif

  if (file) {
    bool success = true;
    for (int i = 0; i < num; i++) {
      if (file.write((const uint8_t*)&keys[i], sizeof(TransportKey)) != sizeof(TransportKey)) {
        success = false;
        break;
      }
    }
    file.close();
    if (!success) {
      _fs->remove(filename);
    }
    return success;
  }
  return false;
}

bool FileSystemKeyBackend::removeKeys(uint16_t id) {
  char filename[64];
  snprintf(filename, sizeof(filename), "%s/%04X.tks", _dir, id);
  if (_fs->exists(filename)) {
    return _fs->remove(filename);
  }
  return false;
}

bool FileSystemKeyBackend::clear() {
#if defined(ESP_PLATFORM) || defined(RP2040_PLATFORM)
  File root = _fs->open(_dir);
  if (!root || !root.isDirectory()) {
    return false;
  }

#if defined(RP2040_PLATFORM)
  // RP2040 LittleFS might need slightly different directory iteration or just openNextFile
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (name.endsWith(".tks")) {
        _fs->remove(String(_dir) + "/" + name);
      }
    }
    file = root.openNextFile();
  }
#else
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (name.endsWith(".tks")) {
        // ESP32 returns full path or relative depending on FS, so check.
        String path = String(_dir) + "/" + name;
        if (name.startsWith("/")) path = name;
        _fs->remove(path);
      }
    }
    file = root.openNextFile();
  }
#endif
  return true;
#else
  // For other platforms without direct directory iteration, it's harder to implement clear()
  // without storing an index. We'll return false here to indicate it's not supported via this backend alone.
  return false;
#endif
}
