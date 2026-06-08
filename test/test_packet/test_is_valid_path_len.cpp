#include <gtest/gtest.h>
#include "Packet.h"

using namespace mesh;

TEST(PacketIsValidPathLen, ValidPathLengths) {
    // MAX_PATH_SIZE is 64

    // Path Len is composed of hash_count and hash_size
    // hash_count = path_len & 63
    // hash_size = (path_len >> 6) + 1
    // condition: hash_count * hash_size <= 64

    // 0 -> hash_count = 0, hash_size = 1
    EXPECT_TRUE(Packet::isValidPathLen(0));

    // 63 -> hash_count = 63, hash_size = 1
    EXPECT_TRUE(Packet::isValidPathLen(63));

    // hash_size = 2 -> (1 << 6) = 64
    // 64 -> hash_count = 0, hash_size = 2
    EXPECT_TRUE(Packet::isValidPathLen(64));

    // 64 + 32 = 96 -> hash_count = 32, hash_size = 2 -> 32 * 2 = 64
    EXPECT_TRUE(Packet::isValidPathLen(96));

    // hash_size = 3 -> (2 << 6) = 128
    // 128 -> hash_count = 0, hash_size = 3
    EXPECT_TRUE(Packet::isValidPathLen(128));

    // 128 + 21 = 149 -> hash_count = 21, hash_size = 3 -> 21 * 3 = 63 <= 64
    EXPECT_TRUE(Packet::isValidPathLen(149));
}

TEST(PacketIsValidPathLen, InvalidPathLengths) {
    // 64 + 33 = 97 -> hash_count = 33, hash_size = 2 -> 33 * 2 = 66 > 64
    EXPECT_FALSE(Packet::isValidPathLen(97));

    // 128 + 22 = 150 -> hash_count = 22, hash_size = 3 -> 22 * 3 = 66 > 64
    EXPECT_FALSE(Packet::isValidPathLen(150));
}

TEST(PacketIsValidPathLen, ReservedHashSize) {
    // hash_size = 4 (reserved) -> (3 << 6) = 192
    EXPECT_FALSE(Packet::isValidPathLen(192));
    EXPECT_FALSE(Packet::isValidPathLen(192 + 10)); // hash_size = 4, hash_count = 10
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
