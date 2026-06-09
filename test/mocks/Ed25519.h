#pragma once
#include <stdint.h>
#include <stddef.h>

class Ed25519 {
public:
    static bool verify(const uint8_t* sig, const uint8_t* pub_key, const uint8_t* message, size_t msg_len) {
        return true;
    }
};
