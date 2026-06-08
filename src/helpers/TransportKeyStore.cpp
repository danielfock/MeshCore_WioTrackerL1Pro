#include "TransportKeyStore.h"
#include <SHA256.h>

uint16_t TransportKey::calcTransportCode(const mesh::Packet* packet) const {
  uint16_t code;
  SHA256 sha;
  sha.resetHMAC(key, sizeof(key));
  uint8_t type = packet->getPayloadType();
  sha.update(&type, 1);
  sha.update(packet->payload, packet->payload_len);
  sha.finalizeHMAC(key, sizeof(key), &code, 2);
  if (code == 0) {     // reserve codes 0000 and FFFF
    code++;
  } else if (code == 0xFFFF) {
    code--;
  }
  return code;
}

bool TransportKey::isNull() const {
  for (int i = 0; i < sizeof(key); i++) {
    if (key[i]) return false;
  }
  return true;  // key is all zeroes
}

void TransportKeyStore::putCache(uint16_t id, const TransportKey& key) {
  if (num_cache < MAX_TKS_ENTRIES) {
    cache_ids[num_cache] = id;
    cache_keys[num_cache] = key;
    num_cache++;
  } else {
    // TODO: evict oldest cache entry
  }
}

void TransportKeyStore::getAutoKeyFor(uint16_t id, const char* name, TransportKey& dest) {
  for (int i = 0; i < num_cache; i++) {  // first, check cache
    if (cache_ids[i] == id) {   // cache hit!
      dest = cache_keys[i];
      return;
    }
  }
  // calc key for publicly-known hashtag region name
  SHA256 sha;
  sha.update(name, strlen(name));
  sha.finalize(&dest.key, sizeof(dest.key));

  putCache(id, dest);
}

int TransportKeyStore::loadKeysFor(uint16_t id, TransportKey keys[], int max_num) {
  int n = 0;
  for (int i = 0; i < num_cache && n < max_num; i++) {  // first, check cache
    if (cache_ids[i] == id) {
      keys[n++] = cache_keys[i];
    }
  }
  if (n > 0) return n;   // cache hit!

  if (_fs && _dir) {
    char filename[64];
    snprintf(filename, sizeof(filename), "%s/%x.tks", _dir, id);
    if (_fs->exists(filename)) {
#if defined(RP2040_PLATFORM)
      File file = _fs->open(filename, "r");
#else
      File file = _fs->open(filename);
#endif
      if (file) {
        while (n < max_num && file.available() >= sizeof(TransportKey)) {
          file.read((uint8_t*)&keys[n], sizeof(TransportKey));
          n++;
        }
        file.close();
      }
    }
  }

  // store in cache (if room)
  for (int i = 0; i < n; i++) {
    putCache(id, keys[i]);
  }
  return n;
}

bool TransportKeyStore::saveKeysFor(uint16_t id, const TransportKey keys[], int num) {
  invalidateCache();

  if (_fs && _dir) {
    char filename[64];
    snprintf(filename, sizeof(filename), "%s/%x.tks", _dir, id);

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    _fs->remove(filename);
    File file = _fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
    File file = _fs->open(filename, "w");
#else
    File file = _fs->open(filename, "w", true);
#endif
    if (file) {
      for (int i = 0; i < num; i++) {
        file.write((const uint8_t*)&keys[i], sizeof(TransportKey));
      }
      file.close();
      return true;
    }
  }

  return false;  // failed
}

bool TransportKeyStore::removeKeys(uint16_t id) {
  invalidateCache();

  if (_fs && _dir) {
    char filename[64];
    snprintf(filename, sizeof(filename), "%s/%x.tks", _dir, id);
    if (_fs->exists(filename)) {
      _fs->remove(filename);
      return true;
    }
  }

  return false;  // failed
}

bool TransportKeyStore::clear() {
  invalidateCache();

  if (_fs && _dir) {
#if defined(ESP32) || defined(RP2040_PLATFORM)
    File root = _fs->open(_dir);
    if (root && root.isDirectory()) {
#if defined(ESP32)
      File file = root.openNextFile();
#else
      // For RP2040 LittleFS/LittleFS
      File file = root.openNextFile();
#endif
      while (file) {
        String fname = file.name();
        file.close();
        if (fname.endsWith(".tks")) {
          if (fname.startsWith("/")) {
            _fs->remove(fname);
          } else {
            String path = String(_dir) + "/" + fname;
            _fs->remove(path);
          }
        }
        file = root.openNextFile();
      }
      root.close();
      return true;
    }
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    File root = _fs->open(_dir);
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        String fname = file.name();
        file.close();
        if (fname.endsWith(".tks")) {
          if (fname.startsWith("/")) {
            _fs->remove(fname.c_str());
          } else {
            String path = String(_dir) + "/" + fname;
            _fs->remove(path.c_str());
          }
        }
        file = root.openNextFile();
      }
      root.close();
      return true;
    }
#endif
  }

  return false;  // failed
}
