#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock SHA256 class for testing
// Provides minimal interface to allow Utils.cpp to compile
class SHA256 {
public:
  void update(const void* data, size_t len) {}
  void finalize(void* hash, size_t hashLen) {}
  void resetHMAC(const void* key, size_t keyLen) {}
  void finalizeHMAC(const void* key, size_t keyLen, void* hash, size_t hashLen) {}
};
