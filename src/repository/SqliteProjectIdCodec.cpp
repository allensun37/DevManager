#include "repository/SqliteProjectIdCodec.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>
#include <system_error>

namespace devmanager {
namespace {

constexpr std::size_t kEncodedProjectIdWidth = 20;
constexpr const char* kDecodeErrorMessage = "invalid SQLite project ID encoding";

}  // namespace

std::string SqliteProjectIdCodec::encode(ProjectId id) {
    std::array<char, kEncodedProjectIdWidth> digits {};
    const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), id, 10);
    if (result.ec != std::errc {}) {
        throw std::runtime_error("failed to encode SQLite project ID");
    }

    const auto digitCount = static_cast<std::size_t>(result.ptr - digits.data());
    std::string encoded(kEncodedProjectIdWidth - digitCount, '0');
    encoded.append(digits.data(), digitCount);
    return encoded;
}

ProjectId SqliteProjectIdCodec::decode(std::string_view encoded) {
    if (encoded.size() != kEncodedProjectIdWidth ||
        !std::all_of(encoded.begin(), encoded.end(), [](char character) {
            return character >= '0' && character <= '9';
        })) {
        throw std::runtime_error(kDecodeErrorMessage);
    }

    ProjectId id = 0;
    const auto result = std::from_chars(encoded.data(), encoded.data() + encoded.size(), id, 10);
    if (result.ec != std::errc {} || result.ptr != encoded.data() + encoded.size()) {
        throw std::runtime_error(kDecodeErrorMessage);
    }

    return id;
}

}  // namespace devmanager
