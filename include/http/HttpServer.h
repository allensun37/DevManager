#pragma once

#include "http/ProjectHttpController.h"
#include "http/RequestId.h"
#include "application/ReadinessState.h"
#include "application/ServiceRuntime.h"
#include "infrastructure/auth/ApiKeyAuthenticator.h"
#include "infrastructure/logging/Logger.h"

#include <httplib.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace devmanager {

class HttpServer final : public ServiceRuntime {
public:
    HttpServer(ProjectService& service,
               const ApiKeyAuthenticator& authenticator,
               std::string host,
               std::uint16_t port,
               RequestIdGenerator requestIdGenerator = {});
    HttpServer(ProjectService& service,
               const ApiKeyAuthenticator& authenticator,
               ReadinessState& readiness,
               std::string host,
               std::uint16_t port,
               RequestIdGenerator requestIdGenerator = {});
    HttpServer(ProjectService& service,
               Logger& logger,
               const ApiKeyAuthenticator& authenticator,
               std::string host,
               std::uint16_t port,
               RequestIdGenerator requestIdGenerator = {});
    HttpServer(ProjectService& service,
               Logger& logger,
               const ApiKeyAuthenticator& authenticator,
               ReadinessState& readiness,
               std::string host,
               std::uint16_t port,
               RequestIdGenerator requestIdGenerator = {});
    ~HttpServer() override;

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void bind();
    void run();
    void runAsync();
    void stop() noexcept;
    void stopAccepting() noexcept override;
    [[nodiscard]] bool waitUntilDrained(
        std::chrono::milliseconds timeout) noexcept override;
    [[nodiscard]] std::uint16_t boundPort() const noexcept;

private:
    ProjectService& service_;
    Logger* logger_;
    const ApiKeyAuthenticator& authenticator_;
    ReadinessState* readiness_ {nullptr};
    std::string host_;
    std::uint16_t requestedPort_;
    std::uint16_t boundPort_ {0};
    bool bound_ {false};
    std::mutex listenerMutex_;
    std::condition_variable listenerFinishedCondition_;
    bool listenerFinished_ {false};
    std::thread listenerThread_;
    RequestIdGenerator requestIdGenerator_;
    httplib::Server server_;
    ProjectHttpController controller_;
};

}  // namespace devmanager
