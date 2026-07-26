#pragma once

#include "domain/Project.h"

#include <vector>

namespace devmanager {

struct ProjectStore {
    ProjectId nextId {1};
    std::vector<Project> projects;
};

class ProjectRepository {
public:
    virtual ~ProjectRepository() = default;

    [[nodiscard]] virtual ProjectStore load() const = 0;
    virtual void save(const ProjectStore& store) const = 0;
};

}  // namespace devmanager
