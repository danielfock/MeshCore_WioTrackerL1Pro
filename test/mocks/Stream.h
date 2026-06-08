#pragma once
#include <stdint.h>
#include <stddef.h>

// Mock Stream class for native testing
// Provides minimal interface needed by Utils.h and others

class Stream {
public:
    virtual void print(char c) {}
    virtual void print(const char* str) {}
    virtual void println() {}
    virtual void println(const char* str) {}
    virtual size_t readBytes(uint8_t *buffer, size_t length) { return 0; }
    virtual size_t write(const uint8_t *buffer, size_t size) { return 0; }
};
