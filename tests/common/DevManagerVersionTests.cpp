#include "DevManagerVersion.h"

#include <gtest/gtest.h>

#include <string>

TEST(DevManagerVersionTest, ExposesTheCMakeProjectVersion) {
    EXPECT_FALSE(std::string{devmanager::kDevManagerVersion}.empty());
    EXPECT_STREQ(devmanager::kDevManagerVersion, "0.4.0");
}
