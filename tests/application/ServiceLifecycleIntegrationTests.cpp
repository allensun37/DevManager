#include "application/ProjectManager.h"
#include "application/ProjectService.h"
#include "application/ReadinessState.h"
#include "application/ServiceLifecycle.h"
#include "application/StopRequestSource.h"
#include "http/HttpServer.h"
#include "infrastructure/auth/ApiKeyAuthenticator.h"
#include "infrastructure/logging/Logger.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

namespace {

class TestStopSource final : public devmanager::StopRequestSource {
public:
    [[nodiscard]] bool stopRequested() const noexcept override {
        return stopRequested_.load();
    }

    [[nodiscard]] bool forceStopRequested() const noexcept override {
        return false;
    }

    void requestStop() noexcept {
        stopRequested_.store(true);
    }

private:
    std::atomic<bool> stopRequested_ {false};
};

class TemporaryLogDirectory final {
public:
    TemporaryLogDirectory() {
        static std::atomic_uint64_t counter {0U};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("devmanager-lifecycle-integration-" +
                      std::to_string(timestamp) + "-" +
                      std::to_string(counter.fetch_add(1U)));
        std::filesystem::create_directories(directory_);
    }

    ~TemporaryLogDirectory() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    [[nodiscard]] std::filesystem::path logPath() const {
        return directory_ / "service.log";
    }

private:
    std::filesystem::path directory_;
};

bool waitForHealth(const devmanager::HttpServer& server) {
    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_connection_timeout(0, 100000);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto result = client.Get(
            "/health", httplib::Headers{{"X-Request-ID", "lifecycle-probe"}});
        if (result && result->status == 200) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

TEST(ServiceLifecycleIntegrationTest, StopsListeningAfterARequestedGracefulShutdown) {
    TemporaryLogDirectory temporaryLog;
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::ReadinessState readiness;
    TestStopSource stopSource;
    devmanager::Logger logger(temporaryLog.logPath(), "debug");
    const devmanager::ApiKeyAuthenticator authenticator("test-key");
    devmanager::HttpServer server(
        service, logger, authenticator, readiness, "127.0.0.1", 0);
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, server, logger);

    server.bind();
    server.runAsync();
    ASSERT_TRUE(waitForHealth(server));

    lifecycle.markReady();
    httplib::Client readyClient("127.0.0.1", static_cast<int>(server.boundPort()));
    const auto ready = readyClient.Get("/ready");
    ASSERT_TRUE(ready);
    EXPECT_EQ(ready->status, 200);

    stopSource.requestStop();
    EXPECT_EQ(lifecycle.stopWhenRequested(), devmanager::ServiceExit::Stopped);
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Stopped);

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    client.set_default_headers(httplib::Headers{{"Authorization", "Bearer test-key"}});
    EXPECT_FALSE(client.Get("/api/projects"));
}

}  // namespace
