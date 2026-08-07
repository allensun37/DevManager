#include "EmbeddedMigrations.h"
#include "infrastructure/sqlite/MigrationManager.h"
#include "infrastructure/sqlite/SqliteConnection.h"
#include "infrastructure/sqlite/SqliteStatement.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

namespace {

using devmanager::Migration;
using devmanager::MigrationManager;
using devmanager::SqliteConnection;

TEST(MigrationManagerTest, AppliesMigrationOnFirstRun) {
    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);

    manager.migrate({Migration {1, "create_items", "CREATE TABLE items(id INTEGER)"}});

    auto table = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'items'");
    ASSERT_TRUE(table.stepRow());
    EXPECT_EQ(table.columnInt64(0), 1);

    auto applied = connection.prepare("SELECT COUNT(*) FROM schema_migrations");
    ASSERT_TRUE(applied.stepRow());
    EXPECT_EQ(applied.columnInt64(0), 1);
}

TEST(MigrationManagerTest, DoesNotApplyTheSameMigrationTwice) {
    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);
    const std::vector<Migration> migrations {
        Migration {1, "create_items", "CREATE TABLE items(id INTEGER)"},
    };

    manager.migrate(migrations);
    EXPECT_NO_THROW(manager.migrate(migrations));

    auto applied = connection.prepare("SELECT COUNT(*) FROM schema_migrations");
    ASSERT_TRUE(applied.stepRow());
    EXPECT_EQ(applied.columnInt64(0), 1);
}

TEST(MigrationManagerTest, AppliesUnorderedMigrationsInAscendingVersionOrder) {
    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);

    manager.migrate({
        Migration {2, "seed_items", "INSERT INTO items(id) VALUES (7)"},
        Migration {1, "create_items", "CREATE TABLE items(id INTEGER)"},
    });

    auto item = connection.prepare("SELECT id FROM items");
    ASSERT_TRUE(item.stepRow());
    EXPECT_EQ(item.columnInt64(0), 7);

    auto applied = connection.prepare("SELECT version FROM schema_migrations ORDER BY rowid");
    ASSERT_TRUE(applied.stepRow());
    EXPECT_EQ(applied.columnInt64(0), 1);
    ASSERT_TRUE(applied.stepRow());
    EXPECT_EQ(applied.columnInt64(0), 2);
}

TEST(MigrationManagerTest, RejectsDuplicateVersionsBeforeChangingTheDatabase) {
    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);

    EXPECT_THROW(
        manager.migrate({
            Migration {1, "first", "CREATE TABLE first_table(id INTEGER)"},
            Migration {1, "duplicate", "CREATE TABLE duplicate_table(id INTEGER)"},
        }),
        std::invalid_argument);

    auto tableCount = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type = 'table' "
        "AND name IN ('first_table', 'duplicate_table', 'schema_migrations')");
    ASSERT_TRUE(tableCount.stepRow());
    EXPECT_EQ(tableCount.columnInt64(0), 0);
}

TEST(MigrationManagerTest, RejectsNonPositiveVersionsBeforeChangingTheDatabase) {
    for (const int version : {0, -1}) {
        SqliteConnection connection(":memory:");
        MigrationManager manager(connection);

        EXPECT_THROW(
            manager.migrate({Migration {version, "invalid", "CREATE TABLE invalid(id INTEGER)"}}),
            std::invalid_argument);

        auto tableCount = connection.prepare(
            "SELECT COUNT(*) FROM sqlite_master "
            "WHERE type = 'table' AND name IN ('invalid', 'schema_migrations')");
        ASSERT_TRUE(tableCount.stepRow());
        EXPECT_EQ(tableCount.columnInt64(0), 0);
    }
}

TEST(MigrationManagerTest, RejectsAppliedVersionsMissingFromEmbeddedMigrations) {
    SqliteConnection connection(":memory:");
    connection.execute(
        "CREATE TABLE schema_migrations("
        "version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL)");
    connection.execute(
        "INSERT INTO schema_migrations(version, name, applied_at) "
        "VALUES (99, 'unknown', '2026-01-01T00:00:00.000Z')");
    MigrationManager manager(connection);

    EXPECT_THROW(
        manager.migrate({Migration {1, "known", "CREATE TABLE known(id INTEGER)"}}),
        std::runtime_error);

    auto knownTable = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'known'");
    ASSERT_TRUE(knownTable.stepRow());
    EXPECT_EQ(knownTable.columnInt64(0), 0);
}

TEST(MigrationManagerTest, PropagatesMigrationSqlErrorsAsInitializationFailure) {
    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);

    EXPECT_THROW(
        manager.migrate({Migration {1, "broken", "CREATE TABL broken(id INTEGER)"}}),
        std::runtime_error);
}

TEST(MigrationManagerTest, RollsBackMetadataAndPartialSchemaWhenFirstMigrationFails) {
    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);

    EXPECT_THROW(
        manager.migrate({Migration {
            1,
            "partial_then_fail",
            "CREATE TABLE partial(id INTEGER);"
            "INSERT INTO missing_table(id) VALUES (1);",
        }}),
        std::runtime_error);

    auto tableCount = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type = 'table' AND name IN ('partial', 'schema_migrations')");
    ASSERT_TRUE(tableCount.stepRow());
    EXPECT_EQ(tableCount.columnInt64(0), 0);
}

TEST(MigrationManagerTest, StopsAfterFailureAndKeepsEarlierCommittedMigration) {
    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);

    EXPECT_THROW(
        manager.migrate({
            Migration {1, "first", "CREATE TABLE first_table(id INTEGER)"},
            Migration {
                2,
                "failing",
                "CREATE TABLE rolled_back(id INTEGER);"
                "INSERT INTO missing_table(id) VALUES (1);",
            },
            Migration {3, "later", "CREATE TABLE later_table(id INTEGER)"},
        }),
        std::runtime_error);

    auto tableCount = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type = 'table' AND name IN ('first_table', 'rolled_back', 'later_table')");
    ASSERT_TRUE(tableCount.stepRow());
    EXPECT_EQ(tableCount.columnInt64(0), 1);

    auto applied = connection.prepare("SELECT version FROM schema_migrations");
    ASSERT_TRUE(applied.stepRow());
    EXPECT_EQ(applied.columnInt64(0), 1);
    EXPECT_FALSE(applied.stepRow());
}

TEST(MigrationManagerTest, RecordsVersionNameAndUtcApplicationTimestamp) {
    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);

    manager.migrate({Migration {7, "named_migration", "CREATE TABLE named(id INTEGER)"}});

    auto applied = connection.prepare(
        "SELECT version, name, applied_at, "
        "applied_at LIKE '____-__-__T__:__:__.___Z' "
        "FROM schema_migrations");
    ASSERT_TRUE(applied.stepRow());
    EXPECT_EQ(applied.columnInt64(0), 7);
    EXPECT_EQ(applied.columnText(1), "named_migration");
    EXPECT_EQ(applied.columnInt64(3), 1);
    EXPECT_FALSE(applied.columnText(2).empty());
    EXPECT_FALSE(applied.stepRow());
}

TEST(MigrationManagerTest, EmbeddedInitialMigrationCreatesAndSeedsRequiredSchema) {
    ASSERT_EQ(devmanager::kEmbeddedMigrations.size(), 1U);
    EXPECT_EQ(devmanager::kEmbeddedMigrations.front().version, 1);
    EXPECT_EQ(devmanager::kEmbeddedMigrations.front().name, "init");

    SqliteConnection connection(":memory:");
    MigrationManager manager(connection);
    manager.migrate(devmanager::kEmbeddedMigrations);

    auto tables = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type = 'table' "
        "AND name IN ('schema_migrations', 'repository_state', 'projects', 'project_tags')");
    ASSERT_TRUE(tables.stepRow());
    EXPECT_EQ(tables.columnInt64(0), 4);

    auto indexes = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type = 'index' "
        "AND name IN ('idx_projects_normalized_status', "
        "'idx_project_tags_normalized_tag', 'idx_project_tags_project_id')");
    ASSERT_TRUE(indexes.stepRow());
    EXPECT_EQ(indexes.columnInt64(0), 3);

    auto state = connection.prepare("SELECT singleton, next_id FROM repository_state");
    ASSERT_TRUE(state.stepRow());
    EXPECT_EQ(state.columnInt64(0), 1);
    EXPECT_EQ(state.columnText(1), "00000000000000000001");
    EXPECT_FALSE(state.stepRow());

    connection.execute(
        "INSERT INTO projects("
        "id, name, normalized_name, description, status, normalized_status, status_sort_key) "
        "VALUES ('00000000000000000001', 'Name', 'name', '', 'Active', 'active', 'active')");
    connection.execute(
        "INSERT INTO project_tags(project_id, position, tag, normalized_tag) "
        "VALUES ('00000000000000000001', 0, 'C++', 'c++')");
    connection.execute("DELETE FROM projects WHERE id = '00000000000000000001'");

    auto tags = connection.prepare("SELECT COUNT(*) FROM project_tags");
    ASSERT_TRUE(tags.stepRow());
    EXPECT_EQ(tags.columnInt64(0), 0);

    EXPECT_THROW(
        connection.execute(
            "UPDATE repository_state SET next_id = '00000000000000000000' "
            "WHERE singleton = 1"),
        std::runtime_error);
    EXPECT_THROW(
        connection.execute(
            "INSERT INTO projects("
            "id, name, normalized_name, description, status, normalized_status, status_sort_key) "
            "VALUES ('18446744073709551615', 'Too large', 'too large', '', "
            "'Active', 'active', 'active')"),
        std::runtime_error);
    EXPECT_THROW(
        connection.execute(
            "INSERT INTO projects("
            "id, name, normalized_name, description, status, normalized_status, status_sort_key) "
            "VALUES (NULL, 'Null id', 'null id', '', 'Active', 'active', 'active')"),
        std::runtime_error);

    auto projects = connection.prepare("SELECT COUNT(*) FROM projects");
    ASSERT_TRUE(projects.stepRow());
    EXPECT_EQ(projects.columnInt64(0), 0);
}

}  // namespace
