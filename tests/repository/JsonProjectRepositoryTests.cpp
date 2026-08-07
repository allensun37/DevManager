#include "repository/JsonProjectRepository.h"
#include "repository/FileReplacer.h"
#include "repository/ProjectStoreValidator.h"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

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

devmanager::Project makeProject(
    devmanager::ProjectId id,
    std::string name = "Project",
    std::vector<std::string> techStack = {"C++"},
    std::string description = "Description",
    std::string status = "In progress") {
    return devmanager::Project{id, std::move(name), std::move(techStack),
                               std::move(description), std::move(status)};
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool hasProjectTemporaryFile(const std::filesystem::path& directory) {
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename().string().find("projects.json.tmp-") == 0) {
            return true;
        }
    }
    return false;
}

std::vector<devmanager::ProjectId> projectIds(
    const std::vector<devmanager::Project>& projects) {
    std::vector<devmanager::ProjectId> ids;
    ids.reserve(projects.size());
    for (const devmanager::Project& project : projects) {
        ids.push_back(project.id());
    }
    return ids;
}

class FailingFileReplacer final : public devmanager::FileReplacer {
public:
    void replace(const std::filesystem::path&, const std::filesystem::path&) const override {
        throw std::runtime_error("Injected replacement failure");
    }
};

TEST(ProjectStoreValidatorTest, EnforcesSharedProjectStoreRules) {
    EXPECT_NO_THROW(devmanager::validateProjectStore(devmanager::ProjectStore{2, {}}));
    EXPECT_THROW(devmanager::validateProjectStore(devmanager::ProjectStore{0, {}}),
                 std::invalid_argument);
    EXPECT_THROW(devmanager::validateProjectStore(
                     devmanager::ProjectStore{1, {makeProject(0)}}),
                 std::invalid_argument);
    EXPECT_THROW(devmanager::validateProjectStore(
                     devmanager::ProjectStore{3, {makeProject(1), makeProject(1)}}),
                 std::invalid_argument);
    EXPECT_THROW(devmanager::validateProjectStore(
                     devmanager::ProjectStore{2, {makeProject(2)}}),
                 std::invalid_argument);
}

TEST_F(JsonProjectRepositoryTest, MissingDataFileCreatesAnEmptyStore) {
    const devmanager::JsonProjectRepository repository(filePath);

    const devmanager::ProjectStore store = repository.loadStore();

    EXPECT_EQ(store.nextId, 1);
    EXPECT_TRUE(store.projects.empty());
}

TEST_F(JsonProjectRepositoryTest, ReportsTheDataPathWhenItCannotBeOpened) {
    ASSERT_TRUE(std::filesystem::create_directories(filePath));
    const devmanager::JsonProjectRepository repository(filePath);

    try {
        static_cast<void>(repository.loadStore());
        FAIL() << "Expected the directory path to be rejected as a data file";
    } catch (const std::exception& error) {
        EXPECT_NE(std::string(error.what()).find(filePath.string()), std::string::npos);
    }
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

TEST_F(JsonProjectRepositoryTest, CreatePersistsTheProjectAndExactNextId) {
    devmanager::JsonProjectRepository repository(filePath);
    const devmanager::Project project =
        makeProject(1, "Created", {"C++", "SQLite"}, "Created description", "Ready");

    repository.create(project, 7);

    const devmanager::ProjectStore stored = repository.loadStore();
    EXPECT_EQ(stored.nextId, 7);
    ASSERT_EQ(stored.projects.size(), 1U);
    EXPECT_EQ(stored.projects[0].id(), 1);
    EXPECT_EQ(stored.projects[0].name(), "Created");
    EXPECT_EQ(stored.projects[0].techStack(),
              (std::vector<std::string>{"C++", "SQLite"}));
    EXPECT_EQ(stored.projects[0].description(), "Created description");
    EXPECT_EQ(stored.projects[0].status(), "Ready");
}

TEST_F(JsonProjectRepositoryTest, CreateRejectsADuplicateIdWithoutChangingTheFile) {
    devmanager::JsonProjectRepository repository(filePath);
    repository.saveStore({5, {makeProject(1, "Original")}});
    const std::string originalContent = readFile(filePath);

    EXPECT_THROW(repository.create(makeProject(1, "Duplicate"), 6), std::runtime_error);

    EXPECT_EQ(readFile(filePath), originalContent);
}

TEST_F(JsonProjectRepositoryTest, UpdateReplacesEveryEditableFieldAndPreservesIdAndNextId) {
    devmanager::JsonProjectRepository repository(filePath);
    repository.saveStore({9, {makeProject(4, "Before", {"C++"}, "Old", "Planned")}});

    repository.update(makeProject(4, "After", {"Rust", "CMake"}, "New", "Complete"));

    const devmanager::ProjectStore stored = repository.loadStore();
    EXPECT_EQ(stored.nextId, 9);
    ASSERT_EQ(stored.projects.size(), 1U);
    EXPECT_EQ(stored.projects[0].id(), 4);
    EXPECT_EQ(stored.projects[0].name(), "After");
    EXPECT_EQ(stored.projects[0].techStack(),
              (std::vector<std::string>{"Rust", "CMake"}));
    EXPECT_EQ(stored.projects[0].description(), "New");
    EXPECT_EQ(stored.projects[0].status(), "Complete");
}

TEST_F(JsonProjectRepositoryTest, UpdateRejectsAMissingIdWithoutChangingTheFile) {
    devmanager::JsonProjectRepository repository(filePath);
    repository.saveStore({5, {makeProject(1, "Original")}});
    const std::string originalContent = readFile(filePath);

    EXPECT_THROW(repository.update(makeProject(2, "Missing")), std::runtime_error);

    EXPECT_EQ(readFile(filePath), originalContent);
}

TEST_F(JsonProjectRepositoryTest, RemoveDeletesExactlyOneProjectAndPreservesNextId) {
    devmanager::JsonProjectRepository repository(filePath);
    repository.saveStore({8, {makeProject(1), makeProject(2), makeProject(3)}});

    repository.remove(2);

    const devmanager::ProjectStore stored = repository.loadStore();
    EXPECT_EQ(stored.nextId, 8);
    EXPECT_EQ(projectIds(stored.projects),
              (std::vector<devmanager::ProjectId>{1, 3}));
}

TEST_F(JsonProjectRepositoryTest, RemoveRejectsAMissingIdWithoutChangingTheFile) {
    devmanager::JsonProjectRepository repository(filePath);
    repository.saveStore({5, {makeProject(1, "Original")}});
    const std::string originalContent = readFile(filePath);

    EXPECT_THROW(repository.remove(2), std::runtime_error);

    EXPECT_EQ(readFile(filePath), originalContent);
}

TEST_F(JsonProjectRepositoryTest, FindByIdReturnsACopyOrNullopt) {
    devmanager::JsonProjectRepository repository(filePath);
    repository.saveStore({3, {makeProject(1, "Original"), makeProject(2, "Other")}});

    const std::optional<devmanager::Project> found = repository.findById(1);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name(), "Original");
    EXPECT_FALSE(repository.findById(99).has_value());

    repository.update(makeProject(1, "Updated"));
    EXPECT_EQ(found->name(), "Original");
    EXPECT_EQ(repository.findById(1)->name(), "Updated");
}

TEST_F(JsonProjectRepositoryTest, QueryUsesNormalizationSortingAndWindowWhileCountIgnoresWindow) {
    const devmanager::JsonProjectRepository repository(filePath);
    repository.saveStore({4,
                          {makeProject(3, "Zeta API", {"Rust", "C++"}, "", " Active "),
                           makeProject(1, "Alpha API", {"cpp"}, "", "active"),
                           makeProject(2, "alpha api", {"C++20"}, "", "ACTIVE")}});
    devmanager::ProjectQuery query;
    query.name = "API";
    query.status = " active ";
    query.technology = " c++ ";
    query.sort = devmanager::ProjectSortKey::Name;
    query.offset = 1;
    query.limit = 1;

    EXPECT_EQ(projectIds(repository.query(query)),
              (std::vector<devmanager::ProjectId>{2}));
    EXPECT_EQ(repository.count(query), 3U);
}

TEST_F(JsonProjectRepositoryTest, RawTechnologyTagOrderAndDuplicatesRoundTripExactly) {
    devmanager::JsonProjectRepository repository(filePath);
    const std::vector<std::string> tags{"C++", "Rust", "C++", "  CMake  "};

    repository.create(makeProject(1, "Tags", tags), 2);

    const std::optional<devmanager::Project> restored = repository.findById(1);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->techStack(), tags);
}

TEST_F(JsonProjectRepositoryTest, CreateReplacementFailurePreservesFileAndCleansTemporaryFile) {
    devmanager::JsonProjectRepository(filePath).saveStore({2, {makeProject(1)}});
    const std::string originalContent = readFile(filePath);
    devmanager::JsonProjectRepository repository(
        filePath, std::make_shared<FailingFileReplacer>());

    EXPECT_THROW(repository.create(makeProject(2), 3), std::runtime_error);

    EXPECT_EQ(readFile(filePath), originalContent);
    EXPECT_FALSE(hasProjectTemporaryFile(directory));
}

TEST_F(JsonProjectRepositoryTest, UpdateReplacementFailurePreservesFileAndCleansTemporaryFile) {
    devmanager::JsonProjectRepository(filePath).saveStore({2, {makeProject(1, "Original")}});
    const std::string originalContent = readFile(filePath);
    devmanager::JsonProjectRepository repository(
        filePath, std::make_shared<FailingFileReplacer>());

    EXPECT_THROW(repository.update(makeProject(1, "Updated")), std::runtime_error);

    EXPECT_EQ(readFile(filePath), originalContent);
    EXPECT_FALSE(hasProjectTemporaryFile(directory));
}

TEST_F(JsonProjectRepositoryTest, RemoveReplacementFailurePreservesFileAndCleansTemporaryFile) {
    devmanager::JsonProjectRepository(filePath).saveStore({3, {makeProject(1), makeProject(2)}});
    const std::string originalContent = readFile(filePath);
    devmanager::JsonProjectRepository repository(
        filePath, std::make_shared<FailingFileReplacer>());

    EXPECT_THROW(repository.remove(1), std::runtime_error);

    EXPECT_EQ(readFile(filePath), originalContent);
    EXPECT_FALSE(hasProjectTemporaryFile(directory));
}

TEST_F(JsonProjectRepositoryTest, DeleteLastProjectKeepsForwardNextIdInExistingStore) {
    devmanager::JsonProjectRepository repository(filePath);
    repository.saveStore({2, {makeProject(1)}});

    repository.remove(1);

    const devmanager::ProjectStore stored = repository.loadStore();
    EXPECT_EQ(stored.nextId, 2);
    EXPECT_TRUE(stored.projects.empty());
}

TEST_F(JsonProjectRepositoryTest, RejectsEverySemanticInvalidSnapshot) {
    const std::vector<devmanager::ProjectStore> invalidStores{
        {0, {}},
        {1, {makeProject(0)}},
        {3, {makeProject(1), makeProject(1)}},
        {2, {makeProject(2)}},
    };
    const devmanager::JsonProjectRepository repository(filePath);

    for (const devmanager::ProjectStore& invalidStore : invalidStores) {
        nlohmann::json payload;
        payload["nextId"] = invalidStore.nextId;
        payload["projects"] = nlohmann::json::array();
        for (const devmanager::Project& project : invalidStore.projects) {
            payload["projects"].push_back(project.toJson());
        }
        {
            std::ofstream output(filePath, std::ios::trunc);
            ASSERT_TRUE(output.is_open());
            output << payload.dump();
        }

        EXPECT_THROW(static_cast<void>(repository.loadStore()), std::runtime_error);
    }
}

}  // namespace
