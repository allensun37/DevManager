#include "application/ProjectManager.h"
#include "repository/JsonProjectRepository.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
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

}  // namespace
