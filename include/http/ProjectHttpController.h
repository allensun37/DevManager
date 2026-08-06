#pragma once

#include "application/ProjectService.h"

#include <httplib.h>

namespace devmanager {

class ProjectHttpController final {
public:
    explicit ProjectHttpController(ProjectService& service);

    void registerRoutes(httplib::Server& server);

private:
    void handleCreate(const httplib::Request& request, httplib::Response& response);
    void handleList(const httplib::Request& request, httplib::Response& response);
    void handleUpdate(const httplib::Request& request, httplib::Response& response);
    void handleDelete(const httplib::Request& request, httplib::Response& response);

    ProjectService& service_;
};

}  // namespace devmanager
