#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace devmanager {

using ProjectId = std::uint64_t;

class Project {
public:
    Project(ProjectId id,
            std::string name,
            std::vector<std::string> techStack,
            std::string description,
            std::string status);

    [[nodiscard]] ProjectId id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::vector<std::string>& techStack() const noexcept;
    [[nodiscard]] const std::string& description() const noexcept;
    [[nodiscard]] const std::string& status() const noexcept;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Project fromJson(const nlohmann::json& payload);

private:
    ProjectId id_;
    std::string name_;
    std::vector<std::string> techStack_;
    std::string description_;
    std::string status_;
};

}  // namespace devmanager
