#pragma once

#include "domain/Project.h"
#include "query/ProjectQuery.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace devmanager {

class ProjectRepository;
struct ProjectStore;

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
    [[nodiscard]] std::vector<Project> queryProjects(const ProjectQuery& projectQuery) const;
    [[nodiscard]] std::uint64_t countProjects(const ProjectQuery& projectQuery) const;

private:
    void commitCreated(ProjectStore candidate, const Project& created);
    void commitUpdated(ProjectStore candidate, const Project& updated);
    void commitRemoved(ProjectStore candidate, ProjectId removedId);

    std::vector<Project> projects_;
    ProjectId nextId_ {1};
    ProjectRepository* repository_ {nullptr};
};

}  // namespace devmanager
