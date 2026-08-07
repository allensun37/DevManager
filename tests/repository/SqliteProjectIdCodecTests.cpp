#include "repository/SqliteProjectIdCodec.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kDecodeErrorMessage = "invalid SQLite project ID encoding";

void expectDecodeError(std::string_view encoded) {
    try {
        static_cast<void>(devmanager::SqliteProjectIdCodec::decode(encoded));
        FAIL() << "Expected decode to reject: " << encoded;
    } catch (const std::runtime_error& error) {
        EXPECT_EQ(error.what(), kDecodeErrorMessage);
    } catch (...) {
        FAIL() << "Expected std::runtime_error";
    }
}

TEST(SqliteProjectIdCodecTest, EncodesZeroAsTwentyDigits) {
    EXPECT_EQ(devmanager::SqliteProjectIdCodec::encode(0), "00000000000000000000");
}

TEST(SqliteProjectIdCodecTest, EncodesOneAsTwentyDigits) {
    EXPECT_EQ(devmanager::SqliteProjectIdCodec::encode(1), "00000000000000000001");
}

TEST(SqliteProjectIdCodecTest, EncodesMaximumUnsignedValueAsTwentyDigits) {
    EXPECT_EQ(devmanager::SqliteProjectIdCodec::encode(
                  std::numeric_limits<devmanager::ProjectId>::max()),
              "18446744073709551615");
}

TEST(SqliteProjectIdCodecTest, DecodesCanonicalValuesAcrossTheUnsignedRange) {
    EXPECT_EQ(devmanager::SqliteProjectIdCodec::decode("00000000000000000000"), 0U);
    EXPECT_EQ(devmanager::SqliteProjectIdCodec::decode("00000000000000000001"), 1U);
    EXPECT_EQ(devmanager::SqliteProjectIdCodec::decode("18446744073709551615"),
              std::numeric_limits<devmanager::ProjectId>::max());
}

TEST(SqliteProjectIdCodecTest, RejectsNonCanonicalOrOutOfRangeValues) {
    constexpr std::array invalidValues {
        std::string_view {""},
        std::string_view {"0000000000000000000"},
        std::string_view {"000000000000000000000"},
        std::string_view {"+0000000000000000001"},
        std::string_view {"-0000000000000000001"},
        std::string_view {" 0000000000000000001"},
        std::string_view {"0000000000000000001 "},
        std::string_view {"0000000000000000000x"},
        std::string_view {"18446744073709551616"},
    };

    for (const std::string_view invalidValue : invalidValues) {
        SCOPED_TRACE(invalidValue);
        expectDecodeError(invalidValue);
    }
}

TEST(SqliteProjectIdCodecTest, RoundTripsRepresentativeUnsignedValues) {
    constexpr std::array<devmanager::ProjectId, 6> values {
        0U,
        1U,
        42U,
        1'000'000'000'000U,
        std::numeric_limits<devmanager::ProjectId>::max() - 1U,
        std::numeric_limits<devmanager::ProjectId>::max(),
    };

    for (const devmanager::ProjectId value : values) {
        SCOPED_TRACE(value);
        EXPECT_EQ(devmanager::SqliteProjectIdCodec::decode(
                      devmanager::SqliteProjectIdCodec::encode(value)),
                  value);
    }
}

TEST(SqliteProjectIdCodecTest, LinksThePinnedSqliteVersion) {
    EXPECT_EQ(sqlite3_libversion_number(), 3'053'004);
    EXPECT_STREQ(sqlite3_libversion(), "3.53.4");
}

}  // namespace
