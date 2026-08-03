#include "http/ProjectHttpController.h"

#include "common/ProjectIdParser.h"
#include "http/HttpError.h"
#include "http/ProjectHttpJsonMapper.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace devmanager {
namespace {

constexpr const char* kJsonContentType = "application/json; charset=UTF-8";

void sendError(httplib::Response& response, HttpError error) {
    response.status = error.status;
    response.set_content(error.toJson().dump(), kJsonContentType);
}

void sendNotImplemented(httplib::Response& response) {
    sendError(response, HttpError{501, "not_implemented", "This endpoint is not implemented yet"});
}

void sendException(httplib::Response& response, const std::exception& error) {
    if (dynamic_cast<const std::invalid_argument*>(&error) != nullptr) {
        sendError(response, HttpError{400, "invalid_request", error.what()});
        return;
    }

    if (dynamic_cast<const std::overflow_error*>(&error) != nullptr) {
        sendError(response, HttpError{409, "id_exhausted", error.what()});
        return;
    }

    if (dynamic_cast<const std::runtime_error*>(&error) != nullptr) {
        sendError(response, HttpError{500, "persistence_failure", error.what()});
        return;
    }

    sendError(response, HttpError{500, "internal_error", "Internal server error"});
}

}  // namespace

ProjectHttpController::ProjectHttpController(ProjectManager& manager) : manager_(manager) {}

void ProjectHttpController::registerRoutes(httplib::Server& server) {
    server.Get("/api/projects", [this](const httplib::Request& request,
                                       httplib::Response& response) {
        handleList(request, response);
    });

    server.Post("/api/projects", [this](const httplib::Request& request,
                                        httplib::Response& response) {
        handleNotImplemented(request, response);
    });

    server.Put(R"(/api/projects/([^/]+))",
               [this](const httplib::Request& request, httplib::Response& response) {
                   handleNotImplemented(request, response);
               });

    server.Delete(R"(/api/projects/([^/]+))",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      handleDelete(request, response);
                  });
}

void ProjectHttpController::handleList(const httplib::Request& /*request*/,
                                       httplib::Response& response) {
    try {
        std::vector<Project> projects;
        {
            std::lock_guard<std::mutex> lock(managerMutex_);
            projects = manager_.listProjects();
        }

        nlohmann::json payload = nlohmann::json::array();
        for (const Project& project : projects) {
            payload.push_back(ProjectHttpJsonMapper::toJson(project));
        }

        response.status = 200;
        response.set_content(payload.dump(), kJsonContentType);
    } catch (const std::exception& error) {
        sendException(response, error);
    } catch (...) {
        sendError(response, HttpError{500, "internal_error", "Internal server error"});
    }
}

void ProjectHttpController::handleDelete(const httplib::Request& request,
                                         httplib::Response& response) {
    if (request.matches.size() < 2U) {
        sendError(response, HttpError{400, "invalid_id", "Project ID is invalid"});
        return;
    }

    const std::optional<ProjectId> id = parseProjectId(request.matches[1].str());
    if (!id.has_value()) {
        sendError(response, HttpError{400, "invalid_id", "Project ID is invalid"});
        return;
    }

    try {
        bool deleted = false;
        {
            std::lock_guard<std::mutex> lock(managerMutex_);
            deleted = manager_.deleteProject(*id);
        }

        if (!deleted) {
            sendError(response,
                      HttpError{404, "project_not_found", "Project does not exist"});
            return;
        }

        response.status = 204;
    } catch (const std::exception& error) {
        sendException(response, error);
    } catch (...) {
        sendError(response, HttpError{500, "internal_error", "Internal server error"});
    }
}

void ProjectHttpController::handleNotImplemented(const httplib::Request& /*request*/,
                                                 httplib::Response& response) {
    sendNotImplemented(response);
}

}  // namespace devmanager
