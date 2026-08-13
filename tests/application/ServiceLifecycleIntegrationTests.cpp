#include "application/ProjectManager.h"
#include "application/ProjectService.h"
#include "application/ReadinessState.h"
#include "application/ServiceLifecycle.h"
#include "application/StopRequestSource.h"
#include "http/HttpServer.h"
#include "http/HttpServerTestHook.h"
#include "infrastructure/auth/ApiKeyAuthenticator.h"
#include "infrastructure/logging/Logger.h"

#include <gtest/gtest.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
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

class BlockingAuthenticatedRequestHook final : public devmanager::HttpServerTestHook {
public:
    void onAuthenticatedRequest() noexcept override {
        std::unique_lock<std::mutex> lock(mutex_);
        ++authenticatedRequestCount_;
        entered_ = true;
        enteredCondition_.notify_all();
        releasedCondition_.wait(lock, [this]() { return released_; });
    }

    [[nodiscard]] bool waitUntilEntered(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return enteredCondition_.wait_for(lock, timeout, [this]() { return entered_; });
    }

    void release() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = true;
        releasedCondition_.notify_all();
    }

    [[nodiscard]] std::size_t authenticatedRequestCount() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return authenticatedRequestCount_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable enteredCondition_;
    std::condition_variable releasedCondition_;
    bool entered_ {false};
    bool released_ {false};
    std::size_t authenticatedRequestCount_ {0U};
};

class RequestThreadCleanup final {
public:
    RequestThreadCleanup(std::shared_ptr<BlockingAuthenticatedRequestHook> hook,
                         std::thread& request,
                         std::thread& coordinator)
        : hook_(std::move(hook)), request_(request), coordinator_(coordinator) {}

    ~RequestThreadCleanup() {
        hook_->release();
        if (coordinator_.joinable()) {
            coordinator_.join();
        }
        if (request_.joinable()) {
            request_.join();
        }
    }

    RequestThreadCleanup(const RequestThreadCleanup&) = delete;
    RequestThreadCleanup& operator=(const RequestThreadCleanup&) = delete;

private:
    std::shared_ptr<BlockingAuthenticatedRequestHook> hook_;
    std::thread& request_;
    std::thread& coordinator_;
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

TEST(ServiceLifecycleIntegrationTest, DoesNotReportReadyWhenAsyncListenerStopsBeforeServing) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    const devmanager::ApiKeyAuthenticator authenticator("test-key");
    devmanager::HttpServer server(service, authenticator, "127.0.0.1", 0);

    server.bind();
    server.stop();
    server.runAsync();

    EXPECT_FALSE(server.waitUntilListening(std::chrono::milliseconds(500)));
    EXPECT_TRUE(server.waitUntilDrained(std::chrono::seconds(1)));
}

TEST(ServiceLifecycleIntegrationTest,
     InFlightAuthenticatedRequestDrainsAndRejectsFreshConnection) {
    TemporaryLogDirectory temporaryLog;
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::ReadinessState readiness;
    TestStopSource stopSource;
    devmanager::Logger logger(temporaryLog.logPath(), "debug");
    const devmanager::ApiKeyAuthenticator authenticator("test-key");
    const auto hook = std::make_shared<BlockingAuthenticatedRequestHook>();
    devmanager::HttpServer server(
        service, logger, authenticator, readiness, "127.0.0.1", 0, {}, hook);
    devmanager::ServiceLifecycle lifecycle(readiness, stopSource, server, logger);

    server.bind();
    server.runAsync();
    ASSERT_TRUE(server.waitUntilListening(std::chrono::seconds(2)));
    lifecycle.markReady();

    std::optional<int> requestStatus;
    std::thread requestThread;
    std::thread coordinatorThread;
    RequestThreadCleanup cleanup(hook, requestThread, coordinatorThread);

    requestThread = std::thread([&]() {
        httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
        client.set_connection_timeout(0, 100000);
        client.set_default_headers(httplib::Headers{{"Authorization", "Bearer test-key"}});
        const auto response = client.Get("/api/projects");
        if (response) {
            requestStatus = response->status;
        }
    });
    ASSERT_TRUE(hook->waitUntilEntered(std::chrono::seconds(2)));

    stopSource.requestStop();
    devmanager::ServiceExit exit = devmanager::ServiceExit::NoStopRequested;
    coordinatorThread = std::thread([&]() { exit = lifecycle.stopWhenRequested(); });
    ASSERT_TRUE(server.waitUntilAcceptingStopped(std::chrono::seconds(2)));

    httplib::Client freshClient("127.0.0.1", static_cast<int>(server.boundPort()));
    freshClient.set_connection_timeout(0, 100000);
    freshClient.set_read_timeout(0, 100000);
    freshClient.set_default_headers(httplib::Headers{{"Authorization", "Bearer test-key"}});
    EXPECT_FALSE(freshClient.Get("/api/projects"));
    EXPECT_EQ(hook->authenticatedRequestCount(), 1U);

    hook->release();
    requestThread.join();
    coordinatorThread.join();
    ASSERT_TRUE(requestStatus.has_value());
    EXPECT_EQ(*requestStatus, 200);
    EXPECT_EQ(exit, devmanager::ServiceExit::Stopped);
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Stopped);
    EXPECT_TRUE(server.waitUntilDrained(std::chrono::milliseconds(0)));
    EXPECT_EQ(hook->authenticatedRequestCount(), 1U);
}

TEST(ServiceLifecycleIntegrationTest, UnauthenticatedRequestDoesNotReachTestHook) {
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    const devmanager::ApiKeyAuthenticator authenticator("test-key");
    const auto hook = std::make_shared<BlockingAuthenticatedRequestHook>();
    devmanager::HttpServer server(service, authenticator, "127.0.0.1", 0, {}, hook);

    server.bind();
    server.runAsync();
    ASSERT_TRUE(server.waitUntilListening(std::chrono::seconds(2)));
    hook->release();

    httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
    const auto response = client.Get("/api/projects");

    ASSERT_TRUE(response);
    EXPECT_EQ(response->status, 401);
    EXPECT_EQ(hook->authenticatedRequestCount(), 0U);
}

TEST(ServiceLifecycleIntegrationTest, TimeoutRetainsResultUntilBlockedRequestDrains) {
    TemporaryLogDirectory temporaryLog;
    devmanager::ProjectManager manager;
    devmanager::ProjectService service(manager);
    devmanager::ReadinessState readiness;
    TestStopSource stopSource;
    devmanager::Logger logger(temporaryLog.logPath(), "debug");
    const devmanager::ApiKeyAuthenticator authenticator("test-key");
    const auto hook = std::make_shared<BlockingAuthenticatedRequestHook>();
    devmanager::HttpServer server(
        service, logger, authenticator, readiness, "127.0.0.1", 0, {}, hook);
    devmanager::ServiceLifecycle lifecycle(
        readiness, stopSource, server, logger, std::chrono::milliseconds(20));

    server.bind();
    server.runAsync();
    ASSERT_TRUE(server.waitUntilListening(std::chrono::seconds(2)));
    lifecycle.markReady();

    std::thread requestThread;
    std::thread unusedCoordinatorThread;
    RequestThreadCleanup cleanup(hook, requestThread, unusedCoordinatorThread);
    requestThread = std::thread([&]() {
        httplib::Client client("127.0.0.1", static_cast<int>(server.boundPort()));
        client.set_connection_timeout(0, 100000);
        client.set_default_headers(httplib::Headers{{"Authorization", "Bearer test-key"}});
        static_cast<void>(client.Get("/api/projects"));
    });
    ASSERT_TRUE(hook->waitUntilEntered(std::chrono::seconds(2)));

    stopSource.requestStop();
    EXPECT_EQ(lifecycle.stopWhenRequested(), devmanager::ServiceExit::ShutdownTimeout);
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Stopping);
    ASSERT_TRUE(server.waitUntilAcceptingStopped(std::chrono::seconds(2)));

    hook->release();
    requestThread.join();
    EXPECT_EQ(lifecycle.finishAfterDrain(), devmanager::ServiceExit::ShutdownTimeout);
    EXPECT_EQ(readiness.state(), devmanager::ServiceState::Stopped);
    EXPECT_TRUE(server.waitUntilDrained(std::chrono::milliseconds(0)));
}

}  // namespace
