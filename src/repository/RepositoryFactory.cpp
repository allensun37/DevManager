#include "repository/RepositoryFactory.h"

#include "EmbeddedMigrations.h"
#include "infrastructure/sqlite/MigrationManager.h"
#include "infrastructure/sqlite/SqliteConnection.h"
#include "repository/JsonProjectRepository.h"
#include "repository/ProjectRepository.h"
#include "repository/SqliteProjectRepository.h"

#include <stdexcept>
#include <utility>

namespace devmanager {

std::unique_ptr<ProjectRepository> RepositoryFactory::create(
    const StorageConfig& storage) {
    switch (storage.type) {
    case StorageType::Json:
        return std::make_unique<JsonProjectRepository>(storage.path);
    case StorageType::Sqlite: {
        auto connection = std::make_unique<SqliteConnection>(storage.path);
        MigrationManager(*connection).migrate(kEmbeddedMigrations);
        return std::make_unique<SqliteProjectRepository>(std::move(connection));
    }
    default:
        throw std::runtime_error("unsupported storage backend");
    }
}

}  // namespace devmanager
