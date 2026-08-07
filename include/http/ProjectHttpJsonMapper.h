#pragma once

#include "domain/Project.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace devmanager {

struct ProjectHttpInput {
    std::string name;
    std::vector<std::string> techStack;
    std::string description;
    std::string status;
};

class ProjectHttpJsonMapper final {
public:
    [[nodiscard]] static nlohmann::json toJson(const Project& project);
    [[nodiscard]] static ProjectHttpInput parseInput(const nlohmann::json& payload);
};

}  // namespace devmanager
