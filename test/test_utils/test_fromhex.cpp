#include <gtest/gtest.h>
#include "Utils.h"

using namespace mesh;

TEST(UtilsFromHex, ConvertValidHex) {
    uint8_t dest[4];
    bool result = Utils::fromHex(dest, sizeof(dest), "01234567");

    EXPECT_TRUE(result);
    EXPECT_EQ(0x01, dest[0]);
    EXPECT_EQ(0x23, dest[1]);
    EXPECT_EQ(0x45, dest[2]);
    EXPECT_EQ(0x67, dest[3]);
}

TEST(UtilsFromHex, ConvertUpperCaseHex) {
    uint8_t dest[2];
    bool result = Utils::fromHex(dest, sizeof(dest), "ABCD");

    EXPECT_TRUE(result);
    EXPECT_EQ(0xAB, dest[0]);
    EXPECT_EQ(0xCD, dest[1]);
}

TEST(UtilsFromHex, ConvertLowerCaseHex) {
    uint8_t dest[2];
    bool result = Utils::fromHex(dest, sizeof(dest), "abcd");

    EXPECT_TRUE(result);
    EXPECT_EQ(0xAB, dest[0]);
    EXPECT_EQ(0xCD, dest[1]);
}

TEST(UtilsFromHex, ConvertZeroHex) {
    uint8_t dest[2];
    bool result = Utils::fromHex(dest, sizeof(dest), "0000");

    EXPECT_TRUE(result);
    EXPECT_EQ(0x00, dest[0]);
    EXPECT_EQ(0x00, dest[1]);
}

TEST(UtilsFromHex, IncorrectLengthTooLong) {
    uint8_t dest[2];
    bool result = Utils::fromHex(dest, sizeof(dest), "ABCDE"); // Length 5 instead of 4

    EXPECT_FALSE(result);
}

TEST(UtilsFromHex, IncorrectLengthTooShort) {
    uint8_t dest[2];
    bool result = Utils::fromHex(dest, sizeof(dest), "ABC"); // Length 3 instead of 4

    EXPECT_FALSE(result);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
