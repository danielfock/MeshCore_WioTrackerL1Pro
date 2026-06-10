#pragma once
#ifndef MOCK_SHA256_H
#define MOCK_SHA256_H

#include <stdint.h>
#include <stddef.h>

#ifdef TESTING_PACKET_HASH
#include <vector>

struct SHA256UpdateRecord {
  std::vector<uint8_t> data;
};

extern std::vector<SHA256UpdateRecord> g_sha256_updates;
extern size_t g_sha256_finalize_len;
#endif

// Mock SHA256 class for testing
// Provides minimal interface to allow Utils.cpp to compile
class SHA256 {
public:
  void reset() {}

  void resetHMAC(const uint8_t* key, size_t keyLen) {
    (void)key;
    (void)keyLen;
  }

  void update(const void* data, size_t len) {
#ifdef TESTING_PACKET_HASH
    SHA256UpdateRecord rec;
    rec.data.assign(
      static_cast<const uint8_t*>(data),
      static_cast<const uint8_t*>(data) + len
    );
    g_sha256_updates.push_back(rec);
#else
    (void)data;
    (void)len;
#endif
  }

  void finalize(void* hash, size_t hashLen) {
#ifdef TESTING_PACKET_HASH
    g_sha256_finalize_len = hashLen;
#else
    (void)hashLen;
#endif
    (void)hash;
  }

  void finalizeHMAC(const uint8_t* key, size_t keyLen, void* hash, size_t hashLen) {
    (void)key;
    (void)keyLen;
    (void)hash;
    (void)hashLen;
  }
};

#endif