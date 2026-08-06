#pragma once

#include "application/ProjectManager.h"

#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devmanager {

struct ProjectStatistics {
    std::size_t totalProjects {0};
    std::map<std::string, std::size_t> status;
    std::map<std::string, std::size_t> technology;
};

class ProjectService final {
public:
    explicit ProjectService(ProjectManager& manager);

    [[nodiscard]] Project addProject(std::string name,
                                     std::vector<std::string> techStack,
                                     std::string description,
                                     std::string status);
    [[nodiscard]] std::optional<Project> updateProject(ProjectId id,
                                                       std::string name,
                                                       std::vector<std::string> techStack,
                                                       std::string description,
                                                       std::string status);
    [[nodiscard]] bool deleteProject(ProjectId id);

    [[nodiscard]] std::vector<Project> listProjects() const;
    [[nodiscard]] std::vector<Project> searchByName(std::string_view query) const;
    [[nodiscard]] std::vector<Project> searchByTechnology(std::string_view query) const;
    [[nodiscard]] std::vector<Project> filterByStatus(std::string_view status) const;
    [[nodiscard]] std::vector<Project> sortedProjects(ProjectSortKey key) const;
    [[nodiscard]] std::vector<Project> sortProjects(std::vector<Project> projects,
                                                    ProjectSortKey key) const;
    [[nodiscard]] ProjectStatistics statistics() const;

private:
    ProjectManager& manager_;
    mutable std::mutex mutex_;
};

}  // namespace devmanager
