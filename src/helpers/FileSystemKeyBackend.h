#pragma once

#include "TransportKeyStore.h"
#include <helpers/IdentityStore.h>

class FileSystemKeyBackend : public TransportKeyBackend {
  FILESYSTEM* _fs;
  char _dir[32];

public:
  FileSystemKeyBackend(FILESYSTEM& fs, const char* dir) : _fs(&fs) {
    if (dir) {
      strncpy(_dir, dir, sizeof(_dir) - 1);
      _dir[sizeof(_dir) - 1] = '\0';
    } else {
      _dir[0] = '\0';
    }
  }

  void begin() {
    if (_dir[0] == '/') {
      _fs->mkdir(_dir);
    }
  }

  int loadKeysFor(uint16_t id, TransportKey keys[], int max_num) override;
  bool saveKeysFor(uint16_t id, const TransportKey keys[], int num) override;
  bool removeKeys(uint16_t id) override;
  bool clear() override;
};
