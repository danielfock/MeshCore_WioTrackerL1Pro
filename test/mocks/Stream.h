#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock Stream class for native testing
// Provides interface needed by Utils.h and Identity.h
class Stream {
public:
  virtual void print(char c) {
    (void)c;
  }

  virtual void print(const char* str) {
    (void)str;
  }

  virtual void println() {}

  virtual void println(const char* str) {
    (void)str;
  }

  virtual size_t write(const uint8_t* buffer, size_t size) {
    (void)buffer;
    return size;
  }

  virtual size_t readBytes(uint8_t* buffer, size_t length) {
    (void)buffer;
    return length;
  }
};