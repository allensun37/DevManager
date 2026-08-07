#include "common/ProjectIdParser.h"

#include <gtest/gtest.h>

#include <limits>
#include <string_view>

namespace {

TEST(ProjectIdParserTest, AcceptsTrimmedPositiveDecimalId) {
    const auto parsed = devmanager::parseProjectId(" 42 ");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, 42U);
}

TEST(ProjectIdParserTest, RejectsEmptyInput) {
    EXPECT_FALSE(devmanager::parseProjectId("").has_value());
}

TEST(ProjectIdParserTest, RejectsZero) {
    EXPECT_FALSE(devmanager::parseProjectId("0").has_value());
}

TEST(ProjectIdParserTest, RejectsExplicitPositiveSign) {
    EXPECT_FALSE(devmanager::parseProjectId("+1").has_value());
}

TEST(ProjectIdParserTest, RejectsNegativeSign) {
    EXPECT_FALSE(devmanager::parseProjectId("-1").has_value());
}

TEST(ProjectIdParserTest, RejectsTrailingCharacters) {
    EXPECT_FALSE(devmanager::parseProjectId("12x").has_value());
}

TEST(ProjectIdParserTest, RejectsValueOutsideProjectIdRange) {
    EXPECT_FALSE(devmanager::parseProjectId("18446744073709551616").has_value());
}

TEST(ProjectIdParserTest, AcceptsMaximumProjectId) {
    const auto parsed = devmanager::parseProjectId("18446744073709551615");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, std::numeric_limits<devmanager::ProjectId>::max());
}

}  // namespace
