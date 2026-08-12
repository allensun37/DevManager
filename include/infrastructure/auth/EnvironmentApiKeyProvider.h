#pragma once

#include "infrastructure/auth/ApiKeyProvider.h"

namespace devmanager {

class EnvironmentApiKeyProvider final : public ApiKeyProvider {
public:
    std::string load() const override;
};

} // namespace devmanager
