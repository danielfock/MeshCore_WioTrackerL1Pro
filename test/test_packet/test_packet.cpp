#include <gtest/gtest.h>
#include "Packet.h"

using namespace mesh;

TEST(PacketReadFrom, InvalidPathLenReserved) {
    Packet packet;
    // path_len = 0xC0 (hash_size == 4 -> invalid)
    uint8_t src[] = {0x01, 0xC0, 0x00, 0x00};
    EXPECT_FALSE(packet.readFrom(src, sizeof(src)));
}

TEST(PacketReadFrom, InvalidPathLenExceedsMax) {
    Packet packet;
    // path_len = 0xBF (hash_size == 3, hash_count == 63 -> 189 bytes, exceeds MAX_PATH_SIZE 64)
    uint8_t src[] = {0x01, 0xBF, 0x00, 0x00};
    EXPECT_FALSE(packet.readFrom(src, sizeof(src)));
}

TEST(PacketReadFrom, OutOfBoundsAfterPath) {
    Packet packet;
    // Header 0x01 (No transport codes)
    // Path len 0x00 (0 bytes path)
    // We provide length 2. After header and path_len, i = 2.
    // i >= len -> return false
    uint8_t src[] = {0x01, 0x00};
    EXPECT_FALSE(packet.readFrom(src, sizeof(src)));
}

TEST(PacketReadFrom, OutOfBoundsDuringPath) {
    Packet packet;
    // Header 0x01 (No transport codes)
    // Path len 0x02 (hash_size == 1, hash_count == 2 -> 2 bytes path)
    // Length provided is 3, i = 2 before copying path, so we read 2 bytes for path and we reach i = 4.
    // i >= len -> return false.
    uint8_t src[] = {0x01, 0x02, 0x00};
    EXPECT_FALSE(packet.readFrom(src, sizeof(src)));
}

TEST(PacketReadFrom, PayloadExceedsMax) {
    Packet packet;
    // Header 0x01
    // Path len 0x00
    // Exactly MAX_PACKET_PAYLOAD (184) + 1 bytes of payload
    uint8_t src[187] = {0};
    src[0] = 0x01;
    src[1] = 0x00;
    EXPECT_FALSE(packet.readFrom(src, 187));
}

TEST(PacketReadFrom, ValidPacket) {
    Packet packet;
    // Header 0x01
    // Path len 0x00
    // Payload length = 8 bytes
    // Total len = 10
    uint8_t src[10] = {0};
    src[0] = 0x01;
    src[1] = 0x00;
    EXPECT_TRUE(packet.readFrom(src, 10));
    EXPECT_EQ(packet.payload_len, 8);
}

TEST(PacketReadFrom, ValidPacketWithTransportCodes) {
    Packet packet;
    // Header 0x00 (ROUTE_TYPE_TRANSPORT_FLOOD -> hasTransportCodes() is true)
    // Needs 4 bytes for transport codes.
    // path_len 0x00
    // Payload length = 8 bytes
    // Total len = 1 + 4 + 1 + 8 = 14 bytes
    uint8_t src[14] = {0};
    src[0] = 0x00; // ROUTE_TYPE_TRANSPORT_FLOOD
    src[1] = 0x11; // transport_codes[0] LSB
    src[2] = 0x22; // transport_codes[0] MSB
    src[3] = 0x33; // transport_codes[1] LSB
    src[4] = 0x44; // transport_codes[1] MSB
    src[5] = 0x00; // path_len
    // remaining is payload
    EXPECT_TRUE(packet.readFrom(src, 14));
    EXPECT_EQ(packet.transport_codes[0], 0x2211);
    EXPECT_EQ(packet.transport_codes[1], 0x4433);
    EXPECT_EQ(packet.payload_len, 8);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
