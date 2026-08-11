#include "infrastructure/auth/ApiKeyAuthenticator.h"

#include <stdexcept>
#include <utility>

namespace devmanager {

namespace {

bool isAsciiWhitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
           value == '\f' || value == '\v';
}

} // namespace

ApiKeyAuthenticator::ApiKeyAuthenticator(std::string expectedKey)
    : expectedKey_(std::move(expectedKey)) {
    if (expectedKey_.empty() || isAsciiWhitespace(expectedKey_.front()) ||
        isAsciiWhitespace(expectedKey_.back())) {
        throw std::invalid_argument(
            "expected API key must not be empty or have boundary whitespace");
    }
}

bool ApiKeyAuthenticator::authenticate(std::string_view authorization) const noexcept {
    constexpr std::string_view prefix = "Bearer ";
    if (authorization.size() <= prefix.size() ||
        authorization.compare(0, prefix.size(), prefix) != 0 ||
        authorization[prefix.size()] == ' ') {
        return false;
    }

    const std::string_view token = authorization.substr(prefix.size());
    if (isAsciiWhitespace(token.front()) || isAsciiWhitespace(token.back())) {
        return false;
    }

    return secureEquals(token, expectedKey_);
}

bool ApiKeyAuthenticator::secureEquals(std::string_view left,
                                       std::string_view right) noexcept {
    const std::size_t maxLength = left.size() > right.size() ? left.size() : right.size();
    std::size_t difference = left.size() ^ right.size();

    for (std::size_t index = 0; index < maxLength; ++index) {
        const unsigned char leftByte =
            index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
        const unsigned char rightByte =
            index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
        difference |= leftByte ^ rightByte;
    }

    return difference == 0U;
}

} // namespace devmanager
