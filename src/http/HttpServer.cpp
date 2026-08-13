#include "http/HttpServer.h"
#include "http/HttpError.h"

#include <chrono>
#ifndef _WIN32
#include <csignal>
#include <pthread.h>
#endif
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace devmanager {

namespace {

bool requiresAuthentication(const std::string& path) noexcept {
    if (path == "/api/info" || path == "/api/statistics" ||
        path == "/api/projects") {
        return true;
    }
    constexpr std::string_view projectPrefix = "/api/projects/";
    if (path.rfind(projectPrefix, 0) != 0) {
        return false;
    }
    const std::string_view projectId =
        std::string_view(path).substr(projectPrefix.size());
    return !projectId.empty() && projectId.find('/') == std::string_view::npos;
}

#ifndef _WIN32
class ListenerSignalMask final {
public:
    ListenerSignalMask() {
        sigset_t blockedSignals;
        static_cast<void>(sigemptyset(&blockedSignals));
        static_cast<void>(sigaddset(&blockedSignals, SIGINT));
        static_cast<void>(sigaddset(&blockedSignals, SIGTERM));
        if (pthread_sigmask(SIG_BLOCK, &blockedSignals, &previousMask_) != 0) {
            throw std::runtime_error("failed to block stop signals in HTTP listener");
        }
        active_ = true;
    }

    ~ListenerSignalMask() {
        if (active_) {
            static_cast<void>(pthread_sigmask(SIG_SETMASK, &previousMask_, nullptr));
        }
    }

    ListenerSignalMask(const ListenerSignalMask&) = delete;
    ListenerSignalMask& operator=(const ListenerSignalMask&) = delete;

private:
    sigset_t previousMask_ {};
    bool active_ {false};
};
#endif

}  // namespace

HttpServer::HttpServer(ProjectService& service,
                       const ApiKeyAuthenticator& authenticator,
                       std::string host,
                       std::uint16_t port,
                       RequestIdGenerator requestIdGenerator)
    : service_(service),
      logger_(nullptr),
      authenticator_(authenticator),
      host_(std::move(host)),
      requestedPort_(port),
      requestIdGenerator_(std::move(requestIdGenerator)),
      controller_(service_, logger_, requestIdGenerator_) {}

HttpServer::HttpServer(ProjectService& service,
                       const ApiKeyAuthenticator& authenticator,
                       ReadinessState& readiness,
                       std::string host,
                       std::uint16_t port,
                       RequestIdGenerator requestIdGenerator)
    : HttpServer(service, authenticator, std::move(host), port, std::move(requestIdGenerator)) {
    readiness_ = &readiness;
}

HttpServer::~HttpServer() {
    stop();
    static_cast<void>(waitUntilDrained(std::chrono::milliseconds::max()));
}

HttpServer::HttpServer(ProjectService& service,
                       Logger& logger,
                       const ApiKeyAuthenticator& authenticator,
                       std::string host,
                       std::uint16_t port,
                       RequestIdGenerator requestIdGenerator)
    : service_(service),
      logger_(&logger),
      authenticator_(authenticator),
      host_(std::move(host)),
      requestedPort_(port),
      requestIdGenerator_(std::move(requestIdGenerator)),
      controller_(service_, logger_, requestIdGenerator_) {}

HttpServer::HttpServer(ProjectService& service,
                       Logger& logger,
                       const ApiKeyAuthenticator& authenticator,
                       ReadinessState& readiness,
                       std::string host,
                       std::uint16_t port,
                       RequestIdGenerator requestIdGenerator)
    : HttpServer(service, logger, authenticator, std::move(host), port,
                 std::move(requestIdGenerator)) {
    readiness_ = &readiness;
}

void HttpServer::bind() {
    if (bound_) {
        throw std::logic_error("HTTP server is already bound");
    }

    server_.set_payload_max_length(1024U * 1024U);
    server_.set_start_handler([this]() {
        {
            std::lock_guard<std::mutex> lock(listenerMutex_);
            listenerStarted_ = true;
        }
        listenerFinishedCondition_.notify_all();
    });
    controller_.registerRoutes(server_);
    server_.Get("/ready", [this](const httplib::Request& request, httplib::Response& response) {
        const auto started = std::chrono::steady_clock::now();
        const std::string candidate = request.has_header("X-Request-ID")
                                          ? request.get_header_value("X-Request-ID")
                                          : std::string{};
        response.set_header("X-Request-ID", request_id::resolve(candidate, requestIdGenerator_));
        if (readiness_ != nullptr && readiness_->isReady()) {
            response.status = 200;
            response.set_content("{\"status\":\"ready\"}", "application/json");
        } else {
            response.status = 503;
            response.set_content(HttpError{503, "not_ready", "service is not ready"}.toJson().dump(),
                                 "application/json");
        }
        if (logger_ != nullptr) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started);
            const std::string message = std::string(response.status >= 400 ? "HTTP error" : "HTTP request") +
                                        " method=" + request.method +
                                        " path=" + request.path +
                                        " status=" + std::to_string(response.status) +
                                        " request_id=" + response.get_header_value("X-Request-ID") +
                                        " duration_ms=" + std::to_string(elapsed.count());
            if (response.status >= 400) {
                logger_->error(message);
            } else {
                logger_->info(message);
            }
        }
    });
    server_.set_pre_routing_handler(
        [this](const httplib::Request& request, httplib::Response& response) {
            if (!requiresAuthentication(request.path) ||
                authenticator_.authenticate(
                    request.has_header("Authorization")
                        ? request.get_header_value("Authorization")
                        : std::string_view{})) {
                return httplib::Server::HandlerResponse::Unhandled;
            }

            const std::string candidate = request.has_header("X-Request-ID")
                                              ? request.get_header_value("X-Request-ID")
                                              : std::string{};
            const std::string requestId = request_id::resolve(candidate, requestIdGenerator_);
            response.status = 401;
            response.set_header("WWW-Authenticate", "Bearer");
            response.set_header("X-Request-ID", requestId);
            response.set_content(
                HttpError{401, "unauthorized", "authentication required"}.toJson().dump(),
                "application/json");
            if (logger_ != nullptr) {
                logger_->warn("HTTP unauthorized method=" + request.method +
                              " path=" + request.path + " status=401 request_id=" +
                              requestId);
            }
            return httplib::Server::HandlerResponse::Handled;
        });
    server_.set_error_handler([this](const httplib::Request& request,
                                     httplib::Response& response) {
        const std::string candidate = response.has_header("X-Request-ID")
                                          ? response.get_header_value("X-Request-ID")
                                          : (request.has_header("X-Request-ID")
                                                 ? request.get_header_value("X-Request-ID")
                                                 : std::string{});
        const std::string requestId = request_id::resolve(candidate, requestIdGenerator_);
        response.set_header("X-Request-ID", requestId);
        if (response.status == 413) {
            response.set_content(
                HttpError{413, "payload_too_large", "request payload exceeds 1 MiB"}.toJson().dump(),
                "application/json");
        }
        if (logger_ != nullptr) {
            logger_->error("HTTP error method=" + request.method +
                           " path=" + request.path +
                           " status=" + std::to_string(response.status) +
                           " request_id=" + requestId);
        }
    });

    const int actualPort = requestedPort_ == 0
                               ? server_.bind_to_any_port(host_)
                               : (server_.bind_to_port(host_, requestedPort_)
                                      ? static_cast<int>(requestedPort_)
                                      : -1);
    if (actualPort <= 0) {
        throw std::runtime_error("Failed to bind HTTP server to " + host_ + ":" +
                                 std::to_string(requestedPort_));
    }

    boundPort_ = static_cast<std::uint16_t>(actualPort);
    bound_ = true;
    if (logger_ != nullptr) {
        logger_->info("HTTP server started host=" + host_ +
                      " port=" + std::to_string(boundPort_));
    }
}

void HttpServer::run() {
    if (!bound_) {
        throw std::logic_error("HTTP server must be bound before run");
    }

    static_cast<void>(server_.listen_after_bind());
}

void HttpServer::runAsync() {
    if (!bound_) {
        throw std::logic_error("HTTP server must be bound before run");
    }

    std::lock_guard<std::mutex> lock(listenerMutex_);
    if (listenerThread_.joinable()) {
        throw std::logic_error("HTTP server is already running asynchronously");
    }
    listenerStarted_ = false;
    listenerFinished_ = false;
    listenerSucceeded_ = false;
    listenerThread_ = std::thread([this]() {
        bool listenerSucceeded = false;
        try {
#ifndef _WIN32
            const ListenerSignalMask signalMask;
#endif
            listenerSucceeded = server_.listen_after_bind();
        } catch (...) {
            listenerSucceeded = false;
        }
        {
            std::lock_guard<std::mutex> lock(listenerMutex_);
            listenerSucceeded_ = listenerSucceeded;
            listenerFinished_ = true;
        }
        listenerFinishedCondition_.notify_all();
    });
}

void HttpServer::stop() noexcept {
    if (bound_) {
        server_.stop();
    }
}

void HttpServer::stopAccepting() noexcept {
    stop();
}

bool HttpServer::waitUntilListening(std::chrono::milliseconds timeout) noexcept {
    std::unique_lock<std::mutex> lock(listenerMutex_);
    const auto listenerStateChanged = [this]() {
        return listenerStarted_ || listenerFinished_;
    };
    if (!listenerStateChanged()) {
        if (timeout == std::chrono::milliseconds::max()) {
            listenerFinishedCondition_.wait(lock, listenerStateChanged);
        } else if (!listenerFinishedCondition_.wait_for(lock, timeout, listenerStateChanged)) {
            return false;
        }
    }
    return listenerStarted_ && !listenerFinished_ && server_.is_running();
}

bool HttpServer::waitUntilDrained(std::chrono::milliseconds timeout) noexcept {
    std::thread listener;
    {
        std::unique_lock<std::mutex> lock(listenerMutex_);
        if (!listenerThread_.joinable()) {
            return true;
        }
        if (!listenerFinished_) {
            if (timeout == std::chrono::milliseconds::max()) {
                listenerFinishedCondition_.wait(lock, [this]() { return listenerFinished_; });
            } else if (!listenerFinishedCondition_.wait_for(
                           lock, timeout, [this]() { return listenerFinished_; })) {
                return false;
            }
        }
        listener = std::move(listenerThread_);
    }
    listener.join();
    return true;
}

bool HttpServer::isListening() const noexcept {
    return server_.is_running();
}

std::uint16_t HttpServer::boundPort() const noexcept {
    return boundPort_;
}

}  // namespace devmanager
