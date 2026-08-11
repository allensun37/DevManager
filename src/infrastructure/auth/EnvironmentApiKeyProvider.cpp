#include "infrastructure/auth/EnvironmentApiKeyProvider.h"

#include <cstdlib>
#include <stdexcept>

namespace devmanager {

std::string EnvironmentApiKeyProvider::load() const {
    const char* value = std::getenv("DEVMANAGER_API_KEY");
    if (value == nullptr || *value == '\0') {
        throw std::runtime_error("DEVMANAGER_API_KEY is missing or empty");
    }

    return std::string(value);
}

} // namespace devmanager
