#include "application/ApplicationBootstrap.h"

#include "repository/JsonProjectRepository.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace {

std::filesystem::path makeUniqueTestDirectory() {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("devmanager-bootstrap-tests-" + std::to_string(timestamp) + "-" +
                            std::to_string(counter++));
    std::filesystem::create_directories(directory);
    return directory;
}

class ApplicationBootstrapTest : public testing::Test {
protected:
    void SetUp() override {
        directory = makeUniqueTestDirectory();
        storagePath = directory / "projects.json";
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) {
            ADD_FAILURE() << "Failed to remove test directory: " << error.message();
        }
    }

    devmanager::Config config() const {
        devmanager::Config config;
        config.server.host = "0.0.0.0";
        config.server.port = 9090;
        config.storage.path = storagePath;
        config.logging.level = "debug";
        config.logging.path = directory / "devmanager.log";
        return config;
    }

    std::filesystem::path directory;
    std::filesystem::path storagePath;
};

TEST_F(ApplicationBootstrapTest, RetainsConfigAndCreatesAnEmptyManager) {
    devmanager::ApplicationBootstrap bootstrap(config());

    EXPECT_EQ(bootstrap.config().server.host, "0.0.0.0");
    EXPECT_EQ(bootstrap.config().server.port, 9090);
    EXPECT_EQ(bootstrap.config().storage.path, storagePath);
    EXPECT_TRUE(bootstrap.manager().listProjects().empty());
}

TEST_F(ApplicationBootstrapTest, LoadsManagerFromConfiguredStoragePath) {
    devmanager::JsonProjectRepository repository(storagePath);
    repository.saveStore(devmanager::ProjectStore{
        8,
        {devmanager::Project{7, "Loaded", {"C++"}, "from disk", "active"}},
    });

    devmanager::ApplicationBootstrap bootstrap(config());

    ASSERT_EQ(bootstrap.manager().listProjects().size(), 1U);
    EXPECT_EQ(bootstrap.manager().listProjects().front().id(), 7);
    EXPECT_EQ(bootstrap.manager().listProjects().front().name(), "Loaded");
}

TEST_F(ApplicationBootstrapTest, MissingStorageStartsWithTheDefaultNextId) {
    devmanager::ApplicationBootstrap bootstrap(config());

    EXPECT_EQ(bootstrap.manager().addProject("New", {"C++"}, "", "active"), 1);
}

TEST_F(ApplicationBootstrapTest, OwnsAServiceOverTheSameManager) {
    devmanager::ApplicationBootstrap bootstrap(config());

    const devmanager::Project project =
        bootstrap.service().addProject("Service project", {"C++"}, "", "active");

    EXPECT_EQ(project.id(), 1U);
    ASSERT_EQ(bootstrap.manager().listProjects().size(), 1U);
    EXPECT_EQ(bootstrap.service().listProjects().front().id(), project.id());
}

}  // namespace
