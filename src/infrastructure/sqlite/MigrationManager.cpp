#include "infrastructure/sqlite/MigrationManager.h"

#include "infrastructure/sqlite/SqliteConnection.h"
#include "infrastructure/sqlite/SqliteStatement.h"
#include "infrastructure/sqlite/SqliteTransaction.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>

namespace devmanager {
namespace {

bool migrationTableExists(SqliteConnection& connection) {
    auto statement = connection.prepare(
        "SELECT COUNT(*) FROM sqlite_master "
        "WHERE type = 'table' AND name = 'schema_migrations'");
    return statement.stepRow() && statement.columnInt64(0) == 1;
}

std::set<std::int64_t> readAppliedVersions(SqliteConnection& connection) {
    std::set<std::int64_t> versions;
    if (!migrationTableExists(connection)) {
        return versions;
    }

    auto statement = connection.prepare("SELECT version FROM schema_migrations");
    while (statement.stepRow()) {
        versions.insert(statement.columnInt64(0));
    }
    return versions;
}

}  // namespace

MigrationManager::MigrationManager(SqliteConnection& connection)
    : connection_(connection) {}

void MigrationManager::migrate(const std::vector<Migration>& migrations) {
    std::set<std::int64_t> embeddedVersions;
    for (const auto& migration : migrations) {
        if (migration.version <= 0) {
            throw std::invalid_argument(
                "migration version must be positive: " +
                std::to_string(migration.version));
        }
        if (!embeddedVersions.insert(migration.version).second) {
            throw std::invalid_argument(
                "duplicate migration version: " + std::to_string(migration.version));
        }
    }

    auto sortedMigrations = migrations;
    std::sort(
        sortedMigrations.begin(),
        sortedMigrations.end(),
        [](const Migration& left, const Migration& right) {
            return left.version < right.version;
        });

    const auto appliedVersions = readAppliedVersions(connection_);
    for (const auto version : appliedVersions) {
        if (embeddedVersions.count(version) == 0) {
            throw std::runtime_error(
                "database contains unknown migration version: " +
                std::to_string(version));
        }
    }

    for (const auto& migration : sortedMigrations) {
        if (appliedVersions.count(migration.version) != 0) {
            continue;
        }

        SqliteTransaction transaction(connection_);
        connection_.execute(
            "CREATE TABLE IF NOT EXISTS schema_migrations("
            "version INTEGER PRIMARY KEY,"
            "name TEXT NOT NULL,"
            "applied_at TEXT NOT NULL)");
        connection_.execute(migration.sql);

        auto insert = connection_.prepare(
            "INSERT INTO schema_migrations(version, name, applied_at) "
            "VALUES (?1, ?2, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))");
        insert.bindInt64(1, migration.version);
        insert.bindText(2, migration.name);
        insert.executeDone();
        transaction.commit();
    }
}

}  // namespace devmanager
