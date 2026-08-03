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

    [[nodiscard]] virtual ProjectStore loadStore() const = 0;
    virtual void saveStore(const ProjectStore& store) const = 0;
};

}  // namespace devmanager
