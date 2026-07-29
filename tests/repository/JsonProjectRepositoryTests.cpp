#include "repository/JsonProjectRepository.h"
#include "repository/FileReplacer.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
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

devmanager::Project makeProject(devmanager::ProjectId id) {
    return devmanager::Project{id, "Project", {"C++"}, "Description", "In progress"};
}

class FailingFileReplacer final : public devmanager::FileReplacer {
public:
    void replace(const std::filesystem::path&, const std::filesystem::path&) const override {
        throw std::runtime_error("Injected replacement failure");
    }
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

TEST_F(JsonProjectRepositoryTest, AllowsAnEmptyStoreWithAForwardNextId) {
    const devmanager::JsonProjectRepository repository(filePath);
    const devmanager::ProjectStore store{2, {}};

    repository.saveStore(store);

    EXPECT_TRUE(std::filesystem::exists(filePath));
    EXPECT_EQ(repository.loadStore().nextId, 2);
}

TEST_F(JsonProjectRepositoryTest, RejectsAnEmptyStoreWithAZeroNextId) {
    const devmanager::JsonProjectRepository repository(filePath);
    const devmanager::ProjectStore store{0, {}};

    EXPECT_THROW(repository.saveStore(store), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(filePath));
}

TEST_F(JsonProjectRepositoryTest, RejectsAStoreWithAZeroProjectId) {
    const devmanager::JsonProjectRepository repository(filePath);
    const devmanager::ProjectStore store{1, {makeProject(0)}};

    EXPECT_THROW(repository.saveStore(store), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(filePath));
}

TEST_F(JsonProjectRepositoryTest, RejectsAStoreWithDuplicateProjectIds) {
    const devmanager::JsonProjectRepository repository(filePath);
    const devmanager::ProjectStore store{3, {makeProject(1), makeProject(1)}};

    EXPECT_THROW(repository.saveStore(store), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(filePath));
}

TEST_F(JsonProjectRepositoryTest, RejectsAStoreWhoseNextIdDoesNotExceedItsProjectIds) {
    const devmanager::JsonProjectRepository repository(filePath);
    const devmanager::ProjectStore store{2, {makeProject(2)}};

    EXPECT_THROW(repository.saveStore(store), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(filePath));
}

TEST_F(JsonProjectRepositoryTest, RejectsAStoredSemanticInvalidSnapshot) {
    nlohmann::json payload;
    payload["nextId"] = 3;
    payload["projects"] = nlohmann::json::array({makeProject(1).toJson(), makeProject(1).toJson()});
    {
        std::ofstream output(filePath);
        ASSERT_TRUE(output.is_open());
        output << payload.dump();
    }

    const devmanager::JsonProjectRepository repository(filePath);

    EXPECT_THROW(static_cast<void>(repository.loadStore()), std::runtime_error);
}

TEST_F(JsonProjectRepositoryTest, PreservesTheOriginalFileAndCleansTemporaryFilesWhenReplacementFails) {
    const devmanager::ProjectStore originalStore{2, {makeProject(1)}};
    const devmanager::ProjectStore replacementStore{3, {makeProject(1), makeProject(2)}};
    devmanager::JsonProjectRepository(filePath).saveStore(originalStore);

    std::ifstream originalInput(filePath, std::ios::binary);
    const std::string originalContent{std::istreambuf_iterator<char>(originalInput),
                                      std::istreambuf_iterator<char>()};

    const auto replacer = std::make_shared<FailingFileReplacer>();
    const devmanager::JsonProjectRepository repository(filePath, replacer);

    EXPECT_THROW(repository.saveStore(replacementStore), std::runtime_error);

    std::ifstream resultingInput(filePath, std::ios::binary);
    const std::string resultingContent{std::istreambuf_iterator<char>(resultingInput),
                                       std::istreambuf_iterator<char>()};
    EXPECT_EQ(resultingContent, originalContent);

    bool hasTemporaryFile = false;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().find("projects.json.tmp-") == 0) {
            hasTemporaryFile = true;
        }
    }
    EXPECT_FALSE(hasTemporaryFile);
}

}  // namespace
