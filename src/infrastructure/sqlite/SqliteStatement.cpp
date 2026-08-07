#include "infrastructure/sqlite/SqliteStatement.h"

#include <sqlite3.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace devmanager {
namespace {

[[noreturn]] void throwSqliteError(
    std::string_view operation,
    sqlite3* connection,
    int resultCode) {
    const char* message = connection != nullptr
        ? sqlite3_errmsg(connection)
        : sqlite3_errstr(resultCode);
    throw std::runtime_error(std::string(operation) + ": " + message);
}

int checkedTextLength(std::string_view value) {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("failed to bind SQLite text: value is too large");
    }
    return static_cast<int>(value.size());
}

}  // namespace

SqliteStatement::SqliteStatement(sqlite3* connection, sqlite3_stmt* statement) noexcept
    : connection_(connection), statement_(statement) {}

SqliteStatement::~SqliteStatement() noexcept {
    if (statement_ != nullptr) {
        static_cast<void>(sqlite3_finalize(statement_));
    }
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : connection_(std::exchange(other.connection_, nullptr)),
      statement_(std::exchange(other.statement_, nullptr)),
      started_(std::exchange(other.started_, false)),
      rowAvailable_(std::exchange(other.rowAvailable_, false)),
      finished_(std::exchange(other.finished_, false)) {}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    if (statement_ != nullptr) {
        static_cast<void>(sqlite3_finalize(statement_));
    }
    connection_ = std::exchange(other.connection_, nullptr);
    statement_ = std::exchange(other.statement_, nullptr);
    started_ = std::exchange(other.started_, false);
    rowAvailable_ = std::exchange(other.rowAvailable_, false);
    finished_ = std::exchange(other.finished_, false);
    return *this;
}

void SqliteStatement::bindText(int index, std::string_view value) {
    requireBindable();
    const char* text = value.empty() ? "" : value.data();
    const int result = sqlite3_bind_text(
        statement_, index, text, checkedTextLength(value), SQLITE_TRANSIENT);
    if (result != SQLITE_OK) {
        throwSqliteError("failed to bind SQLite text", connection_, result);
    }
}

void SqliteStatement::bindInt64(int index, std::int64_t value) {
    requireBindable();
    const int result = sqlite3_bind_int64(
        statement_, index, static_cast<sqlite3_int64>(value));
    if (result != SQLITE_OK) {
        throwSqliteError("failed to bind SQLite integer", connection_, result);
    }
}

bool SqliteStatement::stepRow() {
    requireUsable();
    if (finished_) {
        throw std::runtime_error("failed to step SQLite statement: statement is finished");
    }

    started_ = true;
    const int result = sqlite3_step(statement_);
    if (result == SQLITE_ROW) {
        rowAvailable_ = true;
        return true;
    }

    rowAvailable_ = false;
    finished_ = true;
    if (result == SQLITE_DONE) {
        return false;
    }
    throwSqliteError("failed to step SQLite statement", connection_, result);
}

void SqliteStatement::executeDone() {
    requireUsable();
    if (finished_) {
        throw std::runtime_error("failed to execute SQLite statement: statement is finished");
    }

    started_ = true;
    const int result = sqlite3_step(statement_);
    if (result == SQLITE_DONE) {
        rowAvailable_ = false;
        finished_ = true;
        return;
    }
    if (result == SQLITE_ROW) {
        rowAvailable_ = true;
        throw std::runtime_error(
            "failed to execute SQLite statement: statement produced a row");
    }

    rowAvailable_ = false;
    finished_ = true;
    throwSqliteError("failed to execute SQLite statement", connection_, result);
}

std::string SqliteStatement::columnText(int index) const {
    requireCurrentColumn(index);
    if (sqlite3_column_type(statement_, index) == SQLITE_NULL) {
        throw std::runtime_error("failed to read SQLite text column: value is NULL");
    }

    const unsigned char* text = sqlite3_column_text(statement_, index);
    if (text == nullptr) {
        throwSqliteError(
            "failed to read SQLite text column", connection_, sqlite3_errcode(connection_));
    }
    const int byteCount = sqlite3_column_bytes(statement_, index);
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(byteCount));
}

std::int64_t SqliteStatement::columnInt64(int index) const {
    requireCurrentColumn(index);
    if (sqlite3_column_type(statement_, index) == SQLITE_NULL) {
        throw std::runtime_error("failed to read SQLite integer column: value is NULL");
    }
    return static_cast<std::int64_t>(sqlite3_column_int64(statement_, index));
}

void SqliteStatement::requireUsable() const {
    if (statement_ == nullptr || connection_ == nullptr) {
        throw std::runtime_error("SQLite statement is not usable");
    }
}

void SqliteStatement::requireBindable() const {
    requireUsable();
    if (started_ || finished_) {
        throw std::runtime_error("failed to bind SQLite value: statement has already started");
    }
}

void SqliteStatement::requireCurrentColumn(int index) const {
    requireUsable();
    if (!rowAvailable_) {
        throw std::runtime_error("failed to read SQLite column: no current row");
    }
    const int columnCount = sqlite3_column_count(statement_);
    if (index < 0 || index >= columnCount) {
        throw std::runtime_error("failed to read SQLite column: index is out of range");
    }
}

}  // namespace devmanager
