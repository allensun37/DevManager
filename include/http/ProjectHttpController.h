#pragma once

#include "application/ProjectManager.h"

#include <httplib.h>

#include <mutex>

namespace devmanager {

class ProjectHttpController final {
public:
    explicit ProjectHttpController(ProjectManager& manager);

    void registerRoutes(httplib::Server& server);

private:
    void handleCreate(const httplib::Request& request, httplib::Response& response);
    void handleList(const httplib::Request& request, httplib::Response& response);
    void handleUpdate(const httplib::Request& request, httplib::Response& response);
    void handleDelete(const httplib::Request& request, httplib::Response& response);

    ProjectManager& manager_;
    std::mutex managerMutex_;
};

}  // namespace devmanager
