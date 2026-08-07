#pragma once

#include "domain/Project.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace devmanager {

enum class ProjectSortKey {
    Id,
    Name,
    Status,
};

struct ProjectQuery {
    std::optional<std::string> name;
    std::optional<std::string> status;
    std::optional<std::string> technology;
    ProjectSortKey sort {ProjectSortKey::Id};
    std::uint64_t offset {0};
    std::uint64_t limit {0};
};

struct PagedProjects {
    std::vector<Project> items;
    std::uint64_t total {0};
    std::uint64_t page {1};
    std::uint64_t size {20};
};

}  // namespace devmanager
