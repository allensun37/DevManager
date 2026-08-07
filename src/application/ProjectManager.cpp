#include "application/ProjectManager.h"
#include "common/ProjectSearchText.h"
#include "repository/ProjectRepository.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace devmanager {

ProjectManager::ProjectManager(ProjectRepository& repository) : repository_(&repository) {
    const ProjectStore store = repository_->loadStore();
    projects_ = store.projects;
    nextId_ = store.nextId;
}

ProjectId ProjectManager::addProject(std::string name,
                                     std::vector<std::string> techStack,
                                     std::string description,
                                     std::string status) {
    if (nextId_ == std::numeric_limits<ProjectId>::max()) {
        throw std::overflow_error("Project ID space is exhausted");
    }

    const ProjectId id = nextId_;
    ProjectStore candidate{nextId_ + 1, projects_};
    candidate.projects.emplace_back(id,
                                    std::move(name),
                                    std::move(techStack),
                                    std::move(description),
                                    std::move(status));
    commitCandidate(std::move(candidate));
    return id;
}

bool ProjectManager::updateProject(ProjectId id,
                                   std::string name,
                                   std::vector<std::string> techStack,
                                   std::string description,
                                   std::string status) {
    const auto iterator = std::find_if(projects_.begin(), projects_.end(),
                                       [id](const Project& project) {
                                           return project.id() == id;
                                       });
    if (iterator == projects_.end()) {
        return false;
    }

    ProjectStore candidate{nextId_, projects_};
    const std::size_t index = static_cast<std::size_t>(std::distance(projects_.begin(), iterator));
    candidate.projects[index] = Project{id,
                                        std::move(name),
                                        std::move(techStack),
                                        std::move(description),
                                        std::move(status)};
    commitCandidate(std::move(candidate));
    return true;
}

bool ProjectManager::deleteProject(ProjectId id) {
    const auto iterator = std::find_if(projects_.begin(), projects_.end(),
                                       [id](const Project& project) {
                                           return project.id() == id;
                                       });
    if (iterator == projects_.end()) {
        return false;
    }

    ProjectStore candidate{nextId_, projects_};
    candidate.projects.erase(candidate.projects.begin() +
                             std::distance(projects_.begin(), iterator));
    commitCandidate(std::move(candidate));
    return true;
}

const std::vector<Project>& ProjectManager::listProjects() const noexcept {
    return projects_;
}

std::vector<Project> ProjectManager::searchByName(std::string_view query) const {
    const std::string normalizedQuery = project_search_text::normalizeName(query);
    if (normalizedQuery.empty()) {
        return {};
    }

    std::vector<Project> matches;
    for (const Project& project : projects_) {
        if (project_search_text::normalizeName(project.name()).find(normalizedQuery) !=
            std::string::npos) {
            matches.push_back(project);
        }
    }

    return matches;
}

std::vector<Project> ProjectManager::searchByTechnology(std::string_view query) const {
    const std::string normalizedQuery = project_search_text::normalizeTechnology(query);
    if (normalizedQuery.empty()) {
        return {};
    }

    std::vector<Project> matches;
    for (const Project& project : projects_) {
        const bool hasMatch = std::any_of(project.techStack().begin(), project.techStack().end(),
                                          [&normalizedQuery](const std::string& technology) {
                                              return project_search_text::normalizeTechnology(
                                                         technology)
                                                         .find(normalizedQuery) != std::string::npos;
                                          });
        if (hasMatch) {
            matches.push_back(project);
        }
    }

    return matches;
}

std::vector<Project> ProjectManager::filterByStatus(std::string_view status) const {
    const std::string normalizedStatus = project_search_text::normalizeStatus(status);
    if (normalizedStatus.empty()) {
        return {};
    }

    std::vector<Project> matches;
    for (const Project& project : projects_) {
        if (project_search_text::normalizeStatus(project.status()) == normalizedStatus) {
            matches.push_back(project);
        }
    }
    return matches;
}

std::vector<Project> ProjectManager::sortedProjects(ProjectSortKey key) const {
    std::vector<Project> sorted = projects_;
    std::sort(sorted.begin(), sorted.end(), [key](const Project& left, const Project& right) {
        if (key == ProjectSortKey::Id) {
            return left.id() < right.id();
        }

        const std::string leftValue = key == ProjectSortKey::Name
                                          ? project_search_text::nameSortKey(left.name())
                                          : project_search_text::statusSortKey(left.status());
        const std::string rightValue = key == ProjectSortKey::Name
                                           ? project_search_text::nameSortKey(right.name())
                                           : project_search_text::statusSortKey(right.status());
        if (leftValue == rightValue) {
            return left.id() < right.id();
        }
        return leftValue < rightValue;
    });
    return sorted;
}

void ProjectManager::commitCandidate(ProjectStore candidate) {
    if (repository_ != nullptr) {
        repository_->saveStore(candidate);
    }

    projects_ = std::move(candidate.projects);
    nextId_ = candidate.nextId;
}

}  // namespace devmanager
