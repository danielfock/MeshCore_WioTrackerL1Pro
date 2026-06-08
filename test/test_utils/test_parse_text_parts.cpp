#include <gtest/gtest.h>
#include "Utils.h"

using namespace mesh;

TEST(UtilsParseTextParts, EmptyString) {
    char text[] = "";
    const char* parts[5];
    int num = Utils::parseTextParts(text, parts, 5);

    EXPECT_EQ(0, num);
}

TEST(UtilsParseTextParts, SinglePart) {
    char text[] = "part1";
    const char* parts[5];
    int num = Utils::parseTextParts(text, parts, 5);

    EXPECT_EQ(1, num);
    EXPECT_STREQ("part1", parts[0]);
}

TEST(UtilsParseTextParts, MultipleParts) {
    char text[] = "part1,part2,part3";
    const char* parts[5];
    int num = Utils::parseTextParts(text, parts, 5);

    EXPECT_EQ(3, num);
    EXPECT_STREQ("part1", parts[0]);
    EXPECT_STREQ("part2", parts[1]);
    EXPECT_STREQ("part3", parts[2]);
}

TEST(UtilsParseTextParts, EmptyParts) {
    char text[] = "part1,,part3";
    const char* parts[5];
    int num = Utils::parseTextParts(text, parts, 5);

    EXPECT_EQ(3, num);
    EXPECT_STREQ("part1", parts[0]);
    EXPECT_STREQ("", parts[1]);
    EXPECT_STREQ("part3", parts[2]);
}

TEST(UtilsParseTextParts, CustomSeparator) {
    char text[] = "part1|part2|part3";
    const char* parts[5];
    int num = Utils::parseTextParts(text, parts, 5, '|');

    EXPECT_EQ(3, num);
    EXPECT_STREQ("part1", parts[0]);
    EXPECT_STREQ("part2", parts[1]);
    EXPECT_STREQ("part3", parts[2]);
}

TEST(UtilsParseTextParts, MaxElementsReached) {
    char text[] = "part1,part2,part3,part4,part5";
    const char* parts[3];
    int num = Utils::parseTextParts(text, parts, 3);

    EXPECT_EQ(3, num);
    EXPECT_STREQ("part1", parts[0]);
    EXPECT_STREQ("part2", parts[1]);
    EXPECT_STREQ("part3", parts[2]);
}

TEST(UtilsParseTextParts, MaxElementsReached_WithTrailingSeparator) {
    char text[] = "part1,part2,part3";
    const char* parts[2];
    int num = Utils::parseTextParts(text, parts, 2);

    EXPECT_EQ(2, num);
    EXPECT_STREQ("part1", parts[0]);
    EXPECT_STREQ("part2", parts[1]);
}
