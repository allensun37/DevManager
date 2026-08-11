#include "http/HttpServer.h"
#include "http/HttpError.h"

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

void HttpServer::bind() {
    if (bound_) {
        throw std::logic_error("HTTP server is already bound");
    }

    controller_.registerRoutes(server_);
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

void HttpServer::stop() noexcept {
    if (bound_) {
        server_.stop();
    }
}

std::uint16_t HttpServer::boundPort() const noexcept {
    return boundPort_;
}

}  // namespace devmanager
