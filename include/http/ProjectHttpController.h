#pragma once

#include "application/ProjectService.h"
#include "infrastructure/logging/Logger.h"

#include <httplib.h>

#include <chrono>

namespace devmanager {

class ProjectHttpController final {
public:
    explicit ProjectHttpController(ProjectService& service, Logger* logger = nullptr);

    void registerRoutes(httplib::Server& server);

private:
    void handleCreate(const httplib::Request& request, httplib::Response& response);
    void handleList(const httplib::Request& request, httplib::Response& response);
    void handleUpdate(const httplib::Request& request, httplib::Response& response);
    void handleDelete(const httplib::Request& request, httplib::Response& response);
    void logRequest(const httplib::Request& request,
                    const httplib::Response& response,
                    std::chrono::steady_clock::time_point started) const;

    ProjectService& service_;
    Logger* logger_;
};

}  // namespace devmanager
