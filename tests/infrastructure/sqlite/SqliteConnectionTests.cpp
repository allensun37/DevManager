#include "infrastructure/sqlite/SqliteConnection.h"
#include "infrastructure/sqlite/SqliteStatement.h"
#include "infrastructure/sqlite/SqliteTransaction.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using devmanager::SqliteConnection;
using devmanager::SqliteStatement;
using devmanager::SqliteTransaction;

static_assert(!std::is_copy_constructible_v<SqliteConnection>);
static_assert(!std::is_copy_assignable_v<SqliteConnection>);
static_assert(!std::is_copy_constructible_v<SqliteStatement>);
static_assert(!std::is_copy_assignable_v<SqliteStatement>);
static_assert(std::is_nothrow_move_constructible_v<SqliteStatement>);
static_assert(std::is_nothrow_move_assignable_v<SqliteStatement>);
static_assert(!std::is_copy_constructible_v<SqliteTransaction>);
static_assert(!std::is_copy_assignable_v<SqliteTransaction>);
static_assert(!std::is_move_constructible_v<SqliteTransaction>);
static_assert(!std::is_move_assignable_v<SqliteTransaction>);

TEST(SqliteConnectionTest, EnablesForeignKeysWhenOpening) {
    SqliteConnection connection(":memory:");

    auto statement = connection.prepare("PRAGMA foreign_keys");
    ASSERT_TRUE(statement.stepRow());
    EXPECT_EQ(statement.columnInt64(0), 1);
    EXPECT_FALSE(statement.stepRow());
}

TEST(SqliteConnectionTest, EnforcesForeignKeys) {
    SqliteConnection connection(":memory:");
    connection.execute("CREATE TABLE parent (id INTEGER PRIMARY KEY)");
    connection.execute(
        "CREATE TABLE child (parent_id INTEGER NOT NULL REFERENCES parent(id))");

    EXPECT_THROW(connection.execute("INSERT INTO child(parent_id) VALUES (42)"),
                 std::runtime_error);
}

TEST(SqliteConnectionTest, ExecuteAndPreparedTextStoreLiteralData) {
    SqliteConnection connection(":memory:");
    connection.execute("CREATE TABLE notes (id INTEGER PRIMARY KEY, body TEXT NOT NULL)");
    connection.execute("INSERT INTO notes(body) VALUES ('plain')");
    EXPECT_EQ(connection.changes(), 1);

    std::string expected = "quoted ' text ); DROP TABLE notes; --";
    expected.push_back('\0');
    expected.append("tail");

    auto insert = connection.prepare("INSERT INTO notes(body) VALUES (?1)");
    {
        std::string source = expected;
        source.append("ignored suffix");
        insert.bindText(1, std::string_view(source.data(), expected.size()));
        std::fill(source.begin(), source.end(), 'x');
    }
    insert.executeDone();
    EXPECT_EQ(connection.changes(), 1);

    auto select = connection.prepare("SELECT body FROM notes WHERE id = 2");
    ASSERT_TRUE(select.stepRow());
    EXPECT_EQ(select.columnText(0), expected);
    EXPECT_FALSE(select.stepRow());

    auto count = connection.prepare("SELECT COUNT(*) FROM notes");
    ASSERT_TRUE(count.stepRow());
    EXPECT_EQ(count.columnInt64(0), 2);
}

TEST(SqliteStatementTest, BindInt64AndStepRowReadAllRowsUntilDone) {
    SqliteConnection connection(":memory:");
    connection.execute("CREATE TABLE values_table (number INTEGER NOT NULL, label TEXT NOT NULL)");

    auto firstInsert = connection.prepare(
        "INSERT INTO values_table(number, label) VALUES (?1, ?2)");
    firstInsert.bindInt64(1, 17);
    firstInsert.bindText(2, "seventeen");
    firstInsert.executeDone();

    auto secondInsert = connection.prepare(
        "INSERT INTO values_table(number, label) VALUES (?1, ?2)");
    secondInsert.bindInt64(1, 42);
    secondInsert.bindText(2, "forty-two");
    secondInsert.executeDone();

    auto select = connection.prepare(
        "SELECT number, label FROM values_table ORDER BY number");
    ASSERT_TRUE(select.stepRow());
    EXPECT_EQ(select.columnInt64(0), 17);
    EXPECT_EQ(select.columnText(1), "seventeen");
    ASSERT_TRUE(select.stepRow());
    EXPECT_EQ(select.columnInt64(0), 42);
    EXPECT_EQ(select.columnText(1), "forty-two");
    EXPECT_FALSE(select.stepRow());
}

TEST(SqliteStatementTest, BindsEmptyTextAsTextRatherThanNull) {
    SqliteConnection connection(":memory:");
    auto statement = connection.prepare("SELECT typeof(?1), ?1");
    statement.bindText(1, std::string_view {});

    ASSERT_TRUE(statement.stepRow());
    EXPECT_EQ(statement.columnText(0), "text");
    EXPECT_EQ(statement.columnText(1), "");
}

TEST(SqliteStatementTest, ExecuteDoneRejectsStatementsThatProduceRows) {
    SqliteConnection connection(":memory:");
    auto statement = connection.prepare("SELECT 1");

    EXPECT_THROW(statement.executeDone(), std::runtime_error);
}

TEST(SqliteStatementTest, RejectsNullTextInvalidColumnsAndColumnsWithoutCurrentRow) {
    SqliteConnection connection(":memory:");
    auto statement = connection.prepare("SELECT NULL, 7");

    EXPECT_THROW(static_cast<void>(statement.columnText(0)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(statement.columnInt64(1)), std::runtime_error);
    ASSERT_TRUE(statement.stepRow());
    EXPECT_THROW(static_cast<void>(statement.columnText(0)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(statement.columnText(-1)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(statement.columnInt64(2)), std::runtime_error);
    EXPECT_EQ(statement.columnInt64(1), 7);
}

TEST(SqliteStatementTest, MalformedSqlAndInvalidBindIndexesThrow) {
    SqliteConnection connection(":memory:");

    EXPECT_THROW(static_cast<void>(connection.prepare("SELEC broken syntax")),
                 std::runtime_error);
    EXPECT_THROW(connection.execute("CREATE TABL broken syntax"), std::runtime_error);

    auto statement = connection.prepare("SELECT ?1");
    EXPECT_THROW(statement.bindText(0, "invalid"), std::runtime_error);
    EXPECT_THROW(statement.bindInt64(2, 9), std::runtime_error);
}

TEST(SqliteStatementTest, MoveTransfersOwnershipAndMovedFromDestructionIsSafe) {
    SqliteConnection connection(":memory:");
    auto original = connection.prepare("SELECT 23");
    auto moved = std::move(original);

    ASSERT_TRUE(moved.stepRow());
    EXPECT_EQ(moved.columnInt64(0), 23);
    EXPECT_THROW(static_cast<void>(original.stepRow()), std::runtime_error);

    auto assigned = connection.prepare("SELECT 99");
    assigned = std::move(moved);
    EXPECT_FALSE(assigned.stepRow());
    EXPECT_THROW(static_cast<void>(moved.stepRow()), std::runtime_error);
}

TEST(SqliteTransactionTest, DestructorRollsBackUncommittedWrites) {
    SqliteConnection connection(":memory:");
    connection.execute("CREATE TABLE entries (value INTEGER NOT NULL)");

    {
        SqliteTransaction transaction(connection);
        connection.execute("INSERT INTO entries(value) VALUES (1)");
    }

    auto count = connection.prepare("SELECT COUNT(*) FROM entries");
    ASSERT_TRUE(count.stepRow());
    EXPECT_EQ(count.columnInt64(0), 0);
}

TEST(SqliteTransactionTest, CommitPersistsWritesAndRejectsSecondCommit) {
    SqliteConnection connection(":memory:");
    connection.execute("CREATE TABLE entries (value INTEGER NOT NULL)");

    {
        SqliteTransaction transaction(connection);
        connection.execute("INSERT INTO entries(value) VALUES (1)");
        transaction.commit();
        EXPECT_THROW(transaction.commit(), std::runtime_error);
    }

    auto count = connection.prepare("SELECT COUNT(*) FROM entries");
    ASSERT_TRUE(count.stepRow());
    EXPECT_EQ(count.columnInt64(0), 1);
}

}  // namespace
