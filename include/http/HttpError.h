#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace devmanager {

struct HttpError {
    int status;
    std::string code;
    std::string message;

    [[nodiscard]] nlohmann::json toJson() const;
};

}  // namespace devmanager
