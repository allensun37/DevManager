#include "config/Config.h"
#include "infrastructure/sqlite/SqliteConnection.h"
#include "infrastructure/sqlite/SqliteStatement.h"
#include "repository/JsonProjectRepository.h"
#include "repository/RepositoryFactory.h"
#include "repository/SqliteProjectRepository.h"

#include "EmbeddedMigrations.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace {

std::filesystem::path makeUniqueTestDirectory() {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("devmanager-factory-tests-" + std::to_string(timestamp) + "-" +
         std::to_string(counter++));
    std::filesystem::create_directories(directory);
    return directory;
}

class RepositoryFactoryTest : public testing::Test {
protected:
    void SetUp() override { directory = makeUniqueTestDirectory(); }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) {
            ADD_FAILURE() << "Failed to remove test directory: " << error.message();
        }
    }

    std::filesystem::path directory;
};

TEST_F(RepositoryFactoryTest, CreatesJsonRepositoryWithoutCreatingSqliteDatabase) {
    const auto jsonPath = directory / "projects.json";
    const auto sqlitePath = directory / "devmanager.db";
    devmanager::StorageConfig config{devmanager::StorageType::Json, jsonPath};

    auto repository = devmanager::RepositoryFactory::create(config);

    ASSERT_NE(dynamic_cast<devmanager::JsonProjectRepository*>(repository.get()), nullptr);
    EXPECT_FALSE(std::filesystem::exists(sqlitePath));
}

TEST_F(RepositoryFactoryTest, CreatesAndMigratesSqliteRepository) {
    const auto sqlitePath = directory / "devmanager.db";
    devmanager::StorageConfig config{devmanager::StorageType::Sqlite, sqlitePath};

    auto repository = devmanager::RepositoryFactory::create(config);

    ASSERT_NE(dynamic_cast<devmanager::SqliteProjectRepository*>(repository.get()), nullptr);
    ASSERT_TRUE(std::filesystem::exists(sqlitePath));
    EXPECT_EQ(repository->loadStore().nextId, devmanager::ProjectId{1});

    devmanager::SqliteConnection connection(sqlitePath);
    auto statement = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'schema_migrations'");
    ASSERT_TRUE(statement.stepRow());
    EXPECT_EQ(statement.columnInt64(0), 1);
    auto migration = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'projects'");
    ASSERT_TRUE(migration.stepRow());
    EXPECT_EQ(migration.columnInt64(0), 1);
}

TEST_F(RepositoryFactoryTest, PropagatesMigrationFailure) {
    const auto sqlitePath = directory / "broken.db";
    {
        devmanager::SqliteConnection connection(sqlitePath);
        connection.execute(
            "CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL)");
        connection.execute(
            "INSERT INTO schema_migrations(version, name, applied_at) VALUES (99, 'unknown', 'now')");
    }

    devmanager::StorageConfig config{devmanager::StorageType::Sqlite, sqlitePath};
    EXPECT_THROW(static_cast<void>(devmanager::RepositoryFactory::create(config)), std::runtime_error);
}

TEST_F(RepositoryFactoryTest, RejectsInvalidStorageType) {
    devmanager::StorageConfig config{static_cast<devmanager::StorageType>(99),
                                     directory / "invalid.db"};

    EXPECT_THROW(static_cast<void>(devmanager::RepositoryFactory::create(config)), std::runtime_error);
}

}  // namespace
