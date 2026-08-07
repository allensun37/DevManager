#include "repository/ProjectStoreValidator.h"

#include "repository/ProjectRepository.h"

#include <stdexcept>
#include <unordered_set>

namespace devmanager {

void validateProjectStore(const ProjectStore& store) {
    if (store.nextId == 0) {
        throw std::invalid_argument("nextId must be greater than zero");
    }

    std::unordered_set<ProjectId> projectIds;
    ProjectId maximumId = 0;
    for (const Project& project : store.projects) {
        const ProjectId id = project.id();
        if (id == 0) {
            throw std::invalid_argument("Project IDs must be greater than zero");
        }
        if (!projectIds.insert(id).second) {
            throw std::invalid_argument("Project IDs must be unique");
        }
        if (id > maximumId) {
            maximumId = id;
        }
    }

    if (!store.projects.empty() && store.nextId <= maximumId) {
        throw std::invalid_argument("nextId must exceed every project ID");
    }
}

}  // namespace devmanager
