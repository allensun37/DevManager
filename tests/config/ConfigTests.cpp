#include "config/Config.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

std::filesystem::path makeUniqueTestDirectory() {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto suffix = std::to_string(timestamp) + "-" + std::to_string(counter++);
    const auto directory =
        std::filesystem::temp_directory_path() / ("devmanager-config-tests-" + suffix);
    std::filesystem::create_directories(directory);
    return directory;
}

class ConfigLoaderTest : public testing::Test {
protected:
    void SetUp() override {
        directory = makeUniqueTestDirectory();
        configPath = directory / "devmanager.json";
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) {
            ADD_FAILURE() << "Failed to remove test directory: " << error.message();
        }
    }

    void writeConfig(const std::string& content) const {
        std::ofstream output(configPath, std::ios::binary);
        ASSERT_TRUE(output.is_open());
        output << content;
        ASSERT_TRUE(output.good());
    }

    std::filesystem::path directory;
    std::filesystem::path configPath;
};

TEST_F(ConfigLoaderTest, MissingFileUsesDocumentedDefaults) {
    const devmanager::Config config = devmanager::ConfigLoader::load(configPath);

    EXPECT_EQ(config.server.host, "127.0.0.1");
    EXPECT_EQ(config.server.port, 8080);
    EXPECT_EQ(config.storage.path, std::filesystem::path{"data/projects.json"});
    EXPECT_EQ(config.logging.level, "info");
    EXPECT_EQ(config.logging.path, std::filesystem::path{"logs/devmanager.log"});
}

TEST_F(ConfigLoaderTest, ValidFileOverridesSupportedValues) {
    writeConfig(R"({
        "server": {"host": "0.0.0.0", "port": 9090},
        "storage": {"path": "custom/projects.json"},
        "logging": {"level": "debug", "path": "custom/devmanager.log"}
    })");

    const devmanager::Config config = devmanager::ConfigLoader::load(configPath);

    EXPECT_EQ(config.server.host, "0.0.0.0");
    EXPECT_EQ(config.server.port, 9090);
    EXPECT_EQ(config.storage.path, std::filesystem::path{"custom/projects.json"});
    EXPECT_EQ(config.logging.level, "debug");
    EXPECT_EQ(config.logging.path, std::filesystem::path{"custom/devmanager.log"});
}

TEST_F(ConfigLoaderTest, RejectsNonObjectJson) {
    writeConfig("[]");

    EXPECT_THROW(static_cast<void>(devmanager::ConfigLoader::load(configPath)), std::runtime_error);
}

TEST_F(ConfigLoaderTest, RejectsCorruptJson) {
    writeConfig(R"({"server": {"host": "127.0.0.1"})");

    EXPECT_THROW(static_cast<void>(devmanager::ConfigLoader::load(configPath)), std::runtime_error);
}

TEST_F(ConfigLoaderTest, ReportsFieldForTypeErrors) {
    writeConfig(R"({"server": {"port": "8080"}})");

    try {
        static_cast<void>(devmanager::ConfigLoader::load(configPath));
        FAIL() << "Expected a type validation error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("server.port"), std::string::npos);
    }
}

TEST_F(ConfigLoaderTest, ReportsFieldForStringTypeErrors) {
    writeConfig(R"({"server": {"host": 127}})");
    try {
        static_cast<void>(devmanager::ConfigLoader::load(configPath));
        FAIL() << "Expected a server.host type validation error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("server.host"), std::string::npos);
    }

    writeConfig(R"({"storage": {"path": false}})");
    try {
        static_cast<void>(devmanager::ConfigLoader::load(configPath));
        FAIL() << "Expected a storage.path type validation error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("storage.path"), std::string::npos);
    }

    writeConfig(R"({"logging": {"level": []}})");
    try {
        static_cast<void>(devmanager::ConfigLoader::load(configPath));
        FAIL() << "Expected a logging.level type validation error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("logging.level"), std::string::npos);
    }

    writeConfig(R"({"logging": {"path": {}}})");
    try {
        static_cast<void>(devmanager::ConfigLoader::load(configPath));
        FAIL() << "Expected a logging.path type validation error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("logging.path"), std::string::npos);
    }
}

TEST_F(ConfigLoaderTest, RejectsEmptyHost) {
    writeConfig(R"({"server": {"host": ""}})");

    try {
        static_cast<void>(devmanager::ConfigLoader::load(configPath));
        FAIL() << "Expected an empty host validation error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("server.host"), std::string::npos);
    }
}

TEST_F(ConfigLoaderTest, RejectsEmptyPathsAndLogLevel) {
    writeConfig(R"({"storage": {"path": ""}})");
    EXPECT_THROW(static_cast<void>(devmanager::ConfigLoader::load(configPath)), std::runtime_error);

    writeConfig(R"({"logging": {"path": ""}})");
    EXPECT_THROW(static_cast<void>(devmanager::ConfigLoader::load(configPath)), std::runtime_error);

    writeConfig(R"({"logging": {"level": ""}})");
    EXPECT_THROW(static_cast<void>(devmanager::ConfigLoader::load(configPath)), std::runtime_error);
}

TEST_F(ConfigLoaderTest, RejectsPortsOutsideValidTcpRange) {
    writeConfig(R"({"server": {"port": 0}})");
    EXPECT_THROW(static_cast<void>(devmanager::ConfigLoader::load(configPath)), std::runtime_error);

    writeConfig(R"({"server": {"port": 65536}})");
    EXPECT_THROW(static_cast<void>(devmanager::ConfigLoader::load(configPath)), std::runtime_error);
}

}  // namespace
