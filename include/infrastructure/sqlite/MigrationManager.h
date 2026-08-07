#pragma once

#include <string>
#include <vector>

namespace devmanager {

class SqliteConnection;

struct Migration {
    int version;
    std::string name;
    std::string sql;
};

class MigrationManager final {
public:
    explicit MigrationManager(SqliteConnection& connection);

    void migrate(const std::vector<Migration>& migrations);

private:
    SqliteConnection& connection_;
};

}  // namespace devmanager
