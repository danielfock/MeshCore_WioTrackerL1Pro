#define TESTING_PACKET_HASH 1

#include <vector>
#include <stdint.h>
#include <stddef.h>
struct SHA256UpdateRecord {
    std::vector<uint8_t> data;
};
std::vector<SHA256UpdateRecord> g_sha256_updates;
size_t g_sha256_finalize_len = 0;

#include <gtest/gtest.h>
#include "Packet.h"

using namespace mesh;

class PacketHashTest : public ::testing::Test {
protected:
    void SetUp() override {
        g_sha256_updates.clear();
        g_sha256_finalize_len = 0;
    }
};

TEST_F(PacketHashTest, NonTracePacketHash) {
    Packet packet;
    packet.header = (PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT);
    packet.payload_len = 3;
    packet.payload[0] = 0xAA;
    packet.payload[1] = 0xBB;
    packet.payload[2] = 0xCC;

    uint8_t hash[MAX_HASH_SIZE] = {0};
    packet.calculatePacketHash(hash);

    // Expect 2 updates: type, payload
    ASSERT_EQ(g_sha256_updates.size(), 2);

    // Update 1: type
    EXPECT_EQ(g_sha256_updates[0].data.size(), 1);
    EXPECT_EQ(g_sha256_updates[0].data[0], PAYLOAD_TYPE_TXT_MSG);

    // Update 2: payload
    EXPECT_EQ(g_sha256_updates[1].data.size(), 3);
    EXPECT_EQ(g_sha256_updates[1].data[0], 0xAA);
    EXPECT_EQ(g_sha256_updates[1].data[1], 0xBB);
    EXPECT_EQ(g_sha256_updates[1].data[2], 0xCC);

    EXPECT_EQ(g_sha256_finalize_len, MAX_HASH_SIZE);
}

TEST_F(PacketHashTest, TracePacketHash) {
    Packet packet;
    packet.header = (PAYLOAD_TYPE_TRACE << PH_TYPE_SHIFT);
    packet.path_len = 0x1234;
    packet.payload_len = 2;
    packet.payload[0] = 0x11;
    packet.payload[1] = 0x22;

    uint8_t hash[MAX_HASH_SIZE] = {0};
    packet.calculatePacketHash(hash);

    // Expect 3 updates: type, path_len, payload
    ASSERT_EQ(g_sha256_updates.size(), 3);

    // Update 1: type
    EXPECT_EQ(g_sha256_updates[0].data.size(), 1);
    EXPECT_EQ(g_sha256_updates[0].data[0], PAYLOAD_TYPE_TRACE);

    // Update 2: path_len
    EXPECT_EQ(g_sha256_updates[1].data.size(), sizeof(packet.path_len));
    uint16_t expected_path_len = 0x1234;
    std::vector<uint8_t> path_len_bytes(
        (uint8_t*)&expected_path_len,
        (uint8_t*)&expected_path_len + sizeof(expected_path_len)
    );
    ASSERT_EQ(g_sha256_updates[1].data.size(), path_len_bytes.size());
    for (size_t i = 0; i < path_len_bytes.size(); i++) {
        EXPECT_EQ(g_sha256_updates[1].data[i], path_len_bytes[i]);
    }

    // Update 3: payload
    EXPECT_EQ(g_sha256_updates[2].data.size(), 2);
    EXPECT_EQ(g_sha256_updates[2].data[0], 0x11);
    EXPECT_EQ(g_sha256_updates[2].data[1], 0x22);

    EXPECT_EQ(g_sha256_finalize_len, MAX_HASH_SIZE);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
