#include "http/HttpServer.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace devmanager {

HttpServer::HttpServer(ProjectService& service,
                       std::string host,
                       std::uint16_t port,
                       RequestIdGenerator requestIdGenerator)
    : service_(service),
      logger_(nullptr),
      host_(std::move(host)),
      requestedPort_(port),
      requestIdGenerator_(std::move(requestIdGenerator)),
      controller_(service_, logger_, requestIdGenerator_) {}

HttpServer::HttpServer(ProjectService& service,
                       Logger& logger,
                       std::string host,
                       std::uint16_t port,
                       RequestIdGenerator requestIdGenerator)
    : service_(service),
      logger_(&logger),
      host_(std::move(host)),
      requestedPort_(port),
      requestIdGenerator_(std::move(requestIdGenerator)),
      controller_(service_, logger_, requestIdGenerator_) {}

void HttpServer::bind() {
    if (bound_) {
        throw std::logic_error("HTTP server is already bound");
    }

    controller_.registerRoutes(server_);
    server_.set_error_handler([this](const httplib::Request& request,
                                     httplib::Response& response) {
        const std::string candidate = request.has_header("X-Request-ID")
                                          ? request.get_header_value("X-Request-ID")
                                          : std::string{};
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
