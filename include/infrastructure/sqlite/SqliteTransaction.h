#pragma once

namespace devmanager {

class SqliteConnection;

class SqliteTransaction final {
public:
    explicit SqliteTransaction(SqliteConnection& connection);
    ~SqliteTransaction() noexcept;

    SqliteTransaction(const SqliteTransaction&) = delete;
    SqliteTransaction& operator=(const SqliteTransaction&) = delete;
    SqliteTransaction(SqliteTransaction&&) = delete;
    SqliteTransaction& operator=(SqliteTransaction&&) = delete;

    void commit();

private:
    SqliteConnection& connection_;
    bool active_ {true};
};

}  // namespace devmanager
