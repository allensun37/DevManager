#pragma once

#include "domain/Project.h"

#include <string>
#include <string_view>
#include <vector>

namespace devmanager {

class ProjectRepository;

class ProjectManager {
public:
    ProjectManager() = default;
    explicit ProjectManager(ProjectRepository& repository);

    [[nodiscard]] ProjectId addProject(std::string name,
                                       std::vector<std::string> techStack,
                                       std::string description,
                                       std::string status);

    [[nodiscard]] bool deleteProject(ProjectId id);
    [[nodiscard]] const std::vector<Project>& listProjects() const noexcept;
    [[nodiscard]] std::vector<Project> searchByName(std::string_view query) const;
    [[nodiscard]] std::vector<Project> searchByTechnology(std::string_view query) const;

private:
    void saveCurrentState() const;

    std::vector<Project> projects_;
    ProjectId nextId_ {1};
    ProjectRepository* repository_ {nullptr};
};

}  // namespace devmanager
