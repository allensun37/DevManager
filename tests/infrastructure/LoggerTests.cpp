#include "infrastructure/logging/Logger.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace {

std::filesystem::path makeUniqueTestDirectory() {
    static std::atomic_uint64_t counter{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("devmanager-logger-tests-" + std::to_string(timestamp) + "-" +
                            std::to_string(counter++));
    std::filesystem::create_directories(directory);
    return directory;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

class LoggerTest : public testing::Test {
protected:
    void SetUp() override {
        directory = makeUniqueTestDirectory();
        logPath = directory / "nested" / "devmanager.log";
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) {
            ADD_FAILURE() << "Failed to remove temporary logger directory: "
                          << error.message();
        }
    }

    std::filesystem::path directory;
    std::filesystem::path logPath;
};

TEST_F(LoggerTest, CreatesConfiguredDirectoryAndWritesInfoMessage) {
    devmanager::Logger logger(logPath, "info");

    logger.info("service started");

    ASSERT_TRUE(std::filesystem::exists(logPath));
    const std::string contents = readFile(logPath);
    EXPECT_NE(contents.find("service started"), std::string::npos);
}

TEST_F(LoggerTest, WritesWarningAndErrorMessagesToConfiguredFile) {
    devmanager::Logger logger(logPath, "debug");

    logger.warn("configuration warning");
    logger.error("repository error");

    const std::string contents = readFile(logPath);
    EXPECT_NE(contents.find("configuration warning"), std::string::npos);
    EXPECT_NE(contents.find("repository error"), std::string::npos);
}

TEST_F(LoggerTest, RejectsUnsupportedLogLevel) {
    EXPECT_THROW(devmanager::Logger(logPath, "not-a-level"), std::invalid_argument);
}

}  // namespace
