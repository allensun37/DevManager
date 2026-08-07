#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

struct sqlite3;

namespace devmanager {

class SqliteStatement;

class SqliteConnection final {
public:
    explicit SqliteConnection(const std::filesystem::path& path);
    ~SqliteConnection() noexcept;

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    void execute(std::string_view sql);
    [[nodiscard]] SqliteStatement prepare(std::string_view sql);
    [[nodiscard]] sqlite3* nativeHandle() noexcept;
    [[nodiscard]] std::int64_t changes() const noexcept;

private:
    sqlite3* handle_ {nullptr};
};

}  // namespace devmanager
