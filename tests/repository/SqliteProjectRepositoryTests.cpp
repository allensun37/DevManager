#include "EmbeddedMigrations.h"
#include "common/ProjectSearchText.h"
#include "infrastructure/sqlite/MigrationManager.h"
#include "infrastructure/sqlite/SqliteConnection.h"
#include "infrastructure/sqlite/SqliteStatement.h"
#include "repository/SqliteProjectIdCodec.h"
#include "repository/SqliteProjectRepository.h"

#include <gtest/gtest.h>
#include <sqlite3.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using devmanager::Project;
using devmanager::ProjectId;
using devmanager::ProjectQuery;
using devmanager::ProjectSortKey;
using devmanager::ProjectStore;
using devmanager::SqliteConnection;
using devmanager::SqliteProjectIdCodec;
using devmanager::SqliteProjectRepository;

class TemporaryDatabaseFile final {
public:
    TemporaryDatabaseFile() {
        static std::atomic_uint64_t counter {0};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("devmanager-sqlite-repository-tests-" + std::to_string(timestamp) + "-" +
                      std::to_string(counter++));
        std::filesystem::create_directories(directory_);
        path_ = directory_ / "projects.sqlite3";
    }

    ~TemporaryDatabaseFile() noexcept {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    TemporaryDatabaseFile(const TemporaryDatabaseFile&) = delete;
    TemporaryDatabaseFile& operator=(const TemporaryDatabaseFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    [[nodiscard]] std::error_code cleanup() noexcept {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
        return error;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

Project makeProject(ProjectId id,
                    std::string name = "Project",
                    std::vector<std::string> tags = {"C++"},
                    std::string description = "Description",
                    std::string status = "Active") {
    return Project{id,
                   std::move(name),
                   std::move(tags),
                   std::move(description),
                   std::move(status)};
}

void expectProjectsEqual(const Project& actual, const Project& expected) {
    EXPECT_EQ(actual.id(), expected.id());
    EXPECT_EQ(actual.name(), expected.name());
    EXPECT_EQ(actual.techStack(), expected.techStack());
    EXPECT_EQ(actual.description(), expected.description());
    EXPECT_EQ(actual.status(), expected.status());
}

void expectStoresEqual(const ProjectStore& actual, const ProjectStore& expected) {
    EXPECT_EQ(actual.nextId, expected.nextId);
    ASSERT_EQ(actual.projects.size(), expected.projects.size());
    for (std::size_t index = 0; index < expected.projects.size(); ++index) {
        expectProjectsEqual(actual.projects[index], expected.projects[index]);
    }
}

std::vector<ProjectId> projectIds(const std::vector<Project>& projects) {
    std::vector<ProjectId> ids;
    ids.reserve(projects.size());
    for (const Project& project : projects) {
        ids.push_back(project.id());
    }
    return ids;
}

std::int64_t scalarInt(SqliteConnection& connection, std::string_view sql) {
    auto statement = connection.prepare(sql);
    if (!statement.stepRow()) {
        throw std::runtime_error("expected scalar query to return one row");
    }
    const std::int64_t value = statement.columnInt64(0);
    if (statement.stepRow()) {
        throw std::runtime_error("expected scalar query to return exactly one row");
    }
    return value;
}

std::string scalarText(SqliteConnection& connection, std::string_view sql) {
    auto statement = connection.prepare(sql);
    if (!statement.stepRow()) {
        throw std::runtime_error("expected scalar query to return one row");
    }
    const std::string value = statement.columnText(0);
    if (statement.stepRow()) {
        throw std::runtime_error("expected scalar query to return exactly one row");
    }
    return value;
}

void insertRawProject(SqliteConnection& connection, std::string_view encodedId) {
    auto project = connection.prepare(
        "INSERT INTO projects("
        "id, name, normalized_name, description, status, normalized_status, status_sort_key) "
        "VALUES (?1, 'Raw', 'raw', 'Raw description', 'Active', 'active', 'active')");
    project.bindText(1, encodedId);
    project.executeDone();
}

void insertRawTag(SqliteConnection& connection,
                  std::string_view encodedId,
                  std::int64_t position,
                  std::string_view tag) {
    auto statement = connection.prepare(
        "INSERT INTO project_tags(project_id, position, tag, normalized_tag) "
        "VALUES (?1, ?2, ?3, ?4)");
    statement.bindText(1, encodedId);
    statement.bindInt64(2, position);
    statement.bindText(3, tag);
    statement.bindText(4, devmanager::project_search_text::normalizeTechnology(tag));
    statement.executeDone();
}

enum class MalformedTagPositionStorageClass {
    Text,
    Real,
};

void replaceTagsWithMalformedPosition(SqliteConnection& connection,
                                      ProjectId projectId,
                                      MalformedTagPositionStorageClass storageClass) {
    connection.execute("DROP TABLE project_tags");
    connection.execute(
        "CREATE TABLE project_tags("
        "project_id TEXT NOT NULL,position,tag TEXT NOT NULL,normalized_tag TEXT NOT NULL)");
    auto insert = connection.prepare(
        storageClass == MalformedTagPositionStorageClass::Text
            ? "INSERT INTO project_tags(project_id,position,tag,normalized_tag) "
              "VALUES (?1,'not-an-integer','C++','cpp')"
            : "INSERT INTO project_tags(project_id,position,tag,normalized_tag) "
              "VALUES (?1,0.5,'C++','cpp')");
    insert.bindText(1, SqliteProjectIdCodec::encode(projectId));
    insert.executeDone();
}

class StatementPause final {
public:
    StatementPause(SqliteConnection& connection, std::string sqlFragment)
        : handle_(connection.nativeHandle()), sqlFragment_(std::move(sqlFragment)) {
        const int result = sqlite3_trace_v2(
            handle_, SQLITE_TRACE_STMT, &StatementPause::trace, this);
        if (result != SQLITE_OK) {
            throw std::runtime_error("failed to install SQLite statement trace");
        }
    }

    ~StatementPause() noexcept {
        release();
        static_cast<void>(sqlite3_trace_v2(handle_, 0, nullptr, nullptr));
    }

    StatementPause(const StatementPause&) = delete;
    StatementPause& operator=(const StatementPause&) = delete;

    [[nodiscard]] bool waitUntilPaused() {
        std::unique_lock<std::mutex> lock(mutex_);
        return condition_.wait_for(lock, std::chrono::seconds(5), [this] {
            return paused_;
        });
    }

    void release() noexcept {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            released_ = true;
        }
        condition_.notify_all();
    }

private:
    static int trace(unsigned int traceType,
                     void* context,
                     void* statement,
                     void*) noexcept {
        if (traceType != SQLITE_TRACE_STMT) {
            return 0;
        }

        auto& pause = *static_cast<StatementPause*>(context);
        const char* sql = sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
        if (sql == nullptr || std::string_view(sql).find(pause.sqlFragment_) ==
                                  std::string_view::npos) {
            return 0;
        }

        std::unique_lock<std::mutex> lock(pause.mutex_);
        pause.paused_ = true;
        pause.condition_.notify_all();
        pause.condition_.wait(lock, [&pause] {
            return pause.released_;
        });
        return 0;
    }

    sqlite3* handle_;
    std::string sqlFragment_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool paused_ {false};
    bool released_ {false};
};

class SqliteTraceCapture final {
public:
    explicit SqliteTraceCapture(SqliteConnection& connection)
        : handle_(connection.nativeHandle()) {
        const int result = sqlite3_trace_v2(handle_, SQLITE_TRACE_STMT,
                                            &SqliteTraceCapture::trace, this);
        if (result != SQLITE_OK) {
            throw std::runtime_error("failed to install SQLite statement trace");
        }
    }

    ~SqliteTraceCapture() noexcept {
        static_cast<void>(sqlite3_trace_v2(handle_, 0, nullptr, nullptr));
    }

    SqliteTraceCapture(const SqliteTraceCapture&) = delete;
    SqliteTraceCapture& operator=(const SqliteTraceCapture&) = delete;

    [[nodiscard]] std::vector<std::string> statements() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return statements_;
    }

private:
    static int trace(unsigned int traceType,
                     void* context,
                     void* statement,
                     void*) noexcept {
        if (traceType != SQLITE_TRACE_STMT) {
            return 0;
        }
        const char* sql = sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
        if (sql == nullptr) {
            return 0;
        }
        auto& capture = *static_cast<SqliteTraceCapture*>(context);
        const std::lock_guard<std::mutex> lock(capture.mutex_);
        capture.statements_.emplace_back(sql);
        return 0;
    }

    sqlite3* handle_;
    mutable std::mutex mutex_;
    std::vector<std::string> statements_;
};

class SqliteProjectRepositoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        openRepository();
    }

    void TearDown() override {
        repository_.reset();
        rawConnection_ = nullptr;
        const std::error_code cleanupError = database_.cleanup();
        EXPECT_FALSE(cleanupError) << cleanupError.message();
    }

    void openRepository() {
        auto connection = std::make_unique<SqliteConnection>(database_.path());
        devmanager::MigrationManager(*connection).migrate(devmanager::kEmbeddedMigrations);
        rawConnection_ = connection.get();
        repository_ = std::make_unique<SqliteProjectRepository>(std::move(connection));
    }

    void reopenRepository() {
        repository_.reset();
        rawConnection_ = nullptr;
        openRepository();
    }

    [[nodiscard]] ProjectStore loadStore() const {
        return repository_->loadStore();
    }

    TemporaryDatabaseFile database_;
    SqliteConnection* rawConnection_ {nullptr};
    std::unique_ptr<SqliteProjectRepository> repository_;
};

TEST(SqliteProjectRepositoryConstructorTest, RejectsNullConnection) {
    EXPECT_THROW(SqliteProjectRepository(nullptr), std::invalid_argument);
}

TEST_F(SqliteProjectRepositoryTest, LoadsEmptyMigratedStoreWithInitialNextId) {
    const ProjectStore store = loadStore();

    EXPECT_TRUE(store.projects.empty());
    EXPECT_EQ(store.nextId, 1U);
}

TEST_F(SqliteProjectRepositoryTest, CreatePersistsEveryFieldTagsAndNextIdAcrossReopen) {
    const Project expected = makeProject(
        1,
        "SQLite ' Project",
        {"C++", "Rust", "C++"},
        "Description with ); DROP TABLE projects; --",
        " In Progress ");

    repository_->create(expected, 2);

    ProjectStore stored = loadStore();
    ASSERT_EQ(stored.projects.size(), 1U);
    EXPECT_EQ(stored.nextId, 2U);
    expectProjectsEqual(stored.projects.front(), expected);
    const std::optional<Project> found = repository_->findById(1);
    ASSERT_TRUE(found.has_value());
    expectProjectsEqual(*found, expected);

    reopenRepository();
    stored = loadStore();
    ASSERT_EQ(stored.projects.size(), 1U);
    EXPECT_EQ(stored.nextId, 2U);
    expectProjectsEqual(stored.projects.front(), expected);
    const std::optional<Project> foundAfterReopen = repository_->findById(1);
    ASSERT_TRUE(foundAfterReopen.has_value());
    expectProjectsEqual(*foundAfterReopen, expected);
}

TEST_F(SqliteProjectRepositoryTest, CreateStoresSharedNormalizedSearchColumns) {
    const Project project = makeProject(
        1, "Mixed CASE", {"C++ / CMake"}, "Description", " In Progress ");

    repository_->create(project, 2);

    auto storedProject = rawConnection_->prepare(
        "SELECT normalized_name, normalized_status, status_sort_key FROM projects");
    ASSERT_TRUE(storedProject.stepRow());
    EXPECT_EQ(storedProject.columnText(0),
              devmanager::project_search_text::normalizeName(project.name()));
    EXPECT_EQ(storedProject.columnText(1),
              devmanager::project_search_text::normalizeStatus(project.status()));
    EXPECT_EQ(storedProject.columnText(2),
              devmanager::project_search_text::statusSortKey(project.status()));
    EXPECT_FALSE(storedProject.stepRow());
    EXPECT_EQ(scalarText(*rawConnection_, "SELECT normalized_tag FROM project_tags"),
              devmanager::project_search_text::normalizeTechnology(project.techStack().front()));
}

TEST_F(SqliteProjectRepositoryTest, UpdateReplacesEveryFieldAndPreservesDuplicateTagOrder) {
    repository_->create(makeProject(1, "Old", {"C", "Rust"}, "Old description", "Planned"),
                        2);
    const Project expected = makeProject(
        1,
        "New name",
        {"Rust", "C++", "Rust", "Go"},
        "New description",
        " Complete ");

    repository_->update(expected);

    const ProjectStore stored = loadStore();
    ASSERT_EQ(stored.projects.size(), 1U);
    EXPECT_EQ(stored.nextId, 2U);
    expectProjectsEqual(stored.projects.front(), expected);
    reopenRepository();
    const std::optional<Project> found = repository_->findById(1);
    ASSERT_TRUE(found.has_value());
    expectProjectsEqual(*found, expected);
}

TEST_F(SqliteProjectRepositoryTest, RemoveCascadesTagsAndKeepsNextIdAfterDeletingLastProject) {
    repository_->create(makeProject(1, "One", {"C++", "Rust", "C++"}), 2);
    ASSERT_EQ(scalarInt(*rawConnection_, "SELECT COUNT(*) FROM project_tags"), 3);

    repository_->remove(1);

    EXPECT_EQ(scalarInt(*rawConnection_, "SELECT COUNT(*) FROM project_tags"), 0);
    EXPECT_EQ(scalarInt(*rawConnection_, "SELECT COUNT(*) FROM projects"), 0);
    const ProjectStore stored = loadStore();
    EXPECT_TRUE(stored.projects.empty());
    EXPECT_EQ(stored.nextId, 2U);
    reopenRepository();
    EXPECT_TRUE(loadStore().projects.empty());
    EXPECT_EQ(loadStore().nextId, 2U);
}

TEST_F(SqliteProjectRepositoryTest, FindByIdReturnsNulloptWhenProjectDoesNotExist) {
    EXPECT_EQ(repository_->findById(42), std::nullopt);
}

TEST_F(SqliteProjectRepositoryTest, UpdateMissingProjectThrowsWithoutChangingState) {
    repository_->create(makeProject(1), 2);
    const ProjectStore before = loadStore();

    EXPECT_THROW(repository_->update(makeProject(2, "Missing")), std::runtime_error);

    expectStoresEqual(loadStore(), before);
}

TEST_F(SqliteProjectRepositoryTest, RemoveMissingProjectThrowsWithoutChangingState) {
    repository_->create(makeProject(1), 2);
    const ProjectStore before = loadStore();

    EXPECT_THROW(repository_->remove(2), std::runtime_error);

    expectStoresEqual(loadStore(), before);
}

TEST_F(SqliteProjectRepositoryTest, PersistsMaximumAllowedProjectAndNextIdsAcrossReopen) {
    constexpr ProjectId maximumProjectId = std::numeric_limits<ProjectId>::max() - 1;
    constexpr ProjectId maximumNextId = std::numeric_limits<ProjectId>::max();
    const Project expected = makeProject(maximumProjectId, "Boundary", {"C++", "C++"});

    repository_->create(expected, maximumNextId);
    reopenRepository();

    const ProjectStore stored = loadStore();
    ASSERT_EQ(stored.projects.size(), 1U);
    EXPECT_EQ(stored.nextId, maximumNextId);
    expectProjectsEqual(stored.projects.front(), expected);
}

TEST_F(SqliteProjectRepositoryTest, RejectsInvalidProjectAndNextIdsWithoutChangingState) {
    const ProjectStore empty = loadStore();
    const std::vector<std::pair<Project, ProjectId>> invalidCreates {
        {makeProject(0, "Zero"), 1},
        {makeProject(std::numeric_limits<ProjectId>::max(), "Maximum"),
         std::numeric_limits<ProjectId>::max()},
        {makeProject(1, "Zero next"), 0},
        {makeProject(1, "Non-forward next"), 1},
    };

    for (const auto& invalidCreate : invalidCreates) {
        SCOPED_TRACE(invalidCreate.first.name());
        EXPECT_THROW(repository_->create(invalidCreate.first, invalidCreate.second),
                     std::runtime_error);
        expectStoresEqual(loadStore(), empty);
    }
}

TEST_F(SqliteProjectRepositoryTest, RejectsNextIdThatDoesNotExceedExistingMaximum) {
    constexpr ProjectId maximumProjectId = std::numeric_limits<ProjectId>::max() - 1;
    repository_->create(makeProject(maximumProjectId, "Boundary"),
                        std::numeric_limits<ProjectId>::max());
    const ProjectStore before = loadStore();

    EXPECT_THROW(repository_->create(makeProject(1, "Invalid rewind"), 2),
                 std::runtime_error);

    expectStoresEqual(loadStore(), before);
}

TEST_F(SqliteProjectRepositoryTest, MissingRepositoryStateSingletonMakesLoadFail) {
    rawConnection_->execute("DELETE FROM repository_state WHERE singleton = 1");

    EXPECT_THROW(static_cast<void>(repository_->loadStore()), std::runtime_error);
}

TEST_F(SqliteProjectRepositoryTest, ReadSnapshotFailureRollsBackStartedTransaction) {
    rawConnection_->execute("DROP TABLE repository_state");

    EXPECT_THROW(static_cast<void>(repository_->loadStore()), std::runtime_error);

    rawConnection_->execute(
        "CREATE TABLE repository_state(singleton INTEGER PRIMARY KEY, next_id TEXT NOT NULL)");
    rawConnection_->execute(
        "INSERT INTO repository_state(singleton,next_id) "
        "VALUES (1,'00000000000000000001')");
    EXPECT_NO_THROW(static_cast<void>(repository_->loadStore()));
}

TEST_F(SqliteProjectRepositoryTest, ExtraRepositoryStateRowMakesLoadFail) {
    rawConnection_->execute("PRAGMA ignore_check_constraints = ON");
    rawConnection_->execute(
        "INSERT INTO repository_state(singleton,next_id) "
        "VALUES (2,'00000000000000000002')");
    rawConnection_->execute("PRAGMA ignore_check_constraints = OFF");
    ASSERT_EQ(scalarInt(*rawConnection_, "SELECT COUNT(*) FROM repository_state"), 2);

    EXPECT_THROW(static_cast<void>(repository_->loadStore()), std::runtime_error);
}

TEST_F(SqliteProjectRepositoryTest, MalformedPersistedProjectIdMakesLoadFail) {
    rawConnection_->execute("PRAGMA ignore_check_constraints = ON");
    insertRawProject(*rawConnection_, "malformed-id");
    rawConnection_->execute("PRAGMA ignore_check_constraints = OFF");

    EXPECT_THROW(static_cast<void>(repository_->loadStore()), std::runtime_error);
}

TEST_F(SqliteProjectRepositoryTest, MalformedPersistedNextIdMakesLoadFail) {
    rawConnection_->execute("PRAGMA ignore_check_constraints = ON");
    rawConnection_->execute(
        "UPDATE repository_state SET next_id = 'malformed-next-id' WHERE singleton = 1");
    rawConnection_->execute("PRAGMA ignore_check_constraints = OFF");

    EXPECT_THROW(static_cast<void>(repository_->loadStore()), std::runtime_error);
}

TEST_F(SqliteProjectRepositoryTest, SemanticallyInvalidSnapshotMakesLoadFail) {
    const std::string encodedId = SqliteProjectIdCodec::encode(2);
    insertRawProject(*rawConnection_, encodedId);
    insertRawTag(*rawConnection_, encodedId, 0, "C++");

    EXPECT_THROW(static_cast<void>(repository_->loadStore()), std::runtime_error);
}

TEST_F(SqliteProjectRepositoryTest, LoadStoreRejectsTextAndRealTagPositions) {
    repository_->create(makeProject(1), 2);

    for (const MalformedTagPositionStorageClass storageClass : {
             MalformedTagPositionStorageClass::Text,
             MalformedTagPositionStorageClass::Real,
         }) {
        SCOPED_TRACE(storageClass == MalformedTagPositionStorageClass::Text ? "TEXT" : "REAL");
        replaceTagsWithMalformedPosition(*rawConnection_, 1, storageClass);
        ASSERT_EQ(scalarText(*rawConnection_, "SELECT typeof(position) FROM project_tags"),
                  storageClass == MalformedTagPositionStorageClass::Text ? "text" : "real");

        EXPECT_THROW(static_cast<void>(repository_->loadStore()), std::runtime_error);
    }
}

TEST_F(SqliteProjectRepositoryTest, FindByIdRejectsTextAndRealTagPositions) {
    repository_->create(makeProject(1), 2);

    for (const MalformedTagPositionStorageClass storageClass : {
             MalformedTagPositionStorageClass::Text,
             MalformedTagPositionStorageClass::Real,
         }) {
        SCOPED_TRACE(storageClass == MalformedTagPositionStorageClass::Text ? "TEXT" : "REAL");
        replaceTagsWithMalformedPosition(*rawConnection_, 1, storageClass);
        ASSERT_EQ(scalarText(*rawConnection_, "SELECT typeof(position) FROM project_tags"),
                  storageClass == MalformedTagPositionStorageClass::Text ? "text" : "real");

        EXPECT_THROW(static_cast<void>(repository_->findById(1)), std::runtime_error);
    }
}

TEST_F(SqliteProjectRepositoryTest, LoadStoreUsesOneSnapshotAcrossStateAndProjects) {
    repository_->create(makeProject(1, "Original"), 2);
    ASSERT_EQ(scalarText(*rawConnection_, "PRAGMA journal_mode = WAL"), "wal");
    auto writerConnection = std::make_unique<SqliteConnection>(database_.path());
    devmanager::MigrationManager(*writerConnection).migrate(devmanager::kEmbeddedMigrations);
    SqliteProjectRepository writer(std::move(writerConnection));
    StatementPause pause(*rawConnection_,
                         "SELECT id,name,description,status FROM projects");
    std::optional<ProjectStore> readerStore;
    std::exception_ptr readerError;
    std::thread reader([this, &readerStore, &readerError] {
        try {
            readerStore = repository_->loadStore();
        } catch (...) {
            readerError = std::current_exception();
        }
    });

    const bool paused = pause.waitUntilPaused();
    std::exception_ptr writerError;
    if (paused) {
        try {
            writer.create(makeProject(2, "Concurrent"), 3);
        } catch (...) {
            writerError = std::current_exception();
        }
    }
    pause.release();
    reader.join();

    ASSERT_TRUE(paused) << "reader did not reach the controlled projects query";
    EXPECT_EQ(writerError, nullptr);
    EXPECT_EQ(readerError, nullptr);
    ASSERT_TRUE(readerStore.has_value());
    ASSERT_EQ(readerStore->projects.size(), 1U);
    EXPECT_EQ(readerStore->nextId, 2U);
    EXPECT_EQ(readerStore->projects.front().name(), "Original");
}

TEST_F(SqliteProjectRepositoryTest, LoadStoreUsesOneSnapshotAcrossProjectsAndTags) {
    const Project original = makeProject(1, "Original", {"Old", "Tags"});
    const Project updated = makeProject(1, "Updated", {"New", "Tags", "New"});
    repository_->create(original, 2);
    ASSERT_EQ(scalarText(*rawConnection_, "PRAGMA journal_mode = WAL"), "wal");
    auto writerConnection = std::make_unique<SqliteConnection>(database_.path());
    devmanager::MigrationManager(*writerConnection).migrate(devmanager::kEmbeddedMigrations);
    SqliteProjectRepository writer(std::move(writerConnection));
    StatementPause pause(*rawConnection_,
                         "SELECT project_id,position,tag");
    std::optional<ProjectStore> readerStore;
    std::exception_ptr readerError;
    std::thread reader([this, &readerStore, &readerError] {
        try {
            readerStore = repository_->loadStore();
        } catch (...) {
            readerError = std::current_exception();
        }
    });

    const bool paused = pause.waitUntilPaused();
    std::exception_ptr writerError;
    if (paused) {
        try {
            writer.update(updated);
        } catch (...) {
            writerError = std::current_exception();
        }
    }
    pause.release();
    reader.join();

    ASSERT_TRUE(paused) << "reader did not reach the controlled tags query";
    EXPECT_EQ(writerError, nullptr);
    EXPECT_EQ(readerError, nullptr);
    ASSERT_TRUE(readerStore.has_value());
    ASSERT_EQ(readerStore->projects.size(), 1U);
    EXPECT_EQ(readerStore->nextId, 2U);
    expectProjectsEqual(readerStore->projects.front(), original);
}

TEST_F(SqliteProjectRepositoryTest, FindByIdUsesOneSnapshotAcrossProjectAndTags) {
    const Project original = makeProject(1, "Original", {"Old", "Tags"});
    const Project updated = makeProject(1, "Updated", {"New", "Tags", "New"});
    repository_->create(original, 2);
    ASSERT_EQ(scalarText(*rawConnection_, "PRAGMA journal_mode = WAL"), "wal");
    auto writerConnection = std::make_unique<SqliteConnection>(database_.path());
    devmanager::MigrationManager(*writerConnection).migrate(devmanager::kEmbeddedMigrations);
    SqliteProjectRepository writer(std::move(writerConnection));
    StatementPause pause(*rawConnection_,
                         "SELECT id,name,description,status FROM projects");
    std::optional<Project> readerProject;
    std::exception_ptr readerError;
    std::thread reader([this, &readerProject, &readerError] {
        try {
            readerProject = repository_->findById(1);
        } catch (...) {
            readerError = std::current_exception();
        }
    });

    const bool paused = pause.waitUntilPaused();
    std::exception_ptr writerError;
    if (paused) {
        try {
            writer.update(updated);
        } catch (...) {
            writerError = std::current_exception();
        }
    }
    pause.release();
    reader.join();

    ASSERT_TRUE(paused) << "reader did not reach the controlled project query";
    EXPECT_EQ(writerError, nullptr);
    EXPECT_EQ(readerError, nullptr);
    ASSERT_TRUE(readerProject.has_value());
    expectProjectsEqual(*readerProject, original);
}

TEST_F(SqliteProjectRepositoryTest, CreateRollsBackProjectTagsAndNextIdWhenSecondTagFails) {
    repository_->create(makeProject(1, "Existing", {"C"}), 2);
    const ProjectStore before = loadStore();
    rawConnection_->execute(
        "CREATE TRIGGER fail_second_tag BEFORE INSERT ON project_tags "
        "WHEN NEW.position = 1 BEGIN SELECT RAISE(ABORT, 'second tag rejected'); END");

    EXPECT_THROW(repository_->create(makeProject(2, "New", {"Rust", "Go"}), 3),
                 std::runtime_error);

    expectStoresEqual(loadStore(), before);
    EXPECT_EQ(scalarInt(*rawConnection_, "SELECT COUNT(*) FROM projects"), 1);
    EXPECT_EQ(scalarInt(*rawConnection_, "SELECT COUNT(*) FROM project_tags"), 1);
    EXPECT_EQ(scalarText(*rawConnection_,
                         "SELECT next_id FROM repository_state WHERE singleton = 1"),
              SqliteProjectIdCodec::encode(2));
}

TEST_F(SqliteProjectRepositoryTest, UpdateRollsBackRowAndOldTagsWhenTagReplacementFails) {
    const Project original = makeProject(
        1, "Original", {"C++", "Rust", "C++"}, "Original description", "Active");
    repository_->create(original, 2);
    rawConnection_->execute(
        "CREATE TRIGGER fail_update_tag BEFORE INSERT ON project_tags "
        "WHEN NEW.tag = 'FAIL' BEGIN SELECT RAISE(ABORT, 'tag replacement rejected'); END");

    EXPECT_THROW(repository_->update(makeProject(
                     1, "Updated", {"Go", "FAIL"}, "Updated description", "Complete")),
                 std::runtime_error);

    const ProjectStore stored = loadStore();
    ASSERT_EQ(stored.projects.size(), 1U);
    EXPECT_EQ(stored.nextId, 2U);
    expectProjectsEqual(stored.projects.front(), original);
}

TEST_F(SqliteProjectRepositoryTest, RemoveRollsBackProjectTagsAndNextIdWhenDeleteFails) {
    const Project original = makeProject(1, "Original", {"C++", "Rust"});
    repository_->create(original, 2);
    const ProjectStore before = loadStore();
    rawConnection_->execute(
        "CREATE TRIGGER fail_project_delete BEFORE DELETE ON projects "
        "BEGIN SELECT RAISE(ABORT, 'delete rejected'); END");

    EXPECT_THROW(repository_->remove(1), std::runtime_error);

    expectStoresEqual(loadStore(), before);
    EXPECT_EQ(scalarInt(*rawConnection_, "SELECT COUNT(*) FROM project_tags"), 2);
    EXPECT_EQ(scalarText(*rawConnection_,
                         "SELECT next_id FROM repository_state WHERE singleton = 1"),
              SqliteProjectIdCodec::encode(2));
}

TEST_F(SqliteProjectRepositoryTest, QueryAndCountDelegateToExistingEvaluatorSemantics) {
    repository_->create(makeProject(1, "Alpha", {"C++ / CMake"}, "", "Active"), 2);
    repository_->create(makeProject(2, "alphabet", {"Rust"}, "", "Paused"), 3);
    repository_->create(makeProject(3, "Beta", {"C++"}, "", "Active"), 4);
    ProjectQuery query;
    query.name = "ALP";
    query.sort = ProjectSortKey::Name;
    query.offset = 1;
    query.limit = 1;

    const std::vector<Project> result = repository_->query(query);

    ASSERT_EQ(result.size(), 1U);
    EXPECT_EQ(result.front().id(), 2U);
    EXPECT_EQ(repository_->count(query), 2U);
}

TEST_F(SqliteProjectRepositoryTest, QueryRejectsWindowValuesThatDoNotFitSQLiteInteger) {
    ProjectQuery query;
    query.offset = std::numeric_limits<std::uint64_t>::max();
    query.limit = 1;

    EXPECT_THROW(static_cast<void>(repository_->query(query)), std::runtime_error);

    query.offset = 0;
    query.limit = std::numeric_limits<std::uint64_t>::max();
    EXPECT_THROW(static_cast<void>(repository_->query(query)), std::runtime_error);
}

TEST_F(SqliteProjectRepositoryTest, QueryFiltersUseNormalizedNameStatusAndTechnologySemantics) {
    repository_->create(makeProject(1, "Alpha C++ Builder", {"C++ / CMake", "Rust"}, "", " Active "), 2);
    repository_->create(makeProject(2, "alphabet", {"Rust"}, "", "Paused"), 3);
    repository_->create(makeProject(3, "Beta", {"C++"}, "", "ACTIVE"), 4);
    repository_->create(makeProject(4, "C punctuation", {"C---"}, "", "Active"), 5);

    ProjectQuery query;
    query.name = "ALP";
    EXPECT_EQ(projectIds(repository_->query(query)), (std::vector<ProjectId>{1, 2}));

    query = {};
    query.status = " active ";
    EXPECT_EQ(projectIds(repository_->query(query)), (std::vector<ProjectId>{1, 3, 4}));

    query = {};
    query.technology = "C++";
    EXPECT_EQ(projectIds(repository_->query(query)), (std::vector<ProjectId>{1, 3}));

    query = {};
    query.name = "";
    EXPECT_TRUE(repository_->query(query).empty());
    query = {};
    query.status = "   ";
    EXPECT_TRUE(repository_->query(query).empty());
    query = {};
    query.technology = "!!!";
    EXPECT_TRUE(repository_->query(query).empty());
    query = {};
    EXPECT_EQ(repository_->count(query), 4U);
}

TEST_F(SqliteProjectRepositoryTest, QuerySortsEveryKeyWithIdTieBreakAndAppliesWindow) {
    repository_->create(makeProject(1, "Zulu", {"C"}, "", "Paused"), 2);
    repository_->create(makeProject(2, "Alpha", {"Rust"}, "", "Active"), 3);
    repository_->create(makeProject(3, "alpha", {"Go"}, "", "Active"), 4);

    ProjectQuery query;
    query.sort = ProjectSortKey::Id;
    EXPECT_EQ(projectIds(repository_->query(query)), (std::vector<ProjectId>{1, 2, 3}));
    query.sort = ProjectSortKey::Name;
    EXPECT_EQ(projectIds(repository_->query(query)), (std::vector<ProjectId>{2, 3, 1}));
    query.sort = ProjectSortKey::Status;
    EXPECT_EQ(projectIds(repository_->query(query)), (std::vector<ProjectId>{2, 3, 1}));

    query.sort = ProjectSortKey::Id;
    query.offset = 1;
    query.limit = 1;
    EXPECT_EQ(projectIds(repository_->query(query)), (std::vector<ProjectId>{2}));
    query.offset = 99;
    EXPECT_TRUE(repository_->query(query).empty());
    query.offset = 99;
    query.limit = 0;
    EXPECT_EQ(projectIds(repository_->query(query)), (std::vector<ProjectId>{1, 2, 3}));

    query.name = "a";
    EXPECT_EQ(repository_->count(query), 2U);
}

TEST_F(SqliteProjectRepositoryTest, QueryLoadsSelectedTagsInOneOrderedBatch) {
    repository_->create(makeProject(1, "One", {"C++", "Rust", "C++"}), 2);
    repository_->create(makeProject(2, "Two", {"Go", "Go"}), 3);

    SqliteTraceCapture trace(*rawConnection_);
    ProjectQuery query;
    const std::vector<Project> result = repository_->query(query);
    const std::vector<std::string> statements = trace.statements();

    ASSERT_EQ(result.size(), 2U);
    EXPECT_EQ(result[0].techStack(), (std::vector<std::string>{"C++", "Rust", "C++"}));
    EXPECT_EQ(result[1].techStack(), (std::vector<std::string>{"Go", "Go"}));

    std::size_t tagSelectCount = 0;
    for (const std::string& sql : statements) {
        if (sql.find("SELECT project_id,position,tag,typeof(position) FROM project_tags WHERE project_id IN") !=
            std::string::npos) {
            ++tagSelectCount;
        }
        EXPECT_EQ(sql.find("SELECT id,name,description,status FROM projects ORDER BY id ASC"),
                  std::string::npos);
    }
    EXPECT_EQ(tagSelectCount, 1U);
}

TEST_F(SqliteProjectRepositoryTest, QueryAndCountReadMalformedSelectedTagPositionsStrictly) {
    repository_->create(makeProject(1), 2);
    replaceTagsWithMalformedPosition(*rawConnection_, 1, MalformedTagPositionStorageClass::Text);
    EXPECT_THROW(static_cast<void>(repository_->query({})), std::runtime_error);
}

}  // namespace
