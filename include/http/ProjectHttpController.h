#pragma once

#include "application/ProjectService.h"
#include "infrastructure/logging/Logger.h"
#include "http/RequestId.h"

#include <httplib.h>

#include <chrono>

namespace devmanager {

class ProjectHttpController final {
public:
    explicit ProjectHttpController(ProjectService& service,
                                   Logger* logger = nullptr,
                                   RequestIdGenerator requestIdGenerator = {});

    void registerRoutes(httplib::Server& server);

private:
    void handleCreate(const httplib::Request& request, httplib::Response& response);
    void handleList(const httplib::Request& request, httplib::Response& response);
    void handleUpdate(const httplib::Request& request, httplib::Response& response);
    void handleDelete(const httplib::Request& request, httplib::Response& response);
    void handleHealth(const httplib::Request& request, httplib::Response& response);
    void handleInfo(const httplib::Request& request, httplib::Response& response);
    void handleStatistics(const httplib::Request& request, httplib::Response& response);
    void setRequestId(const httplib::Request& request, httplib::Response& response) const;
    void logRequest(const httplib::Request& request,
                    const httplib::Response& response,
                    std::chrono::steady_clock::time_point started) const;

    ProjectService& service_;
    Logger* logger_;
    RequestIdGenerator requestIdGenerator_;
};

}  // namespace devmanager
