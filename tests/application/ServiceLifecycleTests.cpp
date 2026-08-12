#include "application/ReadinessState.h"
#include "application/ServiceLifecycle.h"
#include "application/ServiceRuntime.h"
#include "application/StopRequestSource.h"
#include "infrastructure/logging/Logger.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

namespace {

class FakeStopSource final : public devmanager::StopRequestSource {
public:
    [[nodiscard]] bool stopRequested() const noexcept override {
        return stop;
    }

    [[nodiscard]] bool forceStopRequested() const noexcept override {
        return force;
    }

    bool stop {false};
    bool force {false};
};

class FakeRuntime final : public devmanager::ServiceRuntime {
public:
    void stopAccepting() noexcept override {
        ++stopCalls;
    }

    [[nodiscard]] bool waitUntilDrained(std::chrono::milliseconds timeout) noexcept override {
        ++waitCalls;
        observedTimeout = timeout;
        if (onWait) {
            onWait();
        }
        return drained;
    }

    unsigned int stopCalls {0U};
    unsigned int waitCalls {0U};
    bool drained {true};
    std::chrono::milliseconds observedTimeout {0};
    std::function<void()> onWait;
};

std::filesystem::path makeUniqueTestDirectory() {
    static std::atomic_uint64_t counter {0U};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
                           ("devmanager-service-lifecycle-tests-" +
                            std::to_string(timestamp) + "-" +
                            std::to_string(counter.fetch_add(1U)));
    std::filesystem::create_directories(directory);
    return directory;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

class ServiceLifecycleTest : public testing::Test {
protected:
    void SetUp() override {
        directory = makeUniqueTestDirectory();
        logPath = directory / "service.log";
        logger = std::make_unique<devmanager::Logger>(logPath, "debug");
    }

    void TearDown() override {
        logger.reset();
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (error) {
            ADD_FAILURE() << "Failed to remove temporary lifecycle directory: "
                          << error.message();
        }
    }

    std::filesystem::path directory;
    std::filesystem::path logPath;
    std::unique_ptr<devmanager::Logger> logger;
};

TEST_F(ServiceLifecycleTest, LeavesRuntimeUntouchedWhenNoStopWasRequested) {
    devmanager::ReadinessState readiness;
    FakeStopSource stopSource;
    FakeRuntime runtime;
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, runtime, *logger);

    const devmanager::ServiceExit result = lifecycle.stopWhenRequested();

    EXPECT_EQ(result, devmanager::ServiceExit::NoStopRequested);
    EXPECT_EQ(runtime.stopCalls, 0U);
    EXPECT_EQ(runtime.waitCalls, 0U);
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Starting);
}

TEST_F(ServiceLifecycleTest, StopsOnceAndDrainsForExactlyFiveSeconds) {
    devmanager::ReadinessState readiness;
    FakeStopSource stopSource;
    FakeRuntime runtime;
    stopSource.stop = true;
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, runtime, *logger);

    const devmanager::ServiceExit result = lifecycle.stopWhenRequested();

    EXPECT_EQ(result, devmanager::ServiceExit::Stopped);
    EXPECT_EQ(runtime.stopCalls, 1U);
    EXPECT_EQ(runtime.waitCalls, 1U);
    EXPECT_EQ(runtime.observedTimeout, std::chrono::seconds(5));
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Stopped);
    EXPECT_FALSE(readiness.isReady());
}

TEST_F(ServiceLifecycleTest, ReportsShutdownTimeoutWithoutSensitiveLogContent) {
    devmanager::ReadinessState readiness;
    FakeStopSource stopSource;
    FakeRuntime runtime;
    stopSource.stop = true;
    runtime.drained = false;
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, runtime, *logger);

    const devmanager::ServiceExit result = lifecycle.stopWhenRequested();

    EXPECT_EQ(result, devmanager::ServiceExit::ShutdownTimeout);
    EXPECT_EQ(runtime.stopCalls, 1U);
    EXPECT_EQ(runtime.waitCalls, 1U);
    const std::string contents = readFile(logPath);
    EXPECT_NE(contents.find("shutdown_timeout"), std::string::npos);
    EXPECT_EQ(contents.find("Authorization"), std::string::npos);
    EXPECT_EQ(contents.find("test-api-key"), std::string::npos);
}

TEST_F(ServiceLifecycleTest, ForceStopSkipsDrainAfterStoppingListener) {
    devmanager::ReadinessState readiness;
    FakeStopSource stopSource;
    FakeRuntime runtime;
    stopSource.stop = true;
    stopSource.force = true;
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, runtime, *logger);

    const devmanager::ServiceExit result = lifecycle.stopWhenRequested();

    EXPECT_EQ(result, devmanager::ServiceExit::ForceStopRequested);
    EXPECT_EQ(runtime.stopCalls, 1U);
    EXPECT_EQ(runtime.waitCalls, 0U);
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Stopped);
}

TEST_F(ServiceLifecycleTest, ForceStopObservedDuringDrainWinsOverNormalCompletion) {
    devmanager::ReadinessState readiness;
    FakeStopSource stopSource;
    FakeRuntime runtime;
    stopSource.stop = true;
    runtime.onWait = [&stopSource]() { stopSource.force = true; };
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, runtime, *logger);

    const devmanager::ServiceExit result = lifecycle.stopWhenRequested();

    EXPECT_EQ(result, devmanager::ServiceExit::ForceStopRequested);
    EXPECT_EQ(runtime.stopCalls, 1U);
    EXPECT_EQ(runtime.waitCalls, 1U);
}

TEST_F(ServiceLifecycleTest, MarksFailureWithoutTouchingRuntime) {
    devmanager::ReadinessState readiness;
    FakeStopSource stopSource;
    FakeRuntime runtime;
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, runtime, *logger);

    const devmanager::ServiceExit result = lifecycle.markFailed();

    EXPECT_EQ(result, devmanager::ServiceExit::Failed);
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Failed);
    EXPECT_EQ(runtime.stopCalls, 0U);
    EXPECT_EQ(runtime.waitCalls, 0U);
}

TEST_F(ServiceLifecycleTest, DoesNotReportFailureAfterSuccessfulStop) {
    devmanager::ReadinessState readiness;
    FakeStopSource stopSource;
    FakeRuntime runtime;
    stopSource.stop = true;
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, runtime, *logger);

    EXPECT_EQ(lifecycle.stopWhenRequested(), devmanager::ServiceExit::Stopped);

    EXPECT_EQ(lifecycle.markFailed(), devmanager::ServiceExit::Stopped);
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Stopped);
}

TEST_F(ServiceLifecycleTest, DoesNotStopAcceptingTwice) {
    devmanager::ReadinessState readiness;
    FakeStopSource stopSource;
    FakeRuntime runtime;
    stopSource.stop = true;
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, runtime, *logger);

    EXPECT_EQ(lifecycle.stopWhenRequested(), devmanager::ServiceExit::Stopped);
    EXPECT_EQ(lifecycle.stopWhenRequested(), devmanager::ServiceExit::Stopped);

    EXPECT_EQ(runtime.stopCalls, 1U);
    EXPECT_EQ(runtime.waitCalls, 1U);
}

}  // namespace
