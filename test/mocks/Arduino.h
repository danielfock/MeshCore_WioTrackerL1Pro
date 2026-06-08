#pragma once
#include <stdint.h>
#include <string.h>

class File {
public:
    operator bool() const { return false; }
    void close() {}
    size_t read(uint8_t* buf, size_t size) { return 0; }
    size_t write(const uint8_t* buf, size_t size) { return 0; }
};

class FILESYSTEM {
public:
    bool exists(const char* path) { return false; }
    File open(const char* path, const char* mode = "r", bool create = false) { return File(); }
    void remove(const char* path) {}
    void mkdir(const char* path) {}
};
