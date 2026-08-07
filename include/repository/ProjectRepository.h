#pragma once

#include "domain/Project.h"
#include "query/ProjectQuery.h"

#include <cstdint>
#include <optional>
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
    virtual void create(const Project& project, ProjectId nextIdAfterCreate) = 0;
    virtual void update(const Project& project) = 0;
    virtual void remove(ProjectId id) = 0;
    [[nodiscard]] virtual std::optional<Project> findById(ProjectId id) const = 0;
    [[nodiscard]] virtual std::vector<Project> query(const ProjectQuery& projectQuery) const = 0;
    [[nodiscard]] virtual std::uint64_t count(const ProjectQuery& projectQuery) const = 0;
};

}  // namespace devmanager
