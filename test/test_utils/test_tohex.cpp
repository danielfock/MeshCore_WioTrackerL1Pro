#include <gtest/gtest.h>
#include "Utils.h"

using namespace mesh;

#define HEX_BUFFER_SIZE(input) (sizeof(input) * 2 + 1)

TEST(UtilsToHex, ConvertSingleByte) {
    uint8_t input[] = {0xAB};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("AB", output);
}

TEST(UtilsToHex, ConvertMultipleBytes) {
    uint8_t input[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("0123456789ABCDEF", output);
}

TEST(UtilsToHex, ConvertZeroByte) {
    uint8_t input[] = {0x00};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("00", output);
}

TEST(UtilsToHex, ConvertMaxByte) {
    uint8_t input[] = {0xFF};
    char output[HEX_BUFFER_SIZE(input)];

    Utils::toHex(output, input, sizeof(input));

    EXPECT_STREQ("FF", output);
}

TEST(UtilsToHex, NullTerminatesOnEmptyInput) {
    uint8_t input[] = {0xAB};
    char output[] = "X";  // Pre-fill with X.

    Utils::toHex(output, input, 0);

    // Should just null-terminate at position 0
    EXPECT_EQ('\0', output[0]);
}

TEST(UtilsIsHexChar, ValidHexChars) {
    // Test numbers
    for (char c = '0'; c <= '9'; ++c) {
        EXPECT_TRUE(Utils::isHexChar(c));
    }
    // Test uppercase letters
    for (char c = 'A'; c <= 'F'; ++c) {
        EXPECT_TRUE(Utils::isHexChar(c));
    }
    // Test lowercase letters
    for (char c = 'a'; c <= 'f'; ++c) {
        EXPECT_TRUE(Utils::isHexChar(c));
    }
}

TEST(UtilsIsHexChar, InvalidHexChars) {
    EXPECT_FALSE(Utils::isHexChar('G'));
    EXPECT_FALSE(Utils::isHexChar('g'));
    EXPECT_FALSE(Utils::isHexChar('Z'));
    EXPECT_FALSE(Utils::isHexChar('z'));
    EXPECT_FALSE(Utils::isHexChar('/'));
    EXPECT_FALSE(Utils::isHexChar(':'));
    EXPECT_FALSE(Utils::isHexChar('@'));
    EXPECT_FALSE(Utils::isHexChar('`'));
    EXPECT_FALSE(Utils::isHexChar('\0'));
    EXPECT_FALSE(Utils::isHexChar(' '));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
