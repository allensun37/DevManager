#pragma once

#include <string>
#include <string_view>

namespace devmanager {

class ApiKeyAuthenticator final {
public:
    explicit ApiKeyAuthenticator(std::string expectedKey);
    bool authenticate(std::string_view authorization) const noexcept;

private:
    static bool secureEquals(std::string_view left,
                             std::string_view right) noexcept;
    std::string expectedKey_;
};

} // namespace devmanager
