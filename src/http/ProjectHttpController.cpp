#include "http/ProjectHttpController.h"

#include "common/AsciiText.h"
#include "common/ProjectIdParser.h"
#include "http/HttpError.h"
#include "http/ProjectHttpJsonMapper.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devmanager {
namespace {

constexpr const char* kJsonContentType = "application/json; charset=UTF-8";

void sendError(httplib::Response& response, HttpError error) {
    response.status = error.status;
    response.set_content(error.toJson().dump(), kJsonContentType);
}

[[nodiscard]] std::optional<ProjectHttpInput> parseProjectInput(
    const httplib::Request& request,
    httplib::Response& response) {
    try {
        return ProjectHttpJsonMapper::parseInput(nlohmann::json::parse(request.body));
    } catch (const nlohmann::json::parse_error& error) {
        sendError(response, HttpError{400, "invalid_json", error.what()});
    } catch (const std::invalid_argument& error) {
        sendError(response, HttpError{400, "invalid_request", error.what()});
    }
    return std::nullopt;
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

[[nodiscard]] bool isKnownQueryParameter(std::string_view key) {
    constexpr std::array<std::string_view, 4> knownKeys{
        "name", "technology", "status", "sort"};
    return std::find(knownKeys.begin(), knownKeys.end(), key) != knownKeys.end();
}

[[nodiscard]] bool isFilterParameter(std::string_view key) {
    return key == "name" || key == "technology" || key == "status";
}

[[nodiscard]] bool hasRepeatedRawQueryKey(const httplib::Request& request) {
    const std::size_t queryStart = request.target.find('?');
    if (queryStart == std::string::npos) {
        return false;
    }

    const std::string_view query(request.target.data() + queryStart + 1,
                                 request.target.size() - queryStart - 1);
    std::map<std::string, std::size_t> keyCounts;
    std::size_t pairStart = 0;
    while (pairStart <= query.size()) {
        const std::size_t pairEnd = query.find('&', pairStart);
        const std::size_t end = pairEnd == std::string_view::npos
                                    ? query.size()
                                    : pairEnd;
        const std::string_view pair = query.substr(pairStart, end - pairStart);
        const std::size_t equals = pair.find('=');
        const std::size_t keyEnd = equals == std::string_view::npos
                                       ? pair.size()
                                       : equals;
        if (!pair.empty() && keyEnd == 0U) {
            return true;
        }
        if (keyEnd > 0U) {
            const std::string decodedKey = httplib::decode_query_component(
                std::string(pair.substr(0, keyEnd)));
            if (++keyCounts[decodedKey] > 1U) {
                return true;
            }
        }

        if (pairEnd == std::string_view::npos) {
            break;
        }
        pairStart = pairEnd + 1;
    }

    return false;
}

[[nodiscard]] std::optional<ProjectSortKey> parseSortKey(std::string_view value) {
    if (value == "id") {
        return ProjectSortKey::Id;
    }
    if (value == "name") {
        return ProjectSortKey::Name;
    }
    if (value == "status") {
        return ProjectSortKey::Status;
    }
    return std::nullopt;
}

[[nodiscard]] bool validateQuery(const httplib::Request& request,
                                 std::optional<ProjectSortKey>& sortKey,
                                 std::string& filterKey) {
    if (hasRepeatedRawQueryKey(request)) {
        return false;
    }

    std::size_t filterCount = 0;

    for (const auto& parameter : request.params) {
        const std::string& key = parameter.first;
        if (!isKnownQueryParameter(key) ||
            request.get_param_value_count(key) != 1U || parameter.second.empty()) {
            return false;
        }

        if (isFilterParameter(key)) {
            ++filterCount;
            filterKey = key;
        }
    }

    if (filterCount > 1U) {
        return false;
    }

    if (request.has_param("sort")) {
        sortKey = parseSortKey(request.get_param_value("sort"));
        if (!sortKey.has_value()) {
            return false;
        }
    }

    return true;
}

void sortProjects(std::vector<Project>& projects, ProjectSortKey key) {
    std::sort(projects.begin(), projects.end(), [key](const Project& left,
                                                       const Project& right) {
        if (key == ProjectSortKey::Id) {
            return left.id() < right.id();
        }

        const std::string leftValue = key == ProjectSortKey::Name
                                          ? ascii::toLower(left.name())
                                          : ascii::toLower(left.status());
        const std::string rightValue = key == ProjectSortKey::Name
                                           ? ascii::toLower(right.name())
                                           : ascii::toLower(right.status());
        if (leftValue == rightValue) {
            return left.id() < right.id();
        }
        return leftValue < rightValue;
    });
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
        handleCreate(request, response);
    });

    server.Put(R"(/api/projects/([^/]+))",
               [this](const httplib::Request& request, httplib::Response& response) {
                   handleUpdate(request, response);
               });

    server.Delete(R"(/api/projects/([^/]+))",
                  [this](const httplib::Request& request, httplib::Response& response) {
                      handleDelete(request, response);
                  });
}

void ProjectHttpController::handleCreate(const httplib::Request& request,
                                         httplib::Response& response) {
    const std::optional<ProjectHttpInput> input = parseProjectInput(request, response);
    if (!input.has_value()) {
        return;
    }

    try {
        std::optional<Project> created;
        {
            std::lock_guard<std::mutex> lock(managerMutex_);
            const ProjectId id = manager_.addProject(input->name,
                                                     input->techStack,
                                                     input->description,
                                                     input->status);
            const auto iterator = std::find_if(
                manager_.listProjects().begin(), manager_.listProjects().end(),
                [id](const Project& project) { return project.id() == id; });
            if (iterator == manager_.listProjects().end()) {
                throw std::logic_error("Created project is missing from manager state");
            }
            created = *iterator;
        }

        response.status = 201;
        response.set_content(ProjectHttpJsonMapper::toJson(*created).dump(),
                             kJsonContentType);
    } catch (const std::exception& error) {
        sendException(response, error);
    } catch (...) {
        sendError(response, HttpError{500, "internal_error", "Internal server error"});
    }
}

void ProjectHttpController::handleList(const httplib::Request& request,
                                       httplib::Response& response) {
    try {
        std::optional<ProjectSortKey> sortKey;
        std::string filterKey;
        if (!validateQuery(request, sortKey, filterKey)) {
            sendError(response,
                      HttpError{400, "invalid_query", "Project query is invalid"});
            return;
        }

        std::vector<Project> projects;
        {
            std::lock_guard<std::mutex> lock(managerMutex_);
            if (filterKey == "name") {
                projects = manager_.searchByName(request.get_param_value("name"));
            } else if (filterKey == "technology") {
                projects = manager_.searchByTechnology(
                    request.get_param_value("technology"));
            } else if (filterKey == "status") {
                projects = manager_.filterByStatus(request.get_param_value("status"));
            } else if (sortKey.has_value()) {
                projects = manager_.sortedProjects(*sortKey);
            } else {
                projects = manager_.listProjects();
            }
        }

        if (sortKey.has_value() && !filterKey.empty()) {
            sortProjects(projects, *sortKey);
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

void ProjectHttpController::handleUpdate(const httplib::Request& request,
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

    const std::optional<ProjectHttpInput> input = parseProjectInput(request, response);
    if (!input.has_value()) {
        return;
    }

    try {
        std::optional<Project> updated;
        {
            std::lock_guard<std::mutex> lock(managerMutex_);
            if (!manager_.updateProject(*id,
                                        input->name,
                                        input->techStack,
                                        input->description,
                                        input->status)) {
                sendError(response,
                          HttpError{404, "project_not_found", "Project does not exist"});
                return;
            }

            const auto iterator = std::find_if(
                manager_.listProjects().begin(), manager_.listProjects().end(),
                [id](const Project& project) { return project.id() == *id; });
            if (iterator == manager_.listProjects().end()) {
                throw std::logic_error("Updated project is missing from manager state");
            }
            updated = *iterator;
        }

        response.status = 200;
        response.set_content(ProjectHttpJsonMapper::toJson(*updated).dump(),
                             kJsonContentType);
    } catch (const std::exception& error) {
        sendException(response, error);
    } catch (...) {
        sendError(response, HttpError{500, "internal_error", "Internal server error"});
    }
}

}  // namespace devmanager
