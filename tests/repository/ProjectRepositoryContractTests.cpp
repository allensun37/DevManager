#include "EmbeddedMigrations.h"
#include "infrastructure/sqlite/MigrationManager.h"
#include "infrastructure/sqlite/SqliteConnection.h"
#include "repository/JsonProjectRepository.h"
#include "repository/ProjectRepository.h"
#include "repository/SqliteProjectRepository.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using devmanager::Project;
using devmanager::ProjectId;
using devmanager::ProjectQuery;
using devmanager::ProjectRepository;
using devmanager::ProjectSortKey;
using devmanager::SqliteConnection;
using devmanager::SqliteProjectRepository;

enum class RepositoryBackend { Json, Sqlite };

std::filesystem::path uniqueDirectory() {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("devmanager-contract-" + std::to_string(timestamp) + "-" +
                            std::to_string(counter++));
    std::filesystem::create_directories(directory);
    return directory;
}

Project makeProject(ProjectId id,
                    std::string name,
                    std::vector<std::string> tags,
                    std::string description,
                    std::string status) {
    return Project{id, std::move(name), std::move(tags), std::move(description),
                   std::move(status)};
}

ProjectQuery queryByName(std::string value) {
    ProjectQuery query;
    query.name = std::move(value);
    return query;
}

ProjectQuery queryByStatus(std::string value) {
    ProjectQuery query;
    query.status = std::move(value);
    return query;
}

ProjectQuery queryByTechnology(std::string value) {
    ProjectQuery query;
    query.technology = std::move(value);
    return query;
}

void expectProjectEqual(const Project& expected, const Project& actual) {
    EXPECT_EQ(actual.id(), expected.id());
    EXPECT_EQ(actual.name(), expected.name());
    EXPECT_EQ(actual.techStack(), expected.techStack());
    EXPECT_EQ(actual.description(), expected.description());
    EXPECT_EQ(actual.status(), expected.status());
}

std::vector<ProjectId> ids(const std::vector<Project>& projects) {
    std::vector<ProjectId> result;
    for (const Project& project : projects) {
        result.push_back(project.id());
    }
    return result;
}

class ContractRepository final {
public:
    explicit ContractRepository(RepositoryBackend backend)
        : backend_(backend), directory_(uniqueDirectory()) {
        open();
    }

    ~ContractRepository() noexcept {
        repository_.reset();
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    ContractRepository(const ContractRepository&) = delete;
    ContractRepository& operator=(const ContractRepository&) = delete;

    ProjectRepository& get() { return *repository_; }

    void reopen() {
        repository_.reset();
        open();
    }

private:
    void open() {
        if (backend_ == RepositoryBackend::Json) {
            repository_ = std::make_unique<devmanager::JsonProjectRepository>(
                directory_ / "projects.json");
            return;
        }

        auto connection = std::make_unique<SqliteConnection>(directory_ / "projects.db");
        devmanager::MigrationManager(*connection).migrate(devmanager::kEmbeddedMigrations);
        repository_ = std::make_unique<SqliteProjectRepository>(std::move(connection));
    }

    RepositoryBackend backend_;
    std::filesystem::path directory_;
    std::unique_ptr<ProjectRepository> repository_;
};

struct ScenarioResult {
    std::vector<devmanager::ProjectStore> stores;
    std::optional<Project> missingProject;
    std::optional<Project> id2Project;
    std::optional<Project> id3Project;
    std::vector<std::vector<Project>> queries;
    std::vector<std::uint64_t> counts;
};

RepositoryBackend counterpart(RepositoryBackend backend) {
    return backend == RepositoryBackend::Json ? RepositoryBackend::Sqlite
                                               : RepositoryBackend::Json;
}

ScenarioResult runScenario(RepositoryBackend backend) {
    ScenarioResult result;
    ContractRepository repository(backend);
    ProjectRepository& store = repository.get();

    result.stores.push_back(store.loadStore());

    const Project created = makeProject(
        1, "Gamma", {"C++", "C++", "Rust"}, "Initial description", "Active");
    store.create(created, 2);
    result.stores.push_back(store.loadStore());

    repository.reopen();
    result.stores.push_back(repository.get().loadStore());

    const Project updated = makeProject(
        1, "Updated", {"Go", "Go", "C++"}, "Updated description", "In Progress");
    repository.get().update(updated);
    result.stores.push_back(repository.get().loadStore());

    repository.reopen();
    result.stores.push_back(repository.get().loadStore());

    repository.get().remove(1);
    result.stores.push_back(repository.get().loadStore());
    repository.reopen();
    result.stores.push_back(repository.get().loadStore());

    const Project id2 = makeProject(2, "Beta", {"Python"}, "Beta desc", "Active");
    repository.get().create(id2, 3);
    result.missingProject = repository.get().findById(1);
    result.id2Project = repository.get().findById(2);
    result.stores.push_back(repository.get().loadStore());

    const Project id3 = makeProject(3, "alpha", {"C++", "C++"}, "A", "Active");
    const Project id4 = makeProject(4, "ALPHA", {"Java"}, "B", "blocked");
    const Project id5 = makeProject(5, "delta", {"Rust"}, "D", "Active");
    repository.get().create(id3, 4);
    repository.get().create(id4, 5);
    repository.get().create(id5, 6);
    repository.reopen();
    result.stores.push_back(repository.get().loadStore());

    result.queries.push_back(repository.get().query(queryByName("alp")));
    result.queries.push_back(repository.get().query(queryByStatus(" ACTIVE ")));
    result.queries.push_back(repository.get().query(queryByTechnology("c++")));

    ProjectQuery idSort;
    idSort.sort = ProjectSortKey::Id;
    result.queries.push_back(repository.get().query(idSort));
    ProjectQuery nameSort;
    nameSort.sort = ProjectSortKey::Name;
    result.queries.push_back(repository.get().query(nameSort));
    ProjectQuery statusSort;
    statusSort.sort = ProjectSortKey::Status;
    result.queries.push_back(repository.get().query(statusSort));

    ProjectQuery page;
    page.sort = ProjectSortKey::Name;
    page.offset = 1;
    page.limit = 2;
    result.queries.push_back(repository.get().query(page));

    ProjectQuery countQuery;
    countQuery.status = "active";
    countQuery.offset = 1;
    countQuery.limit = 1;
    result.counts.push_back(repository.get().count(countQuery));

    repository.reopen();
    result.queries.push_back(repository.get().query(page));
    result.counts.push_back(repository.get().count(countQuery));
    result.stores.push_back(repository.get().loadStore());
    result.id3Project = repository.get().findById(3);
    return result;
}

void expectProjectEqual(const std::optional<Project>& expected,
                        const std::optional<Project>& actual) {
    ASSERT_EQ(actual.has_value(), expected.has_value());
    if (expected.has_value()) {
        expectProjectEqual(*expected, *actual);
    }
}

void expectStoreEqual(const devmanager::ProjectStore& expected,
                      const devmanager::ProjectStore& actual) {
    ASSERT_EQ(actual.nextId, expected.nextId);
    ASSERT_EQ(actual.projects.size(), expected.projects.size());
    for (std::size_t index = 0; index < expected.projects.size(); ++index) {
        expectProjectEqual(expected.projects[index], actual.projects[index]);
    }
}

void expectProjectsEqual(const std::vector<Project>& expected,
                         const std::vector<Project>& actual) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expectProjectEqual(expected[index], actual[index]);
    }
}

class ProjectRepositoryContractTest : public ::testing::TestWithParam<RepositoryBackend> {};

TEST_P(ProjectRepositoryContractTest, PersistsMutatesRemovesAndComparesBackendSemantics) {
    const ScenarioResult actual = runScenario(GetParam());
    const ScenarioResult expected = runScenario(counterpart(GetParam()));

    ASSERT_EQ(actual.stores.size(), expected.stores.size());
    for (std::size_t index = 0; index < expected.stores.size(); ++index) {
        expectStoreEqual(expected.stores[index], actual.stores[index]);
    }
    expectProjectEqual(expected.missingProject, actual.missingProject);
    expectProjectEqual(expected.id2Project, actual.id2Project);
    expectProjectEqual(expected.id3Project, actual.id3Project);
    ASSERT_EQ(actual.queries.size(), expected.queries.size());
    for (std::size_t index = 0; index < expected.queries.size(); ++index) {
        expectProjectsEqual(expected.queries[index], actual.queries[index]);
    }
    EXPECT_EQ(actual.counts, expected.counts);

    ASSERT_EQ(actual.stores.front().nextId, 1U);
    EXPECT_TRUE(actual.stores.front().projects.empty());
    EXPECT_EQ(actual.stores.back().nextId, 6U);
    EXPECT_EQ(ids(actual.queries[0]), (std::vector<ProjectId>{3, 4}));
    EXPECT_EQ(ids(actual.queries[1]), (std::vector<ProjectId>{2, 3, 5}));
    EXPECT_EQ(ids(actual.queries[2]), (std::vector<ProjectId>{3}));
    EXPECT_EQ(ids(actual.queries[3]), (std::vector<ProjectId>{2, 3, 4, 5}));
    EXPECT_EQ(ids(actual.queries[4]), (std::vector<ProjectId>{3, 4, 2, 5}));
    EXPECT_EQ(ids(actual.queries[5]), (std::vector<ProjectId>{2, 3, 5, 4}));
    EXPECT_EQ(ids(actual.queries[6]), (std::vector<ProjectId>{4, 2}));
    EXPECT_EQ(ids(actual.queries[7]), (std::vector<ProjectId>{4, 2}));
    EXPECT_EQ(actual.counts, (std::vector<std::uint64_t>{3, 3}));
}

INSTANTIATE_TEST_SUITE_P(AllBackends,
                         ProjectRepositoryContractTest,
                         ::testing::Values(RepositoryBackend::Json, RepositoryBackend::Sqlite),
                         [](const ::testing::TestParamInfo<RepositoryBackend>& info) {
                             return info.param == RepositoryBackend::Json ? "Json" : "Sqlite";
                         });

}  // namespace
