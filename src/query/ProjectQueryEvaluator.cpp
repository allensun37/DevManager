#include "query/ProjectQueryEvaluator.h"

#include "common/ProjectSearchText.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

namespace {

using devmanager::Project;
using devmanager::ProjectQuery;
using devmanager::ProjectSortKey;

bool matchesName(const Project& project, const ProjectQuery& query) {
    if (!query.name.has_value()) {
        return true;
    }

    const std::string normalizedQuery =
        devmanager::project_search_text::normalizeName(*query.name);
    return !normalizedQuery.empty() &&
           devmanager::project_search_text::normalizeName(project.name())
                   .find(normalizedQuery) != std::string::npos;
}

bool matchesStatus(const Project& project, const ProjectQuery& query) {
    if (!query.status.has_value()) {
        return true;
    }

    const std::string normalizedQuery =
        devmanager::project_search_text::normalizeStatus(*query.status);
    return !normalizedQuery.empty() &&
           devmanager::project_search_text::normalizeStatus(project.status()) == normalizedQuery;
}

bool matchesTechnology(const Project& project, const ProjectQuery& query) {
    if (!query.technology.has_value()) {
        return true;
    }

    const std::string normalizedQuery =
        devmanager::project_search_text::normalizeTechnology(*query.technology);
    if (normalizedQuery.empty()) {
        return false;
    }

    return std::any_of(project.techStack().begin(), project.techStack().end(),
                       [&normalizedQuery](const std::string& technology) {
                           return devmanager::project_search_text::normalizeTechnology(technology)
                                      .find(normalizedQuery) != std::string::npos;
                       });
}

bool matches(const Project& project, const ProjectQuery& query) {
    return matchesName(project, query) && matchesStatus(project, query) &&
           matchesTechnology(project, query);
}

std::string sortKey(const Project& project, ProjectSortKey key) {
    if (key == ProjectSortKey::Name) {
        return devmanager::project_search_text::nameSortKey(project.name());
    }
    return devmanager::project_search_text::statusSortKey(project.status());
}

bool projectLess(const Project& left, const Project& right, ProjectSortKey key) {
    if (key == ProjectSortKey::Id) {
        return left.id() < right.id();
    }

    const std::string leftKey = sortKey(left, key);
    const std::string rightKey = sortKey(right, key);
    if (leftKey == rightKey) {
        return left.id() < right.id();
    }
    return leftKey < rightKey;
}

std::optional<std::size_t> toSize(std::uint64_t value) {
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            return std::nullopt;
        }
    }
    return static_cast<std::size_t>(value);
}

}  // namespace

namespace devmanager {

std::vector<Project> ProjectQueryEvaluator::query(const std::vector<Project>& projects,
                                                  const ProjectQuery& projectQuery) {
    std::vector<Project> matchesProjects;
    std::copy_if(projects.begin(), projects.end(), std::back_inserter(matchesProjects),
                 [&projectQuery](const Project& project) {
                     return matches(project, projectQuery);
                 });

    std::sort(matchesProjects.begin(), matchesProjects.end(),
              [&projectQuery](const Project& left, const Project& right) {
                  return projectLess(left, right, projectQuery.sort);
              });

    if (projectQuery.limit == 0) {
        return matchesProjects;
    }

    const std::optional<std::size_t> offset = toSize(projectQuery.offset);
    if (!offset.has_value() || *offset >= matchesProjects.size()) {
        return {};
    }

    const std::size_t remaining = matchesProjects.size() - *offset;
    const std::optional<std::size_t> requestedLimit = toSize(projectQuery.limit);
    const std::size_t pageSize = requestedLimit.has_value()
                                     ? std::min(*requestedLimit, remaining)
                                     : remaining;
    const auto first = matchesProjects.begin() + static_cast<std::ptrdiff_t>(*offset);
    const auto last = first + static_cast<std::ptrdiff_t>(pageSize);
    return {first, last};
}

std::uint64_t ProjectQueryEvaluator::count(const std::vector<Project>& projects,
                                           const ProjectQuery& projectQuery) {
    std::uint64_t total = 0;
    for (const Project& project : projects) {
        if (matches(project, projectQuery)) {
            ++total;
        }
    }
    return total;
}

}  // namespace devmanager
