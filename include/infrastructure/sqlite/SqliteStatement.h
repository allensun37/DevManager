#pragma once

#include <cstdint>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace devmanager {

class SqliteConnection;

class SqliteStatement final {
public:
    ~SqliteStatement() noexcept;

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;
    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;

    void bindText(int index, std::string_view value);
    void bindInt64(int index, std::int64_t value);
    [[nodiscard]] bool stepRow();
    void executeDone();
    [[nodiscard]] std::string columnText(int index) const;
    [[nodiscard]] std::int64_t columnInt64(int index) const;

private:
    friend class SqliteConnection;

    SqliteStatement(sqlite3* connection, sqlite3_stmt* statement) noexcept;

    void requireUsable() const;
    void requireBindable() const;
    void requireCurrentColumn(int index) const;

    sqlite3* connection_ {nullptr};
    sqlite3_stmt* statement_ {nullptr};
    bool started_ {false};
    bool rowAvailable_ {false};
    bool finished_ {false};
};

}  // namespace devmanager
