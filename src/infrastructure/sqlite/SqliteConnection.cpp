#include "infrastructure/sqlite/SqliteConnection.h"

#include "infrastructure/sqlite/SqliteStatement.h"

#include <sqlite3.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace devmanager {
namespace {

std::string sqliteMessage(sqlite3* connection, int resultCode) {
    if (connection != nullptr) {
        return sqlite3_errmsg(connection);
    }
    return sqlite3_errstr(resultCode);
}

[[noreturn]] void throwSqliteError(
    std::string_view operation,
    sqlite3* connection,
    int resultCode) {
    throw std::runtime_error(
        std::string(operation) + ": " + sqliteMessage(connection, resultCode));
}

int checkedSqlLength(std::string_view sql) {
    if (sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("failed to prepare SQLite statement: SQL is too large");
    }
    return static_cast<int>(sql.size());
}

}  // namespace

SqliteConnection::SqliteConnection(const std::filesystem::path& path) {
    const std::string databasePath = path.u8string();
    sqlite3* openedConnection = nullptr;
    const int openResult = sqlite3_open_v2(
        databasePath.c_str(),
        &openedConnection,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
        nullptr);
    if (openResult != SQLITE_OK) {
        const std::string message = sqliteMessage(openedConnection, openResult);
        if (openedConnection != nullptr) {
            static_cast<void>(sqlite3_close_v2(openedConnection));
        }
        throw std::runtime_error("failed to open SQLite database: " + message);
    }

    handle_ = openedConnection;
    try {
        execute("PRAGMA foreign_keys = ON");
        auto foreignKeys = prepare("PRAGMA foreign_keys");
        if (!foreignKeys.stepRow() || foreignKeys.columnInt64(0) != 1 ||
            foreignKeys.stepRow()) {
            throw std::runtime_error("failed to enable SQLite foreign key enforcement");
        }
    } catch (...) {
        static_cast<void>(sqlite3_close_v2(handle_));
        handle_ = nullptr;
        throw;
    }
}

SqliteConnection::~SqliteConnection() noexcept {
    if (handle_ != nullptr) {
        static_cast<void>(sqlite3_close_v2(handle_));
    }
}

void SqliteConnection::execute(std::string_view sql) {
    const std::string sqlText(sql);
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(handle_, sqlText.c_str(), nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        const std::string message = errorMessage != nullptr
            ? std::string(errorMessage)
            : sqliteMessage(handle_, result);
        sqlite3_free(errorMessage);
        throw std::runtime_error("failed to execute SQLite statement: " + message);
    }
    sqlite3_free(errorMessage);
}

SqliteStatement SqliteConnection::prepare(std::string_view sql) {
    sqlite3_stmt* statement = nullptr;
    const int result = sqlite3_prepare_v2(
        handle_, sql.data(), checkedSqlLength(sql), &statement, nullptr);
    if (result != SQLITE_OK) {
        throwSqliteError("failed to prepare SQLite statement", handle_, result);
    }
    if (statement == nullptr) {
        throw std::runtime_error("failed to prepare SQLite statement: SQL produced no statement");
    }
    return SqliteStatement(handle_, statement);
}

sqlite3* SqliteConnection::nativeHandle() noexcept {
    return handle_;
}

std::int64_t SqliteConnection::changes() const noexcept {
    return static_cast<std::int64_t>(sqlite3_changes64(handle_));
}

}  // namespace devmanager
