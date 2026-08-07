#include "common/ProjectSearchText.h"

#include <gtest/gtest.h>

#include <string>

namespace {

namespace search_text = devmanager::project_search_text;

TEST(ProjectSearchTextTest, NormalizesNamesByLowercasingAsciiCharacters) {
    EXPECT_EQ(search_text::normalizeName("DevMANAGER"), "devmanager");
}

TEST(ProjectSearchTextTest, NormalizesStatusesByTrimmingAndLowercasingAsciiCharacters) {
    EXPECT_EQ(search_text::normalizeStatus("  In Progress\t"), "in progress");
}

TEST(ProjectSearchTextTest, NormalizesTechnologyWithCppAndPunctuationSearchSemantics) {
    EXPECT_EQ(search_text::normalizeTechnology(" C++ / CMake "), "cppcmake");
}

TEST(ProjectSearchTextTest, BuildsNameSortKeysWithoutTrimming) {
    EXPECT_EQ(search_text::nameSortKey("  Alpha"), "  alpha");
}

TEST(ProjectSearchTextTest, BuildsStatusSortKeysWithoutTrimming) {
    EXPECT_EQ(search_text::statusSortKey(" Active "), " active ");
}

TEST(ProjectSearchTextTest, PreservesNonAsciiBytesWhileLowercasingAsciiCharacters) {
    const std::string input{"A\xE4\xB8\xAD", 4};
    const std::string expected{"a\xE4\xB8\xAD", 4};

    EXPECT_EQ(search_text::normalizeName(input), expected);
}

}  // namespace
