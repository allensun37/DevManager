#include "repository/JsonProjectRepository.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {

std::filesystem::path makeUniqueTestDirectory() {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto suffix = std::to_string(timestamp) + "-" + std::to_string(counter++);
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("devmanager-repository-tests-" + suffix);
    std::filesystem::create_directories(directory);
    return directory;
}

class JsonProjectRepositoryTest : public testing::Test {
protected:
    void SetUp() override {
        directory = makeUniqueTestDirectory();
        filePath = directory / "projects.json";
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) {
            ADD_FAILURE() << "Failed to remove test directory: " << error.message();
        }
    }

    std::filesystem::path directory;
    std::filesystem::path filePath;
};

TEST_F(JsonProjectRepositoryTest, MissingDataFileCreatesAnEmptyStore) {
    const devmanager::JsonProjectRepository repository(filePath);

    const devmanager::ProjectStore store = repository.loadStore();

    EXPECT_EQ(store.nextId, 1);
    EXPECT_TRUE(store.projects.empty());
}

TEST_F(JsonProjectRepositoryTest, SavesAndRestoresAProjectStore) {
    const devmanager::JsonProjectRepository repository(filePath);
    const devmanager::ProjectStore expected{
        3,
        {devmanager::Project{1, "DevManager", {"C++", "CMake"},
                             "Personal project manager.", "开发中"},
         devmanager::Project{2, "HTTP Server", {"C++", "Linux Socket"},
                             "Socket practice.", "学习中"}},
    };

    repository.saveStore(expected);

    EXPECT_TRUE(std::filesystem::exists(filePath));
    const devmanager::ProjectStore restored = repository.loadStore();
    EXPECT_EQ(restored.nextId, 3);
    ASSERT_EQ(restored.projects.size(), 2U);
    EXPECT_EQ(restored.projects[0].name(), "DevManager");
    EXPECT_EQ(restored.projects[0].status(), "开发中");
    EXPECT_EQ(restored.projects[1].techStack()[1], "Linux Socket");
}

TEST_F(JsonProjectRepositoryTest, ReportsCorruptJsonWithoutOverwritingIt) {
    const devmanager::JsonProjectRepository repository(filePath);
    const std::string corruptContent = "{ this is not valid JSON";
    {
        std::ofstream corruptFile(filePath);
        ASSERT_TRUE(corruptFile.is_open());
        corruptFile << corruptContent;
    }

    try {
        static_cast<void>(repository.loadStore());
        FAIL() << "Expected corrupt JSON to throw";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find("Invalid project data file"),
                  std::string::npos);
    }

    std::ifstream unchangedFile(filePath);
    const std::string unchangedContent{std::istreambuf_iterator<char>(unchangedFile),
                                       std::istreambuf_iterator<char>()};
    EXPECT_EQ(unchangedContent, corruptContent);
}

}  // namespace
