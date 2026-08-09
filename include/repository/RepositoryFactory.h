#pragma once

#include "config/Config.h"
#include "repository/ProjectRepository.h"

#include <memory>

namespace devmanager {

class RepositoryFactory final {
public:
    [[nodiscard]] static std::unique_ptr<ProjectRepository> create(
        const StorageConfig& storage);
};

}  // namespace devmanager
