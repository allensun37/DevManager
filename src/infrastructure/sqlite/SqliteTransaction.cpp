#include "infrastructure/sqlite/SqliteTransaction.h"

#include "infrastructure/sqlite/SqliteConnection.h"

#include <stdexcept>

namespace devmanager {

SqliteTransaction::SqliteTransaction(SqliteConnection& connection)
    : connection_(connection) {
    connection_.execute("BEGIN IMMEDIATE");
}

SqliteTransaction::~SqliteTransaction() noexcept {
    if (!active_) {
        return;
    }

    try {
        connection_.execute("ROLLBACK");
    } catch (...) {
    }
}

void SqliteTransaction::commit() {
    if (!active_) {
        throw std::runtime_error("failed to commit SQLite transaction: transaction is inactive");
    }

    connection_.execute("COMMIT");
    active_ = false;
}

}  // namespace devmanager
