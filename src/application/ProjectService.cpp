#include "application/ProjectService.h"

#include "common/AsciiText.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace devmanager {

ProjectService::ProjectService(ProjectManager& manager) : manager_(manager) {}

Project ProjectService::addProject(std::string name,
                                   std::vector<std::string> techStack,
                                   std::string description,
                                   std::string status) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ProjectId id = manager_.addProject(std::move(name),
                                             std::move(techStack),
                                             std::move(description),
                                             std::move(status));
    const std::vector<Project> projects = manager_.listProjects();
    const auto iterator = std::find_if(projects.begin(),
                                       projects.end(),
                                       [id](const Project& project) {
                                           return project.id() == id;
                                       });
    if (iterator == projects.end()) {
        throw std::logic_error("Created project is missing from manager state");
    }
    return *iterator;
}

std::optional<Project> ProjectService::updateProject(ProjectId id,
                                                     std::string name,
                                                     std::vector<std::string> techStack,
                                                     std::string description,
                                                     std::string status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manager_.updateProject(id,
                                std::move(name),
                                std::move(techStack),
                                std::move(description),
                                std::move(status))) {
        return std::nullopt;
    }

    const std::vector<Project> projects = manager_.listProjects();
    const auto iterator = std::find_if(projects.begin(),
                                       projects.end(),
                                       [id](const Project& project) {
                                           return project.id() == id;
                                       });
    if (iterator == projects.end()) {
        throw std::logic_error("Updated project is missing from manager state");
    }
    return *iterator;
}

bool ProjectService::deleteProject(ProjectId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return manager_.deleteProject(id);
}

std::vector<Project> ProjectService::listProjects() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return manager_.listProjects();
}

std::vector<Project> ProjectService::searchByName(std::string_view query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return manager_.searchByName(query);
}

std::vector<Project> ProjectService::searchByTechnology(std::string_view query) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return manager_.searchByTechnology(query);
}

std::vector<Project> ProjectService::filterByStatus(std::string_view status) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return manager_.filterByStatus(status);
}

std::vector<Project> ProjectService::sortedProjects(ProjectSortKey key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return manager_.sortedProjects(key);
}

std::vector<Project> ProjectService::sortProjects(std::vector<Project> projects,
                                                  ProjectSortKey key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::sort(projects.begin(), projects.end(), [key](const Project& left, const Project& right) {
        if (key == ProjectSortKey::Id) {
            return left.id() < right.id();
        }

        const std::string leftValue = key == ProjectSortKey::Name
                                          ? ascii::toLower(left.name())
                                          : ascii::toLower(left.status());
        const std::string rightValue = key == ProjectSortKey::Name
                                           ? ascii::toLower(right.name())
                                           : ascii::toLower(right.status());
        if (leftValue == rightValue) {
            return left.id() < right.id();
        }
        return leftValue < rightValue;
    });
    return projects;
}

ProjectStatistics ProjectService::statistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    ProjectStatistics result;
    const std::vector<Project>& projects = manager_.listProjects();
    result.totalProjects = projects.size();

    for (const Project& project : projects) {
        const std::string status = ascii::toLower(ascii::trim(project.status()));
        if (!status.empty()) {
            ++result.status[status];
        }

        std::set<std::string> projectTechnologies;
        for (const std::string& technology : project.techStack()) {
            const std::string normalized = ascii::toLower(ascii::trim(technology));
            if (!normalized.empty()) {
                projectTechnologies.insert(normalized);
            }
        }
        for (const std::string& technology : projectTechnologies) {
            ++result.technology[technology];
        }
    }

    return result;
}

}  // namespace devmanager
