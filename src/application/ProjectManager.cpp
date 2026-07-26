#include "application/ProjectManager.h"

#include <algorithm>
#include <string>
#include <utility>

namespace {

std::string normalizeTextForSearch(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());

    for (const unsigned char character : value) {
        if (character >= 'A' && character <= 'Z') {
            normalized.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            normalized.push_back(static_cast<char>(character));
        }
    }

    return normalized;
}

bool isAsciiLetterOrDigit(unsigned char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9');
}

std::string normalizeTechnologyForSearch(std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());

    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character == '+' && index + 1 < value.size() && value[index + 1] == '+') {
            normalized += "pp";
            ++index;
            continue;
        }

        if (character < 128 && !isAsciiLetterOrDigit(character)) {
            continue;
        }

        if (character >= 'A' && character <= 'Z') {
            normalized.push_back(static_cast<char>(character - 'A' + 'a'));
        } else {
            normalized.push_back(static_cast<char>(character));
        }
    }

    return normalized;
}

}  // namespace

namespace devmanager {

ProjectId ProjectManager::addProject(std::string name,
                                     std::vector<std::string> techStack,
                                     std::string description,
                                     std::string status) {
    const ProjectId id = nextId_;
    projects_.emplace_back(id,
                           std::move(name),
                           std::move(techStack),
                           std::move(description),
                           std::move(status));
    ++nextId_;
    return id;
}

bool ProjectManager::deleteProject(ProjectId id) {
    const auto iterator = std::find_if(projects_.begin(), projects_.end(),
                                       [id](const Project& project) {
                                           return project.id() == id;
                                       });
    if (iterator == projects_.end()) {
        return false;
    }

    projects_.erase(iterator);
    return true;
}

const std::vector<Project>& ProjectManager::listProjects() const noexcept {
    return projects_;
}

std::vector<Project> ProjectManager::searchByName(std::string_view query) const {
    const std::string normalizedQuery = normalizeTextForSearch(query);
    if (normalizedQuery.empty()) {
        return {};
    }

    std::vector<Project> matches;
    for (const Project& project : projects_) {
        if (normalizeTextForSearch(project.name()).find(normalizedQuery) != std::string::npos) {
            matches.push_back(project);
        }
    }

    return matches;
}

std::vector<Project> ProjectManager::searchByTechnology(std::string_view query) const {
    const std::string normalizedQuery = normalizeTechnologyForSearch(query);
    if (normalizedQuery.empty()) {
        return {};
    }

    std::vector<Project> matches;
    for (const Project& project : projects_) {
        const bool hasMatch = std::any_of(project.techStack().begin(), project.techStack().end(),
                                          [&normalizedQuery](const std::string& technology) {
                                              return normalizeTechnologyForSearch(technology)
                                                         .find(normalizedQuery) != std::string::npos;
                                          });
        if (hasMatch) {
            matches.push_back(project);
        }
    }

    return matches;
}

}  // namespace devmanager
