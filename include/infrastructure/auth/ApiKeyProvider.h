#pragma once

#include <string>

namespace devmanager {

class ApiKeyProvider {
public:
    virtual ~ApiKeyProvider() = default;
    virtual std::string load() const = 0;
};

} // namespace devmanager
