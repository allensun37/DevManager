#pragma once

#include "domain/Project.h"

#include <string>
#include <string_view>
#include <vector>

namespace devmanager {

class ProjectRepository;
struct ProjectStore;

enum class ProjectSortKey {
    Id,
    Name,
    Status,
};

class ProjectManager {
public:
    ProjectManager() = default;
    explicit ProjectManager(ProjectRepository& repository);

    [[nodiscard]] ProjectId addProject(std::string name,
                                       std::vector<std::string> techStack,
                                       std::string description,
                                       std::string status);

    [[nodiscard]] bool updateProject(ProjectId id,
                                     std::string name,
                                     std::vector<std::string> techStack,
                                     std::string description,
                                     std::string status);
    [[nodiscard]] bool deleteProject(ProjectId id);
    [[nodiscard]] const std::vector<Project>& listProjects() const noexcept;
    [[nodiscard]] std::vector<Project> searchByName(std::string_view query) const;
    [[nodiscard]] std::vector<Project> searchByTechnology(std::string_view query) const;
    [[nodiscard]] std::vector<Project> filterByStatus(std::string_view status) const;
    [[nodiscard]] std::vector<Project> sortedProjects(ProjectSortKey key) const;

private:
    void commitCandidate(ProjectStore candidate);

    std::vector<Project> projects_;
    ProjectId nextId_ {1};
    ProjectRepository* repository_ {nullptr};
};

}  // namespace devmanager
