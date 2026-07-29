#include "application/ProjectManager.h"
#include "repository/JsonProjectRepository.h"
#include "repository/ProjectRepository.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
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
    explicit RecordingProjectRepository(devmanager::ProjectStore initialStore)
        : initialStore_(std::move(initialStore)) {
    }

    [[nodiscard]] devmanager::ProjectStore loadStore() const override {
        return initialStore_;
    }

    void saveStore(const devmanager::ProjectStore& store) const override {
        if (failSaves_) {
            throw std::runtime_error("Injected save failure");
        }
        savedStores_.push_back(store);
    }

    void setFailSaves(bool failSaves) {
        failSaves_ = failSaves;
    }

    [[nodiscard]] const std::vector<devmanager::ProjectStore>& savedStores() const {
        return savedStores_;
    }

private:
    devmanager::ProjectStore initialStore_;
    mutable std::vector<devmanager::ProjectStore> savedStores_;
    bool failSaves_ {false};
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

TEST(ProjectManagerRepositoryContractTest, LoadsAndSavesWholeProjectStores) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Existing", {"C++"}, "Already saved.", "开发中"}}},
    };
    devmanager::ProjectManager manager(repository);

    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.addProject("New project", {"CMake"}, "New description.", "计划中"), 2);

    ASSERT_EQ(repository.savedStores().size(), 1U);
    const devmanager::ProjectStore& savedStore = repository.savedStores().front();
    EXPECT_EQ(savedStore.nextId, 3);
    ASSERT_EQ(savedStore.projects.size(), 2U);
    EXPECT_EQ(savedStore.projects[0].id(), 1);
    EXPECT_EQ(savedStore.projects[1].id(), 2);
}

TEST(ProjectManagerPersistenceTest, RollsBackAnAddWhenSavingFails) {
    RecordingProjectRepository repository{{1, {}}};
    repository.setFailSaves(true);
    devmanager::ProjectManager manager(repository);

    EXPECT_THROW(static_cast<void>(manager.addProject("New project", {"C++"},
                                                       "Description", "开发中")),
                 std::runtime_error);
    EXPECT_TRUE(manager.listProjects().empty());
    EXPECT_TRUE(repository.savedStores().empty());

    repository.setFailSaves(false);
    EXPECT_EQ(manager.addProject("New project", {"C++"}, "Description", "开发中"), 1);
}

TEST(ProjectManagerPersistenceTest, RollsBackADeletionWhenSavingFails) {
    RecordingProjectRepository repository{
        {2, {devmanager::Project{1, "Existing", {"C++"}, "Description", "开发中"}}},
    };
    repository.setFailSaves(true);
    devmanager::ProjectManager manager(repository);

    EXPECT_THROW(static_cast<void>(manager.deleteProject(1)), std::runtime_error);
    ASSERT_EQ(manager.listProjects().size(), 1U);
    EXPECT_EQ(manager.listProjects()[0].id(), 1);
    EXPECT_TRUE(repository.savedStores().empty());

    repository.setFailSaves(false);
    EXPECT_TRUE(manager.deleteProject(1));
    ASSERT_EQ(repository.savedStores().size(), 1U);
    EXPECT_EQ(repository.savedStores()[0].nextId, 2);
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
    EXPECT_TRUE(repository.savedStores().empty());
}

}  // namespace
