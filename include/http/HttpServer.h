#pragma once

#include "http/ProjectHttpController.h"
#include "infrastructure/logging/Logger.h"

#include <httplib.h>

#include <cstdint>
#include <string>

namespace devmanager {

class HttpServer final {
public:
    HttpServer(ProjectService& service, std::string host, std::uint16_t port);
    HttpServer(ProjectService& service,
               Logger& logger,
               std::string host,
               std::uint16_t port);

    void bind();
    void run();
    void stop() noexcept;
    [[nodiscard]] std::uint16_t boundPort() const noexcept;

private:
    ProjectService& service_;
    Logger* logger_;
    std::string host_;
    std::uint16_t requestedPort_;
    std::uint16_t boundPort_ {0};
    bool bound_ {false};
    httplib::Server server_;
    ProjectHttpController controller_;
};

}  // namespace devmanager
