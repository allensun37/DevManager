#include "application/ProjectManager.h"
#include "query/ProjectQueryEvaluator.h"
#include "repository/JsonProjectRepository.h"
#include "repository/ProjectRepository.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace {

std::filesystem::path makeUniqueTestDirectory() {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto suffix = std::to_string(timestamp) + "-" + std::to_string(counter++);
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("devmanager-manager-tests-" + suffix);
    std::filesystem::create_directories(directory);
    return directory;
}

class ProjectManagerTest : public testing::Test {
protected:
    void SetUp() override {
        persistenceDirectory = makeUniqueTestDirectory();
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(persistenceDirectory, error);
        if (error) {
            ADD_FAILURE() << "Failed to remove test directory: " << error.message();
        }
    }

    std::filesystem::path persistenceDirectory;
};

class RecordingProjectRepository final : public devmanager::ProjectRepository {
public:
    struct CreateCall {
        devmanager::Project project;
        devmanager::ProjectId nextIdAfterCreate;
    };

    explicit RecordingProjectRepository(devmanager::ProjectStore initialStore)
        : store_(std::move(initialStore)) {
    }

    [[nodiscard]] devmanager::ProjectStore loadStore() const override {
        return store_;
    }

    void create(const devmanager::Project& project,
                devmanager::ProjectId nextIdAfterCreate) override {
        createCalls_.push_back(CreateCall{project, nextIdAfterCreate});
        if (failWrites_) {
            throw std::runtime_error("Injected save failure");
        }
        store_.projects.push_back(project);
        store_.nextId = nextIdAfterCreate;
    }

    void update(const devmanager::Project& project) override {
        updateCalls_.push_back(project);
        if (failWrites_) {
            throw std::runtime_error("Injected save failure");
        }
        const auto iterator = std::find_if(store_.projects.begin(), store_.projects.end(),
                                           [&project](const devmanager::Project& stored) {
                                               return stored.id() == project.id();
                                           });
        if (iterator == store_.projects.end()) {
            throw std::runtime_error("Missing project");
        }
        *iterator = project;
    }

    void remove(devmanager::ProjectId id) override {
        removeCalls_.push_back(id);
        if (failWrites_) {
            throw std::runtime_error("Injected save failure");
        }
        const auto iterator = std::find_if(store_.projects.begin(), store_.projects.end(),
                                           [id](const devmanager::Project& project) {
                                               return project.id() == id;
                                           });
        if (iterator == store_.projects.end()) {
            throw std::runtime_error("Missing project");
        }
        store_.projects.erase(iterator);
    }

    [[nodiscard]] std::optional<devmanager::Project> findById(
        devmanager::ProjectId id) const override {
        const auto iterator = std::find_if(store_.projects.begin(), store_.projects.end(),
                                           [id](const devmanager::Project& project) {
                                               return project.id() == id;
                                           });
        return iterator == store_.projects.end()
                   ? std::nullopt
                   : std::optional<devmanager::Project>{*iterator};
    }

    [[nodiscard]] std::vector<devmanager::Project> query(
        const devmanager::ProjectQuery& projectQuery) const override {
        queryCalls_.push_back(projectQuery);
        return devmanager::ProjectQueryEvaluator::query(store_.projects, projectQuery);
    }

    [[nodiscard]] std::uint64_t count(
        const devmanager::ProjectQuery& projectQuery) const override {
        countCalls_.push_back(projectQuery);
        return devmanager::ProjectQueryEvaluator::count(store_.projects, projectQuery);
    }

    void setFailWrites(bool failWrites) {
        failWrites_ = failWrites;
    }

    [[nodiscard]] const std::vector<CreateCall>& createCalls() const noexcept {
        return createCalls_;
    }

    [[nodiscard]] const std::vector<devmanager::Project>& updateCalls() const noexcept {
        return updateCalls_;
    }

    [[nodiscard]] const std::vector<devmanager::ProjectId>& removeCalls() const noexcept {
        return removeCalls_;
    }

    [[nodiscard]] const std::vector<devmanager::ProjectQuery>& queryCalls() const noexcept {
        return queryCalls_;
    }

    [[nodiscard]] const std::vector<devmanager::ProjectQuery>& countCalls() const noexcept {
        return countCalls_;
    }

private:
    devmanager::ProjectStore store_;
    std::vector<CreateCall> createCalls_;
    std::vector<devmanager::Project> updateCalls_;
    std::vector<devmanager::ProjectId> removeCalls_;
    mutable std::vector<devmanager::ProjectQuery> queryCalls_;
    mutable std::vector<devmanager::ProjectQuery> countCalls_;
    bool failWrites_ {false};
};

TEST_F(ProjectManagerTest, AssignsIncreasingIdsAndKeepsInsertionOrder) {
    devmanager::ProjectManager manager;

    const devmanager::ProjectId firstId = manager.addProject(
        "DevManager", {"C++", "CMake"}, "Personal project manager.", "开发中");
    const devmanager::ProjectId secondId =
        manager.addProject("HTTP Server", {"C++", "Linux Socket"}, "Socket practice.", "学习中");

    ASSERT_EQ(firstId, 1);
    ASSERT_EQ(secondId, 2);

    const std::vector<devmanager::Project>& projects = manager.listProjects();
    ASSERT_EQ(projects.size(), 2U);
    EXPECT_EQ(projects[0].name(), "DevManager");
    EXPECT_EQ(projects[1].id(), secondId);
}

TEST_F(ProjectManagerTest, DeletesExistingProjectsWithoutReusingTheirIds) {
    devmanager::ProjectManager manager;
    const devmanager::ProjectId firstId =
        manager.addProject("DevManager", {"C++"}, "Personal project manager.", "开发中");
    const devmanager::ProjectId secondId =
        manager.addProject("HTTP Server", {"C++"}, "Socket practice.", "学习中");

    EXPECT_TRUE(manager.deleteProject(firstId));
    EXPECT_FALSE(manager.deleteProject(999));
    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects()[0].id(), secondId);

    const devmanager::ProjectId thirdId =
        manager.addProject("Blog System", {"Vue"}, "Frontend practice.", "计划中");
    EXPECT_EQ(thirdId, 3);
}

TEST_F(ProjectManagerTest, SearchesNamesAndTechnologyCaseInsensitively) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("DevManager", {"CMake"},
                                         "Personal project manager.", "开发中"));
    static_cast<void>(manager.addProject("HTTP Server", {"C++", "Linux Socket"},
                                         "Socket practice.", "学习中"));

    const std::vector<devmanager::Project> nameResults = manager.searchByName("SERVER");
    ASSERT_EQ(nameResults.size(), 1U);
    EXPECT_EQ(nameResults[0].name(), "HTTP Server");
    EXPECT_TRUE(manager.searchByName("").empty());

    const std::vector<devmanager::Project> technologyResults =
        manager.searchByTechnology("cpp");
    ASSERT_EQ(technologyResults.size(), 1U);
    EXPECT_EQ(technologyResults[0].name(), "HTTP Server");
    EXPECT_EQ(manager.searchByTechnology("SOCKET").size(), 1U);
    EXPECT_TRUE(manager.searchByTechnology("").empty());
}

TEST_F(ProjectManagerTest, RestoresProjectsAndNextIdFromRepository) {
    const std::filesystem::path persistenceFile = persistenceDirectory / "projects.json";

    {
        devmanager::JsonProjectRepository repository(persistenceFile);
        devmanager::ProjectManager manager(repository);
        EXPECT_EQ(manager.addProject("Persistent Project", {"C++"}, "Saved to JSON.",
                                     "开发中"),
                  1);
    }

    {
        devmanager::JsonProjectRepository repository(persistenceFile);
        devmanager::ProjectManager restoredManager(repository);
        ASSERT_EQ(restoredManager.listProjects().size(), 1U);
        EXPECT_EQ(restoredManager.listProjects()[0].name(), "Persistent Project");
        EXPECT_EQ(restoredManager.addProject("Second Project", {"CMake"}, "Next ID test.",
                                             "计划中"),
                  2);
    }
}

TEST_F(ProjectManagerTest, DoesNotReuseAnIdAfterDeletingTheLastPersistedProject) {
    const std::filesystem::path persistenceFile = persistenceDirectory / "projects.json";

    {
        devmanager::JsonProjectRepository repository(persistenceFile);
        devmanager::ProjectManager manager(repository);
        EXPECT_EQ(manager.addProject("Temporary", {"C++"}, "Description", "开发中"), 1);
        EXPECT_TRUE(manager.deleteProject(1));
    }

    {
        devmanager::JsonProjectRepository repository(persistenceFile);
        devmanager::ProjectManager restoredManager(repository);
        EXPECT_TRUE(restoredManager.listProjects().empty());
        EXPECT_EQ(restoredManager.addProject("Replacement", {"CMake"}, "Description", "计划中"),
                  2);
    }
}

TEST(ProjectManagerRepositoryContractTest, LoadsAStoreAndCreatesThroughTheEntityContract) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Existing", {"C++"}, "Already saved.", "开发中"}}},
    };
    devmanager::ProjectManager manager(repository);

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.addProject("New project", {"CMake"}, "New description.", "计划中"), 2);

    ASSERT_EQ(repository.createCalls().size(), 1U);
    const RecordingProjectRepository::CreateCall& createCall = repository.createCalls().front();
    EXPECT_EQ(createCall.nextIdAfterCreate, 3);
    EXPECT_EQ(createCall.project.id(), 2);
    EXPECT_EQ(createCall.project.name(), "New project");
    EXPECT_EQ(createCall.project.techStack(), (std::vector<std::string>{"CMake"}));
    EXPECT_EQ(createCall.project.description(), "New description.");
    EXPECT_EQ(createCall.project.status(), "计划中");
    const devmanager::ProjectStore stored = repository.loadStore();
    EXPECT_EQ(stored.nextId, 3);
    ASSERT_EQ(stored.projects.size(), 2U);
    EXPECT_EQ(stored.projects[0].id(), 1);
    EXPECT_EQ(stored.projects[1].id(), 2);
}

TEST(ProjectManagerPersistenceTest, RollsBackAnAddWhenSavingFails) {
    RecordingProjectRepository repository{{1, {}}};
    repository.setFailWrites(true);
    devmanager::ProjectManager manager(repository);

    EXPECT_THROW(static_cast<void>(manager.addProject("New project", {"C++"},
                                                       "Description", "开发中")),
                 std::runtime_error);
    EXPECT_TRUE(manager.listProjects().empty());
    ASSERT_EQ(repository.createCalls().size(), 1U);
    EXPECT_EQ(repository.createCalls().front().project.id(), 1);
    EXPECT_EQ(repository.createCalls().front().nextIdAfterCreate, 2);
    const devmanager::ProjectStore failedStore = repository.loadStore();
    EXPECT_EQ(failedStore.nextId, 1);
    EXPECT_TRUE(failedStore.projects.empty());

    repository.setFailWrites(false);
    EXPECT_EQ(manager.addProject("New project", {"C++"}, "Description", "开发中"), 1);
    ASSERT_EQ(repository.createCalls().size(), 2U);
    EXPECT_EQ(repository.createCalls().back().project.id(), 1);
    EXPECT_EQ(repository.createCalls().back().nextIdAfterCreate, 2);
}

TEST(ProjectManagerPersistenceTest, RollsBackADeletionWhenSavingFails) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Existing", {"C++"}, "Description", "开发中"}}},
    };
    repository.setFailWrites(true);
    devmanager::ProjectManager manager(repository);

    EXPECT_THROW(static_cast<void>(manager.deleteProject(1)), std::runtime_error);
    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects()[0].id(), 1);
    ASSERT_EQ(repository.removeCalls().size(), 1U);
    EXPECT_EQ(repository.removeCalls().front(), 1);
    const devmanager::ProjectStore failedStore = repository.loadStore();
    EXPECT_EQ(failedStore.nextId, 2);
    ASSERT_EQ(failedStore.projects.size(), 1U);
    EXPECT_EQ(failedStore.projects.front().id(), 1);

    repository.setFailWrites(false);
    EXPECT_TRUE(manager.deleteProject(1));
    ASSERT_EQ(repository.removeCalls().size(), 2U);
    EXPECT_EQ(repository.removeCalls().back(), 1);
    EXPECT_EQ(repository.loadStore().nextId, 2);
    EXPECT_EQ(manager.addProject("After deletion", {"CMake"}, "", "Planned"), 2);
    EXPECT_EQ(repository.loadStore().nextId, 3);
}

TEST(ProjectManagerPersistenceTest, RejectsAnAddWhenNextIdIsAtTheMaximum) {
    const devmanager::ProjectId maximumId = std::numeric_limits<devmanager::ProjectId>::max();
    RecordingProjectRepository repository{
        {maximumId,
         {devmanager::Project{1, "Existing", {"C++"}, "Description", "开发中"}}},
    };
    devmanager::ProjectManager manager(repository);

    EXPECT_THROW(static_cast<void>(manager.addProject("New project", {"C++"},
                                                       "Description", "开发中")),
                 std::overflow_error);
    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects()[0].id(), 1);
    EXPECT_TRUE(repository.createCalls().empty());
    EXPECT_TRUE(repository.updateCalls().empty());
    EXPECT_TRUE(repository.removeCalls().empty());
}

TEST(ProjectManagerEditTest, ReplacesEveryEditableFieldWithoutChangingTheId) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Original", {"C++"}, "Old description", "Planned"}}},
    };
    devmanager::ProjectManager manager(repository);

    EXPECT_TRUE(manager.updateProject(1,
                                      "Updated",
                                      {"C++", "CMake"},
                                      "New description",
                                      "In progress"));

    ASSERT_EQ(manager.listProjects().size(), 1U);
    const devmanager::Project& updated = manager.listProjects().front();
    EXPECT_EQ(updated.id(), 1);
    EXPECT_EQ(updated.name(), "Updated");
    EXPECT_EQ(updated.techStack(), (std::vector<std::string>{"C++", "CMake"}));
    EXPECT_EQ(updated.description(), "New description");
    EXPECT_EQ(updated.status(), "In progress");
    ASSERT_EQ(repository.updateCalls().size(), 1U);
    EXPECT_EQ(repository.updateCalls().front().id(), 1);
    EXPECT_EQ(repository.updateCalls().front().name(), "Updated");
    EXPECT_EQ(repository.updateCalls().front().techStack(),
              (std::vector<std::string>{"C++", "CMake"}));
    EXPECT_EQ(repository.updateCalls().front().description(), "New description");
    EXPECT_EQ(repository.updateCalls().front().status(), "In progress");
    EXPECT_EQ(repository.loadStore().nextId, 2);
}

TEST(ProjectManagerEditTest, ReportsMissingIdsWithoutSaving) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Existing", {"C++"}, "Description", "Planned"}}},
    };
    devmanager::ProjectManager manager(repository);

    EXPECT_FALSE(manager.updateProject(99, "Updated", {"CMake"}, "Description", "In progress"));

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects().front().name(), "Existing");
    EXPECT_TRUE(repository.createCalls().empty());
    EXPECT_TRUE(repository.updateCalls().empty());
    EXPECT_TRUE(repository.removeCalls().empty());
}

TEST(ProjectManagerEditTest, ReportsMissingDeletesWithoutCallingTheRepository) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Existing", {"C++"}, "Description", "Planned"}}},
    };
    devmanager::ProjectManager manager(repository);

    EXPECT_FALSE(manager.deleteProject(99));

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects().front().name(), "Existing");
    EXPECT_TRUE(repository.createCalls().empty());
    EXPECT_TRUE(repository.updateCalls().empty());
    EXPECT_TRUE(repository.removeCalls().empty());
}

TEST(ProjectManagerEditTest, RollsBackAnEditWhenSavingFails) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Original", {"C++"}, "Old description", "Planned"}}},
    };
    repository.setFailWrites(true);
    devmanager::ProjectManager manager(repository);

    EXPECT_THROW(static_cast<void>(manager.updateProject(1,
                                                          "Updated",
                                                          {"CMake"},
                                                          "New description",
                                                          "In progress")),
                 std::runtime_error);

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects().front().name(), "Original");
    EXPECT_EQ(manager.listProjects().front().techStack(), (std::vector<std::string>{"C++"}));
    ASSERT_EQ(repository.updateCalls().size(), 1U);
    EXPECT_EQ(repository.updateCalls().front().id(), 1);
    EXPECT_EQ(repository.updateCalls().front().name(), "Updated");
    const devmanager::ProjectStore failedStore = repository.loadStore();
    EXPECT_EQ(failedStore.nextId, 2);
    ASSERT_EQ(failedStore.projects.size(), 1U);
    EXPECT_EQ(failedStore.projects.front().name(), "Original");

    repository.setFailWrites(false);
    EXPECT_EQ(manager.addProject("Second", {"CMake"}, "", "Planned"), 2);
}

TEST(ProjectManagerEditTest, AllowsAnEditToClearTheDescription) {
    devmanager::ProjectManager manager;
    const devmanager::ProjectId id =
        manager.addProject("Project", {"C++"}, "Description", "Planned");

    EXPECT_TRUE(manager.updateProject(id, "Project", {"C++"}, "", "In progress"));

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_TRUE(manager.listProjects().front().description().empty());
}

TEST(ProjectManagerEditTest, RejectsInvalidEditsWithoutSavingOrChangingMemory) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Original", {"C++"}, "Description", "Planned"}}},
    };
    devmanager::ProjectManager manager(repository);

    EXPECT_THROW(static_cast<void>(manager.updateProject(1,
                                                          "",
                                                          {"C++"},
                                                          "New description",
                                                          "In progress")),
                 std::invalid_argument);

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects().front().name(), "Original");
    EXPECT_TRUE(repository.createCalls().empty());
    EXPECT_TRUE(repository.updateCalls().empty());
    EXPECT_TRUE(repository.removeCalls().empty());
}

TEST(ProjectManagerQueryTest, ForwardsQueriesAndCountsToTheRepository) {
    RecordingProjectRepository repository{
        {3,
         {devmanager::Project{1, "First", {"C++"}, "", "Planned"},
          devmanager::Project{2, "Second", {"CMake"}, "", "Active"}}},
    };
    devmanager::ProjectManager manager(repository);
    devmanager::ProjectQuery query;
    query.name = "second";
    query.status = "active";
    query.technology = "cmake";
    query.sort = devmanager::ProjectSortKey::Name;
    query.offset = 0;
    query.limit = 1;

    const std::vector<devmanager::Project> matches = manager.queryProjects(query);
    const std::uint64_t matchCount = manager.countProjects(query);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches.front().id(), 2);
    EXPECT_EQ(matchCount, 1U);
    ASSERT_EQ(repository.queryCalls().size(), 1U);
    EXPECT_EQ(repository.queryCalls().front().name, query.name);
    EXPECT_EQ(repository.queryCalls().front().status, query.status);
    EXPECT_EQ(repository.queryCalls().front().technology, query.technology);
    EXPECT_EQ(repository.queryCalls().front().sort, query.sort);
    EXPECT_EQ(repository.queryCalls().front().offset, query.offset);
    EXPECT_EQ(repository.queryCalls().front().limit, query.limit);
    ASSERT_EQ(repository.countCalls().size(), 1U);
    EXPECT_EQ(repository.countCalls().front().name, query.name);
    EXPECT_EQ(repository.countCalls().front().status, query.status);
    EXPECT_EQ(repository.countCalls().front().technology, query.technology);
    EXPECT_EQ(repository.countCalls().front().sort, query.sort);
    EXPECT_EQ(repository.countCalls().front().offset, query.offset);
    EXPECT_EQ(repository.countCalls().front().limit, query.limit);
}

TEST(ProjectManagerQueryTest, EvaluatesQueriesAndCountsWithoutARepository) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("First", {"C++"}, "", "Planned"));
    static_cast<void>(manager.addProject("Second", {"CMake"}, "", "Active"));
    devmanager::ProjectQuery query;
    query.technology = "cmake";

    const std::vector<devmanager::Project> matches = manager.queryProjects(query);

    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches.front().id(), 2);
    EXPECT_EQ(manager.countProjects(query), 1U);
}

TEST(ProjectManagerQueryTest, FiltersStatusWithAsciiNormalizationWithoutReorderingStoredProjects) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("First", {"C++"}, "Description", "Planned"));
    static_cast<void>(manager.addProject("Second", {"CMake"}, "Description", "In Progress"));
    static_cast<void>(manager.addProject("Third", {"C++"}, "Description", "in progress"));

    const std::vector<devmanager::Project> matches = manager.filterByStatus(" \tIN PROGRESS\r\n");

    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].id(), 2);
    EXPECT_EQ(matches[1].id(), 3);
    EXPECT_TRUE(manager.filterByStatus("").empty());
    ASSERT_EQ(manager.listProjects().size(), 3U);
    EXPECT_EQ(manager.listProjects()[0].id(), 1);
    EXPECT_EQ(manager.listProjects()[1].id(), 2);
    EXPECT_EQ(manager.listProjects()[2].id(), 3);
}

TEST(ProjectManagerQueryTest, SortsCopiesByEachSupportedKeyWithIdTieBreakers) {
    devmanager::ProjectManager manager;
    static_cast<void>(manager.addProject("zeta", {"C++"}, "Description", "Planned"));
    static_cast<void>(manager.addProject("Alpha", {"CMake"}, "Description", "In Progress"));
    static_cast<void>(manager.addProject("alpha", {"Linux"}, "Description", "in progress"));

    const std::vector<devmanager::Project> byId =
        manager.sortedProjects(devmanager::ProjectSortKey::Id);
    const std::vector<devmanager::Project> byName =
        manager.sortedProjects(devmanager::ProjectSortKey::Name);
    const std::vector<devmanager::Project> byStatus =
        manager.sortedProjects(devmanager::ProjectSortKey::Status);

    ASSERT_EQ(byId.size(), 3U);
    EXPECT_EQ(byId[0].id(), 1);
    EXPECT_EQ(byId[1].id(), 2);
    EXPECT_EQ(byId[2].id(), 3);

    ASSERT_EQ(byName.size(), 3U);
    EXPECT_EQ(byName[0].id(), 2);
    EXPECT_EQ(byName[1].id(), 3);
    EXPECT_EQ(byName[2].id(), 1);

    ASSERT_EQ(byStatus.size(), 3U);
    EXPECT_EQ(byStatus[0].id(), 2);
    EXPECT_EQ(byStatus[1].id(), 3);
    EXPECT_EQ(byStatus[2].id(), 1);

    ASSERT_EQ(manager.listProjects().size(), 3U);
    EXPECT_EQ(manager.listProjects()[0].id(), 1);
    EXPECT_EQ(manager.listProjects()[1].id(), 2);
    EXPECT_EQ(manager.listProjects()[2].id(), 3);
}

}  // namespace
