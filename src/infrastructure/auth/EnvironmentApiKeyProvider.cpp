#include "infrastructure/auth/EnvironmentApiKeyProvider.h"

#include <cstdlib>
#include <stdexcept>

namespace devmanager {

namespace {

bool isAsciiWhitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
           value == '\f' || value == '\v';
}

} // namespace

std::string EnvironmentApiKeyProvider::load() const {
    const char* value = std::getenv("DEVMANAGER_API_KEY");
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error("DEVMANAGER_API_KEY is missing or empty");
    }

    std::string key(value);
    if (isAsciiWhitespace(key.front()) || isAsciiWhitespace(key.back())) {
        throw std::runtime_error(
            "DEVMANAGER_API_KEY must not have boundary whitespace");
    }

    return key;
}

} // namespace devmanager
