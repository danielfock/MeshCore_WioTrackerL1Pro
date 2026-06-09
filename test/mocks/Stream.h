#pragma once

#include <stdint.h>
#include <stddef.h>

// Mock Stream class for native testing
// Provides interface needed by Utils.h and Identity.h

class Stream {
public:
    virtual void print(char c) {}
    virtual void print(const char* str) {}
    virtual void println() {}
    virtual size_t write(const uint8_t *buffer, size_t size) { return size; }
    virtual size_t readBytes(uint8_t *buffer, size_t length) { return length; }
};
