#include "common/AsciiText.h"

#include <gtest/gtest.h>

#include <string>

namespace {

TEST(AsciiTextTest, TrimsOnlySurroundingAsciiWhitespace) {
    EXPECT_EQ(devmanager::ascii::trim("\t \r\nDevManager\v\f "), "DevManager");
    EXPECT_EQ(devmanager::ascii::trim("\t \r\n\v\f "), "");
}

TEST(AsciiTextTest, PreservesNonAsciiTextWhileTrimmingAsciiWhitespace) {
    EXPECT_EQ(devmanager::ascii::trim(u8" \t开发中\r\n"), u8"开发中");
}

TEST(AsciiTextTest, DoesNotTrimNonAsciiWhitespaceBytes) {
    const std::string nonBreakingSpace{"\xC2\xA0"};
    const std::string value = " \t" + nonBreakingSpace + "DevManager" + nonBreakingSpace + "\r\n";

    EXPECT_EQ(devmanager::ascii::trim(value),
              nonBreakingSpace + "DevManager" + nonBreakingSpace);
}

TEST(AsciiTextTest, LowercasesOnlyAsciiLetters) {
    EXPECT_EQ(devmanager::ascii::toLower("C++ HTTP 开发中"), u8"c++ http 开发中");
    EXPECT_EQ(devmanager::ascii::toLower("123-_"), "123-_");
}

}  // namespace
