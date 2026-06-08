#pragma once
#include <stdint.h>
#include <stddef.h>

class SHA256 {
public:
  void reset() {}
  void resetHMAC(const uint8_t* key, size_t keyLen) {}
  void update(const void* data, size_t len) {}
  void finalize(void* hash, size_t hashLen) {}
  void finalizeHMAC(const uint8_t* key, size_t keyLen, void* hash, size_t hashLen) {}
};
